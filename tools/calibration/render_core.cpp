// render_core.cpp
// Renders a raw float32 stereo file through the plugin DSP (dsp/CompanderCore.h)
// with no JUCE. Called by render_plugin.py.
//
// usage: render_core in.raw out.raw amount threshold_db mode output_db link sample_rate
//   in.raw / out.raw : interleaved float32 stereo, little-endian
//   amount           : -1 to +1 (slope = 1 + amount)
//   mode             : 0 = linear, 1 = above threshold
//   link             : 0 or 1

#include "dsp/CompanderCore.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main (int argc, char** argv)
{
    if (argc != 9)
    {
        std::fprintf (stderr, "usage: render_core in.raw out.raw amount threshold_db mode output_db link sample_rate\n");
        return 2;
    }

    rto119::Parameters p;
    p.amount = std::strtof (argv[3], nullptr);
    p.thresholdDb = std::strtof (argv[4], nullptr);
    const int mode = std::atoi (argv[5]);
    p.mode = mode == 1 ? rto119::Mode::AboveThreshold : rto119::Mode::Linear;
    p.outputDb = std::strtof (argv[6], nullptr);
    p.stereoLink = std::atoi (argv[7]) != 0;
    const double sampleRate = std::strtod (argv[8], nullptr);

    std::FILE* fin = std::fopen (argv[1], "rb");
    if (! fin)
    {
        std::perror ("open input");
        return 1;
    }
    std::vector<float> inter;
    float buf[4096];
    size_t got = 0;
    while ((got = std::fread (buf, sizeof (float), 4096, fin)) > 0)
        inter.insert (inter.end (), buf, buf + got);
    std::fclose (fin);

    const size_t frames = inter.size () / 2;
    std::vector<float> left (frames), right (frames);
    for (size_t i = 0; i < frames; ++i)
    {
        left[i] = inter[2 * i];
        right[i] = inter[2 * i + 1];
    }

    // Set the parameters before prepare(): prepare() resets the smoothers to the
    // target, so the first samples are not faded in from zero.
    rto119::CompanderCore core;
    core.setParameters (p);
    core.prepare (sampleRate);

    const size_t block = 512;   // same order of block size as a DAW host
    for (size_t i = 0; i < frames; i += block)
    {
        const int n = static_cast<int> (std::min (block, frames - i));
        float* channels[2] = { left.data () + i, right.data () + i };
        core.process (channels, 2, n);
    }

    std::vector<float> out (frames * 2);
    for (size_t i = 0; i < frames; ++i)
    {
        out[2 * i] = left[i];
        out[2 * i + 1] = right[i];
    }

    std::FILE* fout = std::fopen (argv[2], "wb");
    if (! fout)
    {
        std::perror ("open output");
        return 1;
    }
    std::fwrite (out.data (), sizeof (float), out.size (), fout);
    std::fclose (fout);
    return 0;
}
