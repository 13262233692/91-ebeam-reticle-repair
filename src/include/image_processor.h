#pragma once
#include "common.h"
#include <opencv2/opencv.hpp>
#include <vector>
#include <string>

struct DetectionOptionsPure {
    double gaussianSigma = 1.5;
    int blockSize = 31;
    double C = 10.0;
    double cannyLow = 40.0;
    double cannyHigh = 120.0;
    int cannyAperture = 3;
};

class ImageProcessor {
public:
    ImageProcessor();
    ~ImageProcessor();

    DetectionResult detectDefects(
        const std::vector<uint8_t>& pixelBuf,
        int width,
        int height,
        int channels,
        const DetectionOptionsPure& options
    );

    DetectionResult detectDefectsFromFile(
        const std::string& filePath,
        const DetectionOptionsPure& options
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
    cv::Mat pixelsToMat(const std::vector<uint8_t>& pixels, int w, int h, int ch);
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
