#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Config.h"

// Automatic time-alignment of the slots.
//
// Microphones at different distances from the speaker produce IRs whose direct
// sound arrives at slightly different times; blended, they cancel each other in
// places (comb filtering). Alignment finds those time differences by
// cross-correlation and delays the earlier slots until all arrive together.
//
// Only the maths lives here. It works on the IRs as the slots play them and
// allocates freely: never call it from the audio thread.

namespace irene::align
{
    /** How one IR sits relative to another. */
    struct PairResult
    {
        bool valid = false;
        double lagSamples = 0.0; // > 0: `other` arrives later than `reference`
        bool inverted = false;   // `other` fits better with its polarity flipped
        float match = 0.0f;      // 0..1: how alike the two are at that lag (1 = identical shape)
    };

    /** Finds the time offset between two IRs (same sample rate) by
        cross-correlation, to a fraction of a sample. Stereo IRs are summed to
        mono for the comparison.

        A flipped polarity is only reported when it fits clearly better than
        the best non-flipped position. */
    PairResult correlate (const juce::AudioBuffer<float>& reference, const juce::AudioBuffer<float>& other, double sampleRate);

    struct SlotAlignment
    {
        bool hasIR = false;
        bool isReference = false;
        bool aligned = false;     // false: too different from the reference; this slot is left as it is
        double lagMs = 0.0;       // arrival time relative to the reference, before alignment
        double delayMs = 0.0;     // the delay to set on this slot
        bool phaseInvert = false; // the polarity to set on this slot
        bool polarityChanged = false;
        float match = 1.0f;
    };

    struct Result
    {
        bool ok = false;
        juce::String message; // what was done, or why nothing was
        std::array<SlotAlignment, kNumSlots> slots;
    };

    /** Below this match a time offset is not trusted. Two microphones on the same
        speaker are typically far above it; unrelated IRs are far below. */
    inline constexpr float kMinMatch = 0.2f;

    struct SlotInput
    {
        juce::AudioBuffer<float> ir; // as played (empty = no IR)
        bool phaseInvert = false;    // the slot's current polarity setting
        juce::String name;           // for the message
    };

    /** Works out delays (and polarities) that line all loaded slots up with the
        reference slot.

        The slot whose sound arrives last gets no delay; every other slot is
        delayed to meet it. The reference slot keeps its polarity, and the
        others are flipped where that matches the reference better.

        A slot whose IR has too little in common with the reference (a match
        below kMinMatch, e.g. a completely different cabinet) is left untouched:
        any offset found for it would be a guess.

        Fails (ok = false, nothing to apply) if fewer than two slots have an IR,
        the reference slot is empty, no slot resembles the reference, or the
        slots are further apart than the delay control can cover. */
    Result computeAlignment (const std::array<SlotInput, kNumSlots>& slots, double sampleRate, int referenceSlot);
}
