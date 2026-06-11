#include "dose_matrix_builder.h"
#include <fstream>
#include <cstring>
#include <cmath>
#include <iomanip>

DoseMatrixBuilder::DoseMatrixBuilder()
    : beamVoltage_(30.0)
    , beamCurrent_(100.0)
    , baseDwellTime_(500.0)
    , pixelSizeNm_(1.0)
{
    calibrations_["opaque"] = { 1.2, 25.0, 1.0 };
    calibrations_["clear"] = { 0.8, 15.0, 0.8 };
    calibrations_["border"] = { 0.6, 10.0, 0.6 };
    calibrations_["target"] = { 1.0, 20.0, 0.9 };
}

DoseMatrixBuilder::~DoseMatrixBuilder() {}

void DoseMatrixBuilder::setCalibration(const std::string& material, const MaterialCalibration& cal) {
    calibrations_[material] = cal;
}

MaterialCalibration DoseMatrixBuilder::getCalibration(const std::string& material) const {
    auto it = calibrations_.find(material);
    if (it != calibrations_.end()) return it->second;
    return { 1.0, 20.0, 1.0 };
}

void DoseMatrixBuilder::setBeamParameters(double voltage, double current_nA) {
    beamVoltage_ = voltage;
    beamCurrent_ = current_nA;
}

void DoseMatrixBuilder::setBaseParameters(double baseDwellTimeNs, double pixelSizeNm) {
    baseDwellTime_ = baseDwellTimeNs;
    pixelSizeNm_ = pixelSizeNm;
}

double DoseMatrixBuilder::calculateDwellTime(
    double baseDwell,
    double doseFactor,
    const MaterialCalibration& cal,
    DefectType::Type type
) {
    double typeMultiplier = 1.0;
    switch (type) {
        case DefectType::OPAQUE: typeMultiplier = 1.15; break;
        case DefectType::CLEAR: typeMultiplier = 0.85; break;
        case DefectType::BORDER: typeMultiplier = 0.7; break;
    }
    return baseDwell * doseFactor * cal.multiplier * typeMultiplier;
}

double DoseMatrixBuilder::calculateVoltage(double coord, double maxCoord, double beamVoltage) {
    if (maxCoord < 1e-12) return 0.0;
    return (coord / maxCoord) * beamVoltage;
}

DoseMatrixResult DoseMatrixBuilder::buildDoseMatrixSimple(
    const std::vector<ScanLine>& scanLines,
    double beamVoltage,
    double baseDwellTime,
    int layerIndex
) {
    DoseMatrixResult result;
    result.success = false;
    result.native = true;
    result.totalCommands = 0;
    result.totalDose = 0.0;
    result.maxDwellTime = 0;
    result.avgDwellTime = 0;
    
    double totalDwellAccum = 0.0;
    int maxDwell = 0;
    
    const double maxCoord = 4096.0;
    
    for (const auto& line : scanLines) {
        for (const auto& pt : line.points) {
            ScanPoint cmd = pt;
            
            MaterialCalibration cal = getCalibration(pt.material);
            DefectType::Type dtype = DefectType::OPAQUE;
            if (pt.material == "clear") dtype = DefectType::CLEAR;
            if (pt.material == "border") dtype = DefectType::BORDER;
            
            const double dwell = calculateDwellTime(baseDwellTime, pt.dose, cal, dtype);
            cmd.dwellTime = static_cast<int>(std::round(dwell));
            
            cmd.xVoltage = calculateVoltage(pt.x, maxCoord, beamVoltage);
            cmd.yVoltage = calculateVoltage(pt.y, maxCoord, beamVoltage);
            cmd.zVoltage = cal.etchDepth * pt.dose * 0.01;
            cmd.z = cmd.zVoltage;
            cmd.layer = layerIndex;
            
            cmd.dose = static_cast<double>(cmd.dwellTime) * beamVoltage * 0.001;
            
            result.commands.push_back(cmd);
            result.totalDose += cmd.dose;
            totalDwellAccum += cmd.dwellTime;
            if (cmd.dwellTime > maxDwell) maxDwell = cmd.dwellTime;
        }
    }
    
    result.totalCommands = static_cast<int>(result.commands.size());
    result.maxDwellTime = maxDwell;
    result.avgDwellTime = result.commands.empty() ? 0 :
        static_cast<int>(std::round(totalDwellAccum / result.commands.size()));
    result.totalDose = std::round(result.totalDose * 100.0) / 100.0;
    result.success = true;
    
    return result;
}

DoseMatrixResult DoseMatrixBuilder::buildDoseMatrix(
    const ScanPathResult& scanPath,
    const std::map<std::string, MaterialCalibration>& materialMap,
    int layerIndex,
    DefectType::Type defectType
) {
    DoseMatrixResult result;
    result.success = false;
    result.native = true;
    result.totalCommands = 0;
    result.totalDose = 0.0;
    result.maxDwellTime = 0;
    result.avgDwellTime = 0;
    
    if (!scanPath.success || scanPath.scanLines.empty()) {
        result.errorMessage = "Invalid scan path input";
        return result;
    }
    
    for (const auto& kv : materialMap) {
        calibrations_[kv.first] = kv.second;
    }
    
    double totalDwellAccum = 0.0;
    int maxDwell = 0;
    const double maxCoord = 4096.0;
    
    for (const auto& line : scanPath.scanLines) {
        for (const auto& pt : line.points) {
            ScanPoint cmd = pt;
            
            MaterialCalibration cal = getCalibration(pt.material);
            DefectType::Type dtype = defectType;
            if (pt.material == "border") dtype = DefectType::BORDER;
            
            const double dwell = calculateDwellTime(baseDwellTime_, pt.dose, cal, dtype);
            cmd.dwellTime = static_cast<int>(std::round(dwell));
            
            cmd.xVoltage = calculateVoltage(pt.x, maxCoord, beamVoltage_);
            cmd.yVoltage = calculateVoltage(pt.y, maxCoord, beamVoltage_);
            cmd.zVoltage = cal.etchDepth * pt.dose * 0.01;
            cmd.z = cmd.zVoltage;
            cmd.layer = layerIndex;
            
            cmd.dose = static_cast<double>(cmd.dwellTime) * beamVoltage_ * beamCurrent_ * 1e-9;
            
            result.commands.push_back(cmd);
            result.totalDose += cmd.dose;
            totalDwellAccum += cmd.dwellTime;
            if (cmd.dwellTime > maxDwell) maxDwell = cmd.dwellTime;
        }
    }
    
    result.totalCommands = static_cast<int>(result.commands.size());
    result.maxDwellTime = maxDwell;
    result.avgDwellTime = result.commands.empty() ? 0 :
        static_cast<int>(std::round(totalDwellAccum / result.commands.size()));
    result.totalDose = std::round(result.totalDose * 1e12 * 100.0) / 100.0;
    result.success = true;
    
    return result;
}

std::vector<uint8_t> DoseMatrixBuilder::serializeCommands(const std::vector<ScanPoint>& commands) {
    const size_t headerSize = 16;
    const size_t recordSize = 36;
    const size_t totalSize = headerSize + commands.size() * recordSize;
    
    std::vector<uint8_t> buffer(totalSize, 0);
    
    uint32_t magic = 0x45425252;
    std::memcpy(buffer.data() + 0, &magic, 4);
    
    uint32_t version = 0x00010000;
    std::memcpy(buffer.data() + 4, &version, 4);
    
    uint32_t count = static_cast<uint32_t>(commands.size());
    std::memcpy(buffer.data() + 8, &count, 4);
    
    uint32_t crc = 0;
    for (size_t i = 0; i < totalSize - 4; ++i) {
        crc = (crc << 8) ^ buffer[i];
    }
    std::memcpy(buffer.data() + 12, &crc, 4);
    
    size_t offset = headerSize;
    for (const auto& cmd : commands) {
        float xf = static_cast<float>(cmd.x);
        float yf = static_cast<float>(cmd.y);
        float zf = static_cast<float>(cmd.z);
        float xv = static_cast<float>(cmd.xVoltage);
        float yv = static_cast<float>(cmd.yVoltage);
        float zv = static_cast<float>(cmd.zVoltage);
        int32_t dw = static_cast<int32_t>(cmd.dwellTime);
        float ds = static_cast<float>(cmd.dose);
        int32_t ly = static_cast<int32_t>(cmd.layer);
        uint8_t mat = 0;
        if (cmd.material == "opaque") mat = 1;
        else if (cmd.material == "clear") mat = 2;
        else if (cmd.material == "border") mat = 3;
        
        std::memcpy(buffer.data() + offset, &xf, 4); offset += 4;
        std::memcpy(buffer.data() + offset, &yf, 4); offset += 4;
        std::memcpy(buffer.data() + offset, &zf, 4); offset += 4;
        std::memcpy(buffer.data() + offset, &xv, 4); offset += 4;
        std::memcpy(buffer.data() + offset, &yv, 4); offset += 4;
        std::memcpy(buffer.data() + offset, &zv, 4); offset += 4;
        std::memcpy(buffer.data() + offset, &dw, 4); offset += 4;
        std::memcpy(buffer.data() + offset, &ds, 4); offset += 4;
        std::memcpy(buffer.data() + offset, &ly, 4); offset += 4;
        buffer[offset++] = mat;
        offset += 3;
    }
    
    return buffer;
}

bool DoseMatrixBuilder::writeToFile(const std::vector<ScanPoint>& commands, const std::string& filePath) {
    std::vector<uint8_t> binary = serializeCommands(commands);
    
    std::ofstream ofs(filePath, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return false;
    
    ofs.write(reinterpret_cast<const char*>(binary.data()), static_cast<std::streamsize>(binary.size()));
    ofs.close();
    
    std::string csvPath = filePath + ".csv";
    std::ofstream csv(csvPath);
    if (!csv.is_open()) return true;
    
    csv << "X,Y,Z,XVoltage,YVoltage,ZVoltage,DwellTimeNs,Dose_nC,Material,Layer\n";
    csv << std::fixed << std::setprecision(6);
    
    for (const auto& cmd : commands) {
        csv << cmd.x << "," << cmd.y << "," << cmd.z << ","
            << cmd.xVoltage << "," << cmd.yVoltage << "," << cmd.zVoltage << ","
            << cmd.dwellTime << "," << std::setprecision(9) << cmd.dose << std::setprecision(6) << ","
            << cmd.material << "," << cmd.layer << "\n";
    }
    
    csv.close();
    return true;
}
