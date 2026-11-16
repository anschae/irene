#include "IRProcessing.h"

#include <juce_dsp/juce_dsp.h>

#include <numbers>

namespace irene::irproc
{
    namespace
    {
        //==============================================================================
        // Windowed-sinc kernel (Kaiser window), tabulated once and read with
        // linear interpolation. 64 zero crossings per side give a transition band
        // of about 5 % of the sample rate and roughly 100 dB of stop-band rejection.
        constexpr int kZeroCrossings = 64;
        constexpr int kTableResolution = 512; // table points per zero crossing
        constexpr double kKaiserBeta = 10.0;

        double besselI0 (double x) noexcept
        {
            auto sum = 1.0, term = 1.0;
            const auto halfSquared = x * x * 0.25;

            for (int k = 1; k < 64; ++k)
            {
                term *= halfSquared / (static_cast<double> (k) * k);
                sum += term;

                if (term < sum * 1.0e-17)
                    break;
            }

            return sum;
        }

        class KernelTable
        {
        public:
            KernelTable()
            {
                const auto size = kZeroCrossings * kTableResolution;
                table.resize (static_cast<size_t> (size) + 2, 0.0);
                const auto i0Beta = besselI0 (kKaiserBeta);

                for (int i = 0; i < size; ++i)
                {
                    const auto u = static_cast<double> (i) / kTableResolution;
                    const auto x = u / kZeroCrossings;
                    const auto window = besselI0 (kKaiserBeta * std::sqrt (std::max (0.0, 1.0 - x * x))) / i0Beta;
                    const auto sinc = i == 0 ? 1.0 : std::sin (std::numbers::pi * u) / (std::numbers::pi * u);
                    table[static_cast<size_t> (i)] = sinc * window;
                }
            }

            double operator() (double u) const noexcept
            {
                const auto pos = std::abs (u) * kTableResolution;

                if (pos >= static_cast<double> (kZeroCrossings * kTableResolution))
                    return 0.0;

                const auto index = static_cast<size_t> (pos);
                const auto frac = pos - static_cast<double> (index);
                return table[index] + frac * (table[index + 1] - table[index]);
            }

        private:
            std::vector<double> table;
        };

        const KernelTable& kernel()
        {
            static const KernelTable instance; // thread-safe initialisation
            return instance;
        }
    }

    //==============================================================================
    juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& source, double sourceRate, double targetRate)
    {
        const auto numChannels = source.getNumChannels();
        const auto inLength = source.getNumSamples();

        if (inLength == 0 || numChannels == 0 || sourceRate <= 0.0 || targetRate <= 0.0
            || std::abs (sourceRate - targetRate) < 1.0e-6)
        {
            juce::AudioBuffer<float> copy;
            copy.makeCopyOf (source);
            return copy;
        }

        const auto step = sourceRate / targetRate;              // source samples per output sample
        const auto cutoff = std::min (1.0, targetRate / sourceRate); // relative to the source Nyquist
        const auto halfWidth = kZeroCrossings / cutoff;
        const auto outLength = std::max (1, static_cast<int> (std::ceil (inLength * targetRate / sourceRate)));

        // `cutoff` keeps the waveform's amplitude; `step` then rescales it so that
        // the IR's frequency response as a filter is unchanged at the new rate.
        const auto scale = cutoff * step;

        const auto& k = kernel();
        juce::AudioBuffer<float> result (numChannels, outLength);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto* in = source.getReadPointer (ch);
            auto* out = result.getWritePointer (ch);

            for (int n = 0; n < outLength; ++n)
            {
                const auto t = n * step;
                const auto first = std::max (0, static_cast<int> (std::ceil (t - halfWidth)));
                const auto last = std::min (inLength - 1, static_cast<int> (std::floor (t + halfWidth)));
                auto acc = 0.0;

                for (int i = first; i <= last; ++i)
                    acc += in[i] * k ((t - i) * cutoff);

                out[n] = static_cast<float> (acc * scale);
            }
        }

        return result;
    }

    //==============================================================================
    float computeNormalisationGainDb (const juce::AudioBuffer<float>& audio, double sampleRate)
    {
        const auto numSamples = audio.getNumSamples();

        if (numSamples == 0 || audio.getNumChannels() == 0 || sampleRate <= 0.0)
            return 0.0f;

        // At least 2^15 points so that the 80 Hz band edge is well resolved even
        // for very short IRs.
        const auto order = juce::jlimit (15, 22, static_cast<int> (std::ceil (std::log2 (static_cast<double> (numSamples)))));
        const auto fftSize = 1 << order;
        const auto used = juce::jmin (numSamples, fftSize);

        const auto binHz = sampleRate / fftSize;
        const auto firstBin = juce::jmax (1, static_cast<int> (std::ceil (80.0 / binHz)));
        const auto lastBin = juce::jmin (fftSize / 2 - 1, static_cast<int> (std::floor (std::min (8000.0, 0.45 * sampleRate) / binHz)));

        if (lastBin <= firstBin)
            return 0.0f;

        juce::dsp::FFT fft (order);
        std::vector<float> data (static_cast<size_t> (fftSize) * 2);
        auto energy = 0.0;

        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        {
            std::fill (data.begin(), data.end(), 0.0f);
            std::copy_n (audio.getReadPointer (ch), used, data.begin());
            fft.performFrequencyOnlyForwardTransform (data.data(), true);

            // Pink weighting: equal weight per octave, i.e. 1/f per linear bin.
            auto sum = 0.0, weightSum = 0.0;

            for (int bin = firstBin; bin <= lastBin; ++bin)
            {
                const auto weight = 1.0 / bin;
                const auto magnitude = static_cast<double> (data[static_cast<size_t> (bin)]);
                sum += weight * magnitude * magnitude;
                weightSum += weight;
            }

            energy += sum / weightSum;
        }

        energy /= audio.getNumChannels();

        if (energy < 1.0e-20)
            return 0.0f;

        return juce::jlimit (-40.0f, 40.0f, static_cast<float> (-10.0 * std::log10 (energy)));
    }

    int findLeadingSilence (const juce::AudioBuffer<float>& audio)
    {
        constexpr int kPreRoll = 2;
        const auto numSamples = audio.getNumSamples();
        const auto peak = audio.getMagnitude (0, numSamples);

        if (peak <= 0.0f)
            return 0;

        const auto threshold = peak * 0.001f; // -60 dB

        for (int i = 0; i < numSamples; ++i)
            for (int ch = 0; ch < audio.getNumChannels(); ++ch)
                if (std::abs (audio.getSample (ch, i)) >= threshold)
                    return juce::jmax (0, i - kPreRoll);

        return 0;
    }

    int trimmedLength (int numSamples, float lengthPercent, double sampleRate)
    {
        if (lengthPercent >= 100.0f)
            return numSamples;

        const auto minimum = juce::jmin (numSamples, juce::jmax (16, static_cast<int> (0.001 * sampleRate)));
        const auto wanted = juce::roundToInt (numSamples * static_cast<double> (lengthPercent) / 100.0);
        return juce::jlimit (minimum, numSamples, wanted);
    }

    void applyLength (juce::AudioBuffer<float>& audio, float lengthPercent, double sampleRate)
    {
        const auto keep = trimmedLength (audio.getNumSamples(), lengthPercent, sampleRate);

        if (keep >= audio.getNumSamples())
            return;

        audio.setSize (audio.getNumChannels(), keep, true);

        // Short raised-cosine fade-out: 5 ms, but never more than a quarter of the IR.
        const auto fade = juce::jlimit (2, juce::jmax (2, keep / 4), static_cast<int> (0.005 * sampleRate));

        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        {
            auto* data = audio.getWritePointer (ch);

            for (int i = 0; i < fade; ++i)
            {
                const auto phase = static_cast<double> (i + 1) / fade;
                data[keep - fade + i] *= static_cast<float> (0.5 * (1.0 + std::cos (std::numbers::pi * phase)));
            }
        }
    }

    //==============================================================================
    juce::AudioBuffer<float> resampleSource (const IRSource& source, double targetRate, bool autoTrim)
    {
        const auto start = autoTrim ? juce::jmin (source.leadingSilenceSamples, source.audio.getNumSamples() - 1) : 0;

        if (start <= 0)
            return resample (source.audio, source.sampleRate, targetRate);

        juce::AudioBuffer<float> trimmed (source.audio.getNumChannels(), source.audio.getNumSamples() - start);

        for (int ch = 0; ch < trimmed.getNumChannels(); ++ch)
            trimmed.copyFrom (ch, 0, source.audio, ch, start, trimmed.getNumSamples());

        return resample (trimmed, source.sampleRate, targetRate);
    }

    juce::AudioBuffer<float> finishPrepared (const juce::AudioBuffer<float>& resampled, const IRSource& source, const PrepareOptions& options)
    {
        juce::AudioBuffer<float> result;
        result.makeCopyOf (resampled);

        applyLength (result, options.lengthPercent, options.targetRate);

        if (options.normalise)
            result.applyGain (juce::Decibels::decibelsToGain (source.normalisationGainDb));

        // The engine uses a 1-sample IR as its "empty slot" marker.
        if (result.getNumSamples() < 2)
            result.setSize (result.getNumChannels(), 2, true, true);

        return result;
    }

    juce::AudioBuffer<float> prepare (const IRSource& source, const PrepareOptions& options)
    {
        return finishPrepared (resampleSource (source, options.targetRate, options.autoTrim), source, options);
    }
}
