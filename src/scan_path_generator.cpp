#include "scan_path_generator.h"
#include <algorithm>
#include <cmath>
#include <cfloat>

ScanPathGenerator::ScanPathGenerator() {}
ScanPathGenerator::~ScanPathGenerator() {}

std::vector<double> ScanPathGenerator::polygonRayIntersections(
    const std::vector<PointD>& polygon,
    double y
) {
    std::vector<double> intersections;
    const size_t n = polygon.size();
    
    for (size_t i = 0; i < n; ++i) {
        const size_t j = (i + 1) % n;
        const PointD& p1 = polygon[i];
        const PointD& p2 = polygon[j];
        
        if ((p1.y <= y && p2.y > y) || (p2.y <= y && p1.y > y)) {
            if (std::fabs(p2.y - p1.y) < 1e-12) continue;
            const double t = (y - p1.y) / (p2.y - p1.y);
            const double xIntersect = p1.x + t * (p2.x - p1.x);
            intersections.push_back(xIntersect);
        }
    }
    
    std::sort(intersections.begin(), intersections.end());
    return intersections;
}

double ScanPathGenerator::pointLineDistance(
    const PointD& pt,
    const PointD& lineStart,
    const PointD& lineEnd
) {
    const double dx = lineEnd.x - lineStart.x;
    const double dy = lineEnd.y - lineStart.y;
    const double len2 = dx * dx + dy * dy;
    
    if (len2 < 1e-12) {
        const double ex = pt.x - lineStart.x;
        const double ey = pt.y - lineStart.y;
        return std::sqrt(ex * ex + ey * ey);
    }
    
    double t = ((pt.x - lineStart.x) * dx + (pt.y - lineStart.y) * dy) / len2;
    t = std::max(0.0, std::min(1.0, t));
    
    const double projX = lineStart.x + t * dx;
    const double projY = lineStart.y + t * dy;
    const double ex = pt.x - projX;
    const double ey = pt.y - projY;
    return std::sqrt(ex * ex + ey * ey);
}

std::vector<PointD> ScanPathGenerator::offsetPolygon(
    const std::vector<PointD>& polygon,
    double offset
) {
    std::vector<PointD> offsetPts;
    const size_t n = polygon.size();
    if (n < 3) return polygon;
    
    double area = polygonArea(polygon);
    PointD centroid = polygonCentroid(polygon);
    const bool isCCW = area > 0;
    
    for (size_t i = 0; i < n; ++i) {
        const size_t prev = (i - 1 + n) % n;
        const size_t next = (i + 1) % n;
        
        const PointD& curr = polygon[i];
        const PointD& p_prev = polygon[prev];
        const PointD& p_next = polygon[next];
        
        double dx1 = curr.x - p_prev.x;
        double dy1 = curr.y - p_prev.y;
        double len1 = std::sqrt(dx1 * dx1 + dy1 * dy1);
        if (len1 < 1e-12) { dx1 = 1; dy1 = 0; len1 = 1; }
        dx1 /= len1; dy1 /= len1;
        
        double dx2 = p_next.x - curr.x;
        double dy2 = p_next.y - curr.y;
        double len2 = std::sqrt(dx2 * dx2 + dy2 * dy2);
        if (len2 < 1e-12) { dx2 = 1; dy2 = 0; len2 = 1; }
        dx2 /= len2; dy2 /= len2;
        
        double nx1, ny1, nx2, ny2;
        if (isCCW) {
            nx1 = -dy1; ny1 = dx1;
            nx2 = -dy2; ny2 = dx2;
        } else {
            nx1 = dy1; ny1 = -dx1;
            nx2 = dy2; ny2 = -dx2;
        }
        
        PointD miter1(curr.x + nx1 * offset, curr.y + ny1 * offset);
        PointD miter2(curr.x + nx2 * offset, curr.y + ny2 * offset);
        
        double angle = std::atan2(dy2, dx2) - std::atan2(dy1, dx1);
        while (angle > M_PI) angle -= 2 * M_PI;
        while (angle < -M_PI) angle += 2 * M_PI;
        
        double sinHalf = std::sin(angle * 0.5);
        if (std::fabs(sinHalf) < 0.1) {
            offsetPts.push_back(PointD(
                (miter1.x + miter2.x) * 0.5,
                (miter1.y + miter2.y) * 0.5
            ));
        } else {
            double miterLen = offset / sinHalf;
            double bisectorX = (dx1 + dx2) * 0.5;
            double bisectorY = (dy1 + dy2) * 0.5;
            double bLen = std::sqrt(bisectorX * bisectorX + bisectorY * bisectorY);
            if (bLen > 1e-12) {
                bisectorX /= bLen; bisectorY /= bLen;
            }
            offsetPts.push_back(PointD(
                curr.x + bisectorX * miterLen,
                curr.y + bisectorY * miterLen
            ));
        }
    }
    
    return offsetPts;
}

bool ScanPathGenerator::isPointInsidePolygon(
    const PointD& pt,
    const std::vector<PointD>& polygon
) {
    bool inside = false;
    const size_t n = polygon.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        const PointD& pi = polygon[i];
        const PointD& pj = polygon[j];
        if (((pi.y > pt.y) != (pj.y > pt.y)) &&
            (pt.x < (pj.x - pi.x) * (pt.y - pi.y) / ((pj.y - pi.y) + 1e-12) + pi.x)) {
            inside = !inside;
        }
    }
    return inside;
}

ScanPathResult ScanPathGenerator::generateRasterScan(
    const std::vector<PointD>& polygon,
    double stepX,
    double stepY,
    double overlap,
    int layerIndex
) {
    ScanPathResult result;
    result.success = false;
    result.native = true;
    result.totalPoints = 0;
    
    if (polygon.size() < 3) {
        result.errorMessage = "Polygon has too few points";
        return result;
    }
    
    BBox bbox = polygonBBox(polygon);
    result.bbox = bbox;
    
    const int xStart = static_cast<int>(std::floor(bbox.minX)) - 1;
    const int xEnd = static_cast<int>(std::ceil(bbox.maxX)) + 1;
    const int yStart = static_cast<int>(std::floor(bbox.minY)) - 1;
    const int yEnd = static_cast<int>(std::ceil(bbox.maxY)) + 1;
    
    const double effectiveStepX = stepX * (1.0 - overlap);
    int direction = 1;
    int totalPts = 0;
    
    for (int y = yStart; y <= yEnd; y = static_cast<int>(y + stepY)) {
        std::vector<double> xs = polygonRayIntersections(polygon, static_cast<double>(y));
        if (xs.size() < 2) continue;
        
        for (size_t seg = 0; seg + 1 < xs.size(); seg += 2) {
            const double x1 = xs[seg];
            const double x2 = xs[seg + 1];
            if (x2 - x1 < stepX * 0.5) continue;
            
            ScanLine line;
            line.y = static_cast<double>(y);
            line.direction = direction;
            
            if (direction == 1) {
                for (double x = x1; x <= x2; x += effectiveStepX) {
                    ScanPoint pt;
                    pt.x = x;
                    pt.y = static_cast<double>(y);
                    pt.z = 0.0;
                    pt.dose = 1.0;
                    pt.material = "target";
                    pt.layer = layerIndex;
                    line.points.push_back(pt);
                }
            } else {
                for (double x = x2; x >= x1; x -= effectiveStepX) {
                    ScanPoint pt;
                    pt.x = x;
                    pt.y = static_cast<double>(y);
                    pt.z = 0.0;
                    pt.dose = 1.0;
                    pt.material = "target";
                    pt.layer = layerIndex;
                    line.points.push_back(pt);
                }
            }
            
            if (!line.points.empty()) {
                line.startX = line.points.front().x;
                line.endX = line.points.back().x;
                totalPts += static_cast<int>(line.points.size());
                result.scanLines.push_back(line);
            }
            
            direction *= -1;
        }
    }
    
    result.totalPoints = totalPts;
    result.success = true;
    return result;
}

ScanPathResult ScanPathGenerator::generateContourScan(
    const std::vector<PointD>& polygon,
    double lineSpacing,
    int numContours,
    int layerIndex
) {
    ScanPathResult result;
    result.success = false;
    result.native = true;
    result.totalPoints = 0;
    result.bbox = polygonBBox(polygon);
    
    if (polygon.size() < 3) {
        result.errorMessage = "Polygon has too few points";
        return result;
    }
    
    BBox bbox = polygonBBox(polygon);
    const double minDim = std::min(bbox.width(), bbox.height());
    const int actualContours = std::min(numContours, static_cast<int>(minDim / (lineSpacing * 2.0)));
    
    int totalPts = 0;
    
    for (int c = actualContours - 1; c >= 0; --c) {
        const double offset = (c + 0.5) * lineSpacing;
        std::vector<PointD> contour = offsetPolygon(polygon, -offset);
        
        if (contour.size() < 3) continue;
        
        ScanLine line;
        line.y = 0;
        line.direction = (c % 2 == 0) ? 1 : -1;
        
        if (line.direction == 1) {
            for (const auto& pt : contour) {
                ScanPoint sp;
                sp.x = pt.x;
                sp.y = pt.y;
                sp.z = 0.0;
                sp.dose = 1.0 - (static_cast<double>(c) / actualContours) * 0.5;
                sp.material = (c == 0) ? "border" : "target";
                sp.layer = layerIndex;
                line.points.push_back(sp);
            }
        } else {
            for (auto it = contour.rbegin(); it != contour.rend(); ++it) {
                ScanPoint sp;
                sp.x = it->x;
                sp.y = it->y;
                sp.z = 0.0;
                sp.dose = 1.0 - (static_cast<double>(c) / actualContours) * 0.5;
                sp.material = (c == 0) ? "border" : "target";
                sp.layer = layerIndex;
                line.points.push_back(sp);
            }
        }
        
        if (!line.points.empty()) {
            line.startX = line.points.front().x;
            line.endX = line.points.back().x;
            totalPts += static_cast<int>(line.points.size());
            result.scanLines.push_back(line);
        }
    }
    
    result.totalPoints = totalPts;
    result.success = true;
    return result;
}

ScanPathResult ScanPathGenerator::generateHybridScan(
    const std::vector<PointD>& polygon,
    double rasterStepX,
    double rasterStepY,
    double contourSpacing,
    int contourCount,
    int layerIndex
) {
    ScanPathResult contourResult = generateContourScan(polygon, contourSpacing, contourCount, layerIndex);
    
    double innerOffset = (contourCount + 1) * contourSpacing;
    std::vector<PointD> innerPoly = offsetPolygon(polygon, -innerOffset);
    
    ScanPathResult rasterResult;
    if (innerPoly.size() >= 3) {
        rasterResult = generateRasterScan(innerPoly, rasterStepX, rasterStepY, 0.3, layerIndex);
    } else {
        rasterResult.success = true;
        rasterResult.native = true;
        rasterResult.scanLines.clear();
        rasterResult.totalPoints = 0;
    }
    
    ScanPathResult combined;
    combined.success = true;
    combined.native = true;
    combined.bbox = polygonBBox(polygon);
    combined.totalPoints = 0;
    
    if (contourResult.success) {
        for (const auto& line : contourResult.scanLines) {
            combined.scanLines.push_back(line);
            combined.totalPoints += static_cast<int>(line.points.size());
        }
    }
    
    if (rasterResult.success) {
        for (const auto& line : rasterResult.scanLines) {
            combined.scanLines.push_back(line);
            combined.totalPoints += static_cast<int>(line.points.size());
        }
    }
    
    return combined;
}
