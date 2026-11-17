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
    // Input high-pass off: at 1 kHz it adds a phase lead of about 0.4 degrees, which
    // changes the output sample by sample (about 3.6e-3 at 0.5 amplitude), so this
    // exact-match test would fail. Its response is tested in testAudioHighPassResponse.
    rto119::CompanderCore::Tuning t;
    t.inputHighPassHz = 0.0f;
    rto119::CompanderCore core;
    core.prepare (kFs, t);
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

        // Sanity: the detector measures the sum of the signals, s = 0.5 (L + R). A signal
        // on one channel only reads 6.02 dB lower: -6 - 6.02 = -12.02 dBFS, 7.98 dB over
        // threshold, gain -0.5 * 7.98 = -3.99 dB. (Was -5.495 with the old power average.)
        CHECK_NEAR (measuredGainDb, -3.99, 0.1);
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

// Linked, left sine at -6 dBFS RMS, right silent: the detector sees s = 0.5 L,
// which is 6.02 dB lower, so the level is -12.02 dBFS.
void testStereoLinkOneChannelLevel()
{
    rto119::CompanderCore core;
    core.prepare (kFs);
    rto119::Parameters p;
    p.amount = -0.5f;
    p.thresholdDb = -20.0f;
    p.mode = rto119::Mode::Linear;
    p.stereoLink = true;
    core.setParameters (p);

    std::vector<float> left = makeSine (kSecond, ampFromRmsDb (-6.0));
    std::vector<float> right (kSecond, 0.0f);
    runStereo (core, left, right);

    CHECK_NEAR (core.getLevelDb (), -12.02, 0.3);
}

// Linked, L = -R: the sum is zero, so the detector sees nothing and stays at the floor.
void testStereoLinkAntiPhase()
{
    rto119::CompanderCore core;
    core.prepare (kFs);
    rto119::Parameters p;
    p.amount = -0.5f;
    p.thresholdDb = -20.0f;
    p.mode = rto119::Mode::Linear;
    p.stereoLink = true;
    core.setParameters (p);

    std::vector<float> left = makeSine (kSecond, ampFromRmsDb (-6.0));
    std::vector<float> right = left;
    for (float& v : right)
        v = -v;
    runStereo (core, left, right);

    CHECK_NEAR (core.getLevelDb (), -80.0, 0.1);
    CHECK_NEAR (core.getGainDb (),
                rto119::computeGainDb (-80.0f, -20.0f, 0.5f, rto119::Mode::Linear, 24.0f, 60.0f),
                0.1);
}

// Mean of the detected level over the last 0.5 s of a 2 s run of a mono-identical
// sine (L = R), read after each 256-sample block.
double meanDetectedLevelDb (double freqHz, double sineRmsDb, const rto119::CompanderCore::Tuning& t)
{
    rto119::CompanderCore core;
    core.prepare (kFs, t);
    rto119::Parameters p;
    p.amount = -0.5f;
    p.thresholdDb = -40.0f;
    p.mode = rto119::Mode::Linear;
    p.stereoLink = true;
    core.setParameters (p);

    const std::size_t n = 2 * kSecond;
    std::vector<float> left = makeSine (n, ampFromRmsDb (sineRmsDb), freqHz);
    std::vector<float> right = left;
    double acc = 0.0;
    int count = 0;
    for (std::size_t start = 0; start < n; start += 256)
    {
        const int len = static_cast<int> (std::min<std::size_t> (256, n - start));
        float* chans[2] = { left.data () + start, right.data () + start };
        core.process (chans, 2, len);
        if (start >= n - kHalfSecond)
        {
            acc += core.getLevelDb ();
            ++count;
        }
    }
    return acc / count;
}

// Detector high-pass at 31.2 Hz: |H(20 Hz)| = 0.540 = -5.36 dB, |H(1 kHz)| about -0.004 dB.
void testDetectorHighPass()
{
    rto119::CompanderCore::Tuning withHp;           // default 31.2 Hz
    const double mid = meanDetectedLevelDb (1000.0, -10.0, withHp);
    const double low = meanDetectedLevelDb (20.0, -10.0, withHp);
    CHECK_NEAR (mid, -10.0, 0.1);
    CHECK_NEAR (mid - low, 5.4, 1.0);

    // With the high-pass off (0 Hz), 20 Hz reads about the same as 1 kHz.
    rto119::CompanderCore::Tuning noHp;
    noHp.detectorHighPassHz = 0.0f;
    const double midOff = meanDetectedLevelDb (1000.0, -10.0, noHp);
    const double lowOff = meanDetectedLevelDb (20.0, -10.0, noHp);
    CHECK_NEAR (midOff - lowOff, 0.0, 1.0);
}

// The threshold LED is lit only in ABOVE THRESHOLD mode.
bool ledAfterLoudSine (rto119::Mode mode)
{
    rto119::CompanderCore core;
    core.prepare (kFs);
    rto119::Parameters p;
    p.amount = -0.5f;
    p.thresholdDb = -20.0f;
    p.mode = mode;
    p.stereoLink = true;
    core.setParameters (p);

    std::vector<float> left = makeSine (kSecond, ampFromRmsDb (-6.0));
    std::vector<float> right = left;
    runStereo (core, left, right);
    return core.isAboveThreshold ();
}

void testThresholdLedMode()
{
    // Signal is 14 dB above threshold in both runs.
    CHECK (!ledAfterLoudSine (rto119::Mode::Linear));
    CHECK (ledAfterLoudSine (rto119::Mode::AboveThreshold));
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

// Length in samples of the last `seconds` of a tone at freqHz, cut to whole cycles.
std::size_t wholeCycleSamples (double freqHz, double seconds)
{
    const double cycles = std::floor (seconds * freqHz);
    return static_cast<std::size_t> (std::llround (cycles * kFs / freqHz));
}

double meanOf (const std::vector<float>& v, std::size_t start, std::size_t end)
{
    double acc = 0.0;
    for (std::size_t i = start; i < end; ++i)
        acc += static_cast<double> (v[i]);
    return acc / static_cast<double> (end - start);
}

enum class Routing { Linked, Unlinked, Mono };

// Steady-state gain of the audio path at unity (amount 0, default tuning), in dB:
// output RMS over input RMS, measured over the last 2 s of a 4 s sine.
double audioPathGainDb (double freqHz, Routing routing)
{
    rto119::CompanderCore core;
    core.prepare (kFs);
    rto119::Parameters p;
    p.amount = 0.0f;
    p.outputDb = 0.0f;
    p.stereoLink = routing == Routing::Linked;
    core.setParameters (p);

    const std::size_t n = 4 * kSecond;
    const std::vector<float> in = makeSine (n, 0.5f, freqHz);
    std::vector<float> left = in;
    std::vector<float> right = in;
    const int numChannels = routing == Routing::Mono ? 1 : 2;
    for (std::size_t start = 0; start < n; start += 256)
    {
        const int len = static_cast<int> (std::min<std::size_t> (256, n - start));
        float* chans[2] = { left.data () + start, right.data () + start };
        core.process (chans, numChannels, len);
    }

    const std::size_t from = n - wholeCycleSamples (freqHz, 2.0);
    return rmsDb (left, from, n) - rmsDb (in, from, n);
}

// Input high-pass at 7.23 Hz (C7/C17 with R6/R16): -3.01 dB at 7.23 Hz, less than
// 0.1 dB loss at 50 Hz, flat at 1 kHz. Checked for linked, unlinked and mono routing.
void testAudioHighPassResponse()
{
    const Routing routings[] = { Routing::Linked, Routing::Unlinked, Routing::Mono };
    const char* names[] = { "linked", "unlinked", "mono" };
    for (int k = 0; k < 3; ++k)
    {
        const double g7 = audioPathGainDb (7.23, routings[k]);
        const double g50 = audioPathGainDb (50.0, routings[k]);
        const double g1k = audioPathGainDb (1000.0, routings[k]);
        std::printf ("info: input high-pass %s: 7.23 Hz %.4f dB, 50 Hz %.4f dB, 1 kHz %.4f dB\n",
                     names[k], g7, g50, g1k);
        CHECK_NEAR (g7, -3.01, 0.3);
        CHECK (g50 >= -0.1 && g50 <= 0.001);
        CHECK_NEAR (g1k, 0.0, 0.01);
    }
}

// A constant input has no output in steady state: the input high-pass removes DC.
void testAudioHighPassRemovesDc()
{
    const bool links[] = { true, false };
    for (bool link : links)
    {
        rto119::CompanderCore core;
        core.prepare (kFs);
        rto119::Parameters p;
        p.amount = 0.0f;
        p.outputDb = 0.0f;
        p.stereoLink = link;
        core.setParameters (p);

        const std::size_t n = 2 * kSecond;
        std::vector<float> left (n, 0.5f);
        std::vector<float> right (n, 0.5f);
        runStereo (core, left, right);

        const std::size_t from = n - kSecond / 5;   // last 0.2 s
        CHECK (std::fabs (meanOf (left, from, n)) < 0.001);
        CHECK (std::fabs (meanOf (right, from, n)) < 0.001);
    }
}

// Detected level of a linked stereo sine after 2 s.
double linkedLevelAfterSine (double freqHz, double sineRmsDb, const rto119::CompanderCore::Tuning& t)
{
    rto119::CompanderCore core;
    core.prepare (kFs, t);
    rto119::Parameters p;
    p.amount = -0.5f;
    p.thresholdDb = -40.0f;
    p.mode = rto119::Mode::Linear;
    p.stereoLink = true;
    core.setParameters (p);

    std::vector<float> left = makeSine (2 * kSecond, ampFromRmsDb (sineRmsDb), freqHz);
    std::vector<float> right = left;
    runStereo (core, left, right);
    return core.getLevelDb ();
}

// The detector taps the raw input, before the input high-pass, so the detected level
// does not depend on inputHighPassHz.
void testDetectorIgnoresInputHighPass()
{
    rto119::CompanderCore::Tuning withIn;      // default 7.23 Hz input high-pass
    rto119::CompanderCore::Tuning noIn;
    noIn.inputHighPassHz = 0.0f;

    // 1 kHz at -10 dBFS RMS (the case in the story). The input high-pass changes this
    // signal by only about 0.0002 dB, so it cannot tell the wiring apart.
    CHECK_NEAR (linkedLevelAfterSine (1000.0, -10.0, withIn),
                linkedLevelAfterSine (1000.0, -10.0, noIn), 0.01);

    // 20 Hz at -10 dBFS RMS (added): the input high-pass alone changes this tone by
    // 0.54 dB, so a detector fed from the filtered signal would fail here.
    CHECK_NEAR (linkedLevelAfterSine (20.0, -10.0, withIn),
                linkedLevelAfterSine (20.0, -10.0, noIn), 0.01);
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
    testStereoLinkOneChannelLevel ();
    testStereoLinkAntiPhase ();
    testDetectorHighPass ();
    testThresholdLedMode ();
    testOutputGain ();
    testAudioHighPassResponse ();
    testAudioHighPassRemovesDc ();
    testDetectorIgnoresInputHighPass ();

    if (g_failures == 0)
    {
        std::printf ("All %d checks passed\n", g_checks);
        return 0;
    }
    std::printf ("%d of %d checks FAILED\n", g_failures, g_checks);
    return 1;
}
