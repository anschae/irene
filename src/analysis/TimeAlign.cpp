#include "TimeAlign.h"

#include <juce_dsp/juce_dsp.h>

#include "params/Settings.h"

namespace irene::align
{
    namespace
    {
        // The direct sound decides the alignment, so the start of each IR is enough.
        constexpr double kAnalysisSeconds = 0.5;

        // A flipped polarity must beat the best non-flipped fit by this factor.
        // Without the margin, two nearly equal peaks could flip a slot by chance.
        constexpr double kInversionMargin = 1.1;

        std::vector<float> monoStart (const juce::AudioBuffer<float>& ir, int maxSamples)
        {
            const auto n = juce::jmin (ir.getNumSamples(), maxSamples);
            std::vector<float> mono (static_cast<size_t> (n), 0.0f);
            const auto scale = 1.0f / static_cast<float> (juce::jmax (1, ir.getNumChannels()));

            for (int ch = 0; ch < ir.getNumChannels(); ++ch)
            {
                const auto* data = ir.getReadPointer (ch);

                for (int i = 0; i < n; ++i)
                    mono[static_cast<size_t> (i)] += data[i] * scale;
            }

            return mono;
        }

        double energyOf (const std::vector<float>& v)
        {
            auto e = 0.0;

            for (const auto x : v)
                e += static_cast<double> (x) * static_cast<double> (x);

            return e;
        }

        juce::String formatMs (double ms)
        {
            return juce::String (ms, 3) + " ms";
        }
    }

    //==============================================================================
    PairResult correlate (const juce::AudioBuffer<float>& reference, const juce::AudioBuffer<float>& other, double sampleRate)
    {
        PairResult result;

        if (sampleRate <= 0.0 || reference.getNumSamples() == 0 || other.getNumSamples() == 0)
            return result;

        const auto maxSamples = juce::jmax (16, static_cast<int> (kAnalysisSeconds * sampleRate));
        const auto a = monoStart (reference, maxSamples);
        const auto b = monoStart (other, maxSamples);
        const auto energyA = energyOf (a);
        const auto energyB = energyOf (b);

        if (energyA <= 0.0 || energyB <= 0.0)
            return result; // a silent IR can't be aligned

        const auto lenA = static_cast<int> (a.size());
        const auto lenB = static_cast<int> (b.size());

        // Cross-correlation through the frequency domain:
        //   r[k] = sum over n of a[n] * b[n + k]   <->   conj (A) * B
        // The transform is long enough that positive and negative lags don't overlap.
        auto order = 4;

        while ((1 << order) < lenA + lenB)
            ++order;

        const auto size = 1 << order;
        juce::dsp::FFT fft (order);

        std::vector<float> fa (static_cast<size_t> (size) * 2, 0.0f), fb (static_cast<size_t> (size) * 2, 0.0f);
        std::copy (a.begin(), a.end(), fa.begin());
        std::copy (b.begin(), b.end(), fb.begin());

        fft.performRealOnlyForwardTransform (fa.data());
        fft.performRealOnlyForwardTransform (fb.data());

        for (size_t bin = 0; bin < static_cast<size_t> (size); ++bin)
        {
            const auto ar = fa[bin * 2], ai = fa[bin * 2 + 1];
            const auto br = fb[bin * 2], bi = fb[bin * 2 + 1];
            fb[bin * 2] = ar * br + ai * bi;
            fb[bin * 2 + 1] = ar * bi - ai * br;
        }

        fft.performRealOnlyInverseTransform (fb.data());

        // r[k] for k >= 0 is at index k, for k < 0 at index size + k.
        const auto valueAt = [&fb, size] (int lag) { return static_cast<double> (fb[static_cast<size_t> (lag >= 0 ? lag : size + lag)]); };
        const auto minLag = -(lenA - 1);
        const auto maxLag = lenB - 1;

        auto bestPositive = 0.0, bestNegative = 0.0;
        auto positiveLag = 0, negativeLag = 0;

        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            const auto v = valueAt (lag);

            if (v > bestPositive)
            {
                bestPositive = v;
                positiveLag = lag;
            }

            if (v < bestNegative)
            {
                bestNegative = v;
                negativeLag = lag;
            }
        }

        result.inverted = -bestNegative > bestPositive * kInversionMargin;

        const auto peakLag = result.inverted ? negativeLag : positiveLag;
        const auto sign = result.inverted ? -1.0 : 1.0;
        const auto peak = sign * valueAt (peakLag);

        if (peak <= 0.0)
            return result;

        // Refine to a fraction of a sample with a parabola through the peak and
        // its two neighbours.
        auto fraction = 0.0;

        if (peakLag > minLag && peakLag < maxLag)
        {
            const auto before = sign * valueAt (peakLag - 1);
            const auto after = sign * valueAt (peakLag + 1);
            const auto curvature = before - 2.0 * peak + after;

            if (curvature < 0.0)
                fraction = juce::jlimit (-0.5, 0.5, 0.5 * (before - after) / curvature);
        }

        result.valid = true;
        result.lagSamples = peakLag + fraction;
        result.match = static_cast<float> (juce::jlimit (0.0, 1.0, peak / std::sqrt (energyA * energyB)));
        return result;
    }

    //==============================================================================
    Result computeAlignment (const std::array<SlotInput, kNumSlots>& slots, double sampleRate, int referenceSlot)
    {
        Result result;
        auto numLoaded = 0;

        for (size_t i = 0; i < slots.size(); ++i)
        {
            result.slots[i].hasIR = slots[i].ir.getNumSamples() > 0;
            result.slots[i].phaseInvert = slots[i].phaseInvert;
            numLoaded += result.slots[i].hasIR ? 1 : 0;
        }

        const auto slotLabel = [&slots] (size_t i)
        {
            return "Slot " + juce::String (static_cast<int> (i) + 1) + (slots[i].name.isNotEmpty() ? " (" + slots[i].name + ")" : juce::String());
        };

        if (referenceSlot < 0 || referenceSlot >= kNumSlots || ! result.slots[static_cast<size_t> (referenceSlot)].hasIR)
        {
            result.message = "This slot is empty, so the other slots can't be aligned to it.";
            return result;
        }

        if (numLoaded < 2)
        {
            result.message = "Load IRs into at least two slots to align them.";
            return result;
        }

        const auto ref = static_cast<size_t> (referenceSlot);
        result.slots[ref].isReference = true;
        result.slots[ref].aligned = true;

        // --- How much later (or earlier) does each slot arrive than the reference? ---
        auto earliest = 0.0, latest = 0.0; // in ms; the reference is at 0
        auto numAligned = 0;

        for (size_t i = 0; i < slots.size(); ++i)
        {
            if (i == ref || ! result.slots[i].hasIR)
                continue;

            const auto pair = correlate (slots[ref].ir, slots[i].ir, sampleRate);
            auto& slot = result.slots[i];
            slot.match = pair.valid ? pair.match : 0.0f;

            if (! pair.valid || pair.match < kMinMatch)
                continue; // too different (or silent): leave this slot alone

            slot.aligned = true;
            slot.lagMs = pair.lagSamples / sampleRate * 1000.0;

            // The reference keeps its polarity; the others follow it.
            slot.phaseInvert = slots[ref].phaseInvert != pair.inverted;
            slot.polarityChanged = slot.phaseInvert != slots[i].phaseInvert;

            earliest = juce::jmin (earliest, slot.lagMs);
            latest = juce::jmax (latest, slot.lagMs);
            ++numAligned;
        }

        const auto resetChanges = [&result]
        {
            for (auto& slot : result.slots)
            {
                slot.aligned = false;
                slot.polarityChanged = false;
                slot.delayMs = 0.0;
            }
        };

        if (numAligned == 0)
        {
            result.message = "The other IRs have too little in common with " + slotLabel (ref)
                             + " to find a reliable time offset. Alignment works for microphones recorded on the same speaker.";
            resetChanges();
            return result;
        }

        const auto span = latest - earliest;

        if (span > static_cast<double> (kMaxDelayMs))
        {
            result.message = "The slots are " + juce::String (span, 1) + " ms apart. The Delay control covers up to "
                             + juce::String (juce::roundToInt (kMaxDelayMs)) + " ms, so they can't be aligned.";
            resetChanges();
            return result;
        }

        // --- Delay everything to meet the slot that arrives last ----------------------
        juce::StringArray lines;

        for (size_t i = 0; i < slots.size(); ++i)
        {
            auto& slot = result.slots[i];

            if (! slot.hasIR)
                continue;

            if (! slot.aligned)
            {
                lines.add (slotLabel (i) + ": not changed (only " + juce::String (juce::roundToInt (slot.match * 100.0f))
                           + " % match: too different from the reference)");
                continue;
            }

            slot.delayMs = juce::jlimit (0.0, static_cast<double> (kMaxDelayMs), std::round ((latest - slot.lagMs) * 1000.0) / 1000.0);

            auto line = slotLabel (i) + ": delay " + formatMs (slot.delayMs);

            if (slot.isReference)
                line << "  (reference)";
            else
                line << (slot.polarityChanged ? ", polarity flipped" : "") << ", " << juce::roundToInt (slot.match * 100.0f) << " % match";

            lines.add (line);
        }

        result.ok = true;
        result.message = lines.joinIntoString ("\n");
        return result;
    }
}
