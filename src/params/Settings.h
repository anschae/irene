#pragma once

#include <array>

#include "Config.h"

// Plain-value snapshots of the plugin's parameters.
//
// The audio engine, the offline exporter and the frequency-response display all
// work from these structs, so they are guaranteed to agree with each other.

namespace irene
{
    inline constexpr int kNumBands = 3; // Low, Mid, High

    inline constexpr float kMinFreq = 20.0f;
    inline constexpr float kMaxFreq = 20000.0f;
    inline constexpr float kMinQ = 0.3f;
    inline constexpr float kMaxQ = 10.0f;
    inline constexpr float kDefaultQ = 0.71f;
    inline constexpr float kMaxBandGainDb = 18.0f;

    inline constexpr float kSlotGainOffDb = -60.0f; // at or below this the slot gain is "off"
    inline constexpr float kSlotGainMaxDb = 12.0f;
    inline constexpr float kMaxDelayMs = 10.0f;
    inline constexpr float kMinLengthPercent = 1.0f;
    inline constexpr float kMaxOutGainDb = 24.0f;

    inline constexpr std::array<float, kNumBands> kDefaultBandFreqs { 100.0f, 1000.0f, 5000.0f };

    struct BandSettings
    {
        bool on = false;
        float freq = 1000.0f;
        float gainDb = 0.0f;
        float q = kDefaultQ;
        bool shelf = false; // only used by the Low and High bands

        bool operator== (const BandSettings&) const = default;
    };

    struct FilterBlockSettings
    {
        bool bypass = false;

        bool hpOn = false;
        float hpFreq = kMinFreq;
        bool hpSteep = false; // false = 12 dB/oct, true = 24 dB/oct

        std::array<BandSettings, kNumBands> bands {
            BandSettings { false, kDefaultBandFreqs[0], 0.0f, kDefaultQ, false },
            BandSettings { false, kDefaultBandFreqs[1], 0.0f, kDefaultQ, false },
            BandSettings { false, kDefaultBandFreqs[2], 0.0f, kDefaultQ, false }
        };

        bool lpOn = false;
        float lpFreq = kMaxFreq;
        bool lpSteep = false;

        bool operator== (const FilterBlockSettings&) const = default;
    };

    struct SlotSettings
    {
        float gainDb = 0.0f;
        float pan = 0.0f; // -1 (left) .. +1 (right)
        bool phaseInvert = false;
        float delayMs = 0.0f;
        bool mute = false;
        bool solo = false;
        float lengthPercent = 100.0f;
        FilterBlockSettings filter;

        bool operator== (const SlotSettings&) const = default;
    };

    struct EngineSettings
    {
        FilterBlockSettings inputFilter;
        std::array<SlotSettings, kNumSlots> slots;
        float mix = 1.0f; // 0 = dry, 1 = wet
        FilterBlockSettings outputFilter;
        float outGainDb = 0.0f;

        bool operator== (const EngineSettings&) const = default;

        bool anySolo() const noexcept
        {
            for (const auto& s : slots)
                if (s.solo)
                    return true;

            return false;
        }

        /** True if the slot is heard, taking mute and solo into account. */
        bool isSlotAudible (int index) const noexcept
        {
            const auto& s = slots[static_cast<size_t> (index)];
            return ! s.mute && (! anySolo() || s.solo);
        }
    };
}
