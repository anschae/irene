#include "SlotDsp.h"

#include <numbers>

namespace irene
{
    namespace
    {
        // Size of the first, low-latency partition of the convolution. Longer IRs
        // continue in a second engine with larger FFT blocks, which keeps the CPU
        // load of long IRs low while the overall latency stays at zero.
        constexpr int kHeadSize = 2048;

        constexpr double kGainRampSeconds = 0.02;

        // juce::dsp::Convolution crossfades between IRs over 50 ms.
        constexpr double kCrossfadeSeconds = 0.06;

        juce::AudioBuffer<float> makeSilentIR()
        {
            juce::AudioBuffer<float> silent (1, 1);
            silent.clear();
            return silent;
        }
    }

    SlotDsp::SlotDsp (juce::dsp::ConvolutionMessageQueue& queue)
        : convolution (juce::dsp::Convolution::NonUniform { kHeadSize }, queue)
    {
        // Without an IR juce::dsp::Convolution passes its input through, so
        // start with the "empty slot" marker instead.
        mailboxBuffer = makeSilentIR();
        mailboxIsEmptyIR = true;
        mailboxFull = true;
    }

    void SlotDsp::prepare (double sampleRate, int maxBlockSize)
    {
        spec = { sampleRate, static_cast<juce::uint32> (maxBlockSize), 2 };

        scratch.setSize (2, maxBlockSize, false, false, true);
        delay.prepare (sampleRate, kMaxDelayMs / 1000.0);
        filter.prepare (sampleRate);
        gainRampLength = juce::jmax (1, static_cast<int> (kGainRampSeconds * sampleRate));
        gainRampRemaining = 0;
        snapGain = true;

        {
            // Not concurrent with process(), so this lock is never contended by
            // the audio thread here.
            const juce::SpinLock::ScopedLockType lock (mailboxLock);

            if (! mailboxFull.load() && irIsEmpty)
            {
                // Re-issue the "empty slot" marker at the new sample rate, so that
                // JUCE doesn't resample the old one into something longer.
                mailboxBuffer = makeSilentIR();
                mailboxIsEmptyIR = true;
                mailboxFull = true;
            }

            if (mailboxFull.exchange (false))
            {
                irIsEmpty = mailboxIsEmptyIR;
                irIsStereo = mailboxBuffer.getNumChannels() > 1;
                convolution.loadImpulseResponse (std::move (mailboxBuffer), sampleRate,
                                                 irIsStereo ? juce::dsp::Convolution::Stereo::yes : juce::dsp::Convolution::Stereo::no,
                                                 juce::dsp::Convolution::Trim::no,
                                                 juce::dsp::Convolution::Normalise::no);
            }
        }

        // Runs any pending load synchronously and installs the engine.
        convolution.prepare (spec);
        convolution.reset();
        prepared = true;

        idle = irIsEmpty;
        countingDown = false;
        stereoLocked = false;
        monoProcessing = false;
    }

    void SlotDsp::setIR (juce::AudioBuffer<float>&& impulseResponse)
    {
        const auto isEmpty = impulseResponse.getNumSamples() == 0 || impulseResponse.getNumChannels() == 0;
        auto incoming = isEmpty ? makeSilentIR() : std::move (impulseResponse);

        {
            const juce::SpinLock::ScopedLockType lock (mailboxLock);
            std::swap (mailboxBuffer, incoming);
            mailboxIsEmptyIR = isEmpty;
            mailboxFull = true;
        }

        // `incoming` now holds whatever was still waiting in the mailbox and is
        // freed here, off the audio thread.
    }

    void SlotDsp::collectMailbox (bool nonRealtime) noexcept
    {
        if (! mailboxFull.load (std::memory_order_acquire))
            return;

        const juce::SpinLock::ScopedTryLockType lock (mailboxLock);

        if (! lock.isLocked()) // setIR() is busy right now: try again next block
            return;

        if (! mailboxFull.exchange (false))
            return;

        const auto previousWasStereo = irIsStereo;
        irIsEmpty = mailboxIsEmptyIR;
        irIsStereo = mailboxBuffer.getNumChannels() > 1;
        stereoLocked = ! irIsStereo && previousWasStereo;
        countingDown = false;

        const auto wasIdle = idle;
        idle = false;

        // Wait-free: ownership of the buffer moves into the convolution's queue.
        convolution.loadImpulseResponse (std::move (mailboxBuffer), spec.sampleRate,
                                         irIsStereo ? juce::dsp::Convolution::Stereo::yes : juce::dsp::Convolution::Stereo::no,
                                         juce::dsp::Convolution::Trim::no,
                                         juce::dsp::Convolution::Normalise::no);

        if (nonRealtime)
        {
            // Offline render: timing doesn't matter but the result must not
            // depend on a background thread, so install the IR right now.
            convolution.prepare (spec);

            if (wasIdle)
                convolution.reset();

            if (irIsEmpty)
                goIdle();
        }
    }

    void SlotDsp::goIdle() noexcept
    {
        idle = true;
        countingDown = false;
        delay.reset();
        filter.reset();
        snapGain = true;
        gainRampRemaining = 0;
    }

    std::array<float, 2> SlotDsp::computeGains (const SlotSettings& s, bool audible, bool stereoOutput, bool irStereo) noexcept
    {
        auto g = (audible && s.gainDb > kSlotGainOffDb + 0.005f) ? juce::Decibels::decibelsToGain (s.gainDb) : 0.0f;

        if (s.phaseInvert)
            g = -g;

        if (! stereoOutput)
            return { g, g };

        // Equal-power law, scaled so that the centre position is unity gain.
        const auto theta = (juce::jlimit (-1.0f, 1.0f, s.pan) + 1.0f) * (std::numbers::pi_v<float> * 0.25f);
        auto left = std::numbers::sqrt2_v<float> * std::cos (theta);
        auto right = std::numbers::sqrt2_v<float> * std::sin (theta);

        if (std::abs (s.pan) < 1.0e-6f)
            left = right = 1.0f;

        if (irStereo)
        {
            // Balance: only attenuates the side that is panned away from.
            left = juce::jmin (1.0f, left);
            right = juce::jmin (1.0f, right);
        }

        return { g * left, g * right };
    }

    void SlotDsp::process (const float* const* input, float* const* wetOut, int numSamples,
                           const SlotSettings& settings, bool audible, bool stereoOutput, bool monoInput, bool nonRealtime) noexcept
    {
        jassert (prepared && numSamples <= scratch.getNumSamples());

        collectMailbox (nonRealtime);

        if (idle || numSamples <= 0)
            return;

        // With the same signal on both inputs and a mono IR, both channels would
        // come out identical: process the left one only and use it for both.
        const auto mono = monoInput && ! irIsStereo && ! stereoLocked;

        if (monoProcessing && ! mono)
        {
            // Back to two channels: the right channel's delay and filter memory
            // must continue from where the left one is (they would have been equal).
            delay.copyChannelState (0, 1);
            filter.copyChannelState (0, 1);
        }

        monoProcessing = mono;
        const auto numChannels = mono ? 1 : 2;

        for (int ch = 0; ch < numChannels; ++ch)
            juce::FloatVectorOperations::copy (scratch.getWritePointer (ch), input[ch], numSamples);

        // In mono, both "channels" point at the processed left channel.
        float* channels[2] { scratch.getWritePointer (0), scratch.getWritePointer (mono ? 0 : 1) };

        {
            juce::dsp::AudioBlock<float> block (channels, static_cast<size_t> (numChannels), static_cast<size_t> (numSamples));
            convolution.process (juce::dsp::ProcessContextReplacing<float> (block));
        }

        if (irIsEmpty && convolution.getCurrentIRSize() <= 1)
        {
            // The silent IR is installed. Once the crossfade to it is over
            // the slot can stop processing.
            if (! countingDown)
            {
                countingDown = true;
                idleCountdown = static_cast<int> (kCrossfadeSeconds * spec.sampleRate);
            }

            idleCountdown -= numSamples;
        }

        delay.setDelaySamples (static_cast<double> (settings.delayMs) * 0.001 * spec.sampleRate);
        delay.process (channels, numChannels, numSamples);

        filter.setSettings (settings.filter);
        filter.process (channels, numChannels, numSamples);

        const auto target = computeGains (settings, audible, stereoOutput, irIsStereo);

        if (snapGain)
        {
            gain = gainTarget = target;
            gainRampRemaining = 0;
            snapGain = false;
        }
        else if (target != gainTarget)
        {
            gainTarget = target;
            gainRampRemaining = gainRampLength;

            for (size_t ch = 0; ch < 2; ++ch)
                gainStep[ch] = (gainTarget[ch] - gain[ch]) / static_cast<float> (gainRampLength);
        }

        auto pos = 0;

        for (; pos < numSamples && gainRampRemaining > 0; ++pos)
        {
            --gainRampRemaining;

            for (size_t ch = 0; ch < 2; ++ch)
            {
                gain[ch] = gainRampRemaining == 0 ? gainTarget[ch] : gain[ch] + gainStep[ch];
                wetOut[ch][pos] += channels[ch][pos] * gain[ch];
            }
        }

        if (pos < numSamples)
            for (size_t ch = 0; ch < 2; ++ch)
                if (gain[ch] != 0.0f)
                    juce::FloatVectorOperations::addWithMultiply (wetOut[ch] + pos, channels[ch] + pos, gain[ch], numSamples - pos);

        if (irIsEmpty && countingDown && idleCountdown <= 0)
            goIdle();
    }
}
