#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

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

    // All buffers are allocated here, never in processBlock.
    const int maxBlock = juce::jmax (1, samplesPerBlock);
    ironMaxBlock = maxBlock;

    // The oversampler is built with factor 2, so it runs at 4x the host rate.
    // initProcessing() sizes its buffers and calls reset().
    oversampler.initProcessing (static_cast<size_t> (maxBlock));

    // Latency (D1P-22). L is the oversampler's IIR delay in host samples (fractional).
    // g is the delay the Iron stage's own 40 kHz softening pole adds to the wet path
    // (DC group delay of a one-pole at 4x, in host samples). It is in the wet path, so
    // it counts toward the wet total. The host gets D, always, whether Iron is on or off,
    // so the reported latency never changes at runtime. The wet path gets a Thiran
    // fractional delay frac = D - (L + g) in [0, 1). The dry path gets D integer samples.
    // Both paths end up at D.
    // D = ceil(L + g), as in D1P-22. Here D = 4 (L = 3.42, g = 0.09); the g term keeps
    // frac in [0, 1) if the values move.
    constexpr double kPi = 3.14159265358979323846;
    const double fourX = sampleRate * 4.0;
    const double poleCoeff = 1.0 - std::exp (-2.0 * kPi * rto119::IronStage::kHighCornerHz / fourX);
    const double poleDelayHost = (1.0 - poleCoeff) / poleCoeff / 4.0;

    const double latency = static_cast<double> (oversampler.getLatencyInSamples()) + poleDelayHost;
    ironLatencySamples = static_cast<int> (std::ceil (latency - 1e-6));
    ironFracSamples = static_cast<float> (juce::jmax (0.0, static_cast<double> (ironLatencySamples) - latency));
    ironFracActive = ironFracSamples >= 1e-4f;
    setLatencySamples (ironLatencySamples);

    iron.prepare (fourX);

    dryBuffer.setSize (2, maxBlock, false, false, true);
    dryRing.assign (static_cast<size_t> (2 * ironLatencySamples), 0.0f);
    dryPos[0] = 0;
    dryPos[1] = 0;

    ironFrac.setMaximumDelayInSamples (2);
    ironFrac.prepare (juce::dsp::ProcessSpec { sampleRate, static_cast<juce::uint32> (maxBlock), 2 });
    ironFrac.setDelay (ironFracSamples);
    wetRunning = false;

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

    // Bypass (D1P-22): the core is skipped, but the output still goes through the same
    // latency alignment, so the bypassed output is delayed by D just like the active one.
    const bool bypassed = bypassParam->load() > 0.5f;

    if (bypassed)
    {
        meterGainDb = 0.0f;
        meterAboveThreshold = false;
    }
    else
    {
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
    }

    processOutput (buffer, bypassed);

    if (! bypassed)
    {
        meterGainDb = core.getGainDb();
        meterLevelDb = core.getLevelDb();
        meterAboveThreshold = core.isAboveThreshold();
    }
}

// Output stage (D1P-21 Iron, D1P-22 latency). NOT PART OF THE ORIGINAL 119 for Iron.
// Real-time safe: no allocation, no locks, no I/O.
//
// Per block:
//   - The Iron (wet) path runs only while the mix is moving or above 0. When it is off
//     and fully faded out, the oversampler is skipped (cheap).
//   - The dry path always runs: the signal is delayed by exactly D samples. So the output
//     is always D samples late, with Iron on, off, or bypassed.
//   - Crossfade: out = dryDelayed + (wetAligned - dryDelayed) * mix.
void RTO119Processor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    meterGainDb = 0.0f;
    meterAboveThreshold = false;
    processOutput (buffer, true);
}

void RTO119Processor::processOutput (juce::AudioBuffer<float>& buffer, bool bypassed)
{
    ironMix.setTargetValue (ironParam->load() > 0.5f ? 1.0f : 0.0f);

    const bool wetNeeded = ironMix.isSmoothing() || ironMix.getCurrentValue() > 0.0f;

    // Iron is starting from zero (mix moving up from 0 after being idle). Clear the
    // oversampler, the fractional delay and the IIR filter, so no stale state is used.
    // JUCE's Oversampling::reset() and DelayLine::reset() only clear existing storage;
    // neither allocates.
    if (wetNeeded && ! wetRunning)
    {
        oversampler.reset();
        ironFrac.reset();
        iron.reset();
    }
    wetRunning = wetNeeded;

    if (ironMaxBlock <= 0)
        return;

    // A host may send a block larger than prepared. Process it in chunks of the
    // prepared size. Every stateful part (oversampler, fractional delay, dry ring)
    // carries its state across chunks, so the chunks join without a gap.
    const int total = buffer.getNumSamples();
    for (int start = 0; start < total; start += ironMaxBlock)
        processOutputChunk (buffer, start, juce::jmin (ironMaxBlock, total - start), wetNeeded, bypassed);
}

void RTO119Processor::processOutputChunk (juce::AudioBuffer<float>& buffer, int start, int len,
                                          bool wetNeeded, bool bypassed)
{
    const int numCh = juce::jmin (2, buffer.getNumChannels());

    // Unprocessed signal for the dry path (and the input to the Iron path).
    for (int ch = 0; ch < numCh; ++ch)
        dryBuffer.copyFrom (ch, 0, buffer, ch, start, len);

    // Wet path: buffer chunk becomes the Iron signal, aligned to D samples.
    if (wetNeeded)
        processIronWet (buffer, start, len, numCh);

    // Dry path: delay by exactly D samples, in place in dryBuffer. Per channel,
    // a ring of D samples. Output sample i is input sample i - D.
    const int D = ironLatencySamples;
    if (D > 0)
    {
        for (int ch = 0; ch < numCh; ++ch)
        {
            float* d = dryBuffer.getWritePointer (ch);
            float* ring = dryRing.data() + static_cast<size_t> (ch) * static_cast<size_t> (D);
            int pos = dryPos[ch];
            for (int i = 0; i < len; ++i)
            {
                const float in = d[i];
                d[i] = ring[pos];
                ring[pos] = in;
                if (++pos == D)
                    pos = 0;
            }
            dryPos[ch] = pos;
        }
    }

    // Output: crossfade from dry (delayed) to wet (aligned). Bypass outputs the dry path only.
    if (wetNeeded)
    {
        for (int i = 0; i < len; ++i)
        {
            const float mix = ironMix.getNextValue();
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float dry = dryBuffer.getSample (ch, i);
                const float wet = buffer.getSample (ch, start + i);
                buffer.setSample (ch, start + i, bypassed ? dry : dry + (wet - dry) * mix);
            }
        }
    }
    else
    {
        // Mix is 0 and not moving: the output is the dry path alone, bit for bit.
        for (int ch = 0; ch < numCh; ++ch)
            buffer.copyFrom (ch, start, dryBuffer, ch, 0, len);
    }
}

// Iron wet path for one chunk: oversample up, Iron stage at 4x, oversample down (latency L),
// then the Thiran fractional delay (frac). Output is aligned to D samples.
void RTO119Processor::processIronWet (juce::AudioBuffer<float>& buffer, int start, int len, int numCh)
{
    const float* inPtrs[2] = { nullptr, nullptr };
    float* outPtrs[2] = { nullptr, nullptr };
    for (int ch = 0; ch < numCh; ++ch)
    {
        inPtrs[ch]  = buffer.getReadPointer (ch, start);
        outPtrs[ch] = buffer.getWritePointer (ch, start);
    }

    // Up: 4x. Input pointers live on the stack.
    juce::dsp::AudioBlock<const float> inBlock (inPtrs, static_cast<size_t> (numCh), static_cast<size_t> (len));
    juce::dsp::AudioBlock<float> up = oversampler.processSamplesUp (inBlock);

    for (int ch = 0; ch < numCh; ++ch)
    {
        float* x = up.getChannelPointer (static_cast<size_t> (ch));
        for (size_t i = 0; i < up.getNumSamples(); ++i)
            x[i] = iron.processSample (ch, x[i]);
    }

    // Down: back to the host rate, into the main buffer chunk.
    juce::dsp::AudioBlock<float> outBlock (outPtrs, static_cast<size_t> (numCh), static_cast<size_t> (len));
    oversampler.processSamplesDown (outBlock);

    // Fractional delay so the wet total is L + frac = D.
    if (ironFracActive)
    {
        for (int ch = 0; ch < numCh; ++ch)
            for (int i = 0; i < len; ++i)
            {
                ironFrac.pushSample (ch, outPtrs[ch][i]);
                outPtrs[ch][i] = ironFrac.popSample (ch);
            }
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
