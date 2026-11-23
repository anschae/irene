#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>

#include <complex>

#include "app/Broadcaster.h"
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
        std::vector<float> combinedDb;                        // all audible slots (with auto gain), through input and output filters
        std::array<std::vector<float>, kNumSlots> slotDb;     // each slot on its own (empty if the slot has no IR)
        bool anything = false;                                // false if no slot has an IR
    };

    /** The magnitude response of the wet path in dB at each display frequency. */
    ResponseCurves computeResponse (const ResponseInput& input, const std::vector<double>& frequencies,
                                    Channel channel, Smoothing smoothing);

    //==============================================================================
    /** Loudness figures for auto gain and A/B loudness matching.

        "Loudness" here is the same measure that IR normalisation uses: the
        average power across 80 Hz - 8 kHz with equal weight per octave (i.e.
        what pink noise in that band would measure), averaged over left and
        right on a stereo output. */
    struct Levels
    {
        int numAudible = 0;              // audible slots with an IR
        float blendDb = -200.0f;         // all audible slots summed (no input/output filters, no auto gain)
        float loudestSlotDb = -200.0f;   // the loudest audible slot on its own
        float wetDb = -200.0f;           // the whole wet path: blend, auto gain, input/output filters, output gain

        /** The auto-gain correction: removes the extra loudness that comes from
            adding slots together. Never boosts, at most -24 dB. */
        float autoGainDb() const noexcept;
    };

    inline constexpr double kLoudnessLowHz = 80.0;
    inline constexpr double kLoudnessHighHz = 8000.0;
    inline constexpr float kMaxAutoGainCutDb = 24.0f;

    /** Uses input.settings (autoGainDb is ignored; the correction is worked
        out here and included in wetDb when settings.autoGainOn is set). */
    Levels computeLevels (const ResponseInput& input, const std::vector<double>& frequencies);

    /** Magnitude of a single filter block in dB (for highlighting the selected block). */
    std::vector<float> computeFilterCurve (const FilterBlockSettings& settings, double sampleRate,
                                           const std::vector<double>& frequencies);

    //==============================================================================
    /** Keeps the slot spectra up to date on a background thread. Owned by the
        processor (auto gain needs it even while the editor is closed).
        Sends a change message (message thread) after spectra changed. */
    class SpectrumWorker final : public Broadcaster,
                                 private juce::Thread
    {
    public:
        explicit SpectrumWorker (SlotManager& slots);
        ~SpectrumWorker() override;

        // (juce::Thread has listener functions of its own)
        using Broadcaster::addListener;
        using Broadcaster::removeListener;

        std::shared_ptr<const SlotSpectrum> getSpectrum (int slot) const;
        const std::vector<double>& getFrequencies() const noexcept { return frequencies; }

        /** Blocks until all spectra match the current IRs, or the timeout
            (milliseconds, -1 = none) has passed. Returns true if up to date. */
        bool waitUntilUpToDate (int timeoutMs = -1);

    private:
        void run() override;
        bool isUpToDate() const;

        SlotManager& slotManager;
        const std::vector<double> frequencies;

        mutable juce::CriticalSection lock;
        std::array<std::shared_ptr<const SlotSpectrum>, kNumSlots> spectra;
        std::array<std::atomic<juce::uint32>, kNumSlots> seenGenerations;
        std::atomic<bool> everRan { false };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumWorker)
    };

    //==============================================================================
    /** Works out the auto-gain correction and the loudness of the wet path
        (for A/B matching) on a background thread, whenever the settings or the
        IRs change. Not real-time critical: the engine smooths the result. */
    class LevelWorker final : private juce::Thread
    {
    public:
        struct Context
        {
            /** The settings to measure (parameters + Solo target, without the derived gains). */
            std::function<EngineSettings()> readSettings;
            std::function<double()> sampleRate;  // 0 = not prepared yet
            std::function<bool()> stereoOutput;
        };

        LevelWorker (SpectrumWorker&, Context);
        ~LevelWorker() override;

        /** The current auto-gain correction (0 if Auto gain is off). Any thread. */
        float getAutoGainDb() const noexcept { return autoGainDb.load (std::memory_order_relaxed); }

        /** Loudness of the whole wet path as it is heard (includes auto gain).
            Returns false if nothing is loaded. Any thread. */
        bool getWetLoudnessDb (float& result) const noexcept;

        /** Measures now, on the calling thread (not the audio thread). */
        void update();

    private:
        void run() override;

        SpectrumWorker& spectra;
        const Context context;

        juce::CriticalSection updateLock;
        bool haveLast = false;
        EngineSettings lastSettings;
        double lastRate = 0.0;
        bool lastStereo = true;
        std::array<std::shared_ptr<const SlotSpectrum>, kNumSlots> lastSpectra;

        std::atomic<float> autoGainDb { 0.0f }, wetDb { -200.0f };
        std::atomic<bool> wetValid { false };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelWorker)
    };
}
