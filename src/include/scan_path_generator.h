#pragma once
#include <napi.h>
#include "common.h"
#include <vector>

class ScanPathGenerator {
public:
    ScanPathGenerator();
    ~ScanPathGenerator();

    ScanPathResult generateRasterScan(
        const std::vector<PointD>& polygon,
        double stepX,
        double stepY,
        double overlap,
        int layerIndex
    );

    ScanPathResult generateContourScan(
        const std::vector<PointD>& polygon,
        double lineSpacing,
        int numContours,
        int layerIndex
    );

    ScanPathResult generateHybridScan(
        const std::vector<PointD>& polygon,
        double rasterStepX,
        double rasterStepY,
        double contourSpacing,
        int contourCount,
        int layerIndex
    );

private:
    std::vector<double> polygonRayIntersections(
        const std::vector<PointD>& polygon,
        double y
    );

    double pointLineDistance(
        const PointD& pt,
        const PointD& lineStart,
        const PointD& lineEnd
    );

    std::vector<PointD> offsetPolygon(
        const std::vector<PointD>& polygon,
        double offset
    );

    bool isPointInsidePolygon(
        const PointD& pt,
        const std::vector<PointD>& polygon
    );
};
