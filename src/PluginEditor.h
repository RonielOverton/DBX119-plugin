#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class RTO119Editor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit RTO119Editor (RTO119Processor&);
    ~RTO119Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    RTO119Processor& processor;

    // Components are declared before the attachments that reference them.
    juce::Label amountLabel, thresholdLabel, outputLabel, modeLabel;
    juce::Slider amountSlider, thresholdSlider, outputSlider;
    juce::ComboBox modeBox;
    juce::ToggleButton linkButton, bypassButton;

    // Areas that the 30 Hz timer repaints.
    juce::Rectangle<int> ledBounds, meterBounds;

    // Attachments are declared last so they are destroyed first.
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   amountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   thresholdAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   outputAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   linkAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   bypassAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RTO119Editor)
};
