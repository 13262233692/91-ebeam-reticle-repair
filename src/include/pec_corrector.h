#pragma once
#include "common.h"
#include <opencv2/opencv.hpp>
#include <vector>
#include <string>

struct DualGaussianPSF {
    double eta = 0.45;
    double alpha = 2.5;
    double beta = 25.0;
    double gamma = 0.9;

    static DualGaussianPSF fromParams(
        double eta_val,
        double alpha_val,
        double beta_val,
        double gamma_val = 0.9
    ) {
        DualGaussianPSF p;
        p.eta = eta_val;
        p.alpha = alpha_val;
        p.beta = beta_val;
        p.gamma = gamma_val;
        return p;
    }
};

struct PECResult {
    bool success = false;
    bool native = true;
    int width = 0;
    int height = 0;
    cv::Mat correctedDoseMap;
    cv::Mat originalDoseMap;
    cv::Mat psfKernel;
    double maxCorrectionFactor = 1.0;
    double minCorrectionFactor = 1.0;
    double avgCorrectionFactor = 1.0;
    double processingTimeMs = 0.0;
    int iterations = 0;
    std::string errorMessage;
};

struct PECOptions {
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

    static PECOptions fromParams(
        int iters,
        double regLambda,
        bool enhance,
        double strength,
        double maxMult = 2.5
    ) {
        PECOptions o;
        o.iterations = iters;
        o.regularizationLambda = regLambda;
        o.applyDogBoneEnhance = enhance;
        o.dogBoneStrength = strength;
        o.maxDoseMultiplier = maxMult;
        return o;
    }
};

class PECCorrector {
public:
    PECCorrector();
    ~PECCorrector();

    cv::Mat generateDualGaussianPSF(
        int kernelSize,
        const DualGaussianPSF& params
    );

    cv::Mat doseMapFromScanPoints(
        const std::vector<ScanLine>& scanLines,
        int width,
        int height,
        double pixelStep = 1.0
    );

    std::vector<ScanLine> scanPointsFromDoseMap(
        const cv::Mat& doseMap,
        const std::vector<ScanLine>& originalLines,
        double pixelStep = 1.0
    );

    PECResult applyCorrection(
        const std::vector<ScanLine>& scanLines,
        int width,
        int height,
        const DualGaussianPSF& psfParams,
        const PECOptions& options
    );

    PECResult applyCorrectionMap(
        const cv::Mat& inputDoseMap,
        const DualGaussianPSF& psfParams,
        const PECOptions& options
    );

private:
    cv::Mat deconvolveRichardsonLucy(
        const cv::Mat& blurred,
        const cv::Mat& psf,
        int iterations,
        double lambda
    );

    cv::Mat deconvolveWiener(
        const cv::Mat& input,
        const cv::Mat& psf,
        double snr
    );

    cv::Mat enhanceDogBone(
        const cv::Mat& doseMap,
        double strength,
        double edgeSigma,
        double cornerSigma
    );

    cv::Mat fftConvolve(
        const cv::Mat& image,
        const cv::Mat& kernel
    );

    void normalizeKernel(cv::Mat& kernel);

    int optimalFFTSize(int dimension);
};
