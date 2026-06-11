#include "common.h"
#include <cmath>
#include <sstream>

double polygonArea(const std::vector<PointD>& pts) {
    if (pts.size() < 3) return 0.0;
    double area = 0.0;
    const size_t n = pts.size();
    for (size_t i = 0; i < n; ++i) {
        const size_t j = (i + 1) % n;
        area += pts[i].x * pts[j].y;
        area -= pts[j].x * pts[i].y;
    }
    return std::fabs(area) * 0.5;
}

PointD polygonCentroid(const std::vector<PointD>& pts) {
    if (pts.empty()) return PointD(0.0, 0.0);
    if (pts.size() < 3) return pts[0];
    
    double cx = 0.0, cy = 0.0, a = 0.0;
    const size_t n = pts.size();
    for (size_t i = 0; i < n; ++i) {
        const size_t j = (i + 1) % n;
        const double cross = pts[i].x * pts[j].y - pts[j].x * pts[i].y;
        a += cross;
        cx += (pts[i].x + pts[j].x) * cross;
        cy += (pts[i].y + pts[j].y) * cross;
    }
    a *= 3.0;
    if (std::fabs(a) < 1e-12) return pts[0];
    return PointD(cx / a, cy / a);
}

BBox polygonBBox(const std::vector<PointD>& pts) {
    BBox bbox;
    bbox.minX = 1e18;
    bbox.minY = 1e18;
    bbox.maxX = -1e18;
    bbox.maxY = -1e18;
    for (const auto& p : pts) {
        if (p.x < bbox.minX) bbox.minX = p.x;
        if (p.y < bbox.minY) bbox.minY = p.y;
        if (p.x > bbox.maxX) bbox.maxX = p.x;
        if (p.y > bbox.maxY) bbox.maxY = p.y;
    }
    return bbox;
}

Napi::Object pointToNapi(Napi::Env env, const PointD& pt) {
    auto obj = Napi::Object::New(env);
    obj.Set("x", Napi::Number::New(env, pt.x));
    obj.Set("y", Napi::Number::New(env, pt.y));
    return obj;
}

Napi::Object bboxToNapi(Napi::Env env, const BBox& bbox) {
    auto obj = Napi::Object::New(env);
    obj.Set("minX", Napi::Number::New(env, bbox.minX));
    obj.Set("minY", Napi::Number::New(env, bbox.minY));
    obj.Set("maxX", Napi::Number::New(env, bbox.maxX));
    obj.Set("maxY", Napi::Number::New(env, bbox.maxY));
    obj.Set("width", Napi::Number::New(env, bbox.width()));
    obj.Set("height", Napi::Number::New(env, bbox.height()));
    return obj;
}

Napi::Object defectToNapi(Napi::Env env, const DefectContour& defect) {
    auto obj = Napi::Object::New(env);
    obj.Set("id", Napi::String::New(env, defect.id));
    obj.Set("type", Napi::String::New(env, defect.type));
    
    auto ptsArr = Napi::Array::New(env, defect.points.size());
    for (size_t i = 0; i < defect.points.size(); ++i) {
        ptsArr.Set(i, pointToNapi(env, defect.points[i]));
    }
    obj.Set("points", ptsArr);
    
    obj.Set("area", Napi::Number::New(env, defect.area));
    obj.Set("centroid", pointToNapi(env, defect.centroid));
    obj.Set("boundingBox", bboxToNapi(env, defect.bbox));
    obj.Set("layer", Napi::Number::New(env, defect.layer));
    return obj;
}

Napi::Object scanPointToNapi(Napi::Env env, const ScanPoint& pt) {
    auto obj = Napi::Object::New(env);
    obj.Set("x", Napi::Number::New(env, pt.x));
    obj.Set("y", Napi::Number::New(env, pt.y));
    obj.Set("z", Napi::Number::New(env, pt.z));
    obj.Set("xVoltage", Napi::Number::New(env, pt.xVoltage));
    obj.Set("yVoltage", Napi::Number::New(env, pt.yVoltage));
    obj.Set("zVoltage", Napi::Number::New(env, pt.zVoltage));
    obj.Set("dwellTime", Napi::Number::New(env, pt.dwellTime));
    obj.Set("dose", Napi::Number::New(env, pt.dose));
    obj.Set("material", Napi::String::New(env, pt.material));
    obj.Set("layer", Napi::Number::New(env, pt.layer));
    return obj;
}

Napi::Object scanLineToNapi(Napi::Env env, const ScanLine& line) {
    auto obj = Napi::Object::New(env);
    obj.Set("y", Napi::Number::New(env, line.y));
    obj.Set("direction", Napi::Number::New(env, line.direction));
    obj.Set("startX", Napi::Number::New(env, line.startX));
    obj.Set("endX", Napi::Number::New(env, line.endX));
    
    auto ptsArr = Napi::Array::New(env, line.points.size());
    for (size_t i = 0; i < line.points.size(); ++i) {
        ptsArr.Set(i, scanPointToNapi(env, line.points[i]));
    }
    obj.Set("points", ptsArr);
    return obj;
}
