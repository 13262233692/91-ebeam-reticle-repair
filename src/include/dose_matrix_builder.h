#pragma once
#include <napi.h>
#include "common.h"
#include <map>
#include <string>
#include <vector>

struct MaterialCalibration {
    double multiplier;
    double etchDepth;
    double sensitivity;
};

class DoseMatrixBuilder {
public:
    DoseMatrixBuilder();
    ~DoseMatrixBuilder();

    void setCalibration(const std::string& material, const MaterialCalibration& cal);
    MaterialCalibration getCalibration(const std::string& material) const;

    void setBeamParameters(double voltage, double current_nA);
    void setBaseParameters(double baseDwellTimeNs, double pixelSizeNm);

    DoseMatrixResult buildDoseMatrix(
        const ScanPathResult& scanPath,
        const std::map<std::string, MaterialCalibration>& materialMap,
        int layerIndex,
        DefectContour::Type defectType
    );

    DoseMatrixResult buildDoseMatrixSimple(
        const std::vector<ScanLine>& scanLines,
        double beamVoltage,
        double baseDwellTime,
        int layerIndex
    );

    std::vector<uint8_t> serializeCommands(const std::vector<ScanPoint>& commands);
    bool writeToFile(const std::vector<ScanPoint>& commands, const std::string& filePath);

private:
    double calculateDwellTime(
        double baseDwell,
        double doseFactor,
        const MaterialCalibration& cal,
        DefectContour::Type type
    );

    double calculateVoltage(double coord, double maxCoord, double beamVoltage);

    std::map<std::string, MaterialCalibration> calibrations_;
    double beamVoltage_;
    double beamCurrent_;
    double baseDwellTime_;
    double pixelSizeNm_;
};
