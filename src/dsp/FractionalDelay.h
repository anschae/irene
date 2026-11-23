#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

namespace irene
{
    /** A short delay line with 3rd-order Lagrange interpolation and a smoothed
        delay time. Zero latency: a delay of 0 passes the signal through
        untouched, and whole-sample delays are exact.

        The tap calculation is public and static so that the offline exporter
        and the response display use exactly the same interpolation. */
    class FractionalDelay
    {
    public:
        static constexpr int kMaxChannels = 2;
        static constexpr int kNumTaps = 4;

        struct Taps
        {
            int firstTap = 0;                       // delay of the first tap, in whole samples
            std::array<double, kNumTaps> h { 1.0, 0.0, 0.0, 0.0 };
        };

        /** Interpolation taps for a delay of `delaySamples` (>= 0). */
        static Taps makeTaps (double delaySamples) noexcept
        {
            Taps t;
            const auto delay = std::max (0.0, delaySamples);

            // Keep the fractional position in the centre of the four taps when possible.
            t.firstTap = delay >= 1.0 ? static_cast<int> (std::floor (delay)) - 1 : 0;
            const auto d = delay - t.firstTap;

            t.h[0] = -(d - 1.0) * (d - 2.0) * (d - 3.0) / 6.0;
            t.h[1] = d * (d - 2.0) * (d - 3.0) / 2.0;
            t.h[2] = -d * (d - 1.0) * (d - 3.0) / 2.0;
            t.h[3] = d * (d - 1.0) * (d - 2.0) / 6.0;
            return t;
        }

        /** Complex response of the delay at angular frequency omega (radians/sample). */
        static std::complex<double> response (double delaySamples, double omega) noexcept
        {
            const auto t = makeTaps (delaySamples);
            std::complex<double> h { 0.0, 0.0 };

            for (int k = 0; k < kNumTaps; ++k)
                h += t.h[static_cast<size_t> (k)] * std::polar (1.0, -omega * (t.firstTap + k));

            return h;
        }

        /** Allocates. Call from prepareToPlay. */
        void prepare (double sampleRate, double maxDelaySeconds)
        {
            const auto needed = static_cast<int> (std::ceil (maxDelaySeconds * sampleRate)) + kNumTaps + 4;
            auto size = 16;

            while (size < needed)
                size *= 2;

            mask = size - 1;
            maxDelay = static_cast<double> (size - kNumTaps - 2);
            rampLength = std::max (1, static_cast<int> (0.05 * sampleRate));

            for (auto& b : buffers)
                b.assign (static_cast<size_t> (size), 0.0f);

            reset();
        }

        /** Clears the delay memory; the next delay time is applied without a glide. */
        void reset() noexcept
        {
            for (auto& b : buffers)
                std::fill (b.begin(), b.end(), 0.0f);

            writePos = 0;
            snapNext = true;
        }

        /** Makes channel `to` continue exactly like channel `from` (used when a
            slot switches from mono to stereo processing). */
        void copyChannelState (int from, int to) noexcept
        {
            buffers[static_cast<size_t> (to)] = buffers[static_cast<size_t> (from)]; // same size: no allocation
        }

        void setDelaySamples (double newDelay) noexcept
        {
            newDelay = std::clamp (newDelay, 0.0, maxDelay);

            if (snapNext)
            {
                current = target = newDelay;
                remaining = 0;
                taps = makeTaps (current);
                snapNext = false;
            }
            else if (newDelay != target)
            {
                target = newDelay;
                remaining = rampLength;
                step = (target - current) / remaining;
            }
        }

        void process (float* const* channels, int numChannels, int numSamples) noexcept
        {
            numChannels = std::min (numChannels, kMaxChannels);

            for (int i = 0; i < numSamples; ++i)
            {
                if (remaining > 0)
                {
                    --remaining;
                    current = remaining == 0 ? target : current + step;
                    taps = makeTaps (current);
                }

                const auto bypass = current == 0.0;

                for (int c = 0; c < numChannels; ++c)
                {
                    auto& buffer = buffers[static_cast<size_t> (c)];
                    buffer[static_cast<size_t> (writePos)] = channels[c][i];

                    if (bypass)
                        continue;

                    auto y = 0.0;

                    for (int k = 0; k < kNumTaps; ++k)
                        y += taps.h[static_cast<size_t> (k)]
                             * buffer[static_cast<size_t> ((writePos - taps.firstTap - k) & mask)];

                    channels[c][i] = static_cast<float> (y);
                }

                writePos = (writePos + 1) & mask;
            }
        }

    private:
        std::array<std::vector<float>, kMaxChannels> buffers;
        int mask = 0, writePos = 0;
        double maxDelay = 0.0;

        double current = 0.0, target = 0.0, step = 0.0;
        int remaining = 0, rampLength = 1;
        bool snapNext = true;
        Taps taps;
    };
}
