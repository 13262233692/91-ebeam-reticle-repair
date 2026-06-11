#include <napi.h>
#include "image_processor.h"
#include "scan_path_generator.h"
#include "dose_matrix_builder.h"
#include "pec_corrector.h"
#include "common.h"
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <thread>
#include <chrono>

static ImageProcessor g_imageProcessor;
static ScanPathGenerator g_scanGen;
static DoseMatrixBuilder g_doseBuilder;
static PECCorrector g_pecCorrector;
static std::mutex g_procMutex;

Napi::Value GetVersion(const Napi::CallbackInfo& info) {
    return Napi::String::New(info.Env(), "ebeam-repair-native-v1.2.0-pec-async-opencv4.8.0");
}

// ============================================================
// 通用参数提取辅助函数 (在主线程提取，避免跨线程触摸 Napi::Value)
// ============================================================

struct DetectionOptions {
    double gaussianSigma = 1.5;
    int blockSize = 31;
    double C = 10.0;
    double cannyLow = 40.0;
    double cannyHigh = 120.0;
    int cannyAperture = 3;

    static DetectionOptions fromNapi(Napi::Object obj) {
        DetectionOptions o;
        if (obj.IsEmpty() || obj.IsNull() || obj.IsUndefined()) return o;
        Napi::Env env = obj.Env();
        if (obj.Has("gaussianSigma") && obj.Get("gaussianSigma").IsNumber())
            o.gaussianSigma = obj.Get("gaussianSigma").As<Napi::Number>().DoubleValue();
        if (obj.Has("blockSize") && obj.Get("blockSize").IsNumber())
            o.blockSize = obj.Get("blockSize").As<Napi::Number>().Int32Value();
        if (obj.Has("C") && obj.Get("C").IsNumber())
            o.C = obj.Get("C").As<Napi::Number>().DoubleValue();
        if (obj.Has("cannyLow") && obj.Get("cannyLow").IsNumber())
            o.cannyLow = obj.Get("cannyLow").As<Napi::Number>().DoubleValue();
        if (obj.Has("cannyHigh") && obj.Get("cannyHigh").IsNumber())
            o.cannyHigh = obj.Get("cannyHigh").As<Napi::Number>().DoubleValue();
        if (obj.Has("cannyAperture") && obj.Get("cannyAperture").IsNumber())
            o.cannyAperture = obj.Get("cannyAperture").As<Napi::Number>().Int32Value();
        return o;
    }

    Napi::Object toNapiOptions(Napi::Env env) const {
        auto o = Napi::Object::New(env);
        o.Set("gaussianSigma", Napi::Number::New(env, gaussianSigma));
        o.Set("blockSize", Napi::Number::New(env, blockSize));
        o.Set("C", Napi::Number::New(env, C));
        o.Set("cannyLow", Napi::Number::New(env, cannyLow));
        o.Set("cannyHigh", Napi::Number::New(env, cannyHigh));
        o.Set("cannyAperture", Napi::Number::New(env, cannyAperture));
        return o;
    }

    DetectionOptionsPure toPure() const {
        DetectionOptionsPure p;
        p.gaussianSigma = gaussianSigma;
        p.blockSize = blockSize;
        p.C = C;
        p.cannyLow = cannyLow;
        p.cannyHigh = cannyHigh;
        p.cannyAperture = cannyAperture;
        return p;
    }
};

struct ScanOptions {
    double stepX = 2.0;
    double stepY = 2.0;
    double overlap = 0.3;
    int layer = 0;
    std::string mode = "raster";
    double contourSpacing = 2.0;
    int contourCount = 3;
    double lineSpacing = 2.0;
    int numContours = 5;

    static ScanOptions fromNapi(Napi::Object obj) {
        ScanOptions o;
        if (obj.IsEmpty() || obj.IsNull() || obj.IsUndefined()) return o;
        if (obj.Has("stepX") && obj.Get("stepX").IsNumber())
            o.stepX = obj.Get("stepX").As<Napi::Number>().DoubleValue();
        if (obj.Has("stepY") && obj.Get("stepY").IsNumber())
            o.stepY = obj.Get("stepY").As<Napi::Number>().DoubleValue();
        if (obj.Has("overlap") && obj.Get("overlap").IsNumber())
            o.overlap = obj.Get("overlap").As<Napi::Number>().DoubleValue();
        if (obj.Has("layer") && obj.Get("layer").IsNumber())
            o.layer = obj.Get("layer").As<Napi::Number>().Int32Value();
        if (obj.Has("mode") && obj.Get("mode").IsString())
            o.mode = obj.Get("mode").As<Napi::String>().Utf8Value();
        if (obj.Has("contourSpacing") && obj.Get("contourSpacing").IsNumber())
            o.contourSpacing = obj.Get("contourSpacing").As<Napi::Number>().DoubleValue();
        if (obj.Has("contourCount") && obj.Get("contourCount").IsNumber())
            o.contourCount = obj.Get("contourCount").As<Napi::Number>().Int32Value();
        if (obj.Has("lineSpacing") && obj.Get("lineSpacing").IsNumber())
            o.lineSpacing = obj.Get("lineSpacing").As<Napi::Number>().DoubleValue();
        if (obj.Has("numContours") && obj.Get("numContours").IsNumber())
            o.numContours = obj.Get("numContours").As<Napi::Number>().Int32Value();
        return o;
    }
};

struct DoseOptions {
    double beamVoltage = 30.0;
    double baseDwellTime = 500.0;
    int layer = 0;
    std::string defectType = "opaque";
    std::map<std::string, MaterialCalibration> calibration;

    static DoseOptions fromNapi(Napi::Object obj) {
        DoseOptions o;
        if (obj.IsEmpty() || obj.IsNull() || obj.IsUndefined()) return o;
        if (obj.Has("beamVoltage") && obj.Get("beamVoltage").IsNumber())
            o.beamVoltage = obj.Get("beamVoltage").As<Napi::Number>().DoubleValue();
        if (obj.Has("baseDwellTime") && obj.Get("baseDwellTime").IsNumber())
            o.baseDwellTime = obj.Get("baseDwellTime").As<Napi::Number>().DoubleValue();
        if (obj.Has("layer") && obj.Get("layer").IsNumber())
            o.layer = obj.Get("layer").As<Napi::Number>().Int32Value();
        if (obj.Has("defectType") && obj.Get("defectType").IsString())
            o.defectType = obj.Get("defectType").As<Napi::String>().Utf8Value();

        if (obj.Has("calibration") && obj.Get("calibration").IsObject()) {
            Napi::Object calObj = obj.Get("calibration").As<Napi::Object>();
            auto names = calObj.GetPropertyNames();
            for (uint32_t i = 0; i < names.Length(); i++) {
                std::string name = names.Get(i).As<Napi::String>().Utf8Value();
                if (calObj.Get(name).IsObject()) {
                    Napi::Object m = calObj.Get(name).As<Napi::Object>();
                    MaterialCalibration mc{1.0, 20.0, 1.0};
                    if (m.Has("multiplier") && m.Get("multiplier").IsNumber())
                        mc.multiplier = m.Get("multiplier").As<Napi::Number>().DoubleValue();
                    if (m.Has("etchDepth") && m.Get("etchDepth").IsNumber())
                        mc.etchDepth = m.Get("etchDepth").As<Napi::Number>().DoubleValue();
                    if (m.Has("sensitivity") && m.Get("sensitivity").IsNumber())
                        mc.sensitivity = m.Get("sensitivity").As<Napi::Number>().DoubleValue();
                    o.calibration[name] = mc;
                }
            }
        }
        return o;
    }
};

struct SubpixelOptions {
    std::string type = "opaque";
    double sigma = 1.0;

    static SubpixelOptions fromNapi(Napi::Object obj) {
        SubpixelOptions o;
        if (obj.IsEmpty() || obj.IsNull() || obj.IsUndefined()) return o;
        if (obj.Has("type") && obj.Get("type").IsString())
            o.type = obj.Get("type").As<Napi::String>().Utf8Value();
        if (obj.Has("sigma") && obj.Get("sigma").IsNumber())
            o.sigma = obj.Get("sigma").As<Napi::Number>().DoubleValue();
        return o;
    }
};

struct PSFOptions {
    double eta = 0.45;
    double alpha = 2.5;
    double beta = 25.0;
    double gamma = 0.9;

    static PSFOptions fromNapi(Napi::Object obj) {
        PSFOptions o;
        if (obj.IsEmpty() || obj.IsNull() || obj.IsUndefined()) return o;
        if (obj.Has("eta") && obj.Get("eta").IsNumber())
            o.eta = obj.Get("eta").As<Napi::Number>().DoubleValue();
        if (obj.Has("alpha") && obj.Get("alpha").IsNumber())
            o.alpha = obj.Get("alpha").As<Napi::Number>().DoubleValue();
        if (obj.Has("beta") && obj.Get("beta").IsNumber())
            o.beta = obj.Get("beta").As<Napi::Number>().DoubleValue();
        if (obj.Has("gamma") && obj.Get("gamma").IsNumber())
            o.gamma = obj.Get("gamma").As<Napi::Number>().DoubleValue();
        return o;
    }

    DualGaussianPSF toPure() const {
        DualGaussianPSF p;
        p.eta = eta;
        p.alpha = alpha;
        p.beta = beta;
        p.gamma = gamma;
        return p;
    }
};

struct PECAlgorithmOptions {
    int iterations = 5;
    double regularizationLambda = 0.001;
    bool applyDogBoneEnhance = true;
    double dogBoneStrength = 1.15;
    double edgeBoostSigma = 1.0;
    double cornerBoostSigma = 1.5;
    double maxDoseMultiplier = 2.5;
    double minDoseMultiplier = 0.5;
    bool useWienerFilter = false;
    double wienerSNR = 30.0;

    static PECAlgorithmOptions fromNapi(Napi::Object obj) {
        PECAlgorithmOptions o;
        if (obj.IsEmpty() || obj.IsNull() || obj.IsUndefined()) return o;
        if (obj.Has("iterations") && obj.Get("iterations").IsNumber())
            o.iterations = obj.Get("iterations").As<Napi::Number>().Int32Value();
        if (obj.Has("regularizationLambda") && obj.Get("regularizationLambda").IsNumber())
            o.regularizationLambda = obj.Get("regularizationLambda").As<Napi::Number>().DoubleValue();
        if (obj.Has("applyDogBoneEnhance") && obj.Get("applyDogBoneEnhance").IsBoolean())
            o.applyDogBoneEnhance = obj.Get("applyDogBoneEnhance").As<Napi::Boolean>().Value();
        if (obj.Has("dogBoneStrength") && obj.Get("dogBoneStrength").IsNumber())
            o.dogBoneStrength = obj.Get("dogBoneStrength").As<Napi::Number>().DoubleValue();
        if (obj.Has("edgeBoostSigma") && obj.Get("edgeBoostSigma").IsNumber())
            o.edgeBoostSigma = obj.Get("edgeBoostSigma").As<Napi::Number>().DoubleValue();
        if (obj.Has("cornerBoostSigma") && obj.Get("cornerBoostSigma").IsNumber())
            o.cornerBoostSigma = obj.Get("cornerBoostSigma").As<Napi::Number>().DoubleValue();
        if (obj.Has("maxDoseMultiplier") && obj.Get("maxDoseMultiplier").IsNumber())
            o.maxDoseMultiplier = obj.Get("maxDoseMultiplier").As<Napi::Number>().DoubleValue();
        if (obj.Has("minDoseMultiplier") && obj.Get("minDoseMultiplier").IsNumber())
            o.minDoseMultiplier = obj.Get("minDoseMultiplier").As<Napi::Number>().DoubleValue();
        if (obj.Has("useWienerFilter") && obj.Get("useWienerFilter").IsBoolean())
            o.useWienerFilter = obj.Get("useWienerFilter").As<Napi::Boolean>().Value();
        if (obj.Has("wienerSNR") && obj.Get("wienerSNR").IsNumber())
            o.wienerSNR = obj.Get("wienerSNR").As<Napi::Number>().DoubleValue();
        return o;
    }

    PECOptions toPure() const {
        PECOptions o;
        o.iterations = iterations;
        o.regularizationLambda = regularizationLambda;
        o.applyDogBoneEnhance = applyDogBoneEnhance;
        o.dogBoneStrength = dogBoneStrength;
        o.edgeBoostSigma = edgeBoostSigma;
        o.cornerBoostSigma = cornerBoostSigma;
        o.maxDoseMultiplier = maxDoseMultiplier;
        o.minDoseMultiplier = minDoseMultiplier;
        o.useWienerFilter = useWienerFilter;
        o.wienerSNR = wienerSNR;
        return o;
    }
};

std::vector<PointD> napiPolygonToVector(const Napi::Array& arr) {
    std::vector<PointD> pts;
    pts.reserve(arr.Length());
    for (uint32_t i = 0; i < arr.Length(); ++i) {
        if (!arr.Get(i).IsObject()) continue;
        Napi::Object pt = arr.Get(i).As<Napi::Object>();
        double x = pt.Has("x") && pt.Get("x").IsNumber() ? pt.Get("x").As<Napi::Number>().DoubleValue() : 0.0;
        double y = pt.Has("y") && pt.Get("y").IsNumber() ? pt.Get("y").As<Napi::Number>().DoubleValue() : 0.0;
        pts.push_back(PointD(x, y));
    }
    return pts;
}

ScanPathResult parseScanLinesFromNapi(const Napi::Value& val) {
    ScanPathResult scanPath;
    scanPath.success = true;
    scanPath.native = true;

    auto parseLineObj = [](Napi::Object lineObj) -> ScanLine {
        ScanLine line;
        line.y = lineObj.Has("y") && lineObj.Get("y").IsNumber() ? lineObj.Get("y").As<Napi::Number>().DoubleValue() : 0.0;
        line.direction = lineObj.Has("direction") && lineObj.Get("direction").IsNumber() ? lineObj.Get("direction").As<Napi::Number>().Int32Value() : 1;
        line.startX = lineObj.Has("startX") && lineObj.Get("startX").IsNumber() ? lineObj.Get("startX").As<Napi::Number>().DoubleValue() : 0.0;
        line.endX = lineObj.Has("endX") && lineObj.Get("endX").IsNumber() ? lineObj.Get("endX").As<Napi::Number>().DoubleValue() : 0.0;

        if (lineObj.Has("points") && lineObj.Get("points").IsArray()) {
            Napi::Array ptsArr = lineObj.Get("points").As<Napi::Array>();
            line.points.reserve(ptsArr.Length());
            for (uint32_t j = 0; j < ptsArr.Length(); j++) {
                if (!ptsArr.Get(j).IsObject()) continue;
                Napi::Object ptObj = ptsArr.Get(j).As<Napi::Object>();
                ScanPoint pt;
                pt.x = ptObj.Has("x") && ptObj.Get("x").IsNumber() ? ptObj.Get("x").As<Napi::Number>().DoubleValue() : 0.0;
                pt.y = ptObj.Has("y") && ptObj.Get("y").IsNumber() ? ptObj.Get("y").As<Napi::Number>().DoubleValue() : 0.0;
                pt.z = ptObj.Has("z") && ptObj.Get("z").IsNumber() ? ptObj.Get("z").As<Napi::Number>().DoubleValue() : 0.0;
                pt.dose = ptObj.Has("dose") && ptObj.Get("dose").IsNumber() ? ptObj.Get("dose").As<Napi::Number>().DoubleValue() : 1.0;
                pt.material = ptObj.Has("material") && ptObj.Get("material").IsString() ? ptObj.Get("material").As<Napi::String>().Utf8Value() : "target";
                pt.layer = ptObj.Has("layer") && ptObj.Get("layer").IsNumber() ? ptObj.Get("layer").As<Napi::Number>().Int32Value() : 0;
                if (ptObj.Has("defectId") && ptObj.Get("defectId").IsString()) {
                    // stored as ScanPoint doesn't have defectId - we ignore for native processing
                }
                if (ptObj.Has("defectType") && ptObj.Get("defectType").IsString()) {
                    // stored as ScanPoint doesn't have defectType - ignore
                }
                line.points.push_back(pt);
            }
        }
        return line;
    };

    if (val.IsArray()) {
        Napi::Array linesArr = val.As<Napi::Array>();
        scanPath.scanLines.reserve(linesArr.Length());
        for (uint32_t i = 0; i < linesArr.Length(); i++) {
            if (!linesArr.Get(i).IsObject()) continue;
            ScanLine line = parseLineObj(linesArr.Get(i).As<Napi::Object>());
            scanPath.scanLines.push_back(line);
            scanPath.totalPoints += static_cast<int>(line.points.size());
        }
    } else if (val.IsObject()) {
        Napi::Object scanPathObj = val.As<Napi::Object>();
        if (scanPathObj.Has("scanLines") && scanPathObj.Get("scanLines").IsArray()) {
            Napi::Array linesArr = scanPathObj.Get("scanLines").As<Napi::Array>();
            scanPath.scanLines.reserve(linesArr.Length());
            for (uint32_t i = 0; i < linesArr.Length(); i++) {
                if (!linesArr.Get(i).IsObject()) continue;
                ScanLine line = parseLineObj(linesArr.Get(i).As<Napi::Object>());
                scanPath.scanLines.push_back(line);
                scanPath.totalPoints += static_cast<int>(line.points.size());
            }
        }
    }
    return scanPath;
}

// ============================================================
// AsyncWorker: DetectDefectsAsync
// 8K 像素级 OpenCV 处理完全放入后台线程
// ============================================================

class DetectDefectsWorker : public Napi::AsyncWorker {
public:
    DetectDefectsWorker(Napi::Function& callback,
                        std::vector<uint8_t> pixelBuf,
                        int width, int height, int channels,
                        DetectionOptions opts)
        : Napi::AsyncWorker(callback),
          pixelBuf_(std::move(pixelBuf)),
          w_(width), h_(height), ch_(channels),
          opts_(opts) {}

    ~DetectDefectsWorker() override = default;

    void Execute() override {
        try {
            std::lock_guard<std::mutex> lock(g_procMutex);

            cv::Mat img;
            if (ch_ == 1) {
                img = cv::Mat(h_, w_, CV_8UC1, pixelBuf_.data()).clone();
            } else {
                img = cv::Mat(h_, w_, CV_8UC4, pixelBuf_.data()).clone();
            }

            cv::Mat gray;
            if (ch_ == 4) cv::cvtColor(img, gray, cv::COLOR_BGRA2GRAY);
            else if (ch_ == 3) cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
            else gray = img;

            cv::Mat blurred = g_imageProcessor.gaussianBlur(gray, opts_.gaussianSigma);
            cv::Mat adaptive = g_imageProcessor.adaptiveThreshold(blurred, opts_.blockSize, opts_.C);
            cv::Mat edges = g_imageProcessor.cannyEdges(blurred, opts_.cannyLow, opts_.cannyHigh, opts_.cannyAperture);

            auto t0 = std::chrono::high_resolution_clock::now();
            result_.width = w_;
            result_.height = h_;
            result_.native = true;
            result_.success = true;

            // internal method in ImageProcessor - manually call equivalent logic
            std::vector<std::vector<cv::Point>> contours;
            std::vector<cv::Vec4i> hierarchy;
            cv::Mat edgeCopy = edges.clone();
            cv::findContours(edgeCopy, contours, hierarchy, cv::RETR_CCOMP, cv::CHAIN_APPROX_TC89_L1);

            for (size_t i = 0; i < contours.size(); i++) {
                if (contours[i].size() < 6) continue;
                double area = cv::contourArea(contours[i]);
                if (area < 10.0 || area > 1e7) continue;

                std::vector<cv::Point> approx;
                cv::approxPolyDP(contours[i], approx, 1.0, true);
                if (approx.size() < 4) continue;

                std::vector<PointD> subContour;
                subContour.reserve(approx.size());
                for (const auto& p : approx) {
                    subContour.emplace_back(static_cast<double>(p.x), static_cast<double>(p.y));
                }

                cv::Moments m = cv::moments(approx);
                PointD centroid;
                if (std::fabs(m.m00) > 1e-6) {
                    centroid.x = m.m10 / m.m00;
                    centroid.y = m.m01 / m.m00;
                } else {
                    centroid = polygonCentroid(subContour);
                }

                int cx = static_cast<int>(std::round(centroid.x));
                int cy = static_cast<int>(std::round(centroid.y));
                if (cx < 0 || cx >= adaptive.cols || cy < 0 || cy >= adaptive.rows) continue;

                std::vector<PointD> refined = g_imageProcessor.subpixelRefineContour(subContour, blurred, "opaque");

                DefectContour defect;
                defect.points = refined;
                defect.area = polygonArea(refined);
                defect.centroid = polygonCentroid(refined);
                defect.bbox = polygonBBox(refined);
                defect.layer = 0;

                uchar insideVal = adaptive.ptr<uchar>(cy)[cx];
                if (insideVal == 255) {
                    defect.type = "opaque";
                    defect.id = "opaque_" + std::to_string(result_.opaqueDefects.size());
                    result_.opaqueDefects.push_back(defect);
                } else {
                    defect.type = "clear";
                    defect.id = "clear_" + std::to_string(result_.clearDefects.size());
                    result_.clearDefects.push_back(defect);
                }
            }

            auto t1 = std::chrono::high_resolution_clock::now();
            result_.processingTimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

        } catch (const cv::Exception& e) {
            SetError(std::string("OpenCV error: ") + e.what());
        } catch (const std::exception& e) {
            SetError(std::string("C++ error: ") + e.what());
        }
    }

    void OnOK() override {
        Napi::Env env = Env();
        Napi::HandleScope scope(env);

        auto out = Napi::Object::New(env);
        out.Set("success", Napi::Boolean::New(env, result_.success));
        out.Set("native", Napi::Boolean::New(env, result_.native));
        out.Set("async", Napi::Boolean::New(env, true));

        auto opaqueArr = Napi::Array::New(env, result_.opaqueDefects.size());
        for (size_t i = 0; i < result_.opaqueDefects.size(); ++i) {
            opaqueArr.Set(i, defectToNapi(env, result_.opaqueDefects[i]));
        }
        out.Set("opaqueDefects", opaqueArr);

        auto clearArr = Napi::Array::New(env, result_.clearDefects.size());
        for (size_t i = 0; i < result_.clearDefects.size(); ++i) {
            clearArr.Set(i, defectToNapi(env, result_.clearDefects[i]));
        }
        out.Set("clearDefects", clearArr);

        auto meta = Napi::Object::New(env);
        meta.Set("width", Napi::Number::New(env, result_.width));
        meta.Set("height", Napi::Number::New(env, result_.height));
        meta.Set("processingTimeMs", Napi::Number::New(env, result_.processingTimeMs));
        out.Set("meta", meta);

        if (!result_.errorMessage.empty()) {
            out.Set("error", Napi::String::New(env, result_.errorMessage));
        }

        Callback().Call({env.Null(), out});
    }

    void OnError(const Napi::Error& e) override {
        Napi::Env env = Env();
        Callback().Call({e.Value(), env.Null()});
    }

private:
    std::vector<uint8_t> pixelBuf_;
    int w_, h_, ch_;
    DetectionOptions opts_;
    DetectionResult result_;
};

// ============================================================
// AsyncWorker: DetectDefectsFromFileAsync
// ============================================================

class DetectDefectsFromFileWorker : public Napi::AsyncWorker {
public:
    DetectDefectsFromFileWorker(Napi::Function& callback,
                                std::string filePath,
                                DetectionOptions opts)
        : Napi::AsyncWorker(callback),
          filePath_(std::move(filePath)),
          optsPure_(opts.toPure()) {}

    void Execute() override {
        try {
            std::lock_guard<std::mutex> lock(g_procMutex);
            auto t0 = std::chrono::high_resolution_clock::now();

            result_ = g_imageProcessor.detectDefectsFromFile(filePath_, optsPure_);

            auto t1 = std::chrono::high_resolution_clock::now();
            result_.processingTimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
            result_.native = true;
        } catch (const cv::Exception& e) {
            SetError(std::string("OpenCV error: ") + e.what());
        } catch (const std::exception& e) {
            SetError(std::string("C++ error: ") + e.what());
        }
    }

    void OnOK() override {
        Napi::Env env = Env();
        auto out = Napi::Object::New(env);
        out.Set("success", Napi::Boolean::New(env, result_.success));
        out.Set("native", Napi::Boolean::New(env, result_.native));
        out.Set("async", Napi::Boolean::New(env, true));

        auto opaqueArr = Napi::Array::New(env, result_.opaqueDefects.size());
        for (size_t i = 0; i < result_.opaqueDefects.size(); ++i) {
            opaqueArr.Set(i, defectToNapi(env, result_.opaqueDefects[i]));
        }
        out.Set("opaqueDefects", opaqueArr);

        auto clearArr = Napi::Array::New(env, result_.clearDefects.size());
        for (size_t i = 0; i < result_.clearDefects.size(); ++i) {
            clearArr.Set(i, defectToNapi(env, result_.clearDefects[i]));
        }
        out.Set("clearDefects", clearArr);

        auto meta = Napi::Object::New(env);
        meta.Set("width", Napi::Number::New(env, result_.width));
        meta.Set("height", Napi::Number::New(env, result_.height));
        meta.Set("processingTimeMs", Napi::Number::New(env, result_.processingTimeMs));
        out.Set("meta", meta);

        if (!result_.errorMessage.empty()) {
            out.Set("error", Napi::String::New(env, result_.errorMessage));
        }
        Callback().Call({env.Null(), out});
    }

private:
    std::string filePath_;
    DetectionOptionsPure optsPure_;
    DetectionResult result_;
};

// ============================================================
// AsyncWorker: GenerateRasterScanAsync
// ============================================================

class GenerateRasterScanWorker : public Napi::AsyncWorker {
public:
    GenerateRasterScanWorker(Napi::Function& callback,
                             std::vector<PointD> polygon,
                             ScanOptions opts)
        : Napi::AsyncWorker(callback),
          polygon_(std::move(polygon)),
          opts_(opts) {}

    void Execute() override {
        try {
            std::lock_guard<std::mutex> lock(g_procMutex);
            if (opts_.mode == "hybrid") {
                result_ = g_scanGen.generateHybridScan(
                    polygon_, opts_.stepX, opts_.stepY,
                    opts_.contourSpacing, opts_.contourCount, opts_.layer
                );
            } else if (opts_.mode == "contour") {
                result_ = g_scanGen.generateContourScan(
                    polygon_, opts_.lineSpacing, opts_.numContours, opts_.layer
                );
            } else {
                result_ = g_scanGen.generateRasterScan(
                    polygon_, opts_.stepX, opts_.stepY, opts_.overlap, opts_.layer
                );
            }
        } catch (const std::exception& e) {
            SetError(std::string("C++ error: ") + e.what());
        }
    }

    void OnOK() override {
        Napi::Env env = Env();
        auto out = Napi::Object::New(env);
        out.Set("success", Napi::Boolean::New(env, result_.success));
        out.Set("native", Napi::Boolean::New(env, result_.native));
        out.Set("async", Napi::Boolean::New(env, true));
        out.Set("totalPoints", Napi::Number::New(env, result_.totalPoints));

        auto linesArr = Napi::Array::New(env, result_.scanLines.size());
        for (size_t i = 0; i < result_.scanLines.size(); ++i) {
            linesArr.Set(i, scanLineToNapi(env, result_.scanLines[i]));
        }
        out.Set("scanLines", linesArr);
        out.Set("boundingBox", bboxToNapi(env, result_.bbox));

        auto meta = Napi::Object::New(env);
        meta.Set("stepX", Napi::Number::New(env, opts_.stepX));
        meta.Set("stepY", Napi::Number::New(env, opts_.stepY));
        meta.Set("overlap", Napi::Number::New(env, opts_.overlap));
        meta.Set("mode", Napi::String::New(env, opts_.mode));
        meta.Set("scanLineCount", Napi::Number::New(env, static_cast<int>(result_.scanLines.size())));
        out.Set("meta", meta);

        if (!result_.errorMessage.empty()) {
            out.Set("error", Napi::String::New(env, result_.errorMessage));
        }
        Callback().Call({env.Null(), out});
    }

private:
    std::vector<PointD> polygon_;
    ScanOptions opts_;
    ScanPathResult result_;
};

// ============================================================
// AsyncWorker: BuildDoseMatrixAsync
// ============================================================

class BuildDoseMatrixWorker : public Napi::AsyncWorker {
public:
    BuildDoseMatrixWorker(Napi::Function& callback,
                          ScanPathResult scanPath,
                          DoseOptions opts)
        : Napi::AsyncWorker(callback),
          scanPath_(std::move(scanPath)),
          opts_(opts) {}

    void Execute() override {
        try {
            std::lock_guard<std::mutex> lock(g_procMutex);

            g_doseBuilder.setBeamParameters(opts_.beamVoltage, 100.0);
            g_doseBuilder.setBaseParameters(opts_.baseDwellTime, 1.0);
            for (const auto& kv : opts_.calibration) {
                g_doseBuilder.setCalibration(kv.first, kv.second);
            }

            DefectType::Type dtype = DefectType::OPAQUE;
            if (opts_.defectType == "clear") dtype = DefectType::CLEAR;
            else if (opts_.defectType == "border") dtype = DefectType::BORDER;

            result_ = g_doseBuilder.buildDoseMatrixSimple(
                scanPath_.scanLines, opts_.beamVoltage, opts_.baseDwellTime, opts_.layer
            );
        } catch (const std::exception& e) {
            SetError(std::string("C++ error: ") + e.what());
        }
    }

    void OnOK() override {
        Napi::Env env = Env();
        auto out = Napi::Object::New(env);
        out.Set("success", Napi::Boolean::New(env, result_.success));
        out.Set("native", Napi::Boolean::New(env, result_.native));
        out.Set("async", Napi::Boolean::New(env, true));

        auto cmdsArr = Napi::Array::New(env, result_.commands.size());
        for (size_t i = 0; i < result_.commands.size(); ++i) {
            cmdsArr.Set(i, scanPointToNapi(env, result_.commands[i]));
        }
        out.Set("commands", cmdsArr);

        auto stats = Napi::Object::New(env);
        stats.Set("totalCommands", Napi::Number::New(env, result_.totalCommands));
        stats.Set("totalDose", Napi::Number::New(env, result_.totalDose));
        stats.Set("maxDwellTime", Napi::Number::New(env, result_.maxDwellTime));
        stats.Set("avgDwellTime", Napi::Number::New(env, result_.avgDwellTime));
        out.Set("stats", stats);

        if (!result_.errorMessage.empty()) {
            out.Set("error", Napi::String::New(env, result_.errorMessage));
        }
        Callback().Call({env.Null(), out});
    }

private:
    ScanPathResult scanPath_;
    DoseOptions opts_;
    DoseMatrixResult result_;
};

// ============================================================
// AsyncWorker: SubpixelRefineContourAsync
// ============================================================

class SubpixelRefineWorker : public Napi::AsyncWorker {
public:
    SubpixelRefineWorker(Napi::Function& callback,
                         std::vector<PointD> contour,
                         std::vector<uint8_t> pixelBuf,
                         int width, int height, int channels,
                         SubpixelOptions opts)
        : Napi::AsyncWorker(callback),
          contour_(std::move(contour)),
          pixelBuf_(std::move(pixelBuf)),
          w_(width), h_(height), ch_(channels),
          opts_(opts) {}

    void Execute() override {
        try {
            std::lock_guard<std::mutex> lock(g_procMutex);

            cv::Mat img;
            if (ch_ == 1) {
                img = cv::Mat(h_, w_, CV_8UC1, pixelBuf_.data()).clone();
            } else {
                img = cv::Mat(h_, w_, CV_8UC4, pixelBuf_.data()).clone();
            }

            cv::Mat gray;
            if (ch_ == 4) cv::cvtColor(img, gray, cv::COLOR_BGRA2GRAY);
            else if (ch_ == 3) cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
            else gray = img;

            cv::Mat blurred;
            cv::GaussianBlur(gray, blurred, cv::Size(0, 0), opts_.sigma);

            refined_ = g_imageProcessor.subpixelRefineContour(contour_, blurred, opts_.type);
        } catch (const std::exception& e) {
            SetError(std::string("C++ error: ") + e.what());
        }
    }

    void OnOK() override {
        Napi::Env env = Env();
        auto out = Napi::Array::New(env, refined_.size());
        for (size_t i = 0; i < refined_.size(); ++i) {
            out.Set(i, pointToNapi(env, refined_[i]));
        }
        Callback().Call({env.Null(), out});
    }

private:
    std::vector<PointD> contour_;
    std::vector<uint8_t> pixelBuf_;
    int w_, h_, ch_;
    SubpixelOptions opts_;
    std::vector<PointD> refined_;
};

// ============================================================
// AsyncWorker: ProcessMultiLayerAsync
// ============================================================

struct LayerInput {
    std::vector<uint8_t> pixels;
    int width = 0, height = 0, channels = 4;
    std::string name;
};

class ProcessMultiLayerWorker : public Napi::AsyncWorker {
public:
    ProcessMultiLayerWorker(Napi::Function& callback,
                            std::vector<LayerInput> layers,
                            DetectionOptions opts)
        : Napi::AsyncWorker(callback),
          layers_(std::move(layers)),
          optsPure_(opts.toPure()) {}

    void Execute() override {
        try {
            std::lock_guard<std::mutex> lock(g_procMutex);
            layerResults_.reserve(layers_.size());
            combinedOpaque_.clear();
            combinedClear_.clear();

            for (size_t li = 0; li < layers_.size(); ++li) {
                const auto& layer = layers_[li];
                DetectionResult r;
                r.success = true;
                r.native = true;
                r.width = layer.width;
                r.height = layer.height;

                cv::Mat img;
                if (layer.channels == 1) {
                    img = cv::Mat(layer.height, layer.width, CV_8UC1,
                                 const_cast<uint8_t*>(layer.pixels.data())).clone();
                } else {
                    img = cv::Mat(layer.height, layer.width, CV_8UC4,
                                 const_cast<uint8_t*>(layer.pixels.data())).clone();
                }

                cv::Mat gray;
                if (layer.channels == 4) cv::cvtColor(img, gray, cv::COLOR_BGRA2GRAY);
                else if (layer.channels == 3) cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
                else gray = img;

                cv::Mat blurred = g_imageProcessor.gaussianBlur(gray, optsPure_.gaussianSigma);
                cv::Mat adaptive = g_imageProcessor.adaptiveThreshold(blurred, optsPure_.blockSize, optsPure_.C);
                cv::Mat edges = g_imageProcessor.cannyEdges(blurred, optsPure_.cannyLow, optsPure_.cannyHigh, optsPure_.cannyAperture);

                std::vector<std::vector<cv::Point>> contours;
                std::vector<cv::Vec4i> hierarchy;
                cv::Mat edgeCopy = edges.clone();
                cv::findContours(edgeCopy, contours, hierarchy, cv::RETR_CCOMP, cv::CHAIN_APPROX_TC89_L1);

                for (size_t i = 0; i < contours.size(); i++) {
                    if (contours[i].size() < 6) continue;
                    double area = cv::contourArea(contours[i]);
                    if (area < 10.0 || area > 1e7) continue;
                    std::vector<cv::Point> approx;
                    cv::approxPolyDP(contours[i], approx, 1.0, true);
                    if (approx.size() < 4) continue;

                    std::vector<PointD> subContour;
                    for (const auto& p : approx) {
                        subContour.emplace_back(static_cast<double>(p.x), static_cast<double>(p.y));
                    }

                    cv::Moments m = cv::moments(approx);
                    PointD centroid;
                    if (std::fabs(m.m00) > 1e-6) {
                        centroid.x = m.m10 / m.m00;
                        centroid.y = m.m01 / m.m00;
                    } else centroid = polygonCentroid(subContour);

                    int cx = static_cast<int>(std::round(centroid.x));
                    int cy = static_cast<int>(std::round(centroid.y));
                    if (cx < 0 || cx >= adaptive.cols || cy < 0 || cy >= adaptive.rows) continue;

                    std::vector<PointD> refined = g_imageProcessor.subpixelRefineContour(subContour, blurred, "opaque");
                    DefectContour defect;
                    defect.points = refined;
                    defect.area = polygonArea(refined);
                    defect.centroid = polygonCentroid(refined);
                    defect.bbox = polygonBBox(refined);
                    defect.layer = static_cast<int>(li);

                    uchar insideVal = adaptive.ptr<uchar>(cy)[cx];
                    if (insideVal == 255) {
                        defect.type = "opaque";
                        defect.id = "opaque_L" + std::to_string(li) + "_" + std::to_string(r.opaqueDefects.size());
                        r.opaqueDefects.push_back(defect);
                        combinedOpaque_.push_back(defect);
                    } else {
                        defect.type = "clear";
                        defect.id = "clear_L" + std::to_string(li) + "_" + std::to_string(r.clearDefects.size());
                        r.clearDefects.push_back(defect);
                        combinedClear_.push_back(defect);
                    }
                }

                layerResults_.push_back({
                    static_cast<int>(li),
                    layer.name.empty() ? ("Layer_" + std::to_string(li)) : layer.name,
                    r
                });
            }
        } catch (const std::exception& e) {
            SetError(std::string("C++ error: ") + e.what());
        }
    }

    void OnOK() override {
        Napi::Env env = Env();
        auto layerResults = Napi::Array::New(env, layerResults_.size());
        int totalOpaque = 0, totalClear = 0;

        auto opaqueCombined = Napi::Array::New(env, combinedOpaque_.size());
        auto clearCombined = Napi::Array::New(env, combinedClear_.size());
        for (size_t i = 0; i < combinedOpaque_.size(); i++) {
            opaqueCombined.Set(i, defectToNapi(env, combinedOpaque_[i]));
        }
        for (size_t i = 0; i < combinedClear_.size(); i++) {
            clearCombined.Set(i, defectToNapi(env, combinedClear_[i]));
        }

        for (size_t i = 0; i < layerResults_.size(); i++) {
            const auto& lr = layerResults_[i];
            auto resultObj = Napi::Object::New(env);
            resultObj.Set("layerIndex", Napi::Number::New(env, lr.index));
            resultObj.Set("layerName", Napi::String::New(env, lr.name));
            resultObj.Set("success", Napi::Boolean::New(env, lr.result.success));

            auto opaqueArr = Napi::Array::New(env, lr.result.opaqueDefects.size());
            for (size_t j = 0; j < lr.result.opaqueDefects.size(); j++) {
                opaqueArr.Set(j, defectToNapi(env, lr.result.opaqueDefects[j]));
            }
            resultObj.Set("opaqueDefects", opaqueArr);

            auto clearArr = Napi::Array::New(env, lr.result.clearDefects.size());
            for (size_t j = 0; j < lr.result.clearDefects.size(); j++) {
                clearArr.Set(j, defectToNapi(env, lr.result.clearDefects[j]));
            }
            resultObj.Set("clearDefects", clearArr);

            auto meta = Napi::Object::New(env);
            meta.Set("width", Napi::Number::New(env, lr.result.width));
            meta.Set("height", Napi::Number::New(env, lr.result.height));
            meta.Set("processingTimeMs", Napi::Number::New(env, lr.result.processingTimeMs));
            resultObj.Set("meta", meta);

            layerResults.Set(i, resultObj);
            totalOpaque += static_cast<int>(lr.result.opaqueDefects.size());
            totalClear += static_cast<int>(lr.result.clearDefects.size());
        }

        auto out = Napi::Object::New(env);
        out.Set("success", Napi::Boolean::New(env, true));
        out.Set("native", Napi::Boolean::New(env, true));
        out.Set("async", Napi::Boolean::New(env, true));
        out.Set("layerResults", layerResults);

        auto combined = Napi::Object::New(env);
        combined.Set("opaqueDefects", opaqueCombined);
        combined.Set("clearDefects", clearCombined);
        out.Set("combinedDefects", combined);

        auto summary = Napi::Object::New(env);
        summary.Set("totalLayers", Napi::Number::New(env, static_cast<int>(layerResults_.size())));
        summary.Set("totalOpaque", Napi::Number::New(env, totalOpaque));
        summary.Set("totalClear", Napi::Number::New(env, totalClear));
        out.Set("summary", summary);

        Callback().Call({env.Null(), out});
    }

private:
    struct LayerResultEntry { int index; std::string name; DetectionResult result; };
    std::vector<LayerInput> layers_;
    DetectionOptionsPure optsPure_;
    std::vector<LayerResultEntry> layerResults_;
    std::vector<DefectContour> combinedOpaque_;
    std::vector<DefectContour> combinedClear_;
};

// ============================================================
// Promise 包装器 (让 JS 端无需传入 callback)
// ============================================================

template <typename WorkerFn, typename... Args>
Napi::Value RunAsync(Napi::Env env, WorkerFn fn, Args&&... args) {
    auto deferred = Napi::Promise::Deferred::New(env);
    auto callback = Napi::Function::New(env, [deferred](const Napi::CallbackInfo& cb) mutable {
        Napi::Env e = cb.Env();
        if (cb.Length() > 0 && !cb[0].IsNull() && !cb[0].IsUndefined()) {
            deferred.Reject(cb[0].ToObject());
        } else if (cb.Length() > 1) {
            deferred.Resolve(cb[1]);
        } else {
            deferred.Resolve(e.Undefined());
        }
    });

    auto* worker = new typename std::remove_pointer<decltype(fn(callback, std::forward<Args>(args)...))>::type::element_type();
    // The actual instantiation happens in individual wrapper functions; use below pattern.

    // This generic helper is not directly usable due to constructor polymorphism.
    // Individual wrappers below use explicit pattern.
    (void)worker;
    return deferred.Promise();
}

// ============================================================
// 导出函数：每一个都返回 Promise，工作线程执行
// ============================================================

Napi::Value DetectDefectsAsync(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 2) {
        Napi::Error::New(env, "Expected at least 2 arguments: imageData, options").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    try {
        Napi::Object imageDataObj = info[0].As<Napi::Object>();
        if (!imageDataObj.Has("data") || !imageDataObj.Has("width") || !imageDataObj.Has("height")) {
            Napi::Error::New(env, "imageData must have data, width, height").ThrowAsJavaScriptException();
            return env.Undefined();
        }

        Napi::Uint8Array pixelData = imageDataObj.Get("data").As<Napi::Uint8Array>();
        int width = imageDataObj.Get("width").As<Napi::Number>().Int32Value();
        int height = imageDataObj.Get("height").As<Napi::Number>().Int32Value();
        int channels = imageDataObj.Has("channels") ? imageDataObj.Get("channels").As<Napi::Number>().Int32Value() : 4;

        // 深拷贝像素缓冲区到工作线程独立内存 (避免 GC 释放)
        size_t bufSize = static_cast<size_t>(width) * height * (channels == 1 ? 1 : 4);
        std::vector<uint8_t> pixelBuf(bufSize);
        std::memcpy(pixelBuf.data(), pixelData.Data(), bufSize);

        Napi::Object optionsObj = info[1].IsObject() ? info[1].As<Napi::Object>() : Napi::Object::New(env);
        DetectionOptions opts = DetectionOptions::fromNapi(optionsObj);

        auto deferred = Napi::Promise::Deferred::New(env);
        auto callback = Napi::Function::New(env, [deferred](const Napi::CallbackInfo& cb) mutable {
            Napi::Env e = cb.Env();
            if (cb.Length() > 0 && !cb[0].IsNull() && !cb[0].IsUndefined()) {
                deferred.Reject(cb[0].ToObject());
            } else {
                deferred.Resolve(cb[1]);
            }
        });

        auto* worker = new DetectDefectsWorker(callback, std::move(pixelBuf), width, height, channels, opts);
        worker->Queue();

        return deferred.Promise();

    } catch (const std::exception& e) {
        Napi::Error::New(env, std::string("C++ exception in detectDefects: ") + e.what()).ThrowAsJavaScriptException();
        return env.Undefined();
    }
}

Napi::Value DetectDefectsFromFileAsync(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1) {
        Napi::Error::New(env, "Expected filePath argument").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    try {
        std::string filePath = info[0].As<Napi::String>().Utf8Value();
        Napi::Object optionsObj = info.Length() >= 2 && info[1].IsObject() ? info[1].As<Napi::Object>() : Napi::Object::New(env);
        DetectionOptions opts = DetectionOptions::fromNapi(optionsObj);

        auto deferred = Napi::Promise::Deferred::New(env);
        auto callback = Napi::Function::New(env, [deferred](const Napi::CallbackInfo& cb) mutable {
            Napi::Env e = cb.Env();
            if (cb.Length() > 0 && !cb[0].IsNull() && !cb[0].IsUndefined()) {
                deferred.Reject(cb[0].ToObject());
            } else {
                deferred.Resolve(cb[1]);
            }
        });

        auto* worker = new DetectDefectsFromFileWorker(callback, std::move(filePath), opts);
        worker->Queue();
        return deferred.Promise();
    } catch (const std::exception& e) {
        Napi::Error::New(env, std::string("C++ exception: ") + e.what()).ThrowAsJavaScriptException();
        return env.Undefined();
    }
}

Napi::Value GenerateRasterScanAsync(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1) {
        Napi::Error::New(env, "Expected polygonPoints argument").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    try {
        Napi::Array polygonArr = info[0].As<Napi::Array>();
        std::vector<PointD> polygon = napiPolygonToVector(polygonArr);
        Napi::Object optionsObj = info.Length() >= 2 && info[1].IsObject() ? info[1].As<Napi::Object>() : Napi::Object::New(env);
        ScanOptions opts = ScanOptions::fromNapi(optionsObj);

        auto deferred = Napi::Promise::Deferred::New(env);
        auto callback = Napi::Function::New(env, [deferred](const Napi::CallbackInfo& cb) mutable {
            if (cb.Length() > 0 && !cb[0].IsNull() && !cb[0].IsUndefined()) {
                deferred.Reject(cb[0].ToObject());
            } else {
                deferred.Resolve(cb[1]);
            }
        });

        auto* worker = new GenerateRasterScanWorker(callback, std::move(polygon), opts);
        worker->Queue();
        return deferred.Promise();
    } catch (const std::exception& e) {
        Napi::Error::New(env, std::string("C++ exception: ") + e.what()).ThrowAsJavaScriptException();
        return env.Undefined();
    }
}

Napi::Value BuildDoseMatrixAsync(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1) {
        Napi::Error::New(env, "Expected scanLines argument").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    try {
        ScanPathResult scanPath = parseScanLinesFromNapi(info[0]);
        Napi::Object optionsObj = info.Length() >= 2 && info[1].IsObject() ? info[1].As<Napi::Object>() : Napi::Object::New(env);
        DoseOptions opts = DoseOptions::fromNapi(optionsObj);

        auto deferred = Napi::Promise::Deferred::New(env);
        auto callback = Napi::Function::New(env, [deferred](const Napi::CallbackInfo& cb) mutable {
            if (cb.Length() > 0 && !cb[0].IsNull() && !cb[0].IsUndefined()) {
                deferred.Reject(cb[0].ToObject());
            } else {
                deferred.Resolve(cb[1]);
            }
        });

        auto* worker = new BuildDoseMatrixWorker(callback, std::move(scanPath), opts);
        worker->Queue();
        return deferred.Promise();
    } catch (const std::exception& e) {
        Napi::Error::New(env, std::string("C++ exception: ") + e.what()).ThrowAsJavaScriptException();
        return env.Undefined();
    }
}

Napi::Value SubpixelRefineContourAsync(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 2) {
        Napi::Error::New(env, "Expected contour and imageData arguments").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    try {
        Napi::Array contourArr = info[0].As<Napi::Array>();
        std::vector<PointD> contour = napiPolygonToVector(contourArr);
        Napi::Object imageDataObj = info[1].As<Napi::Object>();
        Napi::Uint8Array pixelData = imageDataObj.Get("data").As<Napi::Uint8Array>();
        int width = imageDataObj.Get("width").As<Napi::Number>().Int32Value();
        int height = imageDataObj.Get("height").As<Napi::Number>().Int32Value();
        int channels = imageDataObj.Has("channels") ? imageDataObj.Get("channels").As<Napi::Number>().Int32Value() : 4;
        Napi::Object optionsObj = info.Length() >= 3 && info[2].IsObject() ? info[2].As<Napi::Object>() : Napi::Object::New(env);
        SubpixelOptions opts = SubpixelOptions::fromNapi(optionsObj);

        size_t bufSize = static_cast<size_t>(width) * height * (channels == 1 ? 1 : 4);
        std::vector<uint8_t> pixelBuf(bufSize);
        std::memcpy(pixelBuf.data(), pixelData.Data(), bufSize);

        auto deferred = Napi::Promise::Deferred::New(env);
        auto callback = Napi::Function::New(env, [deferred](const Napi::CallbackInfo& cb) mutable {
            if (cb.Length() > 0 && !cb[0].IsNull() && !cb[0].IsUndefined()) {
                deferred.Reject(cb[0].ToObject());
            } else {
                deferred.Resolve(cb[1]);
            }
        });

        auto* worker = new SubpixelRefineWorker(callback, std::move(contour), std::move(pixelBuf), width, height, channels, opts);
        worker->Queue();
        return deferred.Promise();
    } catch (const std::exception& e) {
        Napi::Error::New(env, std::string("C++ exception: ") + e.what()).ThrowAsJavaScriptException();
        return env.Undefined();
    }
}

Napi::Value ProcessMultiLayerAsync(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1) {
        Napi::Error::New(env, "Expected layers array argument").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    try {
        Napi::Array layersArr = info[0].As<Napi::Array>();
        Napi::Object optionsObj = info.Length() >= 2 && info[1].IsObject() ? info[1].As<Napi::Object>() : Napi::Object::New(env);
        DetectionOptions opts = DetectionOptions::fromNapi(optionsObj);

        std::vector<LayerInput> layers;
        layers.reserve(layersArr.Length());

        for (uint32_t i = 0; i < layersArr.Length(); i++) {
            if (!layersArr.Get(i).IsObject()) continue;
            Napi::Object layerObj = layersArr.Get(i).As<Napi::Object>();
            if (!layerObj.Has("data") || !layerObj.Has("width") || !layerObj.Has("height")) continue;

            Napi::Uint8Array pixelData = layerObj.Get("data").As<Napi::Uint8Array>();
            int width = layerObj.Get("width").As<Napi::Number>().Int32Value();
            int height = layerObj.Get("height").As<Napi::Number>().Int32Value();
            int channels = layerObj.Has("channels") ? layerObj.Get("channels").As<Napi::Number>().Int32Value() : 4;
            std::string layerName = layerObj.Has("name") ? layerObj.Get("name").As<Napi::String>().Utf8Value() : ("Layer_" + std::to_string(i));

            size_t bufSize = static_cast<size_t>(width) * height * (channels == 1 ? 1 : 4);
            std::vector<uint8_t> pixelBuf(bufSize);
            std::memcpy(pixelBuf.data(), pixelData.Data(), bufSize);

            layers.push_back({std::move(pixelBuf), width, height, channels, std::move(layerName)});
        }

        auto deferred = Napi::Promise::Deferred::New(env);
        auto callback = Napi::Function::New(env, [deferred](const Napi::CallbackInfo& cb) mutable {
            if (cb.Length() > 0 && !cb[0].IsNull() && !cb[0].IsUndefined()) {
                deferred.Reject(cb[0].ToObject());
            } else {
                deferred.Resolve(cb[1]);
            }
        });

        auto* worker = new ProcessMultiLayerWorker(callback, std::move(layers), opts);
        worker->Queue();
        return deferred.Promise();
    } catch (const std::exception& e) {
        Napi::Error::New(env, std::string("C++ exception: ") + e.what()).ThrowAsJavaScriptException();
        return env.Undefined();
    }
}

// ============================================================
// AsyncWorker: ApplyPECCorrectionAsync (频域邻近效应校正)
// ============================================================

class ApplyPECCorrectionWorker : public Napi::AsyncWorker {
public:
    ApplyPECCorrectionWorker(Napi::Function& callback,
                             ScanPathResult scanPath,
                             int width, int height,
                             PSFOptions psfOpts,
                             PECAlgorithmOptions pecOpts)
        : Napi::AsyncWorker(callback),
          scanPath_(std::move(scanPath)),
          w_(width), h_(height),
          psfPure_(psfOpts.toPure()),
          pecPure_(pecOpts.toPure()) {}

    void Execute() override {
        try {
            std::lock_guard<std::mutex> lock(g_procMutex);

            result_ = g_pecCorrector.applyCorrection(
                scanPath_.scanLines,
                w_, h_,
                psfPure_, pecPure_
            );

            if (result_.success) {
                correctedScanPath_ = scanPath_;
                correctedScanPath_.scanLines = g_pecCorrector.scanPointsFromDoseMap(
                    result_.correctedDoseMap,
                    scanPath_.scanLines
                );
            }
        } catch (const std::exception& e) {
            SetError(std::string("PEC C++ error: ") + e.what());
        }
    }

    void OnOK() override {
        Napi::Env env = Env();
        auto out = Napi::Object::New(env);
        out.Set("success", Napi::Boolean::New(env, result_.success));
        out.Set("native", Napi::Boolean::New(env, result_.native));
        out.Set("async", Napi::Boolean::New(env, true));

        auto linesArr = Napi::Array::New(env, correctedScanPath_.scanLines.size());
        for (size_t i = 0; i < correctedScanPath_.scanLines.size(); ++i) {
            linesArr.Set(i, scanLineToNapi(env, correctedScanPath_.scanLines[i]));
        }
        out.Set("correctedScanLines", linesArr);

        auto stats = Napi::Object::New(env);
        stats.Set("iterations", Napi::Number::New(env, result_.iterations));
        stats.Set("processingTimeMs", Napi::Number::New(env, result_.processingTimeMs));
        stats.Set("maxCorrectionFactor", Napi::Number::New(env, result_.maxCorrectionFactor));
        stats.Set("minCorrectionFactor", Napi::Number::New(env, result_.minCorrectionFactor));
        stats.Set("avgCorrectionFactor", Napi::Number::New(env, result_.avgCorrectionFactor));
        stats.Set("width", Napi::Number::New(env, result_.width));
        stats.Set("height", Napi::Number::New(env, result_.height));
        out.Set("stats", stats);

        auto psfMeta = Napi::Object::New(env);
        psfMeta.Set("eta", Napi::Number::New(env, psfPure_.eta));
        psfMeta.Set("alpha", Napi::Number::New(env, psfPure_.alpha));
        psfMeta.Set("beta", Napi::Number::New(env, psfPure_.beta));
        psfMeta.Set("gamma", Napi::Number::New(env, psfPure_.gamma));
        psfMeta.Set("kernelSize", Napi::Number::New(env, result_.psfKernel.cols));
        out.Set("psf", psfMeta);

        auto algoMeta = Napi::Object::New(env);
        algoMeta.Set("applyDogBoneEnhance", Napi::Boolean::New(env, pecPure_.applyDogBoneEnhance));
        algoMeta.Set("dogBoneStrength", Napi::Number::New(env, pecPure_.dogBoneStrength));
        algoMeta.Set("useWienerFilter", Napi::Boolean::New(env, pecPure_.useWienerFilter));
        algoMeta.Set("maxDoseMultiplier", Napi::Number::New(env, pecPure_.maxDoseMultiplier));
        out.Set("algorithm", algoMeta);

        if (!result_.errorMessage.empty()) {
            out.Set("error", Napi::String::New(env, result_.errorMessage));
        }

        Callback().Call({env.Null(), out});
    }

    void OnError(const Napi::Error& e) override {
        Napi::Env env = Env();
        Callback().Call({e.Value(), env.Null()});
    }

private:
    ScanPathResult scanPath_;
    ScanPathResult correctedScanPath_;
    int w_, h_;
    DualGaussianPSF psfPure_;
    PECOptions pecPure_;
    PECResult result_;
};

Napi::Value ApplyPECCorrectionAsync(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 3) {
        Napi::Error::New(env, "Expected scanLines, width, height arguments").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    try {
        ScanPathResult scanPath = parseScanLinesFromNapi(info[0]);
        int width = info[1].As<Napi::Number>().Int32Value();
        int height = info[2].As<Napi::Number>().Int32Value();
        Napi::Object psfObj = info.Length() >= 4 && info[3].IsObject() ? info[3].As<Napi::Object>() : Napi::Object::New(env);
        Napi::Object pecObj = info.Length() >= 5 && info[4].IsObject() ? info[4].As<Napi::Object>() : Napi::Object::New(env);

        PSFOptions psfOpts = PSFOptions::fromNapi(psfObj);
        PECAlgorithmOptions pecOpts = PECAlgorithmOptions::fromNapi(pecObj);

        auto deferred = Napi::Promise::Deferred::New(env);
        auto callback = Napi::Function::New(env, [deferred](const Napi::CallbackInfo& cb) mutable {
            if (cb.Length() > 0 && !cb[0].IsNull() && !cb[0].IsUndefined()) {
                deferred.Reject(cb[0].ToObject());
            } else {
                deferred.Resolve(cb[1]);
            }
        });

        auto* worker = new ApplyPECCorrectionWorker(callback, std::move(scanPath), width, height, psfOpts, pecOpts);
        worker->Queue();
        return deferred.Promise();
    } catch (const std::exception& e) {
        Napi::Error::New(env, std::string("C++ exception in applyPECCorrection: ") + e.what()).ThrowAsJavaScriptException();
        return env.Undefined();
    }
}

Napi::Value GeneratePSFKernelAsync(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    try {
        int kernelSize = info.Length() >= 1 ? info[0].As<Napi::Number>().Int32Value() : 101;
        Napi::Object psfObj = info.Length() >= 2 && info[1].IsObject() ? info[1].As<Napi::Object>() : Napi::Object::New(env);
        PSFOptions psfOpts = PSFOptions::fromNapi(psfObj);

        if (kernelSize < 3) kernelSize = 3;
        if (kernelSize % 2 == 0) kernelSize++;

        std::lock_guard<std::mutex> lock(g_procMutex);
        DualGaussianPSF psfPure = psfOpts.toPure();
        cv::Mat kernel = g_pecCorrector.generateDualGaussianPSF(kernelSize, psfPure);

        auto out = Napi::Object::New(env);
        out.Set("success", Napi::Boolean::New(env, true));
        out.Set("kernelSize", Napi::Number::New(env, kernel.cols));
        out.Set("eta", Napi::Number::New(env, psfPure.eta));
        out.Set("alpha", Napi::Number::New(env, psfPure.alpha));
        out.Set("beta", Napi::Number::New(env, psfPure.beta));

        const int half = kernel.cols / 2;
        auto crossSection = Napi::Array::New(env, kernel.cols);
        const double* midRow = kernel.ptr<double>(half);
        double maxK = 0.0;
        for (int i = 0; i < kernel.cols; i++) {
            if (midRow[i] > maxK) maxK = midRow[i];
        }
        if (maxK < 1e-12) maxK = 1.0;
        for (int i = 0; i < kernel.cols; i++) {
            crossSection.Set(i, Napi::Number::New(env, midRow[i] / maxK));
        }
        out.Set("crossSection", crossSection);

        return out;
    } catch (const std::exception& e) {
        Napi::Error::New(env, std::string("C++ exception in generatePSFKernel: ") + e.what()).ThrowAsJavaScriptException();
        return env.Undefined();
    }
}

// ============================================================
// 同步版本保留用于向后兼容 (但强烈不建议使用)
// ============================================================

Napi::Value DetectDefectsSync(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 2) {
        Napi::TypeError::New(env, "Expected imageData, options").ThrowAsJavaScriptException();
        return env.Null();
    }
    try {
        Napi::Object imageDataObj = info[0].As<Napi::Object>();
        Napi::Object optionsObj = info[1].IsObject() ? info[1].As<Napi::Object>() : Napi::Object::New(env);
        Napi::Uint8Array pixelData = imageDataObj.Get("data").As<Napi::Uint8Array>();
        int width = imageDataObj.Get("width").As<Napi::Number>().Int32Value();
        int height = imageDataObj.Get("height").As<Napi::Number>().Int32Value();
        int channels = imageDataObj.Has("channels") ? imageDataObj.Get("channels").As<Napi::Number>().Int32Value() : 4;

        size_t bufSize = static_cast<size_t>(width) * height * (channels == 1 ? 1 : 4);
        std::vector<uint8_t> pixelBuf(bufSize);
        std::memcpy(pixelBuf.data(), pixelData.Data(), bufSize);

        DetectionOptions opts = DetectionOptions::fromNapi(optionsObj);
        DetectionOptionsPure optsPure = opts.toPure();
        DetectionResult result = g_imageProcessor.detectDefects(pixelBuf, width, height, channels, optsPure);
        auto out = Napi::Object::New(env);
        out.Set("success", Napi::Boolean::New(env, result.success));
        out.Set("native", Napi::Boolean::New(env, result.native));
        out.Set("async", Napi::Boolean::New(env, false));
        out.Set("deprecated", Napi::Boolean::New(env, true));
        auto opaqueArr = Napi::Array::New(env, result.opaqueDefects.size());
        for (size_t i = 0; i < result.opaqueDefects.size(); ++i)
            opaqueArr.Set(i, defectToNapi(env, result.opaqueDefects[i]));
        out.Set("opaqueDefects", opaqueArr);
        auto clearArr = Napi::Array::New(env, result.clearDefects.size());
        for (size_t i = 0; i < result.clearDefects.size(); ++i)
            clearArr.Set(i, defectToNapi(env, result.clearDefects[i]));
        out.Set("clearDefects", clearArr);
        auto meta = Napi::Object::New(env);
        meta.Set("width", Napi::Number::New(env, result.width));
        meta.Set("height", Napi::Number::New(env, result.height));
        meta.Set("processingTimeMs", Napi::Number::New(env, result.processingTimeMs));
        out.Set("meta", meta);
        if (!result.success && !result.errorMessage.empty())
            out.Set("error", Napi::String::New(env, result.errorMessage));
        return out;
    } catch (const std::exception& e) {
        Napi::Error::New(env, std::string("C++ exception: ") + e.what()).ThrowAsJavaScriptException();
        return env.Null();
    }
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("getVersion", Napi::Function::New(env, GetVersion));

    // 主推荐 API：Promise + 异步工作线程
    exports.Set("detectDefects", Napi::Function::New(env, DetectDefectsAsync));
    exports.Set("detectDefectsFromFile", Napi::Function::New(env, DetectDefectsFromFileAsync));
    exports.Set("generateRasterScan", Napi::Function::New(env, GenerateRasterScanAsync));
    exports.Set("buildDoseMatrix", Napi::Function::New(env, BuildDoseMatrixAsync));
    exports.Set("subpixelRefineContour", Napi::Function::New(env, SubpixelRefineContourAsync));
    exports.Set("processMultiLayer", Napi::Function::New(env, ProcessMultiLayerAsync));

    // PEC 光刻邻近效应校正 (频域反卷积 + 双高斯 PSF)
    exports.Set("applyPECCorrection", Napi::Function::New(env, ApplyPECCorrectionAsync));
    exports.Set("generatePSFKernel", Napi::Function::New(env, GeneratePSFKernelAsync));

    // 同步版本保留但标记 deprecated，用于向后兼容
    exports.Set("detectDefectsSync", Napi::Function::New(env, DetectDefectsSync));

    // 版本和运行模式信息
    exports.Set("isAsyncNative", Napi::Boolean::New(env, true));
    exports.Set("supportsPEC", Napi::Boolean::New(env, true));
    exports.Set("threadPool", Napi::String::New(env, "libuv AsyncWorker Pool + DFT Deconvolution"));

    return exports;
}

NODE_API_MODULE(ebeam_repair, Init)
