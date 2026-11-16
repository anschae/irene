#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <complex>
#include <numbers>

#include "dsp/Engine.h"
#include "ir/IRSource.h"
#include "params/Settings.h"

namespace irene::test
{
    using Layout = juce::AudioProcessor::BusesLayout;

    inline Layout makeLayout (const juce::AudioChannelSet& in, const juce::AudioChannelSet& out)
    {
        Layout layout;
        layout.inputBuses.add (in);
        layout.outputBuses.add (out);
        return layout;
    }

    //==============================================================================
    inline juce::AudioBuffer<float> makeNoise (int numChannels, int numSamples, juce::int64 seed, float amplitude = 0.5f)
    {
        juce::Random random (seed);
        juce::AudioBuffer<float> buffer (numChannels, numSamples);

        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < numSamples; ++i)
                buffer.setSample (ch, i, amplitude * (random.nextFloat() * 2.0f - 1.0f));

        return buffer;
    }

    /** Something that looks like a cabinet IR: decaying noise with a short pre-delay. */
    inline juce::AudioBuffer<float> makeTestIR (int numChannels, int numSamples, juce::int64 seed, int preDelay = 3)
    {
        juce::Random random (seed);
        juce::AudioBuffer<float> ir (numChannels, numSamples);
        ir.clear();

        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = preDelay; i < numSamples; ++i)
            {
                const auto envelope = std::exp (-6.0f * static_cast<float> (i - preDelay) / static_cast<float> (numSamples));
                ir.setSample (ch, i, 0.4f * envelope * (random.nextFloat() * 2.0f - 1.0f));
            }

        return ir;
    }

    inline juce::AudioBuffer<float> makeDelta (int numChannels = 1, int numSamples = 8, int position = 0, float amplitude = 1.0f)
    {
        juce::AudioBuffer<float> ir (numChannels, numSamples);
        ir.clear();

        for (int ch = 0; ch < numChannels; ++ch)
            ir.setSample (ch, position, amplitude);

        return ir;
    }

    /** Direct-form reference convolution of one channel (output length = signal length). */
    inline std::vector<double> convolve (const float* signal, int signalLength, const float* ir, int irLength)
    {
        std::vector<double> out (static_cast<size_t> (signalLength), 0.0);

        for (int n = 0; n < signalLength; ++n)
        {
            const auto kMax = std::min (n, irLength - 1);
            auto acc = 0.0;

            for (int k = 0; k <= kMax; ++k)
                acc += static_cast<double> (ir[k]) * static_cast<double> (signal[n - k]);

            out[static_cast<size_t> (n)] = acc;
        }

        return out;
    }

    /** Error of `actual` against `expected`, in dB relative to the RMS of `expected`. */
    template <typename A, typename B>
    double errorDb (const A* actual, const B* expected, int numSamples)
    {
        auto errorEnergy = 0.0, referenceEnergy = 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto e = static_cast<double> (actual[i]) - static_cast<double> (expected[i]);
            errorEnergy += e * e;
            referenceEnergy += static_cast<double> (expected[i]) * static_cast<double> (expected[i]);
        }

        if (referenceEnergy <= 0.0)
            return errorEnergy <= 0.0 ? -300.0 : 300.0;

        return 10.0 * std::log10 (std::max (errorEnergy / referenceEnergy, 1.0e-30));
    }

    inline double rmsDb (const float* data, int numSamples)
    {
        auto energy = 0.0;

        for (int i = 0; i < numSamples; ++i)
            energy += static_cast<double> (data[i]) * static_cast<double> (data[i]);

        return 10.0 * std::log10 (std::max (energy / std::max (1, numSamples), 1.0e-30));
    }

    /** Discrete-time Fourier transform of a (short) signal at one frequency. */
    inline std::complex<double> dtft (const float* data, int numSamples, double freqHz, double sampleRate)
    {
        const auto omega = 2.0 * std::numbers::pi * freqHz / sampleRate;
        std::complex<double> sum { 0.0, 0.0 };

        for (int n = 0; n < numSamples; ++n)
            sum += static_cast<double> (data[n]) * std::polar (1.0, -omega * n);

        return sum;
    }

    inline double toDb (double magnitude) { return 20.0 * std::log10 (std::max (magnitude, 1.0e-15)); }

    /** Largest jump between neighbouring samples: a click detector. */
    inline float maxStep (const float* data, int numSamples)
    {
        auto worst = 0.0f;

        for (int i = 1; i < numSamples; ++i)
            worst = std::max (worst, std::abs (data[i] - data[i - 1]));

        return worst;
    }

    //==============================================================================
    /** Runs `input` through the engine using the given sequence of block sizes
        (repeated as needed) and returns the output. */
    inline juce::AudioBuffer<float> render (Engine& engine, const juce::AudioBuffer<float>& input,
                                            const EngineSettings& settings, std::vector<int> blockSizes,
                                            bool nonRealtime = true)
    {
        juce::AudioBuffer<float> output;
        output.makeCopyOf (input);

        auto pos = 0;
        size_t blockIndex = 0;

        while (pos < output.getNumSamples())
        {
            const auto n = std::min (blockSizes[blockIndex++ % blockSizes.size()], output.getNumSamples() - pos);
            juce::AudioBuffer<float> block (output.getArrayOfWritePointers(), output.getNumChannels(), pos, n);
            engine.process (block, settings, nonRealtime);
            pos += n;
        }

        return output;
    }

    inline IRSource::Ptr makeSource (const juce::AudioBuffer<float>& audio, double sampleRate, const juce::String& name = "test.wav")
    {
        return IRSource::create (audio, sampleRate, { "/nowhere/" + name, name, hashData (audio.getReadPointer (0), sizeof (float) * static_cast<size_t> (audio.getNumSamples())) });
    }

    /** Writes a WAV file (bitsPerSample 16/24 = integer, 32 = float). */
    inline bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate, int bitsPerSample = 32)
    {
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();

        if (stream == nullptr)
            return false;

        juce::WavAudioFormat format;
        auto options = juce::AudioFormatWriterOptions()
                           .withSampleRate (sampleRate)
                           .withNumChannels (audio.getNumChannels())
                           .withBitsPerSample (bitsPerSample);

        if (bitsPerSample == 32)
            options = options.withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);

        auto writer = format.createWriterFor (stream, options);

        if (writer == nullptr)
            return false;

        return writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    }

    struct TempFolder
    {
        TempFolder()
            : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("IReneTests_" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64())))
        {
            folder.createDirectory();
        }

        ~TempFolder() { folder.deleteRecursively(); }

        juce::File folder;
    };
}
