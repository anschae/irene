#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "ir/IRSource.h"
#include "params/Settings.h"

// "Export as single IR": renders the current setup into one impulse response.
//
// The render is offline: a separate, private Engine instance (never the one
// that is playing) is fed with a unit impulse. Because it runs the very same
// DSP code as the plugin, the exported IR reproduces the wet path exactly.

namespace irene
{
    class IReneProcessor;

    struct ExportOptions
    {
        bool includeInputOutputFilters = true; // the input and output filter blocks
        bool includeOutputGain = false;
        bool stereo = true;          // false: mono, as the plugin sounds on a mono track
        double sampleRate = 0.0;     // 0 = session rate
        int bitDepth = 24;           // 24 (integer) or 32 (float)
        bool autoLength = true;      // until the tail has dropped 90 dB below the peak
        double lengthMs = 500.0;     // used if autoLength is false
        bool normalisePeak = false;  // to -0.3 dBFS

        static constexpr double kAutoTailDb = -90.0;
        static constexpr double kNormalisedPeakDb = -0.3;
        static constexpr double kMinLengthMs = 1.0;
        static constexpr double kMaxLengthMs = 12000.0;
    };

    /** Everything the render needs, captured on the message thread. */
    struct ExportSnapshot
    {
        struct Slot
        {
            IRSource::Ptr source; // null = empty slot
            bool normalise = true;
            bool autoTrim = false;
        };

        EngineSettings settings;
        std::array<Slot, kNumSlots> slots;
        double sessionRate = 48000.0;

        static ExportSnapshot capture (IReneProcessor&);
        bool hasAnyIR() const noexcept;
    };

    struct ExportResult
    {
        juce::AudioBuffer<float> audio;
        double sampleRate = 0.0;
        float peakDb = -100.0f; // of the returned audio
        juce::String error;     // empty on success

        bool succeeded() const noexcept { return error.isEmpty(); }
    };

    /** Renders the IR. Blocking and allocation-heavy: call from a background thread.

        @param progress  called now and then with 0..1; return false to cancel */
    ExportResult renderExport (const ExportSnapshot&, const ExportOptions&,
                               const std::function<bool (float)>& progress = nullptr);

    /** Renders and writes a WAV file. Returns an error message, or an empty string. */
    juce::String exportToFile (const ExportSnapshot&, const ExportOptions&, const juce::File& destination,
                               const std::function<bool (float)>& progress = nullptr);

    /** Writes a 24-bit or 32-bit float WAV file (via a temporary file). */
    juce::String writeExportFile (const juce::File& destination, const juce::AudioBuffer<float>& audio,
                                  double sampleRate, int bitDepth);
}
