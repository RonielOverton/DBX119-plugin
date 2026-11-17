// dsp_tests.cpp
// Unit tests for src/dsp/CompanderCore.h. No test framework.
//
// Build and run:
//   g++ -std=c++17 -O2 -Wall -Wextra -Isrc tests/dsp_tests.cpp -o /tmp/dsp_tests && /tmp/dsp_tests

#include "dsp/CompanderCore.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
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

#define CHECK_NEAR(a, b, tol)                                                             \
    do {                                                                                  \
        ++g_checks;                                                                       \
        const double va_ = static_cast<double> (a);                                       \
        const double vb_ = static_cast<double> (b);                                       \
        const double vt_ = static_cast<double> (tol);                                     \
        if (!(std::fabs (va_ - vb_) <= vt_)) {                                            \
            ++g_failures;                                                                 \
            std::printf ("%s:%d: CHECK_NEAR failed: %s vs %s: got %.6f, expected %.6f, " \
                         "tolerance %.6f\n",                                              \
                         __FILE__, __LINE__, #a, #b, va_, vb_, vt_);                      \
        }                                                                                 \
    } while (0)

constexpr double kFs = 48000.0;
constexpr double kPi = 3.14159265358979323846;
constexpr std::size_t kSecond = 48000;
constexpr std::size_t kHalfSecond = 24000;

// Amplitude of a sine whose RMS is rmsDb dBFS.
float ampFromRmsDb (double rmsDb)
{
    return static_cast<float> (std::pow (10.0, rmsDb / 20.0) * std::sqrt (2.0));
}

std::vector<float> makeSine (std::size_t n, float amp, double freqHz = 1000.0)
{
    std::vector<float> v (n);
    for (std::size_t i = 0; i < n; ++i)
        v[i] = static_cast<float> (amp * std::sin (2.0 * kPi * freqHz * static_cast<double> (i) / kFs));
    return v;
}

// RMS level in dBFS of v[start, end).
double rmsDb (const std::vector<float>& v, std::size_t start, std::size_t end)
{
    double acc = 0.0;
    for (std::size_t i = start; i < end; ++i)
        acc += static_cast<double> (v[i]) * static_cast<double> (v[i]);
    const double ms = acc / static_cast<double> (end - start);
    return 10.0 * std::log10 (std::max (ms, 1.0e-30));
}

double maxAbs (const std::vector<float>& v)
{
    double m = 0.0;
    for (float x : v)
        m = std::max (m, std::fabs (static_cast<double> (x)));
    return m;
}

// Process a stereo pair in place, in blocks of 256 samples.
void runStereo (rto119::CompanderCore& core, std::vector<float>& left, std::vector<float>& right)
{
    const std::size_t n = left.size();
    for (std::size_t start = 0; start < n; start += 256)
    {
        const int len = static_cast<int> (std::min<std::size_t> (256, n - start));
        float* chans[2] = { left.data() + start, right.data() + start };
        core.process (chans, 2, len);
    }
}

// ---------------------------------------------------------------------------

void testSlopeFromAmount()
{
    using rto119::slopeFromAmount;
    CHECK_NEAR (slopeFromAmount (-1.0f), 0.0, 1e-6);
    CHECK_NEAR (slopeFromAmount (0.0f), 1.0, 1e-6);
    CHECK_NEAR (slopeFromAmount (1.0f), 2.0, 1e-6);
    CHECK_NEAR (slopeFromAmount (-5.0f), 0.0, 1e-6);
    CHECK_NEAR (slopeFromAmount (5.0f), 2.0, 1e-6);
}

void testComputeGainLinear()
{
    using rto119::computeGainDb;
    using rto119::Mode;
    const float thr = -20.0f;

    // Slope 1 is unity at every level.
    CHECK_NEAR (computeGainDb (-60.0f, thr, 1.0f, Mode::Linear, 24.0f, 60.0f), 0.0, 1e-6);
    CHECK_NEAR (computeGainDb (-10.0f, thr, 1.0f, Mode::Linear, 24.0f, 60.0f), 0.0, 1e-6);
    CHECK_NEAR (computeGainDb (-30.0f, thr, 1.0f, Mode::Linear, 24.0f, 60.0f), 0.0, 1e-6);

    // Slope 0.5 (2:1 compression).
    CHECK_NEAR (computeGainDb (-10.0f, thr, 0.5f, Mode::Linear, 24.0f, 60.0f), -5.0, 1e-5);
    CHECK_NEAR (computeGainDb (-30.0f, thr, 0.5f, Mode::Linear, 24.0f, 60.0f), 5.0, 1e-5);

    // Slope 2 (1:2 expansion).
    CHECK_NEAR (computeGainDb (-10.0f, thr, 2.0f, Mode::Linear, 24.0f, 60.0f), 10.0, 1e-5);
    CHECK_NEAR (computeGainDb (-30.0f, thr, 2.0f, Mode::Linear, 24.0f, 60.0f), -10.0, 1e-5);
}

void testComputeGainAboveThreshold()
{
    using rto119::computeGainDb;
    using rto119::Mode;
    const float thr = -20.0f;

    // Below threshold: always 0 dB, whatever the slope.
    CHECK_NEAR (computeGainDb (-30.0f, thr, 0.5f, Mode::AboveThreshold, 24.0f, 60.0f), 0.0, 1e-6);
    CHECK_NEAR (computeGainDb (-30.0f, thr, 2.0f, Mode::AboveThreshold, 24.0f, 60.0f), 0.0, 1e-6);
    CHECK_NEAR (computeGainDb (-80.0f, thr, 0.0f, Mode::AboveThreshold, 24.0f, 60.0f), 0.0, 1e-6);

    // Above threshold: same as linear mode.
    CHECK_NEAR (computeGainDb (-10.0f, thr, 0.5f, Mode::AboveThreshold, 24.0f, 60.0f), -5.0, 1e-5);
    CHECK_NEAR (computeGainDb (-10.0f, thr, 2.0f, Mode::AboveThreshold, 24.0f, 60.0f), 10.0, 1e-5);
}

void testComputeGainClamping()
{
    using rto119::computeGainDb;
    using rto119::Mode;

    // Boost limited to +24 dB.
    const float boost = computeGainDb (-80.0f, 0.0f, 0.0f, Mode::Linear, 24.0f, 60.0f);
    CHECK_NEAR (boost, 24.0, 1e-6);

    // Cut limited to -60 dB.
    const float cut = computeGainDb (-100.0f, 0.0f, 2.0f, Mode::Linear, 24.0f, 60.0f);
    CHECK_NEAR (cut, -60.0, 1e-6);
}

void testUnity()
{
    rto119::CompanderCore core;
    core.prepare (kFs);
    rto119::Parameters p;
    p.amount = 0.0f;
    p.outputDb = 0.0f;
    core.setParameters (p);

    std::vector<float> left = makeSine (kSecond, 0.5f);
    std::vector<float> right = left;
    const std::vector<float> inLeft = left;
    const std::vector<float> inRight = right;

    runStereo (core, left, right);

    double maxErr = 0.0;
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        maxErr = std::max (maxErr, std::fabs (static_cast<double> (left[i]) - inLeft[i]));
        maxErr = std::max (maxErr, std::fabs (static_cast<double> (right[i]) - inRight[i]));
    }
    CHECK_NEAR (maxErr, 0.0, 1e-5);
}

void testSteadyStateCompression()
{
    rto119::CompanderCore core;
    core.prepare (kFs);
    rto119::Parameters p;
    p.amount = -0.5f;          // slope 0.5
    p.thresholdDb = -20.0f;
    p.mode = rto119::Mode::Linear;
    p.stereoLink = true;
    core.setParameters (p);

    std::vector<float> left = makeSine (2 * kSecond, ampFromRmsDb (-10.0));
    std::vector<float> right = left;
    runStereo (core, left, right);

    // Input -10 dBFS, over threshold by 10 dB, 2:1 gives -5 dB gain, so -15 dBFS out.
    const double outDb = rmsDb (left, 2 * kSecond - kHalfSecond, 2 * kSecond);
    CHECK_NEAR (outDb, -15.0, 1.0);
}

void testAboveThresholdLeavesQuietSignal()
{
    rto119::CompanderCore core;
    core.prepare (kFs);
    rto119::Parameters p;
    p.amount = -1.0f;          // infinite compression
    p.thresholdDb = -20.0f;
    p.mode = rto119::Mode::AboveThreshold;
    p.stereoLink = true;
    core.setParameters (p);

    std::vector<float> left = makeSine (kSecond, ampFromRmsDb (-40.0));
    std::vector<float> right = left;
    const std::vector<float> inLeft = left;
    runStereo (core, left, right);

    CHECK_NEAR (rmsDb (left, kHalfSecond, kSecond), rmsDb (inLeft, kHalfSecond, kSecond), 0.1);
    CHECK (!core.isAboveThreshold ());
}

void testAboveThresholdLimitsLoudSignal()
{
    rto119::CompanderCore core;
    core.prepare (kFs);
    rto119::Parameters p;
    p.amount = -1.0f;          // infinite compression
    p.thresholdDb = -20.0f;
    p.mode = rto119::Mode::AboveThreshold;
    p.stereoLink = true;
    core.setParameters (p);

    std::vector<float> left = makeSine (2 * kSecond, ampFromRmsDb (-6.0));
    std::vector<float> right = left;
    runStereo (core, left, right);

    // Input -6 dBFS, 14 dB over threshold, infinite ratio: output pinned at -20 dBFS.
    CHECK_NEAR (rmsDb (left, 2 * kSecond - kHalfSecond, 2 * kSecond), -20.0, 1.5);
    CHECK (core.isAboveThreshold ());
}

void testReleaseRate()
{
    rto119::CompanderCore core;
    core.prepare (kFs);
    rto119::Parameters p;
    p.amount = -0.5f;
    p.thresholdDb = -40.0f;
    p.mode = rto119::Mode::Linear;
    p.stereoLink = true;
    core.setParameters (p);

    // One second of signal at -10 dBFS.
    std::vector<float> left = makeSine (kSecond, ampFromRmsDb (-10.0));
    std::vector<float> right = left;
    runStereo (core, left, right);
    const double sineLevelDb = core.getLevelDb ();

    // Then 0.1 s of silence in 64-sample blocks, reading the detected level after each block.
    const int blockSize = 64;
    const int numBlocks = static_cast<int> (kSecond / 10 / blockSize);
    std::vector<double> levels;
    levels.reserve (static_cast<std::size_t> (numBlocks));
    std::vector<float> silenceL (static_cast<std::size_t> (blockSize), 0.0f);
    std::vector<float> silenceR (static_cast<std::size_t> (blockSize), 0.0f);
    for (int b = 0; b < numBlocks; ++b)
    {
        float* chans[2] = { silenceL.data (), silenceR.data () };
        core.process (chans, 2, blockSize);
        levels.push_back (core.getLevelDb ());
    }

    // Information only: drop from the last sine block to the first silent block.
    std::printf ("info: level before silence %.4f dB, first silent block %.4f dB\n",
                 sineLevelDb, levels.front ());

    const double maxDrop = 180.0 * blockSize / kFs + 0.01;
    double worstDrop = 0.0;
    for (std::size_t k = 1; k < levels.size (); ++k)
    {
        const double drop = levels[k - 1] - levels[k];
        worstDrop = std::max (worstDrop, drop);
        CHECK (drop <= maxDrop);
    }
    std::printf ("info: worst block-to-block drop in silence %.5f dB (limit %.5f dB)\n",
                 worstDrop, maxDrop);

    // The level must actually fall during the release.
    CHECK (levels.back () < levels.front () - 1.0);
}

void testStereoLink()
{
    const std::size_t n = kSecond;

    // Link on: left sine at -6 dBFS, right silent.
    {
        rto119::CompanderCore core;
        core.prepare (kFs);
        rto119::Parameters p;
        p.amount = -0.5f;
        p.thresholdDb = -20.0f;
        p.mode = rto119::Mode::Linear;
        p.stereoLink = true;
        core.setParameters (p);

        std::vector<float> left = makeSine (n, ampFromRmsDb (-6.0));
        std::vector<float> right (n, 0.0f);
        const std::vector<float> inLeft = left;
        runStereo (core, left, right);

        CHECK_NEAR (maxAbs (right), 0.0, 0.0);

        const double measuredGainDb = rmsDb (left, kHalfSecond, n) - rmsDb (inLeft, kHalfSecond, n);
        CHECK_NEAR (measuredGainDb, core.getGainDb (), 0.5);

        // Sanity: the detector sums both channels (0.5 * (l^2 + r^2)), so the level is
        // -6 - 3.01 = -9.01 dBFS, 10.99 dB over threshold, gain -0.5 * 10.99 = -5.495 dB.
        CHECK_NEAR (measuredGainDb, -5.495, 0.1);
    }

    // Link off, same input: right channel is still silent (silence times any gain).
    {
        rto119::CompanderCore core;
        core.prepare (kFs);
        rto119::Parameters p;
        p.amount = -0.5f;
        p.thresholdDb = -20.0f;
        p.mode = rto119::Mode::Linear;
        p.stereoLink = false;
        core.setParameters (p);

        std::vector<float> left = makeSine (n, ampFromRmsDb (-6.0));
        std::vector<float> right (n, 0.0f);
        runStereo (core, left, right);

        CHECK_NEAR (maxAbs (right), 0.0, 0.0);
    }
}

void testOutputGain()
{
    rto119::CompanderCore core;
    core.prepare (kFs);
    rto119::Parameters p;
    p.amount = 0.0f;           // no compression
    p.outputDb = 6.0f;
    core.setParameters (p);

    std::vector<float> left = makeSine (kSecond, ampFromRmsDb (-20.0));
    std::vector<float> right = left;
    const std::vector<float> inLeft = left;
    runStereo (core, left, right);

    // Measure after the 20 ms parameter smoothing has settled (last 0.5 s).
    const double outDelta = rmsDb (left, kHalfSecond, kSecond) - rmsDb (inLeft, kHalfSecond, kSecond);
    CHECK_NEAR (outDelta, 6.0, 0.05);
}

} // namespace

int main()
{
    testSlopeFromAmount ();
    testComputeGainLinear ();
    testComputeGainAboveThreshold ();
    testComputeGainClamping ();
    testUnity ();
    testSteadyStateCompression ();
    testAboveThresholdLeavesQuietSignal ();
    testAboveThresholdLimitsLoudSignal ();
    testReleaseRate ();
    testStereoLink ();
    testOutputGain ();

    if (g_failures == 0)
    {
        std::printf ("All %d checks passed\n", g_checks);
        return 0;
    }
    std::printf ("%d of %d checks FAILED\n", g_failures, g_checks);
    return 1;
}
