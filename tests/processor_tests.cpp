// processor_tests.cpp
// Console tests for RTO119Processor latency alignment (D1P-22). No editor, no GUI.
// Built by CMake as rto119_processor_tests (only when RTO119_BUILD_PLUGIN and
// RTO119_BUILD_TESTS are both ON). Returns non-zero on any failure.

// These tests compare samples bit for bit on purpose (latency alignment).
#if defined(__GNUC__) || defined(__clang__)
 #pragma GCC diagnostic ignored "-Wfloat-equal"
#endif

#include "PluginProcessor.h"
#include "dsp/CompanderCore.h"
#include "dsp/IronStage.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::printf ("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

constexpr double kPi = 3.14159265358979323846;
constexpr double kFs = 48000.0;
constexpr int kBlock = 512;

// Sets a parameter through the APVTS, using the normalised value the host would send.
void setParam (RTO119Processor& p, const char* id, float value)
{
    if (auto* param = p.apvts.getParameter (id))
        param->setValueNotifyingHost (param->convertTo0to1 (value));
}

// Sine at dBFS level, mono. Phase starts at sample 0.
std::vector<float> sine (double freq, double dbfs, int n)
{
    std::vector<float> x (static_cast<size_t> (n));
    const double amp = std::pow (10.0, dbfs / 20.0);
    for (int i = 0; i < n; ++i)
        x[static_cast<size_t> (i)] = static_cast<float> (amp * std::sin (2.0 * kPi * freq * i / kFs));
    return x;
}

// Streams in[from, to) through the processor in blocks of blockSize. The same mono
// signal is fed to both channels. Output channels are written to out0 and out1 at
// the same indices (both sized to in.size() by the caller).
void runBlocks (RTO119Processor& p, const std::vector<float>& in, int from, int to,
                int blockSize, std::vector<float>& out0, std::vector<float>& out1)
{
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buf (2, blockSize);
    for (int start = from; start < to; start += blockSize)
    {
        const int len = juce::jmin (blockSize, to - start);
        buf.setSize (2, len, false, false, true);
        for (int ch = 0; ch < 2; ++ch)
        {
            float* w = buf.getWritePointer (ch);
            for (int i = 0; i < len; ++i)
                w[i] = in[static_cast<size_t> (start + i)];
        }
        p.processBlock (buf, midi);
        const float* r0 = buf.getReadPointer (0);
        const float* r1 = buf.getReadPointer (1);
        for (int i = 0; i < len; ++i)
        {
            out0[static_cast<size_t> (start + i)] = r0[i];
            out1[static_cast<size_t> (start + i)] = r1[i];
        }
    }
}

// Runs the whole input through the processor in one pass. Returns channel 0 and 1.
void runAll (RTO119Processor& p, const std::vector<float>& in, int blockSize,
             std::vector<float>& out0, std::vector<float>& out1)
{
    out0.assign (in.size(), 0.0f);
    out1.assign (in.size(), 0.0f);
    runBlocks (p, in, 0, static_cast<int> (in.size()), blockSize, out0, out1);
}

double rmsRange (const std::vector<float>& x, int from, int to)
{
    double s = 0.0;
    for (int i = from; i < to; ++i)
        s += static_cast<double> (x[static_cast<size_t> (i)]) * x[static_cast<size_t> (i)];
    return std::sqrt (s / static_cast<double> (to - from));
}

// Reference oversampler latency: same configuration as the processor's oversampler.
double referenceOversamplerLatency()
{
    juce::dsp::Oversampling<float> ref { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, false };
    ref.initProcessing (static_cast<size_t> (kBlock));
    return static_cast<double> (ref.getLatencyInSamples());
}

// (a) getLatencySamples() equals D = ceil(L), and D is 3 or 4. Also unchanged with Iron on.
void testLatencyReported (int& outD)
{
    RTO119Processor p;
    p.prepareToPlay (kFs, kBlock);

    // g: DC group delay of the Iron stage's 40 kHz pole, at the host rate (see PluginProcessor.cpp).
    const double fourX = kFs * 4.0;
    const double a = 1.0 - std::exp (-2.0 * kPi * rto119::IronStage::kHighCornerHz / fourX);
    const double g = (1.0 - a) / a / 4.0;

    const double L = referenceOversamplerLatency();
    const int expectedD = static_cast<int> (std::ceil (L + g - 1e-6));
    const int D = p.getLatencySamples();
    outD = D;

    std::printf ("info: L (oversampler latency) = %.4f samples, g (Iron pole) = %.4f samples, "
                 "D (reported) = %d, frac = %.4f\n",
                 L, g, D, static_cast<double> (D) - (L + g));

    CHECK (D == expectedD);
    CHECK (D == 3 || D == 4);

    // Iron on: the reported latency must not change.
    setParam (p, ParamIDs::iron, 1.0f);
    std::vector<float> in = sine (1000.0, -20.0, kBlock * 4);
    std::vector<float> o0, o1;
    runAll (p, in, kBlock, o0, o1);
    CHECK (p.getLatencySamples() == D);
}

// (b) Iron off: output[n] == core output[n - D] exactly, for all n >= D.
void testIronOffBitExact (int D)
{
    RTO119Processor p;
    p.prepareToPlay (kFs, kBlock);
    setParam (p, ParamIDs::amount, 0.0f);
    setParam (p, ParamIDs::threshold, -20.0f);
    setParam (p, ParamIDs::mode, 0.0f);
    setParam (p, ParamIDs::link, 1.0f);
    setParam (p, ParamIDs::output, 0.0f);
    setParam (p, ParamIDs::iron, 0.0f);
    setParam (p, ParamIDs::bypass, 0.0f);

    const int N = static_cast<int> (kFs);   // 1 s
    std::vector<float> in = sine (1000.0, -20.0, N);
    std::vector<float> o0, o1;
    runAll (p, in, kBlock, o0, o1);

    rto119::CompanderCore core;
    core.prepare (kFs);
    rto119::Parameters cp;
    cp.amount = 0.0f;
    cp.thresholdDb = -20.0f;
    cp.mode = rto119::Mode::Linear;
    cp.outputDb = 0.0f;
    cp.stereoLink = true;
    core.setParameters (cp);

    std::vector<float> c0 = in, c1 = in;
    for (int start = 0; start < N; start += kBlock)
    {
        float* ch[2] = { c0.data() + start, c1.data() + start };
        core.process (ch, 2, juce::jmin (kBlock, N - start));
    }

    int mismatches = 0;
    float maxDiff = 0.0f;
    for (int n = D; n < N; ++n)
    {
        const size_t k = static_cast<size_t> (n);
        const size_t kd = static_cast<size_t> (n - D);
        if (o0[k] != c0[kd]) ++mismatches;
        if (o1[k] != c1[kd]) ++mismatches;
        maxDiff = juce::jmax (maxDiff, std::fabs (o0[k] - c0[kd]), std::fabs (o1[k] - c1[kd]));
    }
    for (int n = 0; n < D; ++n)
        if (o0[static_cast<size_t> (n)] != 0.0f)
            ++mismatches;

    std::printf ("info: Iron off vs core delayed by D: mismatches = %d, max |diff| = %g\n",
                 mismatches, static_cast<double> (maxDiff));
    CHECK (mismatches == 0);
}

// (c) Alignment: Iron on vs Iron off, 1 kHz at -40 dBFS, 2 s. Compare the last second.
void testAlignment ()
{
    RTO119Processor pOff, pOn;
    pOff.prepareToPlay (kFs, kBlock);
    pOn.prepareToPlay (kFs, kBlock);
    setParam (pOff, ParamIDs::iron, 0.0f);
    setParam (pOn, ParamIDs::iron, 1.0f);

    const int N = static_cast<int> (2 * kFs);
    std::vector<float> in = sine (1000.0, -40.0, N);
    std::vector<float> off0, off1, on0, on1;
    runAll (pOff, in, kBlock, off0, off1);
    runAll (pOn, in, kBlock, on0, on1);

    const int from = N / 2;
    double sumDiff2 = 0.0;
    for (int n = from; n < N; ++n)
    {
        const double d = static_cast<double> (on0[static_cast<size_t> (n)]) - off0[static_cast<size_t> (n)];
        sumDiff2 += d * d;
    }
    const double rmsDiff = std::sqrt (sumDiff2 / static_cast<double> (N - from));
    const double rmsOff = rmsRange (off0, from, N);
    const double relDb = 20.0 * std::log10 (rmsDiff / rmsOff);

    // Analytic error for a misalignment of 3.42 samples: |1 - exp(-j w tau)| = 2 sin(pi f tau).
    const double misDb = 20.0 * std::log10 (2.0 * std::sin (kPi * 1000.0 * 3.42 / kFs));

    std::printf ("info: alignment, Iron on vs off, last 1 s: rel error = %.2f dB (target < -40 dB)\n", relDb);
    std::printf ("info: analytic error for a 3.42-sample misalignment at 1 kHz = %.2f dB (test would show this if misaligned)\n", misDb);
    CHECK (relDb < -40.0);
}

// (d) Crossfade: 1 kHz at -20 dBFS. 0.5 s Iron off, switch on, 0.5 s more. RMS in 5 ms
// windows over the 20 ms after the switch stays within 0.5 dB of the RMS before it.
void testCrossfade ()
{
    RTO119Processor p;
    p.prepareToPlay (kFs, kBlock);
    setParam (p, ParamIDs::iron, 0.0f);

    const int sw = 47 * kBlock;           // 0.5 s, on a block boundary (24064 samples)
    const int N = 2 * sw;
    std::vector<float> in = sine (1000.0, -20.0, N);
    std::vector<float> o0 (static_cast<size_t> (N)), o1 (static_cast<size_t> (N));

    runBlocks (p, in, 0, sw, kBlock, o0, o1);
    setParam (p, ParamIDs::iron, 1.0f);
    runBlocks (p, in, sw, N, kBlock, o0, o1);

    const int win = static_cast<int> (0.005 * kFs);   // 5 ms = 240 samples
    const double before = rmsRange (o0, sw - win, sw);
    std::printf ("info: crossfade RMS before switch (5 ms) = %.5f\n", before);

    for (int k = 0; k < 4; ++k)
    {
        const int from = sw + k * win;
        const double r = rmsRange (o0, from, from + win);
        const double db = 20.0 * std::log10 (r / before);
        std::printf ("info: crossfade window %d (%d ms after switch): RMS = %.5f, delta = %.3f dB\n",
                     k, k * 5, r, db);
        CHECK (std::fabs (db) <= 0.5);
    }
}

// (e) Bypass: an impulse in gives an impulse out at sample D.
void testBypass (int D)
{
    RTO119Processor p;
    p.prepareToPlay (kFs, kBlock);
    setParam (p, ParamIDs::bypass, 1.0f);

    const int N = 1024;
    std::vector<float> in (static_cast<size_t> (N), 0.0f);
    in[0] = 1.0f;
    std::vector<float> o0, o1;
    runAll (p, in, kBlock, o0, o1);

    int nonzero = 0;
    for (int n = 0; n < N; ++n)
        if (o0[static_cast<size_t> (n)] != 0.0f)
            ++nonzero;

    std::printf ("info: bypass impulse: out[D] = %g, nonzero samples = %d, latency = %d\n",
                 static_cast<double> (o0[static_cast<size_t> (D)]), nonzero, p.getLatencySamples());
    CHECK (o0[static_cast<size_t> (D)] == 1.0f);
    CHECK (nonzero == 1);
    CHECK (p.getLatencySamples() == D);

    // Host-side bypass path (processBlockBypassed) must give the same D-sample delay.
    RTO119Processor h;
    h.prepareToPlay (kFs, kBlock);
    juce::AudioBuffer<float> buf (2, N);
    buf.clear();
    buf.setSample (0, 0, 1.0f);
    buf.setSample (1, 0, 1.0f);
    juce::MidiBuffer midi;
    h.processBlockBypassed (buf, midi);
    int nonzeroHost = 0;
    for (int n = 0; n < N; ++n)
        if (buf.getSample (0, n) != 0.0f)
            ++nonzeroHost;
    std::printf ("info: host bypass impulse: out[D] = %g, nonzero samples = %d\n",
                 static_cast<double> (buf.getSample (0, D)), nonzeroHost);
    CHECK (buf.getSample (0, D) == 1.0f);
    CHECK (nonzeroHost == 1);
}

// (f) Oversized block: prepared at 512, fed 2048-sample blocks with Iron on. Must match the
// same signal fed in 512-sample blocks, within 1e-6, and be finite.
void testOversizedBlock ()
{
    RTO119Processor a, b;
    a.prepareToPlay (kFs, kBlock);
    b.prepareToPlay (kFs, kBlock);
    setParam (a, ParamIDs::iron, 1.0f);
    setParam (b, ParamIDs::iron, 1.0f);

    const int N = 2048 * 24;   // 49152 samples, a multiple of 512 and 2048
    std::vector<float> in = sine (1000.0, -10.0, N);
    std::vector<float> a0, a1, b0, b1;
    runAll (a, in, kBlock, a0, a1);
    runAll (b, in, 2048, b0, b1);

    float maxDiff = 0.0f;
    bool finite = true;
    for (int n = 0; n < N; ++n)
    {
        const size_t k = static_cast<size_t> (n);
        maxDiff = juce::jmax (maxDiff, std::fabs (a0[k] - b0[k]), std::fabs (a1[k] - b1[k]));
        finite = finite && std::isfinite (b0[k]) && std::isfinite (b1[k]);
    }

    std::printf ("info: oversized 2048 blocks vs 512 blocks: max |diff| = %g, finite = %d\n",
                 static_cast<double> (maxDiff), finite ? 1 : 0);
    CHECK (finite);
    CHECK (maxDiff <= 1e-6f);
}

} // namespace

int main()
{
    int D = 0;
    testLatencyReported (D);
    testIronOffBitExact (D);
    testAlignment();
    testCrossfade();
    testBypass (D);
    testOversizedBlock();

    std::printf ("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
