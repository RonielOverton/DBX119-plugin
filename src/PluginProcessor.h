#pragma once

#include <JuceHeader.h>
#include "dsp/CompanderCore.h"

#include <atomic>

namespace ParamIDs
{
    inline constexpr const char* amount    = "amount";     // -1 (inf:1) .. 0 (1:1) .. +1 (1:2)
    inline constexpr const char* threshold = "threshold";  // dBFS
    inline constexpr const char* mode      = "mode";       // 0 = Linear, 1 = Above Threshold
    inline constexpr const char* output    = "output";     // dB
    inline constexpr const char* link      = "link";       // stereo-linked detection
    inline constexpr const char* bypass    = "bypass";
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
    rto119::CompanderCore core;

    std::atomic<float>* amountParam = nullptr;
    std::atomic<float>* thresholdParam = nullptr;
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* outputParam = nullptr;
    std::atomic<float>* linkParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RTO119Processor)
};
