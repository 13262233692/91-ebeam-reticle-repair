#pragma once
#include <napi.h>
#include <vector>
#include <string>
#include <memory>

struct PointD {
    double x;
    double y;
    PointD() : x(0.0), y(0.0) {}
    PointD(double _x, double _y) : x(_x), y(_y) {}
};

struct BBox {
    double minX, minY, maxX, maxY;
    double width() const { return maxX - minX; }
    double height() const { return maxY - minY; }
};

struct DefectContour {
    std::string id;
    std::string type;
    std::vector<PointD> points;
    double area;
    PointD centroid;
    BBox bbox;
    int layer;
};

struct ScanPoint {
    double x;
    double y;
    double z;
    double xVoltage;
    double yVoltage;
    double zVoltage;
    int dwellTime;
    double dose;
    std::string material;
    int layer;
};

namespace DefectType {
    enum Type {
        OPAQUE = 0,
        CLEAR = 1,
        BORDER = 2
    };
}


struct ScanLine {
    double y;
    int direction;
    std::vector<ScanPoint> points;
    double startX;
    double endX;
};

struct DetectionResult {
    bool success;
    bool native;
    std::vector<DefectContour> opaqueDefects;
    std::vector<DefectContour> clearDefects;
    int width;
    int height;
    double processingTimeMs;
    std::string errorMessage;
};

struct ScanPathResult {
    bool success;
    bool native;
    std::vector<ScanLine> scanLines;
    int totalPoints;
    BBox bbox;
    std::string errorMessage;
};

struct DoseMatrixResult {
    bool success;
    bool native;
    std::vector<ScanPoint> commands;
    int totalCommands;
    double totalDose;
    int maxDwellTime;
    int avgDwellTime;
    std::string errorMessage;
};

struct ImageData {
    int width;
    int height;
    int channels;
    std::vector<uint8_t> rawData;
};

double polygonArea(const std::vector<PointD>& pts);
PointD polygonCentroid(const std::vector<PointD>& pts);
BBox polygonBBox(const std::vector<PointD>& pts);
std::string pointToJson(const PointD& pt);
std::string bboxToJson(const BBox& bbox);
Napi::Object pointToNapi(Napi::Env env, const PointD& pt);
Napi::Object bboxToNapi(Napi::Env env, const BBox& bbox);
Napi::Object defectToNapi(Napi::Env env, const DefectContour& defect);
Napi::Object scanPointToNapi(Napi::Env env, const ScanPoint& pt);
Napi::Object scanLineToNapi(Napi::Env env, const ScanLine& line);
