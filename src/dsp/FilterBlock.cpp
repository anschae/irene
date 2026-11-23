#include "FilterBlock.h"

#include <numbers>

namespace irene
{
    namespace
    {
        constexpr int kChunkSize = 16;          // coefficient update interval, in samples
        constexpr double kRampSeconds = 0.03;

        inline double lerp (double a, double b, double t) noexcept { return a + (b - a) * t; }
    }

    //==============================================================================
    void FilterBlock::Ramp::setTarget (double v, int rampLength) noexcept
    {
        if (v == target)
            return;

        target = v;
        remaining = std::max (1, rampLength);
        step = (target - current) / remaining;
    }

    void FilterBlock::Ramp::advance (int numSamples) noexcept
    {
        if (remaining <= 0)
            return;

        if (numSamples >= remaining)
        {
            current = target;
            remaining = 0;
        }
        else
        {
            current += step * numSamples;
            remaining -= numSamples;
        }
    }

    //==============================================================================
    void FilterBlock::prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        rampSamples = std::max (1, static_cast<int> (kRampSeconds * sampleRate));

        highPass.type = svf::Type::highPass;
        lowPass.type = svf::Type::lowPass;

        bands[0].shelfType = svf::Type::lowShelf;
        bands[kNumBands - 1].shelfType = svf::Type::highShelf;

        reset();
    }

    void FilterBlock::reset() noexcept
    {
        for (auto* cut : { &highPass, &lowPass })
        {
            for (auto& s : cut->sections)
                s.reset();

            cut->dirty = true;
        }

        for (auto& band : bands)
        {
            band.section.reset();
            band.dirty = true;
        }

        snapNext = true;
        wasIdle = true;
    }

    bool FilterBlock::isIdle() const noexcept
    {
        if (blockMix.current == 0.0 && blockMix.target == 0.0)
            return true;

        if (highPass.running || lowPass.running)
            return false;

        for (const auto& band : bands)
            if (band.running)
                return false;

        return true;
    }

    //==============================================================================
    void FilterBlock::setCutTarget (Cut& cut, bool on, float freq, bool steep, bool snap) noexcept
    {
        const auto logFreq = std::log (static_cast<double> (std::clamp (freq, kMinFreq, kMaxFreq)));
        const auto wasRunning = cut.mix.current > 0.0 || cut.mix.target > 0.0;

        if (snap || ! wasRunning)
        {
            // Nothing audible yet: jump straight to the new values.
            cut.logFreq.snap (logFreq);
            cut.steepMix.snap (steep ? 1.0 : 0.0);
            cut.dirty = true;

            if (snap)
                cut.mix.snap (on ? 1.0 : 0.0);
            else
                cut.mix.setTarget (on ? 1.0 : 0.0, rampSamples);
        }
        else
        {
            cut.logFreq.setTarget (logFreq, rampSamples);
            cut.steepMix.setTarget (steep ? 1.0 : 0.0, rampSamples);
            cut.mix.setTarget (on ? 1.0 : 0.0, rampSamples);
        }

        const auto runningNow = cut.mix.current > 0.0 || cut.mix.target > 0.0;

        if (runningNow && ! cut.running)
        {
            for (auto& s : cut.sections)
                s.reset();

            cut.dirty = true;
        }

        cut.running = runningNow;
    }

    void FilterBlock::setBandTarget (Band& band, const BandSettings& s, bool snap) noexcept
    {
        const auto logFreq = std::log (static_cast<double> (std::clamp (s.freq, kMinFreq, kMaxFreq)));
        const auto logQ = std::log (static_cast<double> (std::clamp (s.q, kMinQ, kMaxQ)));
        const auto hasShelf = band.shelfType != svf::Type::bell;

        band.shelfWanted = hasShelf && s.shelf;
        band.wantedGainDb = s.on ? static_cast<double> (std::clamp (s.gainDb, -kMaxBandGainDb, kMaxBandGainDb)) : 0.0;

        const auto wasRunning = band.gainDb.current != 0.0 || band.gainDb.target != 0.0;

        if (snap || ! wasRunning)
        {
            band.logFreq.snap (logFreq);
            band.logQ.snap (logQ);
            band.shelfNow = band.shelfWanted;
            band.dirty = true;

            if (snap)
                band.gainDb.snap (band.wantedGainDb);
            else
                band.gainDb.setTarget (band.wantedGainDb, rampSamples);
        }
        else
        {
            band.logFreq.setTarget (logFreq, rampSamples);
            band.logQ.setTarget (logQ, rampSamples);

            // A peak <-> shelf change first ramps the gain to 0 dB (where both
            // shapes are transparent), switches, then ramps back up.
            band.gainDb.setTarget (band.shelfNow == band.shelfWanted ? band.wantedGainDb : 0.0, rampSamples);
        }

        const auto runningNow = band.gainDb.current != 0.0 || band.gainDb.target != 0.0;

        if (runningNow && ! band.running)
        {
            band.section.reset();
            band.dirty = true;
        }

        band.running = runningNow;
    }

    void FilterBlock::setSettings (const FilterBlockSettings& s) noexcept
    {
        // While the whole block is bypassed and settled nothing is audible, so
        // inner changes can be applied instantly.
        const auto blockSilent = blockMix.current == 0.0 && blockMix.target == 0.0;
        const auto snap = snapNext || blockSilent;

        setCutTarget (highPass, s.hpOn, s.hpFreq, s.hpSteep, snap);
        setCutTarget (lowPass, s.lpOn, s.lpFreq, s.lpSteep, snap);

        for (size_t b = 0; b < bands.size(); ++b)
            setBandTarget (bands[b], s.bands[b], snap);

        if (snapNext)
            blockMix.snap (s.bypass ? 0.0 : 1.0);
        else
            blockMix.setTarget (s.bypass ? 0.0 : 1.0, rampSamples);

        snapNext = false;
    }

    //==============================================================================
    void FilterBlock::updateCut (Cut& cut, int numSamples) noexcept
    {
        if (cut.logFreq.isActive() || cut.steepMix.isActive())
            cut.dirty = true;

        cut.mix.advance (numSamples);
        cut.steepMix.advance (numSamples);
        cut.logFreq.advance (numSamples);

        if (cut.dirty)
        {
            const auto freq = std::exp (cut.logFreq.current);
            const auto q1 = lerp (kQ12, kQ24a, cut.steepMix.current);
            cut.sections[0].coeffs = svf::makeCoeffs (cut.type, freq, q1, 0.0, sampleRate);
            cut.sections[1].coeffs = svf::makeCoeffs (cut.type, freq, kQ24b, 0.0, sampleRate);
            cut.dirty = false;
        }
    }

    void FilterBlock::updateBand (Band& band, int numSamples) noexcept
    {
        if (band.gainDb.isActive() || band.logFreq.isActive() || band.logQ.isActive())
            band.dirty = true;

        band.gainDb.advance (numSamples);
        band.logFreq.advance (numSamples);
        band.logQ.advance (numSamples);

        if (band.shelfNow != band.shelfWanted && ! band.gainDb.isActive() && band.gainDb.current == 0.0)
        {
            band.shelfNow = band.shelfWanted;
            band.section.reset();
            band.gainDb.setTarget (band.wantedGainDb, rampSamples);
            band.dirty = true;
        }

        if (band.dirty)
        {
            band.section.coeffs = svf::makeCoeffs (band.shelfNow ? band.shelfType : band.peakType,
                                                   std::exp (band.logFreq.current),
                                                   std::exp (band.logQ.current),
                                                   band.gainDb.current,
                                                   sampleRate);
            band.dirty = false;
        }
    }

    void FilterBlock::processChunk (float* const* channels, int numChannels, int numSamples) noexcept
    {
        struct MixRamp
        {
            double start, delta;
            double at (int i, double invN) const noexcept { return start + delta * (i + 1) * invN; }
        };

        const auto captureStart = [] (const Ramp& r) { return r.current; };
        const auto invN = 1.0 / numSamples;

        const auto blockStart = captureStart (blockMix);
        const auto hpMixStart = captureStart (highPass.mix), hpSteepStart = captureStart (highPass.steepMix);
        const auto lpMixStart = captureStart (lowPass.mix), lpSteepStart = captureStart (lowPass.steepMix);

        blockMix.advance (numSamples);

        if (highPass.running)
            updateCut (highPass, numSamples);

        if (lowPass.running)
            updateCut (lowPass, numSamples);

        for (auto& band : bands)
            if (band.running)
                updateBand (band, numSamples);

        const MixRamp blockRamp { blockStart, blockMix.current - blockStart };
        const MixRamp hpMix { hpMixStart, highPass.mix.current - hpMixStart };
        const MixRamp hpSteep { hpSteepStart, highPass.steepMix.current - hpSteepStart };
        const MixRamp lpMix { lpMixStart, lowPass.mix.current - lpMixStart };
        const MixRamp lpSteep { lpSteepStart, lowPass.steepMix.current - lpSteepStart };

        const auto hpUsesSteep = hpSteepStart > 0.0 || highPass.steepMix.current > 0.0;
        const auto lpUsesSteep = lpSteepStart > 0.0 || lowPass.steepMix.current > 0.0;

        if (highPass.running && ! hpUsesSteep)
            highPass.sections[1].reset();

        if (lowPass.running && ! lpUsesSteep)
            lowPass.sections[1].reset();

        const auto runCut = [invN] (Cut& cut, size_t ch, int i, double x, const MixRamp& mix, const MixRamp& steep, bool usesSteep) noexcept
        {
            auto y = cut.sections[0].state[ch].tick (cut.sections[0].coeffs, x);

            if (usesSteep)
            {
                const auto y2 = cut.sections[1].state[ch].tick (cut.sections[1].coeffs, y);
                y += steep.at (i, invN) * (y2 - y);
            }

            return x + mix.at (i, invN) * (y - x);
        };

        for (int c = 0; c < numChannels; ++c)
        {
            const auto ch = static_cast<size_t> (c);
            auto* data = channels[c];

            for (int i = 0; i < numSamples; ++i)
            {
                const auto dry = static_cast<double> (data[i]);
                auto x = dry;

                if (highPass.running)
                    x = runCut (highPass, ch, i, x, hpMix, hpSteep, hpUsesSteep);

                for (auto& band : bands)
                    if (band.running)
                        x = band.section.state[ch].tick (band.section.coeffs, x);

                if (lowPass.running)
                    x = runCut (lowPass, ch, i, x, lpMix, lpSteep, lpUsesSteep);

                data[i] = static_cast<float> (dry + blockRamp.at (i, invN) * (x - dry));
            }
        }

        // Stages that have finished fading out stop running (and cost nothing).
        for (auto* cut : { &highPass, &lowPass })
            cut->running = cut->mix.current > 0.0 || cut->mix.target > 0.0;

        for (auto& band : bands)
            band.running = band.gainDb.current != 0.0 || band.gainDb.target != 0.0 || band.shelfNow != band.shelfWanted;
    }

    void FilterBlock::process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        numChannels = std::min (numChannels, kMaxChannels);

        if (isIdle())
        {
            // Nothing is in the signal path, so a pending bypass fade has no
            // audible effect and can complete immediately.
            blockMix.snap (blockMix.target);

            if (! wasIdle)
            {
                // Just became transparent: forget the filter memory so that
                // nothing stale is heard when a stage is switched on again.
                for (auto* cut : { &highPass, &lowPass })
                    for (auto& s : cut->sections)
                        s.reset();

                for (auto& band : bands)
                    band.section.reset();

                wasIdle = true;
            }

            return;
        }

        wasIdle = false;

        for (int pos = 0; pos < numSamples; pos += kChunkSize)
        {
            const auto n = std::min (kChunkSize, numSamples - pos);
            float* offsetChannels[kMaxChannels] {};

            for (int c = 0; c < numChannels; ++c)
                offsetChannels[c] = channels[c] + pos;

            processChunk (offsetChannels, numChannels, n);
        }
    }

    //==============================================================================
    std::complex<double> FilterBlock::response (const FilterBlockSettings& s, double freqHz, double fs) noexcept
    {
        std::complex<double> h { 1.0, 0.0 };

        if (s.bypass)
            return h;

        const auto omega = 2.0 * std::numbers::pi * freqHz / fs;

        const auto cut = [&] (svf::Type type, float freq, bool steep)
        {
            const auto f = static_cast<double> (std::clamp (freq, kMinFreq, kMaxFreq));
            h *= svf::response (svf::makeCoeffs (type, f, steep ? kQ24a : kQ12, 0.0, fs), omega);

            if (steep)
                h *= svf::response (svf::makeCoeffs (type, f, kQ24b, 0.0, fs), omega);
        };

        if (s.hpOn)
            cut (svf::Type::highPass, s.hpFreq, s.hpSteep);

        for (size_t b = 0; b < s.bands.size(); ++b)
        {
            const auto& band = s.bands[b];

            if (! band.on || band.gainDb == 0.0f)
                continue;

            auto type = svf::Type::bell;

            if (band.shelf && b == 0)
                type = svf::Type::lowShelf;
            else if (band.shelf && b == s.bands.size() - 1)
                type = svf::Type::highShelf;

            h *= svf::response (svf::makeCoeffs (type,
                                                 std::clamp (band.freq, kMinFreq, kMaxFreq),
                                                 std::clamp (band.q, kMinQ, kMaxQ),
                                                 std::clamp (band.gainDb, -kMaxBandGainDb, kMaxBandGainDb),
                                                 fs),
                                omega);
        }

        if (s.lpOn)
            cut (svf::Type::lowPass, s.lpFreq, s.lpSteep);

        return h;
    }

    void FilterBlock::copyChannelState (int from, int to) noexcept
    {
        const auto f = static_cast<size_t> (from), t = static_cast<size_t> (to);

        for (auto* cut : { &highPass, &lowPass })
            for (auto& section : cut->sections)
                section.state[t] = section.state[f];

        for (auto& band : bands)
            band.section.state[t] = band.section.state[f];
    }
}
