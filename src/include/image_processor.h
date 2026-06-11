#pragma once
#include <napi.h>
#include "common.h"
#include <opencv2/opencv.hpp>

class ImageProcessor {
public:
    ImageProcessor();
    ~ImageProcessor();

    DetectionResult detectDefects(
        const Napi::Uint8Array& pixelData,
        int width,
        int height,
        int channels,
        const Napi::Object& options
    );

    DetectionResult detectDefectsFromFile(
        const std::string& filePath,
        const Napi::Object& options
    );

    std::vector<PointD> subpixelRefineContour(
        const std::vector<PointD>& contour,
        const cv::Mat& grayImage,
        const std::string& defectType
    );

    cv::Mat gaussianBlur(const cv::Mat& src, double sigma, int ksize = 0);
    cv::Mat adaptiveThreshold(const cv::Mat& src, int blockSize, double C);
    cv::Mat cannyEdges(const cv::Mat& src, double lowThreshold, double highThreshold, int apertureSize);

private:
    cv::Mat loadImage(const std::string& path);
    cv::Mat pixelsToMat(const Napi::Uint8Array& pixels, int w, int h, int ch);
    void extractDefectContours(
        const cv::Mat& binary,
        const cv::Mat& edges,
        const cv::Mat& gray,
        std::vector<DefectContour>& opaqueOut,
        std::vector<DefectContour>& clearOut
    );
    bool isInsidePolygon(const PointD& pt, const std::vector<PointD>& polygon);
    static double getVersion();
};
