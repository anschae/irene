#pragma once

#include <juce_dsp/juce_dsp.h>

#include "FilterBlock.h"
#include "FractionalDelay.h"
#include "params/Settings.h"

namespace irene
{
    /** The audio path of one IR slot:
        Convolution -> Delay -> Filter Block -> Phase -> Gain -> Pan -> Mute/Solo.

        Always works on two channels internally (a mono input is duplicated by
        the engine, a mono IR is applied to both channels).

        Impulse responses arrive from other threads through a small wait-free
        mailbox, because juce::dsp::Convolution wants its load calls made in
        step with process(). JUCE then builds the new convolution engine on a
        background thread and crossfades to it, so switching IRs never clicks.

        An empty slot holds a silent one-sample IR and, once the crossfade to it
        has finished, goes idle and costs no CPU. */
    class SlotDsp
    {
    public:
        explicit SlotDsp (juce::dsp::ConvolutionMessageQueue&);

        /** Allocates. Any IR waiting in the mailbox is installed synchronously. */
        void prepare (double sampleRate, int maxBlockSize);

        //==============================================================================
        /** Hands a prepared IR (already at the engine's sample rate, 1 or 2
            channels) to the audio thread. An empty buffer clears the slot.
            Call from any thread except the audio thread. */
        void setIR (juce::AudioBuffer<float>&& impulseResponse);

        //==============================================================================
        /** Audio thread. Convolves `input` (two channels) and adds the result to
            `wetOut` (two channels).

            @param audible       false if the slot is muted or another slot is soloed
            @param stereoOutput  true: apply the pan law. false: pan is ignored
            @param nonRealtime   true while the host renders offline: a new IR is
                                 then installed immediately instead of in the background */
        void process (const float* const* input, float* const* wetOut, int numSamples,
                      const SlotSettings& settings, bool audible, bool stereoOutput, bool nonRealtime) noexcept;

        bool isIdle() const noexcept { return idle; }

        /** Left/right gains for the given settings (also used by export and display). */
        static std::array<float, 2> computeGains (const SlotSettings&, bool audible, bool stereoOutput, bool irIsStereo) noexcept;

    private:
        void collectMailbox (bool nonRealtime) noexcept;
        void goIdle() noexcept;

        juce::dsp::Convolution convolution;
        juce::dsp::ProcessSpec spec { 44100.0, 512, 2 };
        bool prepared = false;

        // Mailbox: written by setIR(), collected by the audio thread.
        juce::SpinLock mailboxLock;
        juce::AudioBuffer<float> mailboxBuffer;
        bool mailboxIsEmptyIR = true;
        std::atomic<bool> mailboxFull { false };

        // State of the most recently collected IR (audio thread).
        bool irIsEmpty = true;
        bool irIsStereo = false;
        bool idle = true;
        bool countingDown = false;
        int idleCountdown = 0;

        FractionalDelay delay;
        FilterBlock filter;
        juce::AudioBuffer<float> scratch;

        std::array<float, 2> gain { 0.0f, 0.0f }, gainTarget { 0.0f, 0.0f }, gainStep { 0.0f, 0.0f };
        int gainRampRemaining = 0, gainRampLength = 1;
        bool snapGain = true;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlotDsp)
    };
}
