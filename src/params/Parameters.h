#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Settings.h"

// Parameter IDs, layout and fast access.
//
// !! Parameter IDs must never be renamed: DAW projects and automation depend on
// !! them. New parameters must be added with a higher version hint.
//
// ID scheme (N = 1..kNumSlots):
//   Global          mix, out_gain, autogain
//   Slot            slotN_gain, slotN_pan, slotN_phase, slotN_delay,
//                   slotN_mute, slotN_solo, slotN_length
//   Filter blocks   <p>_filter_bypass,
//                   <p>_hp_on, <p>_hp_freq, <p>_hp_slope,
//                   <p>_<band>_on, <p>_<band>_freq, <p>_<band>_gain, <p>_<band>_q   (band = low | mid | high)
//                   <p>_low_type, <p>_high_type
//                   <p>_lp_on, <p>_lp_freq, <p>_lp_slope
//                   with prefix <p> = in | slotN | out

namespace irene
{
    /** Version hints: 1 for everything in 0.1/0.2 (mix, out_gain, in/out
        filters, slots 1-3); 2 for what 0.3 added (slots 4 and 5, autogain).
        A parameter's hint never changes once released. */
    inline constexpr int kParamVersionHint = 1;
    int versionHintFor (const juce::String& parameterId);

    namespace ids
    {
        juce::String slotPrefix (int slotIndex);          // "slot1", "slot2", ...
        inline const juce::String inputPrefix { "in" };
        inline const juce::String outputPrefix { "out" };

        inline const juce::String mix { "mix" };
        inline const juce::String outGain { "out_gain" };
        inline const juce::String autoGain { "autogain" }; // added in 0.3 (version hint 2)

        inline const std::array<const char*, kNumBands> bandNames { "low", "mid", "high" };

        // Slot parameter suffixes (append to slotPrefix + "_").
        inline const char* const gain = "gain";
        inline const char* const pan = "pan";
        inline const char* const phase = "phase";
        inline const char* const delay = "delay";
        inline const char* const mute = "mute";
        inline const char* const solo = "solo";
        inline const char* const length = "length";

        juce::String slotParam (int slotIndex, const char* suffix); // e.g. "slot1_gain"

        /** All parameter IDs that belong to a slot, including its filter block.
            Returned as suffixes after "slotN_", so two slots can be matched up. */
        juce::StringArray slotParamSuffixes();

        /** All filter block parameter suffixes (after "<prefix>_"). */
        juce::StringArray filterBlockParamSuffixes();
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    //==============================================================================
    /** Cached pointers to the raw parameter values, for lock-free reads. */
    struct FilterBlockParamRefs
    {
        struct Band
        {
            std::atomic<float>* on = nullptr;
            std::atomic<float>* freq = nullptr;
            std::atomic<float>* gain = nullptr;
            std::atomic<float>* q = nullptr;
            std::atomic<float>* type = nullptr; // null for the Mid band
        };

        std::atomic<float>* bypass = nullptr;
        std::atomic<float>* hpOn = nullptr;
        std::atomic<float>* hpFreq = nullptr;
        std::atomic<float>* hpSlope = nullptr;
        std::array<Band, kNumBands> bands;
        std::atomic<float>* lpOn = nullptr;
        std::atomic<float>* lpFreq = nullptr;
        std::atomic<float>* lpSlope = nullptr;

        void attach (juce::AudioProcessorValueTreeState&, const juce::String& prefix);
        FilterBlockSettings read() const noexcept;
    };

    struct SlotParamRefs
    {
        std::atomic<float>* gain = nullptr;
        std::atomic<float>* pan = nullptr;
        std::atomic<float>* phase = nullptr;
        std::atomic<float>* delay = nullptr;
        std::atomic<float>* mute = nullptr;
        std::atomic<float>* solo = nullptr;
        std::atomic<float>* length = nullptr;
        FilterBlockParamRefs filter;

        void attach (juce::AudioProcessorValueTreeState&, int slotIndex);
        SlotSettings read() const noexcept;
    };

    struct ParamRefs
    {
        FilterBlockParamRefs inputFilter, outputFilter;
        std::array<SlotParamRefs, kNumSlots> slots;
        std::atomic<float>* mix = nullptr;
        std::atomic<float>* outGain = nullptr;
        std::atomic<float>* autoGain = nullptr;

        void attach (juce::AudioProcessorValueTreeState&);

        /** Reads all parameters. Lock-free; safe on the audio thread. */
        EngineSettings read() const noexcept;
    };
}
