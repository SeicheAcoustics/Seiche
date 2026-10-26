#pragma once

#include "ImageSourceMethod.h"
#include "RayTracer.h"

#include <vector>
#include <optional>

namespace prs {

struct RTResult {
    float t20  = 0.0f;  // seconds, extrapolated from -5 to -25 dB
    float t30  = 0.0f;  // seconds, extrapolated from -5 to -35 dB
    float edt  = 0.0f;  // early decay time: from 0 to -10 dB, extrapolated to -60 dB
    bool  valid = false;
};

struct SPLResult {
    float directSPL    = 0.0f;  // dB re: reference
    float totalSPL     = 0.0f;  // dB re: reference (direct + reflected)
    float reflectedSPL = 0.0f;  // dB re: reference (reflected only)
    float totalSPL_A_weighted = 0.0f;  // dB(A) re: reference (direct + reflected)
    float totalSPL_B_weighted = 0.0f;  // dB(B)
    float totalSPL_C_weighted = 0.0f;  // dB(C)
    bool  valid        = false;
};

namespace AcousticMetrics {

std::vector<float> computeSchroederCurve(const std::vector<float>& rir);

std::vector<float> schroederCurveDb(const std::vector<float>& rir);

RTResult computeRT(const std::vector<float>& rir, int sampleRate);

SPLResult computeSPL(
    float sourceVolumeLinear,
    const std::vector<ImageSource>& imageSources,
    const std::vector<RayContribution>& rayContributions,
    int numRaysEmitted);

} // namespace AcousticMetrics

} // namespace prs
