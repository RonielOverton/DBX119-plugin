// IronStage.h
// Optional "Iron" output-transformer color stage. No JUCE dependency, so it can be
// unit tested on its own.
//
// NOT PART OF THE ORIGINAL 119. The dbx 119 has no audio transformer: T1 is the power
// transformer only (docs/research/dbx119.md). Some owners add an output transformer;
// this stage is an addition that imitates that colour. Tuned by ear, ASSUMPTION.
//
// Model (ASSUMPTION, tuned by ear, not measured on a transformer):
//   - Low-frequency flux saturation: lf = one-pole low-pass of x at 120 Hz.
//     sat = tanh (lf * drive) / drive, y = x + (sat - lf) * amount.
//     Only the low band drives the nonlinearity, so distortion is much stronger
//     at low frequencies. The nonlinearity is odd, so it adds odd harmonics
//     (mostly 3rd) and no DC.
//   - High-frequency softening: a one-pole low-pass at 40 kHz on y. This is a
//     slight roll-off near 20 kHz.
//   - Unity gain at small signals and at 1 kHz (the nonlinear term is zero at small
//     levels, and the 40 kHz pole is about -0.003 dB at 1 kHz).
//
// Use: prepare() at the rate the samples are processed at (the plugin runs this at
// 4x the host rate), then processSample() per sample per channel. State is fixed size
// (2 channels). No allocation, no locks, no I/O.

#pragma once

#include <cmath>
#include <cstddef>

namespace rto119 {

class IronStage
{
public:
    // ASSUMPTION: values tuned by ear.
    static constexpr double kLowCornerHz = 120.0;   // flux-saturation band
    static constexpr double kHighCornerHz = 40000.0; // softening pole
    static constexpr float  kDrive = 2.0f;          // tanh drive
    static constexpr float  kAmount = 1.0f;         // strength of the nonlinear term

    // Sets the filter coefficients for the sample rate the stage runs at and clears state.
    void prepare (double sampleRate) noexcept
    {
        const double fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        lowCoeff = static_cast<float> (onePoleCoeff (kLowCornerHz, fs));
        highCoeff = static_cast<float> (onePoleCoeff (kHighCornerHz, fs));
        reset();
    }

    // Clears the filter state of both channels.
    void reset() noexcept
    {
        for (std::size_t c = 0; c < 2; ++c)
        {
            lowState[c] = 0.0f;
            highState[c] = 0.0f;
        }
    }

    // channel 0 or 1. Any other value uses channel 1's state.
    float processSample (int channel, float x) noexcept
    {
        const std::size_t c = channel == 0 ? 0 : 1;

        float& lf = lowState[c];
        lf += lowCoeff * (x - lf);

        const float sat = std::tanh (lf * kDrive) / kDrive;
        const float y = x + (sat - lf) * kAmount;

        float& hf = highState[c];
        hf += highCoeff * (y - hf);
        return hf;
    }

private:
    // One-pole low-pass coefficient for a corner frequency, exact for a one-pole.
    static double onePoleCoeff (double cornerHz, double fs) noexcept
    {
        constexpr double twoPi = 6.28318530717958647692;
        return 1.0 - std::exp (-twoPi * cornerHz / fs);
    }

    float lowCoeff = 0.0f;
    float highCoeff = 0.0f;
    float lowState[2] = { 0.0f, 0.0f };
    float highState[2] = { 0.0f, 0.0f };
};

} // namespace rto119
