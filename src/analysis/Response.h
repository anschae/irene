#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>

#include <complex>

#include "ir/SlotManager.h"
#include "params/Settings.h"

// Frequency response of the wet path, for the display.
//
// Each slot's IR is turned into a complex spectrum once (when the IR changes).
// Everything that depends on parameters -- filter blocks, delay, phase, gain,
// pan, mute/solo -- is then applied analytically, the slots are summed as
// complex values (so comb filtering between slots shows up) and the input and
// output filter blocks are multiplied in. None of this runs on the audio thread.

namespace irene::analysis
{
    inline constexpr double kMinDisplayFreq = 20.0;
    inline constexpr double kMaxDisplayFreq = 20000.0;
    inline constexpr int kPointsPerOctave = 48;

    /** Log-spaced frequencies from 20 Hz to 20 kHz. */
    std::vector<double> makeDisplayFrequencies();

    /** The complex response of one slot's IR at the display frequencies. */
    struct SlotSpectrum
    {
        std::vector<std::complex<float>> left, right; // equal for a mono IR
        bool stereo = false;

        bool isValid() const noexcept { return ! left.empty(); }
    };

    /** Evaluates the IR's spectrum at the given frequencies. May take a while for
        very long IRs: call from a background thread. */
    SlotSpectrum computeSpectrum (const juce::AudioBuffer<float>& impulseResponse, double sampleRate,
                                  const std::vector<double>& frequencies);

    enum class Channel { mid, left, right };
    enum class Smoothing { none, sixthOctave, thirdOctave };

    struct ResponseInput
    {
        EngineSettings settings;
        double sampleRate = 48000.0;
        bool stereoOutput = true;
        std::array<std::shared_ptr<const SlotSpectrum>, kNumSlots> slots; // null or invalid = empty slot
    };

    struct ResponseCurves
    {
        std::vector<float> combinedDb;                        // all audible slots, through input and output filters
        std::array<std::vector<float>, kNumSlots> slotDb;     // each slot on its own (empty if the slot has no IR)
        bool anything = false;                                // false if no slot has an IR
    };

    /** The magnitude response of the wet path in dB at each display frequency. */
    ResponseCurves computeResponse (const ResponseInput& input, const std::vector<double>& frequencies,
                                    Channel channel, Smoothing smoothing);

    /** Magnitude of a single filter block in dB (for highlighting the selected block). */
    std::vector<float> computeFilterCurve (const FilterBlockSettings& settings, double sampleRate,
                                           const std::vector<double>& frequencies);

    //==============================================================================
    /** Keeps the slot spectra up to date on a background thread. Lives only
        while the editor is open. */
    class SpectrumWorker final : private juce::Thread,
                                 private juce::AsyncUpdater
    {
    public:
        explicit SpectrumWorker (SlotManager& slots);
        ~SpectrumWorker() override;

        std::shared_ptr<const SlotSpectrum> getSpectrum (int slot) const;
        const std::vector<double>& getFrequencies() const noexcept { return frequencies; }

        /** Called on the message thread after one or more spectra changed. */
        std::function<void()> onSpectraChanged;

        /** Blocks until all spectra match the current IRs (for tests). */
        void waitUntilUpToDate();

    private:
        void run() override;
        void handleAsyncUpdate() override;
        bool isUpToDate() const;

        SlotManager& slotManager;
        const std::vector<double> frequencies;

        mutable juce::CriticalSection lock;
        std::array<std::shared_ptr<const SlotSpectrum>, kNumSlots> spectra;
        std::array<std::atomic<juce::uint32>, kNumSlots> seenGenerations;
        std::atomic<bool> everRan { false };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumWorker)
    };
}
