#pragma once

#include <JuceHeader.h>
#include "dsp/CompanderCore.h"
#include "dsp/IronStage.h"

#include <atomic>
#include <vector>

namespace ParamIDs
{
    inline constexpr const char* amount    = "amount";     // -1 (inf:1) .. 0 (1:1) .. +1 (1:2)
    inline constexpr const char* threshold = "threshold";  // dBFS
    inline constexpr const char* mode      = "mode";       // 0 = Linear, 1 = Above Threshold
    inline constexpr const char* output    = "output";     // dB
    inline constexpr const char* link      = "link";       // stereo-linked detection
    inline constexpr const char* bypass    = "bypass";
    inline constexpr const char* iron      = "iron";       // Iron (transformer color), off by default (D1P-21)
}

class RTO119Processor final : public juce::AudioProcessor
{
public:
    RTO119Processor();
    ~RTO119Processor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    // Host-side bypass (used by hosts that do not use getBypassParameter): same D-sample
    // delay as the active path, as JUCE requires for a plugin that reports latency.
    using AudioProcessor::processBlockBypassed;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;

    // Meter values for the editor (written on the audio thread).
    std::atomic<float> meterGainDb { 0.0f };
    std::atomic<float> meterLevelDb { -80.0f };
    std::atomic<bool>  meterAboveThreshold { false };

private:
    // Output stage (D1P-22): Iron crossfade plus latency alignment. Runs on every block.
    void processOutput (juce::AudioBuffer<float>& buffer, bool bypassed);
    void processOutputChunk (juce::AudioBuffer<float>& buffer, int start, int len,
                             bool wetNeeded, bool bypassed);
    void processIronWet (juce::AudioBuffer<float>& buffer, int start, int len, int numCh);

    rto119::CompanderCore core;

    // Iron stage (D1P-21). NOT PART OF THE ORIGINAL 119; see src/dsp/IronStage.h.
    // 2 channels, factor 2 = 4x.
    juce::dsp::Oversampling<float> oversampler { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, false };
    rto119::IronStage iron;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> ironMix;
    juce::AudioBuffer<float> dryBuffer;   // preallocated in prepareToPlay, 2 x samplesPerBlock
    int ironMaxBlock = 0;                 // largest block the stage is prepared for
    std::atomic<float>* ironParam = nullptr;

    // Latency alignment (D1P-22). L = oversampler latency (fractional), D = reported
    // latency (whole samples, D = ceil(L)), frac = D - L. Wet path: oversampler (L)
    // then a Thiran fractional delay (frac), so wet total = D. Dry path: an integer
    // delay of D samples, stored in a ring buffer of 2 * D floats.
    int ironLatencySamples = 0;           // D
    float ironFracSamples = 0.0f;         // frac, 0 <= frac < 1
    bool ironFracActive = false;          // frac >= 1e-4
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Thiran> ironFrac;
    std::vector<float> dryRing;           // 2 channels x D samples, preallocated in prepareToPlay
    int dryPos[2] { 0, 0 };
    bool wetRunning = false;              // true while the Iron path has been run on the previous block

    std::atomic<float>* amountParam = nullptr;
    std::atomic<float>* thresholdParam = nullptr;
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* outputParam = nullptr;
    std::atomic<float>* linkParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RTO119Processor)
};
