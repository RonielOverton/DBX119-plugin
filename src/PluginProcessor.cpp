#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Display text for the single compression/expansion control.
    juce::String amountToText (float amount, int)
    {
        const float slope = rto119::slopeFromAmount (amount);
        if (std::abs (slope - 1.0f) < 0.005f)
            return "1.0:1";
        if (slope < 1.0f)
        {
            if (slope < 0.01f)
                return "inf:1";
            return juce::String (1.0f / slope, 1) + ":1";   // compression, e.g. 1.4:1
        }
        return "1:" + juce::String (slope, 1);               // expansion, e.g. 1:1.5
    }
}

RTO119Processor::RTO119Processor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "RTO119", createLayout())
{
    amountParam    = apvts.getRawParameterValue (ParamIDs::amount);
    thresholdParam = apvts.getRawParameterValue (ParamIDs::threshold);
    modeParam      = apvts.getRawParameterValue (ParamIDs::mode);
    outputParam    = apvts.getRawParameterValue (ParamIDs::output);
    linkParam      = apvts.getRawParameterValue (ParamIDs::link);
    bypassParam    = apvts.getRawParameterValue (ParamIDs::bypass);
}

juce::AudioProcessorValueTreeState::ParameterLayout RTO119Processor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::amount, 1 }, "Compress / Expand",
        NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (amountToText)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::threshold, 1 }, "Threshold",
        NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -20.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::mode, 1 }, "Mode",
        StringArray { "Linear", "Above Threshold" }, 0));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::output, 1 }, "Output",
        NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { ParamIDs::link, 1 }, "Stereo Link", true));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { ParamIDs::bypass, 1 }, "Bypass", false));

    return layout;
}

void RTO119Processor::prepareToPlay (double sampleRate, int)
{
    core.prepare (sampleRate);
}

bool RTO119Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void RTO119Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    if (bypassParam->load() > 0.5f)
    {
        meterGainDb = 0.0f;
        meterAboveThreshold = false;
        return;
    }

    rto119::Parameters p;
    p.amount      = amountParam->load();
    p.thresholdDb = thresholdParam->load();
    p.mode        = modeParam->load() > 0.5f ? rto119::Mode::AboveThreshold : rto119::Mode::Linear;
    p.outputDb    = outputParam->load();
    p.stereoLink  = linkParam->load() > 0.5f;
    core.setParameters (p);

    core.process (buffer.getArrayOfWritePointers(),
                  juce::jmin (2, getTotalNumOutputChannels()),
                  buffer.getNumSamples());

    meterGainDb = core.getGainDb();
    meterLevelDb = core.getLevelDb();
    meterAboveThreshold = core.isAboveThreshold();
}

juce::AudioProcessorParameter* RTO119Processor::getBypassParameter() const
{
    return apvts.getParameter (ParamIDs::bypass);
}

juce::AudioProcessorEditor* RTO119Processor::createEditor()
{
    return new RTO119Editor (*this);
}

void RTO119Processor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void RTO119Processor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RTO119Processor();
}
