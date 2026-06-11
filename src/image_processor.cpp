#include "image_processor.h"
#include <chrono>
#include <algorithm>
#include <cmath>

ImageProcessor::ImageProcessor() {}
ImageProcessor::~ImageProcessor() {}

cv::Mat ImageProcessor::loadImage(const std::string& path) {
    return cv::imread(path, cv::IMREAD_UNCHANGED);
}

cv::Mat ImageProcessor::pixelsToMat(const Napi::Uint8Array& pixels, int w, int h, int ch) {
    cv::Mat mat(h, w, ch == 1 ? CV_8UC1 : CV_8UC4);
    const uint8_t* src = pixels.Data();
    
    if (ch == 1) {
        std::memcpy(mat.data, src, w * h * sizeof(uint8_t));
    } else {
        std::memcpy(mat.data, src, w * h * 4 * sizeof(uint8_t));
    }
    return mat;
}

cv::Mat ImageProcessor::gaussianBlur(const cv::Mat& src, double sigma, int ksize) {
    if (ksize <= 0) {
        ksize = cv::getOptimalDFTSize(static_cast<int>(std::ceil(sigma * 3.0) * 2 + 1));
        if (ksize % 2 == 0) ksize++;
    }
    cv::Mat dst;
    cv::GaussianBlur(src, dst, cv::Size(ksize, ksize), sigma, sigma, cv::BORDER_REFLECT_101);
    return dst;
}

cv::Mat ImageProcessor::adaptiveThreshold(const cv::Mat& src, int blockSize, double C) {
    if (blockSize % 2 == 0) blockSize++;
    if (blockSize < 3) blockSize = 3;
    
    cv::Mat dst;
    cv::adaptiveThreshold(
        src, dst, 255,
        cv::ADAPTIVE_THRESH_GAUSSIAN_C,
        cv::THRESH_BINARY,
        blockSize, C
    );
    return dst;
}

cv::Mat ImageProcessor::cannyEdges(const cv::Mat& src, double lowThreshold, double highThreshold, int apertureSize) {
    cv::Mat edges;
    cv::Canny(src, edges, lowThreshold, highThreshold, apertureSize, true);
    return edges;
}

std::vector<PointD> ImageProcessor::subpixelRefineContour(
    const std::vector<PointD>& contour,
    const cv::Mat& grayImage,
    const std::string& defectType
) {
    std::vector<PointD> refined;
    refined.reserve(contour.size());
    
    const int radius = 2;
    const int w = grayImage.cols;
    const int h = grayImage.rows;
    const double searchDist = 1.0;
    const int steps = 10;
    
    for (const auto& pt : contour) {
        const int ix = static_cast<int>(std::round(pt.x));
        const int iy = static_cast<int>(std::round(pt.y));
        
        if (ix < radius + 2 || ix >= w - radius - 2 ||
            iy < radius + 2 || iy >= h - radius - 2) {
            refined.push_back(PointD(pt.x + 0.5, pt.y + 0.5));
            continue;
        }
        
        double gradX = 0.0, gradY = 0.0, weightSum = 0.0;
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                const uchar* row = grayImage.ptr<uchar>(iy + dy);
                const double gx = static_cast<double>(row[ix + dx + 1]) - static_cast<double>(row[ix + dx - 1]);
                const double gy = static_cast<double>(grayImage.ptr<uchar>(iy + dy + 1)[ix + dx]) -
                                  static_cast<double>(grayImage.ptr<uchar>(iy + dy - 1)[ix + dx]);
                const double mag = std::sqrt(gx * gx + gy * gy);
                const double weight = mag * mag;
                gradX += gx * weight;
                gradY += gy * weight;
                weightSum += weight;
            }
        }
        
        if (weightSum < 1e-6) {
            refined.push_back(PointD(pt.x + 0.5, pt.y + 0.5));
            continue;
        }
        
        gradX /= weightSum;
        gradY /= weightSum;
        const double gradMag = std::sqrt(gradX * gradX + gradY * gradY);
        if (gradMag < 1e-6) {
            refined.push_back(PointD(pt.x + 0.5, pt.y + 0.5));
            continue;
        }
        
        const double nx = -gradY / gradMag;
        const double ny = gradX / gradMag;
        double offset = 0.0;
        double bestScore = -1.0;
        
        for (int i = -steps; i <= steps; ++i) {
            const double t = (static_cast<double>(i) / steps) * searchDist;
            const int sx = static_cast<int>(std::round(pt.x + nx * t));
            const int sy = static_cast<int>(std::round(pt.y + ny * t));
            if (sx < 0 || sx >= w || sy < 0 || sy >= h) continue;
            const double val = static_cast<double>(grayImage.ptr<uchar>(sy)[sx]);
            const double score = std::fabs(val - 128.0);
            if (score > bestScore) {
                bestScore = score;
                offset = t;
            }
        }
        
        refined.push_back(PointD(pt.x + nx * offset + 0.5, pt.y + ny * offset + 0.5));
    }
    
    return refined;
}

void ImageProcessor::extractDefectContours(
    const cv::Mat& binary,
    const cv::Mat& edges,
    const cv::Mat& gray,
    std::vector<DefectContour>& opaqueOut,
    std::vector<DefectContour>& clearOut
) {
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    
    cv::Mat edgeCopy = edges.clone();
    cv::findContours(edgeCopy, contours, hierarchy, cv::RETR_CCOMP, cv::CHAIN_APPROX_TC89_L1);
    
    for (size_t i = 0; i < contours.size(); ++i) {
        if (contours[i].size() < 6) continue;
        
        double area = cv::contourArea(contours[i]);
        if (area < 10.0 || area > 1e7) continue;
        
        std::vector<cv::Point> approx;
        cv::approxPolyDP(contours[i], approx, 1.0, true);
        if (approx.size() < 4) continue;
        
        std::vector<PointD> subContour;
        subContour.reserve(approx.size());
        for (const auto& p : approx) {
            subContour.push_back(PointD(static_cast<double>(p.x), static_cast<double>(p.y)));
        }
        
        cv::Moments m = cv::moments(approx);
        PointD centroid;
        if (std::fabs(m.m00) > 1e-6) {
            centroid.x = m.m10 / m.m00;
            centroid.y = m.m01 / m.m00;
        } else if (!subContour.empty()) {
            centroid = polygonCentroid(subContour);
        }
        
        const int cx = static_cast<int>(std::round(centroid.x));
        const int cy = static_cast<int>(std::round(centroid.y));
        if (cx < 0 || cx >= binary.cols || cy < 0 || cy >= binary.rows) continue;
        
        std::vector<PointD> refined = subpixelRefineContour(subContour, gray, "opaque");
        
        DefectContour defect;
        defect.points = refined;
        defect.area = polygonArea(refined);
        defect.centroid = polygonCentroid(refined);
        defect.bbox = polygonBBox(refined);
        defect.layer = 0;
        
        const uchar insideVal = binary.ptr<uchar>(cy)[cx];
        if (insideVal == 255) {
            defect.type = "opaque";
            defect.id = "opaque_" + std::to_string(opaqueOut.size());
            opaqueOut.push_back(defect);
        } else {
            defect.type = "clear";
            defect.id = "clear_" + std::to_string(clearOut.size());
            clearOut.push_back(defect);
        }
    }
}

DetectionResult ImageProcessor::detectDefects(
    const Napi::Uint8Array& pixelData,
    int width,
    int height,
    int channels,
    const Napi::Object& options
) {
    DetectionResult result;
    result.success = false;
    result.native = true;
    result.processingTimeMs = 0.0;
    
    auto t0 = std::chrono::high_resolution_clock::now();
    
    try {
        double gaussianSigma = 1.5;
        int blockSize = 31;
        double C = 10.0;
        double cannyLow = 40.0;
        double cannyHigh = 120.0;
        int cannyAperture = 3;
        
        if (options.Has("gaussianSigma")) gaussianSigma = options.Get("gaussianSigma").As<Napi::Number>().DoubleValue();
        if (options.Has("blockSize")) blockSize = options.Get("blockSize").As<Napi::Number>().Int32Value();
        if (options.Has("C")) C = options.Get("C").As<Napi::Number>().DoubleValue();
        if (options.Has("cannyLow")) cannyLow = options.Get("cannyLow").As<Napi::Number>().DoubleValue();
        if (options.Has("cannyHigh")) cannyHigh = options.Get("cannyHigh").As<Napi::Number>().DoubleValue();
        if (options.Has("cannyAperture")) cannyAperture = options.Get("cannyAperture").As<Napi::Number>().Int32Value();
        
        cv::Mat image = pixelsToMat(pixelData, width, height, channels);
        cv::Mat gray;
        
        if (channels == 4) {
            cv::cvtColor(image, gray, cv::COLOR_BGRA2GRAY);
        } else if (channels == 3) {
            cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
        } else {
            gray = image;
        }
        
        cv::Mat blurred = gaussianBlur(gray, gaussianSigma);
        cv::Mat adaptive = adaptiveThreshold(blurred, blockSize, C);
        cv::Mat edges = cannyEdges(blurred, cannyLow, cannyHigh, cannyAperture);
        
        extractDefectContours(adaptive, edges, blurred, result.opaqueDefects, result.clearDefects);
        
        result.width = width;
        result.height = height;
        result.success = true;
        
        auto t1 = std::chrono::high_resolution_clock::now();
        result.processingTimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        
    } catch (const cv::Exception& e) {
        result.errorMessage = std::string("OpenCV error: ") + e.what();
        result.success = false;
    } catch (const std::exception& e) {
        result.errorMessage = std::string("Error: ") + e.what();
        result.success = false;
    }
    
    return result;
}

DetectionResult ImageProcessor::detectDefectsFromFile(
    const std::string& filePath,
    const Napi::Object& options
) {
    DetectionResult result;
    result.success = false;
    result.native = true;
    result.processingTimeMs = 0.0;
    
    auto t0 = std::chrono::high_resolution_clock::now();
    
    try {
        cv::Mat image = loadImage(filePath);
        if (image.empty()) {
            result.errorMessage = "Failed to load image: " + filePath;
            return result;
        }
        
        result.width = image.cols;
        result.height = image.rows;
        
        double gaussianSigma = 1.5;
        int blockSize = 31;
        double C = 10.0;
        double cannyLow = 40.0;
        double cannyHigh = 120.0;
        int cannyAperture = 3;
        
        if (options.Has("gaussianSigma")) gaussianSigma = options.Get("gaussianSigma").As<Napi::Number>().DoubleValue();
        if (options.Has("blockSize")) blockSize = options.Get("blockSize").As<Napi::Number>().Int32Value();
        if (options.Has("C")) C = options.Get("C").As<Napi::Number>().DoubleValue();
        if (options.Has("cannyLow")) cannyLow = options.Get("cannyLow").As<Napi::Number>().DoubleValue();
        if (options.Has("cannyHigh")) cannyHigh = options.Get("cannyHigh").As<Napi::Number>().DoubleValue();
        if (options.Has("cannyAperture")) cannyAperture = options.Get("cannyAperture").As<Napi::Number>().Int32Value();
        
        cv::Mat gray;
        if (image.channels() == 4) {
            cv::cvtColor(image, gray, cv::COLOR_BGRA2GRAY);
        } else if (image.channels() == 3) {
            cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
        } else {
            gray = image;
        }
        
        cv::Mat blurred = gaussianBlur(gray, gaussianSigma);
        cv::Mat adaptive = adaptiveThreshold(blurred, blockSize, C);
        cv::Mat edges = cannyEdges(blurred, cannyLow, cannyHigh, cannyAperture);
        
        extractDefectContours(adaptive, edges, blurred, result.opaqueDefects, result.clearDefects);
        
        result.success = true;
        auto t1 = std::chrono::high_resolution_clock::now();
        result.processingTimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        
    } catch (const cv::Exception& e) {
        result.errorMessage = std::string("OpenCV error: ") + e.what();
    } catch (const std::exception& e) {
        result.errorMessage = std::string("Error: ") + e.what();
    }
    
    return result;
}
