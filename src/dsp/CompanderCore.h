// CompanderCore.h
// DSP model of a 1970s dB-linear stereo compressor/expander (dbx 119 style).
// No JUCE dependency, so it can be unit tested on its own.
//
// What the hardware does (sources in docs/research/dbx119.md):
//   - One control sets the slope: compression up to infinity:1 through 1:1
//     to expansion up to 1:2. The response is linear in decibels.
//   - THRESHOLD sets the level where gain is unity.
//   - MODE: LINEAR acts on the whole range; ABOVE THRESHOLD acts only on
//     signal above the threshold. The red LED shows when the level is above
//     the threshold, in ABOVE THRESHOLD mode only (119 supplement).
//   - RMS detection. Attack typically 12 ms; release follows the signal
//     envelope up to 180 dB/s.
//   - Stereo link: one detector measures the sum of the two signals, L + R,
//     as the hardware does (R5 and R15 sum into one RMS detector). The detector
//     input is preceded by a first-order high-pass at about 31 Hz (C11, R4,
//     R5||R15), which only affects the detector, not the audio.
//
// Modelling choices that still need checking against a real unit are marked
// ASSUMPTION and tracked in Jira (calibration epic).

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace rto119 {

enum class Mode { Linear = 0, AboveThreshold = 1 };

struct Parameters
{
    // Control position from -1 to +1.
    //   -1 = infinite compression (slope 0)
    //    0 = no change (slope 1)
    //   +1 = 1:2 expansion (slope 2)
    float amount = 0.0f;
    float thresholdDb = -20.0f;  // dBFS
    Mode mode = Mode::Linear;
    float outputDb = 0.0f;
    bool stereoLink = true;
};

// Output dB change per input dB change for a control position.
inline float slopeFromAmount (float amount) noexcept
{
    return 1.0f + std::clamp (amount, -1.0f, 1.0f);
}

// Gain change in dB for a detected level. Pure function, used by the
// processor and by the tests.
inline float computeGainDb (float levelDb, float thresholdDb, float slope, Mode mode,
                            float maxBoostDb, float maxCutDb) noexcept
{
    float over = levelDb - thresholdDb;
    if (mode == Mode::AboveThreshold)
        over = std::max (0.0f, over);
    const float gain = (slope - 1.0f) * over;
    return std::clamp (gain, -maxCutDb, maxBoostDb);
}

class CompanderCore
{
public:
    // Timing and safety limits. ASSUMPTION: the gain limits and the level
    // floor are not documented for the 119; they keep linear-mode
    // compression from raising the noise floor without limit.
    struct Tuning
    {
        float attackMs = 12.0f;          // RMS integrator time constant
        float releaseDbPerSec = 180.0f;  // fastest fall of the detected level
        float maxBoostDb = 24.0f;
        float maxCutDb = 60.0f;
        float levelFloorDb = -80.0f;     // detector ignores anything quieter
        float smoothingMs = 20.0f;       // parameter smoothing
        // Detector high-pass. Manual [M] p.12-13: C11 (0.1 uF) with R5||R15 (50 kOhm)
        // and R4 (1 kOhm) gives about 31.2 Hz = 1 / (2 pi 51e3 0.1e-6). 0 disables it.
        float detectorHighPassHz = 31.2f;
    };

    void prepare (double sampleRate) noexcept { prepare (sampleRate, Tuning {}); }

    void prepare (double sampleRate, Tuning t) noexcept
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        tuning = t;
        attackCoeff = coeffFor (tuning.attackMs);
        smoothCoeff = coeffFor (tuning.smoothingMs);
        releaseStepDb = tuning.releaseDbPerSec / static_cast<float> (fs);
        // One-pole high-pass, y[n] = a (y[n-1] + x[n] - x[n-1]), a = 1 / (1 + 2 pi fc / fs).
        // fc = 0 gives a = 1, which passes the signal unchanged.
        const double hpHz = std::max (0.0, static_cast<double> (tuning.detectorHighPassHz));
        hpAlpha = static_cast<float> (1.0 / (1.0 + 2.0 * 3.14159265358979323846 * hpHz / fs));
        reset();
    }

    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            power[c] = 0.0f;
            levelDb[c] = tuning.levelFloorDb;
            hpIn[c] = 0.0f;
            hpOut[c] = 0.0f;
        }
        smoothedAmount = target.amount;
        smoothedThreshold = target.thresholdDb;
        smoothedOutput = target.outputDb;
        lastGainDb = 0.0f;
        lastLevelDb = tuning.levelFloorDb;
        aboveThreshold = false;
    }

    void setParameters (const Parameters& p) noexcept { target = p; }

    // Process in place. numChannels is 1 or 2.
    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        numChannels = std::clamp (numChannels, 1, 2);
        const bool link = target.stereoLink && numChannels == 2;
        float blockMinGain = 1000.0f;
        float blockMaxLevel = -1000.0f;
        bool blockAbove = false;

        for (int i = 0; i < numSamples; ++i)
        {
            smoothedAmount += smoothCoeff * (target.amount - smoothedAmount);
            smoothedThreshold += smoothCoeff * (target.thresholdDb - smoothedThreshold);
            smoothedOutput += smoothCoeff * (target.outputDb - smoothedOutput);
            const float slope = slopeFromAmount (smoothedAmount);
            const float outGain = dbToGain (smoothedOutput);

            if (link)
            {
                const float l = channels[0][i];
                const float r = channels[1][i];
                // Linked detector input is the sum of the signals, s = 0.5 (L + R), as on
                // the hardware (R5 and R15 sum into one detector). Consequences: a signal
                // on one channel only reads 6.02 dB lower than on both; anti-phase
                // L = -R gives no detection. The 0.5 keeps a mono signal reading the same
                // level as one channel.
                const float s = highPass (0, 0.5f * (l + r));
                const float lvl = detect (0, s * s);
                const float g = computeGainDb (lvl, smoothedThreshold, slope, target.mode,
                                               tuning.maxBoostDb, tuning.maxCutDb);
                const float gain = dbToGain (g) * outGain;
                channels[0][i] = l * gain;
                channels[1][i] = r * gain;
                track (lvl, g, blockMinGain, blockMaxLevel, blockAbove);
            }
            else
            {
                for (int c = 0; c < numChannels; ++c)
                {
                    // Unlinked (or mono): one detector per channel, fed by that channel alone.
                    const float x = channels[c][i];
                    const float y = highPass (c, x);
                    const float lvl = detect (c, y * y);
                    const float g = computeGainDb (lvl, smoothedThreshold, slope, target.mode,
                                                   tuning.maxBoostDb, tuning.maxCutDb);
                    channels[c][i] = x * dbToGain (g) * outGain;
                    track (lvl, g, blockMinGain, blockMaxLevel, blockAbove);
                }
            }
        }

        if (numSamples > 0)
        {
            lastGainDb = blockMinGain;
            lastLevelDb = blockMaxLevel;
            // The 119 supplement: the threshold LED does not light in LINEAR mode.
            aboveThreshold = blockAbove && target.mode == Mode::AboveThreshold;
        }
    }

    // Metering, read after process().
    float getGainDb() const noexcept { return lastGainDb; }
    float getLevelDb() const noexcept { return lastLevelDb; }
    // The red LED: true when the detected level passed the threshold during
    // the last block, in ABOVE THRESHOLD mode only. Always false in LINEAR mode.
    bool isAboveThreshold() const noexcept { return aboveThreshold; }

    static float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
    static float powerToDb (float p) noexcept { return 10.0f * std::log10 (std::max (p, 1.0e-20f)); }

private:
    float coeffFor (float ms) const noexcept
    {
        if (ms <= 0.0f)
            return 1.0f;
        return 1.0f - std::exp (-1.0f / (0.001f * ms * static_cast<float> (fs)));
    }

    // Detector-path high-pass, state per detector index c (0 for linked, 0 or 1 unlinked).
    float highPass (int c, float x) noexcept
    {
        const float y = hpAlpha * (hpOut[c] + x - hpIn[c]);
        hpIn[c] = x;
        hpOut[c] = y;
        return y;
    }

    // RMS detector: one-pole mean-square integrator for the attack, then the
    // fall of the dB level is limited to the release rate.
    float detect (int c, float squared) noexcept
    {
        power[c] += attackCoeff * (squared - power[c]);
        const float instDb = std::max (powerToDb (power[c]), tuning.levelFloorDb);
        levelDb[c] = std::max (instDb, levelDb[c] - releaseStepDb);
        return levelDb[c];
    }

    void track (float lvl, float g, float& minGain, float& maxLevel, bool& above) const noexcept
    {
        minGain = std::min (minGain, g);
        maxLevel = std::max (maxLevel, lvl);
        if (lvl > smoothedThreshold)
            above = true;
    }

    double fs = 44100.0;
    Tuning tuning {};
    Parameters target {};
    float attackCoeff = 1.0f, smoothCoeff = 1.0f, releaseStepDb = 0.0f;
    float power[2] {};
    float levelDb[2] { -80.0f, -80.0f };
    float hpAlpha = 1.0f;
    float hpIn[2] {};
    float hpOut[2] {};
    float smoothedAmount = 0.0f, smoothedThreshold = -20.0f, smoothedOutput = 0.0f;
    float lastGainDb = 0.0f, lastLevelDb = -80.0f;
    bool aboveThreshold = false;
};

} // namespace rto119
