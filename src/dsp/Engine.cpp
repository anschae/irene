#include "Engine.h"

namespace irene
{
    namespace
    {
        constexpr double kSmoothingSeconds = 0.02;

        void updatePeak (std::atomic<float>& peak, const float* data, int numSamples) noexcept
        {
            const auto range = juce::FloatVectorOperations::findMinAndMax (data, numSamples);
            const auto blockPeak = juce::jmax (std::abs (range.getStart()), std::abs (range.getEnd()));

            if (blockPeak > peak.load (std::memory_order_relaxed))
                peak.store (blockPeak, std::memory_order_relaxed);
        }
    }

    Engine::Engine()
    {
        for (auto& slot : slots)
            slot = std::make_unique<SlotDsp> (messageQueue);
    }

    void Engine::prepare (double newSampleRate, int hostBlockSize, int numInputChannels, int numOutputChannels)
    {
        sampleRate = newSampleRate;
        numIn = juce::jlimit (1, 2, numInputChannels);
        numOut = juce::jlimit (1, 2, numOutputChannels);
        chunkSize = juce::jlimit (1, kMaxChunkSize, hostBlockSize);

        work.setSize (2, chunkSize, false, false, true);
        wet.setSize (2, chunkSize, false, false, true);

        inputFilter.prepare (sampleRate);
        outputFilter.prepare (sampleRate);

        for (auto& slot : slots)
            slot->prepare (sampleRate, chunkSize);

        mix.reset (sampleRate, kSmoothingSeconds);
        outGain.reset (sampleRate, kSmoothingSeconds);
        snapSmoothers = true;
        prepared = true;
    }

    void Engine::process (juce::AudioBuffer<float>& buffer, const EngineSettings& settings, bool nonRealtime) noexcept
    {
        jassert (prepared);

        const auto numSamples = buffer.getNumSamples();
        const auto numChannels = buffer.getNumChannels();

        if (numSamples == 0 || numChannels == 0)
            return;

        inputFilter.setSettings (settings.inputFilter);
        outputFilter.setSettings (settings.outputFilter);

        const auto mixTarget = juce::jlimit (0.0f, 1.0f, settings.mix);
        const auto gainTarget = juce::Decibels::decibelsToGain (settings.outGainDb);

        if (snapSmoothers)
        {
            mix.setCurrentAndTargetValue (mixTarget);
            outGain.setCurrentAndTargetValue (gainTarget);
            snapSmoothers = false;
        }
        else
        {
            mix.setTargetValue (mixTarget);
            outGain.setTargetValue (gainTarget);
        }

        for (int pos = 0; pos < numSamples; pos += chunkSize)
        {
            float* host[2] { buffer.getWritePointer (0, pos),
                             numChannels > 1 ? buffer.getWritePointer (1, pos) : nullptr };

            processChunk (host, juce::jmin (chunkSize, numSamples - pos), settings, nonRealtime);
        }
    }

    void Engine::processChunk (float* const* host, int n, const EngineSettings& settings, bool nonRealtime) noexcept
    {
        const auto hasSecondChannel = host[1] != nullptr;
        const auto stereoIn = numIn == 2 && hasSecondChannel;
        const auto stereoOut = numOut == 2 && hasSecondChannel;

        float* workChannels[2] { work.getWritePointer (0), work.getWritePointer (1) };
        float* wetChannels[2] { wet.getWritePointer (0), wet.getWritePointer (1) };

        // --- Input ---------------------------------------------------------------
        juce::FloatVectorOperations::copy (workChannels[0], host[0], n);
        updatePeak (inputPeak[0], workChannels[0], n);

        if (stereoIn)
        {
            juce::FloatVectorOperations::copy (workChannels[1], host[1], n);
            updatePeak (inputPeak[1], workChannels[1], n);
        }

        inputFilter.process (workChannels, stereoIn ? 2 : 1, n);

        if (! stereoIn)
            juce::FloatVectorOperations::copy (workChannels[1], workChannels[0], n);

        // --- Slots (wet) ---------------------------------------------------------
        juce::FloatVectorOperations::clear (wetChannels[0], n);
        juce::FloatVectorOperations::clear (wetChannels[1], n);

        for (int i = 0; i < kNumSlots; ++i)
            slots[static_cast<size_t> (i)]->process (workChannels, wetChannels, n,
                                                     settings.slots[static_cast<size_t> (i)],
                                                     settings.isSlotAudible (i), stereoOut, nonRealtime);

        // --- Dry/wet mix ---------------------------------------------------------
        // `work` holds the dry signal (after the input filter block).
        if (! stereoOut)
        {
            // Mono output: the wet signal is the mono sum (mid) of both channels.
            juce::FloatVectorOperations::add (wetChannels[0], wetChannels[1], n);
            juce::FloatVectorOperations::multiply (wetChannels[0], 0.5f, n);
        }

        const auto numProcessed = stereoOut ? 2 : 1;

        if (mix.isSmoothing())
        {
            for (int i = 0; i < n; ++i)
            {
                const auto m = mix.getNextValue();

                for (int ch = 0; ch < numProcessed; ++ch)
                    workChannels[ch][i] += m * (wetChannels[ch][i] - workChannels[ch][i]);
            }
        }
        else
        {
            const auto m = mix.getCurrentValue();

            for (int ch = 0; ch < numProcessed; ++ch)
            {
                if (m >= 1.0f)
                {
                    juce::FloatVectorOperations::copy (workChannels[ch], wetChannels[ch], n);
                }
                else
                {
                    juce::FloatVectorOperations::multiply (workChannels[ch], 1.0f - m, n);
                    juce::FloatVectorOperations::addWithMultiply (workChannels[ch], wetChannels[ch], m, n);
                }
            }
        }

        // --- Output --------------------------------------------------------------
        outputFilter.process (workChannels, numProcessed, n);

        if (outGain.isSmoothing())
        {
            for (int i = 0; i < n; ++i)
            {
                const auto g = outGain.getNextValue();

                for (int ch = 0; ch < numProcessed; ++ch)
                    workChannels[ch][i] *= g;
            }
        }
        else if (const auto g = outGain.getCurrentValue(); g != 1.0f)
        {
            for (int ch = 0; ch < numProcessed; ++ch)
                juce::FloatVectorOperations::multiply (workChannels[ch], g, n);
        }

        for (int ch = 0; ch < numProcessed; ++ch)
        {
            juce::FloatVectorOperations::copy (host[ch], workChannels[ch], n);
            updatePeak (outputPeak[static_cast<size_t> (ch)], workChannels[ch], n);
        }

        // A host buffer with more channels than the output layout: silence the rest.
        if (hasSecondChannel && ! stereoOut)
            juce::FloatVectorOperations::clear (host[1], n);
    }
}
