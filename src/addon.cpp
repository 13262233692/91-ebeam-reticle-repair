#include <napi.h>
#include "image_processor.h"
#include "scan_path_generator.h"
#include "dose_matrix_builder.h"
#include "common.h"
#include <memory>
#include <string>
#include <vector>
#include <map>

static ImageProcessor g_imageProcessor;
static ScanPathGenerator g_scanGen;
static DoseMatrixBuilder g_doseBuilder;

Napi::Value GetVersion(const Napi::CallbackInfo& info) {
    return Napi::String::New(info.Env(), "ebeam-repair-native-v1.0.0-opencv4.8.0");
}

Napi::Value DetectDefects(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (info.Length() < 2) {
        Napi::TypeError::New(env, "Expected at least 2 arguments: imageData, options").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    try {
        Napi::Object imageDataObj = info[0].As<Napi::Object>();
        Napi::Object options = info[1].IsObject() ? info[1].As<Napi::Object>() : Napi::Object::New(env);
        
        if (!imageDataObj.Has("data") || !imageDataObj.Has("width") || !imageDataObj.Has("height")) {
            Napi::TypeError::New(env, "imageData must have data, width, height properties").ThrowAsJavaScriptException();
            return env.Null();
        }
        
        Napi::Uint8Array pixelData = imageDataObj.Get("data").As<Napi::Uint8Array>();
        int width = imageDataObj.Get("width").As<Napi::Number>().Int32Value();
        int height = imageDataObj.Get("height").As<Napi::Number>().Int32Value();
        int channels = imageDataObj.Has("channels") ? imageDataObj.Get("channels").As<Napi::Number>().Int32Value() : 4;
        
        DetectionResult result = g_imageProcessor.detectDefects(pixelData, width, height, channels, options);
        
        auto out = Napi::Object::New(env);
        out.Set("success", Napi::Boolean::New(env, result.success));
        out.Set("native", Napi::Boolean::New(env, result.native));
        
        auto opaqueArr = Napi::Array::New(env, result.opaqueDefects.size());
        for (size_t i = 0; i < result.opaqueDefects.size(); ++i) {
            opaqueArr.Set(i, defectToNapi(env, result.opaqueDefects[i]));
        }
        out.Set("opaqueDefects", opaqueArr);
        
        auto clearArr = Napi::Array::New(env, result.clearDefects.size());
        for (size_t i = 0; i < result.clearDefects.size(); ++i) {
            clearArr.Set(i, defectToNapi(env, result.clearDefects[i]));
        }
        out.Set("clearDefects", clearArr);
        
        auto meta = Napi::Object::New(env);
        meta.Set("width", Napi::Number::New(env, result.width));
        meta.Set("height", Napi::Number::New(env, result.height));
        meta.Set("processingTimeMs", Napi::Number::New(env, result.processingTimeMs));
        out.Set("meta", meta);
        
        if (!result.success && !result.errorMessage.empty()) {
            out.Set("error", Napi::String::New(env, result.errorMessage));
        }
        
        return out;
        
    } catch (const std::exception& e) {
        Napi::Error::New(env, std::string("C++ exception in detectDefects: ") + e.what()).ThrowAsJavaScriptException();
        return env.Null();
    }
}

Napi::Value DetectDefectsFromFile(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (info.Length() < 1) {
        Napi::TypeError::New(env, "Expected filePath argument").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    try {
        std::string filePath = info[0].As<Napi::String>().Utf8Value();
        Napi::Object options = info.Length() >= 2 && info[1].IsObject() ? info[1].As<Napi::Object>() : Napi::Object::New(env);
        
        DetectionResult result = g_imageProcessor.detectDefectsFromFile(filePath, options);
        
        auto out = Napi::Object::New(env);
        out.Set("success", Napi::Boolean::New(env, result.success));
        out.Set("native", Napi::Boolean::New(env, result.native));
        
        auto opaqueArr = Napi::Array::New(env, result.opaqueDefects.size());
        for (size_t i = 0; i < result.opaqueDefects.size(); ++i) {
            opaqueArr.Set(i, defectToNapi(env, result.opaqueDefects[i]));
        }
        out.Set("opaqueDefects", opaqueArr);
        
        auto clearArr = Napi::Array::New(env, result.clearDefects.size());
        for (size_t i = 0; i < result.clearDefects.size(); ++i) {
            clearArr.Set(i, defectToNapi(env, result.clearDefects[i]));
        }
        out.Set("clearDefects", clearArr);
        
        auto meta = Napi::Object::New(env);
        meta.Set("width", Napi::Number::New(env, result.width));
        meta.Set("height", Napi::Number::New(env, result.height));
        meta.Set("processingTimeMs", Napi::Number::New(env, result.processingTimeMs));
        out.Set("meta", meta);
        
        if (!result.success && !result.errorMessage.empty()) {
            out.Set("error", Napi::String::New(env, result.errorMessage));
        }
        
        return out;
        
    } catch (const std::exception& e) {
        Napi::Error::New(env, std::string("C++ exception: ") + e.what()).ThrowAsJavaScriptException();
        return env.Null();
    }
}

std::vector<PointD> napiPolygonToVector(const Napi::Array& arr) {
    std::vector<PointD> pts;
    pts.reserve(arr.Length());
    for (uint32_t i = 0; i < arr.Length(); ++i) {
        Napi::Object pt = arr.Get(i).As<Napi::Object>();
        double x = pt.Has("x") ? pt.Get("x").As<Napi::Number>().DoubleValue() : 0.0;
        double y = pt.Has("y") ? pt.Get("y").As<Napi::Number>().DoubleValue() : 0.0;
        pts.push_back(PointD(x, y));
    }
    return pts;
}

Napi::Value GenerateRasterScan(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (info.Length() < 1) {
        Napi::TypeError::New(env, "Expected polygonPoints argument").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    try {
        Napi::Array polygonArr = info[0].As<Napi::Array>();
        std::vector<PointD> polygon = napiPolygonToVector(polygonArr);
        
        Napi::Object options = info.Length() >= 2 && info[1].IsObject() ? info[1].As<Napi::Object>() : Napi::Object::New(env);
        double stepX = options.Has("stepX") ? options.Get("stepX").As<Napi::Number>().DoubleValue() : 2.0;
        double stepY = options.Has("stepY") ? options.Get("stepY").As<Napi::Number>().DoubleValue() : 2.0;
        double overlap = options.Has("overlap") ? options.Get("overlap").As<Napi::Number>().DoubleValue() : 0.3;
        int layerIndex = options.Has("layer") ? options.Get("layer").As<Napi::Number>().Int32Value() : 0;
        std::string mode = options.Has("mode") ? options.Get("mode").As<Napi::String>().Utf8Value() : "raster";
        
        ScanPathResult result;
        
        if (mode == "hybrid") {
            double contourSpacing = options.Has("contourSpacing") ? options.Get("contourSpacing").As<Napi::Number>().DoubleValue() : stepY;
            int contourCount = options.Has("contourCount") ? options.Get("contourCount").As<Napi::Number>().Int32Value() : 3;
            result = g_scanGen.generateHybridScan(polygon, stepX, stepY, contourSpacing, contourCount, layerIndex);
        } else if (mode == "contour") {
            double lineSpacing = options.Has("lineSpacing") ? options.Get("lineSpacing").As<Napi::Number>().DoubleValue() : stepY;
            int numContours = options.Has("numContours") ? options.Get("numContours").As<Napi::Number>().Int32Value() : 5;
            result = g_scanGen.generateContourScan(polygon, lineSpacing, numContours, layerIndex);
        } else {
            result = g_scanGen.generateRasterScan(polygon, stepX, stepY, overlap, layerIndex);
        }
        
        auto out = Napi::Object::New(env);
        out.Set("success", Napi::Boolean::New(env, result.success));
        out.Set("native", Napi::Boolean::New(env, result.native));
        out.Set("totalPoints", Napi::Number::New(env, result.totalPoints));
        
        auto linesArr = Napi::Array::New(env, result.scanLines.size());
        for (size_t i = 0; i < result.scanLines.size(); ++i) {
            linesArr.Set(i, scanLineToNapi(env, result.scanLines[i]));
        }
        out.Set("scanLines", linesArr);
        out.Set("boundingBox", bboxToNapi(env, result.bbox));
        
        auto meta = Napi::Object::New(env);
        meta.Set("stepX", Napi::Number::New(env, stepX));
        meta.Set("stepY", Napi::Number::New(env, stepY));
        meta.Set("overlap", Napi::Number::New(env, overlap));
        meta.Set("scanLineCount", Napi::Number::New(env, static_cast<int>(result.scanLines.size())));
        out.Set("meta", meta);
        
        if (!result.success && !result.errorMessage.empty()) {
            out.Set("error", Napi::String::New(env, result.errorMessage));
        }
        
        return out;
        
    } catch (const std::exception& e) {
        Napi::Error::New(env, std::string("C++ exception: ") + e.what()).ThrowAsJavaScriptException();
        return env.Null();
    }
}

Napi::Value BuildDoseMatrix(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (info.Length() < 1) {
        Napi::TypeError::New(env, "Expected scanLines argument").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    try {
        ScanPathResult scanPath;
        scanPath.success = true;
        scanPath.native = true;
        
        Napi::Value firstArg = info[0];
        if (firstArg.IsArray()) {
            Napi::Array linesArr = firstArg.As<Napi::Array>();
            for (uint32_t i = 0; i < linesArr.Length(); ++i) {
                Napi::Object lineObj = linesArr.Get(i).As<Napi::Object>();
                ScanLine line;
                line.y = lineObj.Has("y") ? lineObj.Get("y").As<Napi::Number>().DoubleValue() : 0.0;
                line.direction = lineObj.Has("direction") ? lineObj.Get("direction").As<Napi::Number>().Int32Value() : 1;
                line.startX = lineObj.Has("startX") ? lineObj.Get("startX").As<Napi::Number>().DoubleValue() : 0.0;
                line.endX = lineObj.Has("endX") ? lineObj.Get("endX").As<Napi::Number>().DoubleValue() : 0.0;
                
                if (lineObj.Has("points")) {
                    Napi::Array ptsArr = lineObj.Get("points").As<Napi::Array>();
                    for (uint32_t j = 0; j < ptsArr.Length(); ++j) {
                        Napi::Object ptObj = ptsArr.Get(j).As<Napi::Object>();
                        ScanPoint pt;
                        pt.x = ptObj.Has("x") ? ptObj.Get("x").As<Napi::Number>().DoubleValue() : 0.0;
                        pt.y = ptObj.Has("y") ? ptObj.Get("y").As<Napi::Number>().DoubleValue() : 0.0;
                        pt.z = ptObj.Has("z") ? ptObj.Get("z").As<Napi::Number>().DoubleValue() : 0.0;
                        pt.dose = ptObj.Has("dose") ? ptObj.Get("dose").As<Napi::Number>().DoubleValue() : 1.0;
                        pt.material = ptObj.Has("material") ? ptObj.Get("material").As<Napi::String>().Utf8Value() : "target";
                        pt.layer = ptObj.Has("layer") ? ptObj.Get("layer").As<Napi::Number>().Int32Value() : 0;
                        line.points.push_back(pt);
                    }
                }
                scanPath.scanLines.push_back(line);
                scanPath.totalPoints += static_cast<int>(line.points.size());
            }
        } else if (firstArg.IsObject()) {
            Napi::Object scanPathObj = firstArg.As<Napi::Object>();
            if (scanPathObj.Has("scanLines")) {
                Napi::Array linesArr = scanPathObj.Get("scanLines").As<Napi::Array>();
                for (uint32_t i = 0; i < linesArr.Length(); ++i) {
                    Napi::Object lineObj = linesArr.Get(i).As<Napi::Object>();
                    ScanLine line;
                    line.y = lineObj.Has("y") ? lineObj.Get("y").As<Napi::Number>().DoubleValue() : 0.0;
                    line.direction = lineObj.Has("direction") ? lineObj.Get("direction").As<Napi::Number>().Int32Value() : 1;
                    if (lineObj.Has("points")) {
                        Napi::Array ptsArr = lineObj.Get("points").As<Napi::Array>();
                        for (uint32_t j = 0; j < ptsArr.Length(); ++j) {
                            Napi::Object ptObj = ptsArr.Get(j).As<Napi::Object>();
                            ScanPoint pt;
                            pt.x = ptObj.Has("x") ? ptObj.Get("x").As<Napi::Number>().DoubleValue() : 0.0;
                            pt.y = ptObj.Has("y") ? ptObj.Get("y").As<Napi::Number>().DoubleValue() : 0.0;
                            pt.z = ptObj.Has("z") ? ptObj.Get("z").As<Napi::Number>().DoubleValue() : 0.0;
                            pt.dose = ptObj.Has("dose") ? ptObj.Get("dose").As<Napi::Number>().DoubleValue() : 1.0;
                            pt.material = ptObj.Has("material") ? ptObj.Get("material").As<Napi::String>().Utf8Value() : "target";
                            pt.layer = ptObj.Has("layer") ? ptObj.Get("layer").As<Napi::Number>().Int32Value() : 0;
                            line.points.push_back(pt);
                        }
                    }
                    scanPath.scanLines.push_back(line);
                }
            }
        }
        
        Napi::Object options = info.Length() >= 2 && info[1].IsObject() ? info[1].As<Napi::Object>() : Napi::Object::New(env);
        double beamVoltage = options.Has("beamVoltage") ? options.Get("beamVoltage").As<Napi::Number>().DoubleValue() : 30.0;
        double baseDwellTime = options.Has("baseDwellTime") ? options.Get("baseDwellTime").As<Napi::Number>().DoubleValue() : 500.0;
        int layerIndex = options.Has("layer") ? options.Get("layer").As<Napi::Number>().Int32Value() : 0;
        
        DefectType::Type dtype = DefectType::OPAQUE;
        if (options.Has("defectType")) {
            std::string dtStr = options.Get("defectType").As<Napi::String>().Utf8Value();
            if (dtStr == "clear") dtype = DefectType::CLEAR;
            else if (dtStr == "border") dtype = DefectType::BORDER;
        }
        
        g_doseBuilder.setBeamParameters(beamVoltage, 100.0);
        g_doseBuilder.setBaseParameters(baseDwellTime, 1.0);
        
        if (options.Has("calibration")) {
            Napi::Object calObj = options.Get("calibration").As<Napi::Object>();
            auto props = calObj.GetPropertyNames();
            for (uint32_t i = 0; i < props.Length(); ++i) {
                std::string matName = props.Get(i).As<Napi::String>().Utf8Value();
                Napi::Object matCal = calObj.Get(matName).As<Napi::Object>();
                MaterialCalibration m;
                m.multiplier = matCal.Has("multiplier") ? matCal.Get("multiplier").As<Napi::Number>().DoubleValue() : 1.0;
                m.etchDepth = matCal.Has("etchDepth") ? matCal.Get("etchDepth").As<Napi::Number>().DoubleValue() : 20.0;
                m.sensitivity = matCal.Has("sensitivity") ? matCal.Get("sensitivity").As<Napi::Number>().DoubleValue() : 1.0;
                g_doseBuilder.setCalibration(matName, m);
            }
        }
        
        DoseMatrixResult result = g_doseBuilder.buildDoseMatrixSimple(
            scanPath.scanLines, beamVoltage, baseDwellTime, layerIndex
        );
        
        auto out = Napi::Object::New(env);
        out.Set("success", Napi::Boolean::New(env, result.success));
        out.Set("native", Napi::Boolean::New(env, result.native));
        
        auto cmdsArr = Napi::Array::New(env, result.commands.size());
        for (size_t i = 0; i < result.commands.size(); ++i) {
            cmdsArr.Set(i, scanPointToNapi(env, result.commands[i]));
        }
        out.Set("commands", cmdsArr);
        
        auto stats = Napi::Object::New(env);
        stats.Set("totalCommands", Napi::Number::New(env, result.totalCommands));
        stats.Set("totalDose", Napi::Number::New(env, result.totalDose));
        stats.Set("maxDwellTime", Napi::Number::New(env, result.maxDwellTime));
        stats.Set("avgDwellTime", Napi::Number::New(env, result.avgDwellTime));
        out.Set("stats", stats);
        
        if (!result.success && !result.errorMessage.empty()) {
            out.Set("error", Napi::String::New(env, result.errorMessage));
        }
        
        return out;
        
    } catch (const std::exception& e) {
        Napi::Error::New(env, std::string("C++ exception: ") + e.what()).ThrowAsJavaScriptException();
        return env.Null();
    }
}

Napi::Value SubpixelRefineContour(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (info.Length() < 2) {
        Napi::TypeError::New(env, "Expected contour and imageData arguments").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    try {
        Napi::Array contourArr = info[0].As<Napi::Array>();
        std::vector<PointD> contour = napiPolygonToVector(contourArr);
        
        Napi::Object imageDataObj = info[1].As<Napi::Object>();
        Napi::Uint8Array pixelData = imageDataObj.Get("data").As<Napi::Uint8Array>();
        int width = imageDataObj.Get("width").As<Napi::Number>().Int32Value();
        int height = imageDataObj.Get("height").As<Napi::Number>().Int32Value();
        int channels = imageDataObj.Has("channels") ? imageDataObj.Get("channels").As<Napi::Number>().Int32Value() : 4;
        
        Napi::Object options = info.Length() >= 3 && info[2].IsObject() ? info[2].As<Napi::Object>() : Napi::Object::New(env);
        std::string defectType = options.Has("type") ? options.Get("type").As<Napi::String>().Utf8Value() : "opaque";
        
        cv::Mat image(height, width, channels == 1 ? CV_8UC1 : CV_8UC4);
        std::memcpy(image.data, pixelData.Data(), width * height * (channels == 1 ? 1 : 4));
        
        cv::Mat gray;
        if (channels == 4) cv::cvtColor(image, gray, cv::COLOR_BGRA2GRAY);
        else if (channels == 3) cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
        else gray = image;
        
        cv::Mat blurred;
        double sigma = options.Has("sigma") ? options.Get("sigma").As<Napi::Number>().DoubleValue() : 1.0;
        cv::GaussianBlur(gray, blurred, cv::Size(0, 0), sigma);
        
        std::vector<PointD> refined = g_imageProcessor.subpixelRefineContour(contour, blurred, defectType);
        
        auto out = Napi::Array::New(env, refined.size());
        for (size_t i = 0; i < refined.size(); ++i) {
            out.Set(i, pointToNapi(env, refined[i]));
        }
        return out;
        
    } catch (const std::exception& e) {
        Napi::Error::New(env, std::string("C++ exception: ") + e.what()).ThrowAsJavaScriptException();
        return env.Null();
    }
}

Napi::Value ProcessMultiLayer(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (info.Length() < 1) {
        Napi::TypeError::New(env, "Expected layers array argument").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    try {
        Napi::Array layersArr = info[0].As<Napi::Array>();
        Napi::Object options = info.Length() >= 2 && info[1].IsObject() ? info[1].As<Napi::Object>() : Napi::Object::New(env);
        
        auto layerResults = Napi::Array::New(env, layersArr.Length());
        int totalOpaque = 0, totalClear = 0;
        
        auto opaqueCombined = Napi::Array::New(env);
        auto clearCombined = Napi::Array::New(env);
        uint32_t opIdx = 0, clIdx = 0;
        
        for (uint32_t i = 0; i < layersArr.Length(); ++i) {
            Napi::Object layerObj = layersArr.Get(i).As<Napi::Object>();
            Napi::Uint8Array pixelData = layerObj.Get("data").As<Napi::Uint8Array>();
            int width = layerObj.Get("width").As<Napi::Number>().Int32Value();
            int height = layerObj.Get("height").As<Napi::Number>().Int32Value();
            int channels = layerObj.Has("channels") ? layerObj.Get("channels").As<Napi::Number>().Int32Value() : 4;
            std::string layerName = layerObj.Has("name") ? layerObj.Get("name").As<Napi::String>().Utf8Value() : "Layer_" + std::to_string(i);
            
            DetectionResult result = g_imageProcessor.detectDefects(pixelData, width, height, channels, options);
            
            auto resultObj = Napi::Object::New(env);
            resultObj.Set("layerIndex", Napi::Number::New(env, static_cast<int>(i)));
            resultObj.Set("layerName", Napi::String::New(env, layerName));
            resultObj.Set("success", Napi::Boolean::New(env, result.success));
            
            auto opaqueArr = Napi::Array::New(env, result.opaqueDefects.size());
            for (size_t j = 0; j < result.opaqueDefects.size(); ++j) {
                Napi::Object d = defectToNapi(env, result.opaqueDefects[j]);
                d.Set("layer", Napi::Number::New(env, static_cast<int>(i)));
                opaqueArr.Set(j, d);
                opaqueCombined.Set(opIdx++, d);
            }
            resultObj.Set("opaqueDefects", opaqueArr);
            
            auto clearArr = Napi::Array::New(env, result.clearDefects.size());
            for (size_t j = 0; j < result.clearDefects.size(); ++j) {
                Napi::Object d = defectToNapi(env, result.clearDefects[j]);
                d.Set("layer", Napi::Number::New(env, static_cast<int>(i)));
                clearArr.Set(j, d);
                clearCombined.Set(clIdx++, d);
            }
            resultObj.Set("clearDefects", clearArr);
            
            totalOpaque += static_cast<int>(result.opaqueDefects.size());
            totalClear += static_cast<int>(result.clearDefects.size());
            
            layerResults.Set(i, resultObj);
        }
        
        auto out = Napi::Object::New(env);
        out.Set("success", Napi::Boolean::New(env, true));
        out.Set("native", Napi::Boolean::New(env, true));
        out.Set("layerResults", layerResults);
        
        auto combined = Napi::Object::New(env);
        combined.Set("opaqueDefects", opaqueCombined);
        combined.Set("clearDefects", clearCombined);
        out.Set("combinedDefects", combined);
        
        auto summary = Napi::Object::New(env);
        summary.Set("totalLayers", Napi::Number::New(env, static_cast<int>(layersArr.Length())));
        summary.Set("totalOpaque", Napi::Number::New(env, totalOpaque));
        summary.Set("totalClear", Napi::Number::New(env, totalClear));
        out.Set("summary", summary);
        
        return out;
        
    } catch (const std::exception& e) {
        Napi::Error::New(env, std::string("C++ exception: ") + e.what()).ThrowAsJavaScriptException();
        return env.Null();
    }
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("getVersion", Napi::Function::New(env, GetVersion));
    exports.Set("detectDefects", Napi::Function::New(env, DetectDefects));
    exports.Set("detectDefectsFromFile", Napi::Function::New(env, DetectDefectsFromFile));
    exports.Set("generateRasterScan", Napi::Function::New(env, GenerateRasterScan));
    exports.Set("buildDoseMatrix", Napi::Function::New(env, BuildDoseMatrix));
    exports.Set("subpixelRefineContour", Napi::Function::New(env, SubpixelRefineContour));
    exports.Set("processMultiLayer", Napi::Function::New(env, ProcessMultiLayer));
    return exports;
}

NODE_API_MODULE(ebeam_repair, Init)
