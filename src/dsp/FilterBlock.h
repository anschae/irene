#pragma once

#include <array>
#include <complex>

#include "Svf.h"
#include "params/Settings.h"

namespace irene
{
    /** High-pass -> 3 EQ bands (Low, Mid, High) -> low-pass, with a block bypass.

        Used in five places: input, each slot, output.

        Real-time safe: no allocation, no locks. All parameter changes are
        smoothed: continuous values ramp, switches crossfade, and a peak/shelf
        change passes through 0 dB gain. Stages that are off and settled cost
        no CPU. */
    class FilterBlock
    {
    public:
        static constexpr int kMaxChannels = 2;

        void prepare (double sampleRate) noexcept;

        /** Clears the filter memory. The next settings are applied without ramps. */
        void reset() noexcept;

        /** Sets the target settings. Call once per block, before process(). */
        void setSettings (const FilterBlockSettings& newSettings) noexcept;

        void process (float* const* channels, int numChannels, int numSamples) noexcept;

        /** Makes channel `to` continue exactly like channel `from` (used when a
            slot switches from mono to stereo processing). */
        void copyChannelState (int from, int to) noexcept;

        /** True if process() currently leaves the signal untouched. */
        bool isIdle() const noexcept;

        /** Analytic complex response of a block with the given (settled) settings. */
        static std::complex<double> response (const FilterBlockSettings&, double freqHz, double sampleRate) noexcept;

        // Butterworth Q values
        static constexpr double kQ12 = 0.70710678118654752;  // 2nd order
        static constexpr double kQ24a = 0.54119610014619698; // 4th order, section 1
        static constexpr double kQ24b = 1.30656296487637652; // 4th order, section 2

    private:
        struct Ramp
        {
            double current = 0.0, target = 0.0, step = 0.0;
            int remaining = 0;

            void snap (double v) noexcept { current = target = v; remaining = 0; }
            void setTarget (double v, int rampSamples) noexcept;
            void advance (int numSamples) noexcept;
            bool isActive() const noexcept { return remaining > 0; }
        };

        struct Section
        {
            svf::Coeffs coeffs;
            std::array<svf::State, kMaxChannels> state;

            void reset() noexcept { for (auto& s : state) s.reset(); }
        };

        /** A high-pass or low-pass with switchable 12/24 dB/oct slope. */
        struct Cut
        {
            svf::Type type = svf::Type::highPass;
            Ramp mix;      // 0 = off, 1 = on
            Ramp steepMix; // 0 = 12 dB/oct, 1 = 24 dB/oct
            Ramp logFreq;
            std::array<Section, 2> sections;
            bool running = false;
            bool dirty = true;
        };

        struct Band
        {
            svf::Type peakType = svf::Type::bell;
            svf::Type shelfType = svf::Type::bell;
            bool shelfNow = false;    // type currently in the signal path
            bool shelfWanted = false;
            double wantedGainDb = 0.0;
            Ramp gainDb, logFreq, logQ;
            Section section;
            bool running = false;
            bool dirty = true;
        };

        void setCutTarget (Cut&, bool on, float freq, bool steep, bool snap) noexcept;
        void setBandTarget (Band&, const BandSettings&, bool snap) noexcept;
        void updateCut (Cut&, int numSamples) noexcept;
        void updateBand (Band&, int numSamples) noexcept;
        void processChunk (float* const* channels, int numChannels, int numSamples) noexcept;

        double sampleRate = 44100.0;
        int rampSamples = 1;
        bool snapNext = true;

        Cut highPass, lowPass;
        std::array<Band, kNumBands> bands;
        Ramp blockMix; // 1 = active, 0 = bypassed
        bool wasIdle = true;
    };
}
