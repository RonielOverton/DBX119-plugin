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
    ironParam      = apvts.getRawParameterValue (ParamIDs::iron);
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

    // D1P-21. The dbx 119 has no output transformer; this is an addition, off by default.
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { ParamIDs::iron, 1 }, "Iron (transformer color)", false));

    return layout;
}

void RTO119Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    core.prepare (sampleRate);

    // Iron stage (D1P-21). All buffers are allocated here, never in processBlock.
    // The oversampler is built with factor 2, so it runs at 4x the host rate.
    // The oversampler's IIR latency is NOT reported to the host (getLatencyInSamples()
    // stays 0, as D1P-21 requires). Measured with this configuration: 3.42 samples at the
    // host rate. The Iron path is a parallel crossfade, not a latency-compensated insert.
    const int maxBlock = juce::jmax (1, samplesPerBlock);
    ironMaxBlock = maxBlock;
    oversampler.initProcessing (static_cast<size_t> (maxBlock));
    oversampler.reset();
    iron.prepare (sampleRate * 4.0);
    dryBuffer.setSize (2, maxBlock, false, false, true);

    // 20 ms linear crossfade between the dry and Iron paths. Starts at the saved state.
    ironMix.reset (sampleRate, 0.02);
    ironMix.setCurrentAndTargetValue (ironParam->load() > 0.5f ? 1.0f : 0.0f);
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

    processIron (buffer);

    meterGainDb = core.getGainDb();
    meterLevelDb = core.getLevelDb();
    meterAboveThreshold = core.isAboveThreshold();
}

// Iron stage (D1P-21). NOT PART OF THE ORIGINAL 119. Real-time safe: no allocation,
// no locks, no I/O. Runs only while the mix is moving or on; when off and fully faded
// out, the buffer is left bit-identical.
void RTO119Processor::processIron (juce::AudioBuffer<float>& buffer)
{
    ironMix.setTargetValue (ironParam->load() > 0.5f ? 1.0f : 0.0f);

    if (! ironMix.isSmoothing() && ironMix.getCurrentValue() <= 0.0f)
        return;

    if (ironMaxBlock <= 0)
        return;

    // A host may send a block larger than prepared. Process it in chunks of the
    // prepared size: the oversampler is stateful, so chunks join without a gap.
    const int total = buffer.getNumSamples();
    for (int start = 0; start < total; start += ironMaxBlock)
        processIronChunk (buffer, start, juce::jmin (ironMaxBlock, total - start));
}

void RTO119Processor::processIronChunk (juce::AudioBuffer<float>& buffer, int start, int len)
{
    const int numCh = juce::jmin (2, buffer.getNumChannels());

    // Dry copy for the crossfade.
    for (int ch = 0; ch < numCh; ++ch)
        dryBuffer.copyFrom (ch, 0, buffer, ch, start, len);

    // Up: 4x. Input pointers live on the stack.
    const float* inPtrs[2] = { nullptr, nullptr };
    float* outPtrs[2] = { nullptr, nullptr };
    for (int ch = 0; ch < numCh; ++ch)
    {
        inPtrs[ch]  = buffer.getReadPointer (ch, start);
        outPtrs[ch] = buffer.getWritePointer (ch, start);
    }

    juce::dsp::AudioBlock<const float> inBlock (inPtrs, static_cast<size_t> (numCh), static_cast<size_t> (len));
    juce::dsp::AudioBlock<float> up = oversampler.processSamplesUp (inBlock);

    for (int ch = 0; ch < numCh; ++ch)
    {
        float* x = up.getChannelPointer (static_cast<size_t> (ch));
        for (size_t i = 0; i < up.getNumSamples(); ++i)
            x[i] = iron.processSample (ch, x[i]);
    }

    // Down: back to the host rate, into the main buffer.
    juce::dsp::AudioBlock<float> outBlock (outPtrs, static_cast<size_t> (numCh), static_cast<size_t> (len));
    oversampler.processSamplesDown (outBlock);

    // Crossfade per sample: out = dry + (wet - dry) * mix.
    float* wet[2] = { nullptr, nullptr };
    const float* dry[2] = { nullptr, nullptr };
    for (int ch = 0; ch < numCh; ++ch)
    {
        wet[ch] = outPtrs[ch];
        dry[ch] = dryBuffer.getReadPointer (ch, 0);
    }

    for (int i = 0; i < len; ++i)
    {
        const float mix = ironMix.getNextValue();
        for (int ch = 0; ch < numCh; ++ch)
            wet[ch][i] = dry[ch][i] + (wet[ch][i] - dry[ch][i]) * mix;
    }
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
