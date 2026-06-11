#include "pec_corrector.h"
#include <cmath>
#include <chrono>
#include <algorithm>

PECCorrector::PECCorrector() = default;
PECCorrector::~PECCorrector() = default;

void PECCorrector::normalizeKernel(cv::Mat& kernel) {
    double sum = cv::sum(kernel)[0];
    if (sum > 1e-12) {
        kernel /= sum;
    }
}

int PECCorrector::optimalFFTSize(int dimension) {
    return cv::getOptimalDFTSize(dimension);
}

cv::Mat PECCorrector::generateDualGaussianPSF(
    int kernelSize,
    const DualGaussianPSF& params
) {
    if (kernelSize % 2 == 0) kernelSize++;
    const int half = kernelSize / 2;

    cv::Mat psf(kernelSize, kernelSize, CV_64FC1, cv::Scalar(0.0));

    const double twoAlpha2 = 2.0 * params.alpha * params.alpha;
    const double twoBeta2 = 2.0 * params.beta * params.beta;
    const double invGamma2 = 1.0 / (params.gamma * params.gamma);

    double maxVal = 0.0;

    for (int y = -half; y <= half; y++) {
        double* row = psf.ptr<double>(y + half);
        for (int x = -half; x <= half; x++) {
            const double r2 = static_cast<double>(x * x + y * y);
            const double forward = (1.0 - params.eta) * std::exp(-r2 * invGamma2 / twoAlpha2);
            const double backward = params.eta * std::exp(-r2 * invGamma2 / twoBeta2);
            const double val = forward + backward;
            row[x + half] = val;
            if (val > maxVal) maxVal = val;
        }
    }

    normalizeKernel(psf);
    return psf;
}

cv::Mat PECCorrector::fftConvolve(
    const cv::Mat& image,
    const cv::Mat& kernel
) {
    cv::Mat imgFloat;
    image.convertTo(imgFloat, CV_64FC1);

    const int dftW = optimalFFTSize(image.cols + kernel.cols - 1);
    const int dftH = optimalFFTSize(image.rows + kernel.rows - 1);

    cv::Mat imgPadded(dftH, dftW, CV_64FC1, cv::Scalar(0.0));
    cv::Mat kernelPadded(dftH, dftW, CV_64FC1, cv::Scalar(0.0));

    imgFloat.copyTo(imgPadded(cv::Rect(0, 0, image.cols, image.rows)));

    const int kx = (dftW - kernel.cols) / 2;
    const int ky = (dftH - kernel.rows) / 2;
    kernel.copyTo(kernelPadded(cv::Rect(kx, ky, kernel.cols, kernel.rows)));

    cv::Mat planesImg[2] = { imgPadded, cv::Mat::zeros(dftH, dftW, CV_64FC1) };
    cv::Mat planesKer[2] = { kernelPadded, cv::Mat::zeros(dftH, dftW, CV_64FC1) };
    cv::Mat complexImg, complexKer;
    cv::merge(planesImg, 2, complexImg);
    cv::merge(planesKer, 2, complexKer);

    cv::dft(complexImg, complexImg);
    cv::dft(complexKer, complexKer);

    cv::Mat complexResult;
    cv::mulSpectrums(complexImg, complexKer, complexResult, 0);

    cv::dft(complexResult, complexResult, cv::DFT_INVERSE | cv::DFT_SCALE);

    cv::Mat resultPlanes[2];
    cv::split(complexResult, resultPlanes);
    cv::Mat result = resultPlanes[0];

    const int cx = (dftW - image.cols) / 2;
    const int cy = (dftH - image.rows) / 2;
    return result(cv::Rect(cx, cy, image.cols, image.rows)).clone();
}

cv::Mat PECCorrector::deconvolveRichardsonLucy(
    const cv::Mat& blurred,
    const cv::Mat& psf,
    int iterations,
    double lambda
) {
    cv::Mat blur64;
    blurred.convertTo(blur64, CV_64FC1);

    cv::Mat estimate = blur64.clone();
    cv::Mat psfFlipped;
    cv::flip(psf, psfFlipped, -1);

    cv::Mat onesMat(blurred.rows, blurred.cols, CV_64FC1, cv::Scalar(1.0));
    cv::Mat regularization = cv::Mat::zeros(blurred.rows, blurred.cols, CV_64FC1);

    if (lambda > 0.0) {
        cv::Laplacian(estimate, regularization, CV_64F, 3);
        regularization = cv::abs(regularization);
    }

    for (int i = 0; i < iterations; i++) {
        cv::Mat convolved = fftConvolve(estimate, psf);
        cv::Mat denom = convolved + (lambda > 0.0 ? lambda * regularization : cv::Scalar(1e-10));

        cv::Mat ratio;
        cv::divide(blur64 + 1e-10, denom + 1e-10, ratio);

        cv::Mat correction = fftConvolve(ratio, psfFlipped);

        cv::multiply(estimate, correction, estimate);

        if (lambda > 0.0 && i < iterations - 1) {
            cv::Laplacian(estimate, regularization, CV_64F, 3);
            regularization = cv::abs(regularization);
        }
    }

    return estimate;
}

cv::Mat PECCorrector::deconvolveWiener(
    const cv::Mat& input,
    const cv::Mat& psf,
    double snr
) {
    cv::Mat in64;
    input.convertTo(in64, CV_64FC1);

    const int dftW = optimalFFTSize(input.cols);
    const int dftH = optimalFFTSize(input.rows);

    cv::Mat imgPadded(dftH, dftW, CV_64FC1, cv::Scalar(0.0));
    cv::Mat kernelPadded(dftH, dftW, CV_64FC1, cv::Scalar(0.0));

    in64.copyTo(imgPadded(cv::Rect(0, 0, input.cols, input.rows)));

    const int kx = (dftW - psf.cols) / 2;
    const int ky = (dftH - psf.rows) / 2;
    psf.copyTo(kernelPadded(cv::Rect(kx, ky, psf.cols, psf.rows)));

    cv::Mat planesImg[2] = { imgPadded, cv::Mat::zeros(dftH, dftW, CV_64FC1) };
    cv::Mat planesKer[2] = { kernelPadded, cv::Mat::zeros(dftH, dftW, CV_64FC1) };
    cv::Mat complexImg, complexKer;
    cv::merge(planesImg, 2, complexImg);
    cv::merge(planesKer, 2, complexKer);

    cv::dft(complexImg, complexImg);
    cv::dft(complexKer, complexKer);

    std::vector<cv::Mat> kerChannels;
    cv::split(complexKer, kerChannels);
    cv::Mat kerMag2 = kerChannels[0].mul(kerChannels[0]) + kerChannels[1].mul(kerChannels[1]);

    const double nsr = 1.0 / (snr * snr);

    cv::Mat wienerFilterNum[2] = {
        kerChannels[0].clone(),
        -kerChannels[1].clone()
    };
    cv::Mat denom = kerMag2 + cv::Scalar(nsr);
    wienerFilterNum[0] /= denom;
    wienerFilterNum[1] /= denom;
    cv::Mat wienerComplex;
    cv::merge(wienerFilterNum, 2, wienerComplex);

    cv::Mat complexResult;
    cv::mulSpectrums(complexImg, wienerComplex, complexResult, 0);

    cv::dft(complexResult, complexResult, cv::DFT_INVERSE | cv::DFT_SCALE);

    cv::Mat resultPlanes[2];
    cv::split(complexResult, resultPlanes);
    cv::Mat result = resultPlanes[0];

    return result(cv::Rect(0, 0, input.cols, input.rows)).clone();
}

cv::Mat PECCorrector::enhanceDogBone(
    const cv::Mat& doseMap,
    double strength,
    double edgeSigma,
    double cornerSigma
) {
    cv::Mat map64;
    doseMap.convertTo(map64, CV_64FC1);

    cv::Mat blurredEdges, blurredCorners;
    cv::GaussianBlur(map64, blurredEdges, cv::Size(0, 0), edgeSigma);
    cv::GaussianBlur(map64, blurredCorners, cv::Size(0, 0), cornerSigma);

    cv::Mat edgeMap, cornerMap;
    cv::Laplacian(blurredEdges, edgeMap, CV_64F, 3);
    edgeMap = cv::abs(edgeMap);

    cv::Mat gradX, gradY;
    cv::Sobel(blurredCorners, gradX, CV_64F, 2, 0, 3);
    cv::Sobel(blurredCorners, gradY, CV_64F, 0, 2, 3);
    cornerMap = gradX.mul(gradY);
    cornerMap = cv::abs(cornerMap);

    double emax, cmax;
    cv::minMaxLoc(edgeMap, nullptr, &emax);
    cv::minMaxLoc(cornerMap, nullptr, &cmax);
    if (emax > 1e-12) edgeMap /= emax;
    if (cmax > 1e-12) cornerMap /= cmax;

    cv::Mat enhanced = map64.clone();
    const double edgeBoost = (strength - 1.0) * 0.5 + 1.0;
    const double cornerBoost = strength;

    enhanced = enhanced.mul(1.0 + (edgeBoost - 1.0) * edgeMap);
    enhanced = enhanced.mul(1.0 + (cornerBoost - 1.0) * cornerMap);

    return enhanced;
}

cv::Mat PECCorrector::doseMapFromScanPoints(
    const std::vector<ScanLine>& scanLines,
    int width,
    int height,
    double pixelStep
) {
    cv::Mat doseMap(height, width, CV_64FC1, cv::Scalar(0.0));

    for (const auto& line : scanLines) {
        for (const auto& pt : line.points) {
            const int px = static_cast<int>(std::round(pt.x / pixelStep));
            const int py = static_cast<int>(std::round(pt.y / pixelStep));
            const double dose = pt.dose > 0 ? pt.dose : 1.0;
            if (px >= 0 && px < width && py >= 0 && py < height) {
                doseMap.ptr<double>(py)[px] += dose;
            }
        }
    }

    double maxDose = 0.0;
    cv::minMaxLoc(doseMap, nullptr, &maxDose);
    if (maxDose > 1e-12) {
        doseMap /= maxDose;
    }
    return doseMap;
}

std::vector<ScanLine> PECCorrector::scanPointsFromDoseMap(
    const cv::Mat& doseMap,
    const std::vector<ScanLine>& originalLines,
    double pixelStep
) {
    std::vector<ScanLine> corrected;
    corrected.reserve(originalLines.size());

    for (const auto& line : originalLines) {
        ScanLine newLine;
        newLine.y = line.y;
        newLine.direction = line.direction;
        newLine.startX = line.startX;
        newLine.endX = line.endX;
        newLine.points.reserve(line.points.size());

        for (const auto& pt : line.points) {
            const int px = static_cast<int>(std::round(pt.x / pixelStep));
            const int py = static_cast<int>(std::round(pt.y / pixelStep));
            double corrFactor = 1.0;
            if (px >= 0 && px < doseMap.cols && py >= 0 && py < doseMap.rows) {
                corrFactor = doseMap.ptr<double>(py)[px];
                if (corrFactor < 0.1) corrFactor = 0.1;
            }

            ScanPoint newPt = pt;
            newPt.dose = pt.dose * corrFactor;
            if (pt.dwellTime > 0) {
                newPt.dwellTime = static_cast<int>(std::round(pt.dwellTime * corrFactor));
            }
            newLine.points.push_back(newPt);
        }
        corrected.push_back(newLine);
    }
    return corrected;
}

PECResult PECCorrector::applyCorrection(
    const std::vector<ScanLine>& scanLines,
    int width,
    int height,
    const DualGaussianPSF& psfParams,
    const PECOptions& options
) {
    PECResult result;
    result.width = width;
    result.height = height;
    auto t0 = std::chrono::high_resolution_clock::now();

    try {
        cv::Mat doseMap = doseMapFromScanPoints(scanLines, width, height);
        result.originalDoseMap = doseMap.clone();

        const int kernelW = static_cast<int>(std::ceil(psfParams.beta * 4.0)) | 1;
        result.psfKernel = generateDualGaussianPSF(kernelW, psfParams);

        cv::Mat deconvolved;
        if (options.useWienerFilter) {
            deconvolved = deconvolveWiener(doseMap, result.psfKernel, options.wienerSNR);
        } else {
            deconvolved = deconvolveRichardsonLucy(
                doseMap, result.psfKernel,
                options.iterations, options.regularizationLambda
            );
        }
        result.iterations = options.iterations;

        cv::Mat enhanced;
        if (options.applyDogBoneEnhance) {
            enhanced = enhanceDogBone(
                deconvolved,
                options.dogBoneStrength,
                options.edgeBoostSigma,
                options.cornerBoostSigma
            );
        } else {
            enhanced = deconvolved;
        }

        double origMean = cv::mean(doseMap)[0];
        if (origMean > 1e-12) {
            double scale = origMean / cv::mean(enhanced)[0];
            enhanced *= scale;
        }

        cv::Mat ratio;
        cv::divide(enhanced + 1e-10, doseMap + 1e-10, ratio);
        ratio.setTo(options.maxDoseMultiplier, ratio > options.maxDoseMultiplier);
        ratio.setTo(options.minDoseMultiplier, ratio < options.minDoseMultiplier);

        cv::minMaxLoc(ratio, &result.minCorrectionFactor, &result.maxCorrectionFactor);
        result.avgCorrectionFactor = cv::mean(ratio)[0];
        result.correctedDoseMap = doseMap.mul(ratio);

        result.success = true;
    } catch (const cv::Exception& e) {
        result.errorMessage = std::string("OpenCV PEC error: ") + e.what();
    } catch (const std::exception& e) {
        result.errorMessage = std::string("C++ PEC error: ") + e.what();
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    result.processingTimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return result;
}

PECResult PECCorrector::applyCorrectionMap(
    const cv::Mat& inputDoseMap,
    const DualGaussianPSF& psfParams,
    const PECOptions& options
) {
    PECResult result;
    result.width = inputDoseMap.cols;
    result.height = inputDoseMap.rows;
    auto t0 = std::chrono::high_resolution_clock::now();

    try {
        result.originalDoseMap = inputDoseMap.clone();
        cv::Mat doseMap;
        inputDoseMap.convertTo(doseMap, CV_64FC1);

        double maxVal;
        cv::minMaxLoc(doseMap, nullptr, &maxVal);
        if (maxVal > 1e-12 && maxVal > 1.0) {
            doseMap /= maxVal;
        }

        const int kernelW = static_cast<int>(std::ceil(psfParams.beta * 4.0)) | 1;
        result.psfKernel = generateDualGaussianPSF(kernelW, psfParams);

        cv::Mat deconvolved;
        if (options.useWienerFilter) {
            deconvolved = deconvolveWiener(doseMap, result.psfKernel, options.wienerSNR);
        } else {
            deconvolved = deconvolveRichardsonLucy(
                doseMap, result.psfKernel,
                options.iterations, options.regularizationLambda
            );
        }
        result.iterations = options.iterations;

        cv::Mat enhanced;
        if (options.applyDogBoneEnhance) {
            enhanced = enhanceDogBone(
                deconvolved,
                options.dogBoneStrength,
                options.edgeBoostSigma,
                options.cornerBoostSigma
            );
        } else {
            enhanced = deconvolved;
        }

        double origMean = cv::mean(doseMap)[0];
        if (origMean > 1e-12) {
            double scale = origMean / cv::mean(enhanced)[0];
            enhanced *= scale;
        }

        cv::Mat ratio;
        cv::divide(enhanced + 1e-10, doseMap + 1e-10, ratio);
        ratio.setTo(options.maxDoseMultiplier, ratio > options.maxDoseMultiplier);
        ratio.setTo(options.minDoseMultiplier, ratio < options.minDoseMultiplier);

        cv::minMaxLoc(ratio, &result.minCorrectionFactor, &result.maxCorrectionFactor);
        result.avgCorrectionFactor = cv::mean(ratio)[0];
        result.correctedDoseMap = doseMap.mul(ratio);

        if (maxVal > 1.0) {
            result.correctedDoseMap *= maxVal;
            result.originalDoseMap *= maxVal;
        }

        result.success = true;
    } catch (const cv::Exception& e) {
        result.errorMessage = std::string("OpenCV PEC error: ") + e.what();
    } catch (const std::exception& e) {
        result.errorMessage = std::string("C++ PEC error: ") + e.what();
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    result.processingTimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return result;
}
