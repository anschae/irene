#include "Response.h"

#include <juce_dsp/juce_dsp.h>

#include <numbers>

#include "dsp/FilterBlock.h"
#include "dsp/FractionalDelay.h"
#include "dsp/SlotDsp.h"

namespace irene::analysis
{
    namespace
    {
        // Up to this length the spectrum is evaluated exactly at each display
        // frequency; longer IRs go through one large FFT instead.
        constexpr int kMaxDirectLength = 1 << 17;

        float toDb (double magnitude) noexcept
        {
            return static_cast<float> (20.0 * std::log10 (std::max (magnitude, 1.0e-9)));
        }

        std::vector<std::complex<float>> directSpectrum (const float* data, int numSamples, double sampleRate,
                                                         const std::vector<double>& frequencies)
        {
            std::vector<std::complex<float>> result (frequencies.size());

            for (size_t i = 0; i < frequencies.size(); ++i)
            {
                const auto omega = 2.0 * std::numbers::pi * frequencies[i] / sampleRate;
                const auto step = std::polar (1.0, -omega);
                std::complex<double> phasor { 1.0, 0.0 }, sum { 0.0, 0.0 };

                for (int n = 0; n < numSamples; ++n)
                {
                    sum += static_cast<double> (data[n]) * phasor;
                    phasor *= step;
                }

                result[i] = { static_cast<float> (sum.real()), static_cast<float> (sum.imag()) };
            }

            return result;
        }

        std::vector<std::complex<float>> fftSpectrum (const float* data, int numSamples, double sampleRate,
                                                      const std::vector<double>& frequencies)
        {
            // Zero-pad generously so that neighbouring bins can be interpolated.
            const auto order = juce::jlimit (16, 23, static_cast<int> (std::ceil (std::log2 (static_cast<double> (numSamples)))) + 2);
            const auto size = 1 << order;
            const auto used = juce::jmin (numSamples, size);

            juce::dsp::FFT fft (order);
            std::vector<float> buffer (static_cast<size_t> (size) * 2, 0.0f);
            std::copy_n (data, used, buffer.begin());
            fft.performRealOnlyForwardTransform (buffer.data(), true);

            std::vector<std::complex<float>> result (frequencies.size());

            for (size_t i = 0; i < frequencies.size(); ++i)
            {
                const auto position = juce::jlimit (0.0, static_cast<double> (size / 2 - 1), frequencies[i] / sampleRate * size);
                const auto bin = static_cast<size_t> (position);
                const auto frac = static_cast<float> (position - static_cast<double> (bin));

                const std::complex<float> a { buffer[bin * 2], buffer[bin * 2 + 1] };
                const std::complex<float> b { buffer[bin * 2 + 2], buffer[bin * 2 + 3] };
                result[i] = a + frac * (b - a);
            }

            return result;
        }

        void smooth (std::vector<float>& db, Smoothing smoothing)
        {
            if (smoothing == Smoothing::none || db.empty())
                return;

            // Averages the power over a window of the given width (the points
            // are log-spaced, so a fixed number of points is a fixed fraction of
            // an octave).
            const auto halfWindow = (smoothing == Smoothing::sixthOctave ? kPointsPerOctave / 6 : kPointsPerOctave / 3) / 2;
            const auto n = static_cast<int> (db.size());

            std::vector<double> power (db.size());

            for (size_t i = 0; i < db.size(); ++i)
                power[i] = std::pow (10.0, static_cast<double> (db[i]) / 10.0);

            for (int i = 0; i < n; ++i)
            {
                const auto from = std::max (0, i - halfWindow);
                const auto to = std::min (n - 1, i + halfWindow);
                auto sum = 0.0;

                for (int k = from; k <= to; ++k)
                    sum += power[static_cast<size_t> (k)];

                db[static_cast<size_t> (i)] = static_cast<float> (10.0 * std::log10 (std::max (sum / (to - from + 1), 1.0e-18)));
            }
        }
    }

    //==============================================================================
    std::vector<double> makeDisplayFrequencies()
    {
        const auto octaves = std::log2 (kMaxDisplayFreq / kMinDisplayFreq);
        const auto numPoints = static_cast<int> (std::ceil (octaves * kPointsPerOctave)) + 1;
        std::vector<double> frequencies (static_cast<size_t> (numPoints));

        for (int i = 0; i < numPoints; ++i)
            frequencies[static_cast<size_t> (i)] = std::min (kMaxDisplayFreq, kMinDisplayFreq * std::pow (2.0, static_cast<double> (i) / kPointsPerOctave));

        return frequencies;
    }

    SlotSpectrum computeSpectrum (const juce::AudioBuffer<float>& ir, double sampleRate, const std::vector<double>& frequencies)
    {
        SlotSpectrum spectrum;

        if (ir.getNumSamples() == 0 || ir.getNumChannels() == 0 || sampleRate <= 0.0)
            return spectrum;

        const auto compute = [&] (int channel)
        {
            return ir.getNumSamples() <= kMaxDirectLength
                       ? directSpectrum (ir.getReadPointer (channel), ir.getNumSamples(), sampleRate, frequencies)
                       : fftSpectrum (ir.getReadPointer (channel), ir.getNumSamples(), sampleRate, frequencies);
        };

        spectrum.stereo = ir.getNumChannels() > 1;
        spectrum.left = compute (0);
        spectrum.right = spectrum.stereo ? compute (1) : spectrum.left;
        return spectrum;
    }

    ResponseCurves computeResponse (const ResponseInput& input, const std::vector<double>& frequencies,
                                    Channel channel, Smoothing smoothing)
    {
        using Complex = std::complex<double>;

        ResponseCurves curves;
        const auto numPoints = frequencies.size();
        const auto& s = input.settings;
        const auto fs = input.sampleRate;

        const auto select = [channel] (Complex left, Complex right)
        {
            switch (channel)
            {
                case Channel::left:  return left;
                case Channel::right: return right;
                case Channel::mid:   break;
            }

            return 0.5 * (left + right);
        };

        struct SlotData
        {
            const SlotSpectrum* spectrum = nullptr;
            std::array<float, 2> gains { 0.0f, 0.0f };
            double delaySamples = 0.0;
        };

        std::array<SlotData, kNumSlots> slotData;

        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto index = static_cast<size_t> (i);
            const auto& spectrum = input.slots[index];

            if (spectrum == nullptr || ! spectrum->isValid() || spectrum->left.size() != numPoints)
                continue;

            slotData[index].spectrum = spectrum.get();
            slotData[index].gains = SlotDsp::computeGains (s.slots[index], s.isSlotAudible (i), input.stereoOutput, spectrum->stereo);
            slotData[index].delaySamples = static_cast<double> (juce::jlimit (0.0f, kMaxDelayMs, s.slots[index].delayMs)) * 0.001 * fs;
            curves.slotDb[index].resize (numPoints);
            curves.anything = true;
        }

        curves.combinedDb.resize (numPoints);

        for (size_t p = 0; p < numPoints; ++p)
        {
            const auto f = frequencies[p];
            const auto omega = 2.0 * std::numbers::pi * f / fs;
            const auto inOut = FilterBlock::response (s.inputFilter, f, fs) * FilterBlock::response (s.outputFilter, f, fs);

            Complex sumLeft { 0.0, 0.0 }, sumRight { 0.0, 0.0 };

            for (size_t i = 0; i < slotData.size(); ++i)
            {
                const auto& slot = slotData[i];

                if (slot.spectrum == nullptr)
                    continue;

                const auto common = FilterBlock::response (s.slots[i].filter, f, fs) * FractionalDelay::response (slot.delaySamples, omega);
                auto left = Complex (slot.spectrum->left[p]) * common;
                auto right = Complex (slot.spectrum->right[p]) * common;

                if (input.stereoOutput)
                {
                    left *= static_cast<double> (slot.gains[0]);
                    right *= static_cast<double> (slot.gains[1]);
                }
                else
                {
                    // mono output: the slot's mono sum, pan ignored
                    left = right = 0.5 * (left + right) * static_cast<double> (slot.gains[0]);
                }

                curves.slotDb[i][p] = toDb (std::abs (select (left, right) * inOut));
                sumLeft += left;
                sumRight += right;
            }

            curves.combinedDb[p] = toDb (std::abs (select (sumLeft, sumRight) * inOut));
        }

        smooth (curves.combinedDb, smoothing);

        for (auto& slot : curves.slotDb)
            smooth (slot, smoothing);

        return curves;
    }

    std::vector<float> computeFilterCurve (const FilterBlockSettings& settings, double sampleRate, const std::vector<double>& frequencies)
    {
        std::vector<float> db (frequencies.size());

        for (size_t i = 0; i < frequencies.size(); ++i)
            db[i] = toDb (std::abs (FilterBlock::response (settings, frequencies[i], sampleRate)));

        return db;
    }

    //==============================================================================
    SpectrumWorker::SpectrumWorker (SlotManager& slots)
        : juce::Thread ("IRene response analysis"), slotManager (slots), frequencies (makeDisplayFrequencies())
    {
        for (auto& g : seenGenerations)
            g = 0;

        startThread (juce::Thread::Priority::low);
    }

    SpectrumWorker::~SpectrumWorker()
    {
        cancelPendingUpdate();
        signalThreadShouldExit();
        notify();
        stopThread (10000);
    }

    std::shared_ptr<const SlotSpectrum> SpectrumWorker::getSpectrum (int slot) const
    {
        const juce::ScopedLock sl (lock);
        return spectra[static_cast<size_t> (slot)];
    }

    bool SpectrumWorker::isUpToDate() const
    {
        if (! everRan.load())
            return false;

        for (int i = 0; i < kNumSlots; ++i)
            if (seenGenerations[static_cast<size_t> (i)].load() != slotManager.getBuildGeneration (i))
                return false;

        return true;
    }

    void SpectrumWorker::waitUntilUpToDate()
    {
        while (! isUpToDate() && ! threadShouldExit())
            juce::Thread::sleep (2);
    }

    void SpectrumWorker::run()
    {
        while (! threadShouldExit())
        {
            auto changed = false;

            for (int i = 0; i < kNumSlots && ! threadShouldExit(); ++i)
            {
                const auto index = static_cast<size_t> (i);
                const auto generation = slotManager.getBuildGeneration (i);

                if (everRan.load() && generation == seenGenerations[index].load())
                    continue;

                const auto ir = slotManager.getPreparedIR (i);
                const auto rate = slotManager.getEngineSampleRate();

                auto spectrum = (ir.getNumSamples() > 0 && rate > 0.0)
                                    ? std::make_shared<const SlotSpectrum> (computeSpectrum (ir, rate, frequencies))
                                    : std::shared_ptr<const SlotSpectrum>();

                {
                    const juce::ScopedLock sl (lock);
                    spectra[index] = std::move (spectrum);
                }

                seenGenerations[index] = generation;
                changed = true;
            }

            everRan = true;

            if (changed)
                triggerAsyncUpdate();

            wait (40);
        }
    }

    void SpectrumWorker::handleAsyncUpdate()
    {
        if (onSpectraChanged)
            onSpectraChanged();
    }
}
