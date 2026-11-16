#pragma once

#include <juce_events/juce_events.h>

#include "IRProcessing.h"
#include "IRSource.h"
#include "dsp/Engine.h"
#include "params/Parameters.h"

namespace irene
{
    /** What the UI needs to know about a slot. A plain snapshot; cheap to copy. */
    struct SlotInfo
    {
        enum class Status { empty, ready, missing };

        Status status = Status::empty;
        bool loading = false;      // a file is being loaded in the background
        IRReference reference;     // path / name / hash (also set when missing)
        double sourceRate = 0.0;   // sample rate of the file
        double engineRate = 0.0;   // sample rate it is played at (0 = not prepared yet)
        int numChannels = 0;
        double sourceLengthSeconds = 0.0;
        double playedLengthSeconds = 0.0; // after the length trim
        float normalisationGainDb = 0.0f; // applied only if `normalise`
        bool normalise = true;
        bool autoTrim = false;
        juce::String notice;       // e.g. "4 channels: using the first two"
        juce::String error;        // last load error, if any

        bool isResampled() const noexcept { return status == Status::ready && engineRate > 0.0 && std::abs (sourceRate - engineRate) > 0.5; }
    };

    /** Owns the IR of every slot (outside the audio thread) and keeps the audio
        engine supplied with correctly prepared versions.

        - Files are loaded, resampled, trimmed and normalised on a background
          thread; the result is handed to the engine, which crossfades to it.
        - The original audio is always kept, and every prepared IR is derived
          from that original (never from a resampled copy).
        - Watches the per-slot "IR length" parameter and rebuilds on change.
        - Saves and restores the slots, optionally with the audio embedded.

        Sends a change message (on the message thread) whenever a slot's info
        changes. */
    class SlotManager final : public juce::ChangeBroadcaster,
                              private juce::Thread
    {
    public:
        SlotManager (Engine& engine, const ParamRefs& params);
        ~SlotManager() override;

        //==============================================================================
        /** Tells the manager which rate the engine runs at. If it changed, all
            slots are rebuilt from their originals before this returns. Call from
            prepareToPlay, before Engine::prepare. */
        void setEngineSampleRate (double sampleRate);

        //==============================================================================
        /** Loads a file in the background. Returns immediately. A newer call for
            the same slot supersedes an older one that hasn't finished. */
        void loadFile (int slot, const juce::File& file);

        /** Installs a source right away (blocking). Uses the current load defaults. */
        void setSource (int slot, IRSource::Ptr source);
        void setSource (int slot, IRSource::Ptr source, bool normalise, bool autoTrim);

        /** Marks the slot as referring to an IR that could not be found. */
        void setMissing (int slot, const IRReference& reference);

        void clear (int slot);

        /** Exchanges the IRs (and their per-slot options) of two slots. */
        void swap (int slotA, int slotB);

        void setSlotOptions (int slot, bool normalise, bool autoTrim);

        /** Options given to newly loaded IRs (from the global settings). */
        void setLoadDefaults (bool normalise, bool autoTrim);

        //==============================================================================
        SlotInfo getInfo (int slot) const;
        IRSource::Ptr getSource (int slot) const;

        /** The IR exactly as the engine plays it (resampled, trimmed, normalised).
            Empty if the slot is empty or the engine rate isn't known yet. */
        juce::AudioBuffer<float> getPreparedIR (int slot);

        /** Length of the longest IR as played, in seconds. Lock-free. */
        double getLongestIRSeconds() const noexcept { return longestSeconds.load (std::memory_order_relaxed); }

        /** Blocks until all background loads requested so far have finished. */
        void waitForPendingLoads();

        //==============================================================================
        /** Serialises all slots. With embedAudio the original audio is included,
            so the state is self-contained. */
        juce::ValueTree toValueTree (bool embedAudio) const;

        /** Restores all slots (blocking). Slots without embedded audio are loaded
            from their path if the file exists, otherwise marked as missing. */
        void fromValueTree (const juce::ValueTree& tree);

        /** Optional: finds a moved or renamed file for a reference whose path no
            longer exists (by content hash, then by name). Returns an invalid File
            if nothing matches. Set once, before the manager is used. */
        std::function<juce::File (const IRReference&)> fileResolver;

        static const juce::Identifier treeType;

    private:
        struct Slot
        {
            SlotInfo::Status status = SlotInfo::Status::empty;
            IRSource::Ptr source;
            IRReference reference;
            bool normalise = true, autoTrim = false;
            juce::String error;

            // What is currently in the engine
            double builtRate = 0.0;
            float builtLength = -1.0f;
            int builtNumSamples = 0;

            // Cache of the expensive resampling step (guarded by buildLock)
            juce::AudioBuffer<float> resampled;
            IRSource::Ptr resampledSource;
            double resampledRate = 0.0;
            bool resampledAutoTrim = false;
        };

        void run() override;
        void rebuild (int slot);
        void installSource (int slot, IRSource::Ptr source, bool normalise, bool autoTrim);
        void resetSlot (int slot, SlotInfo::Status status, const IRReference& reference);
        bool ensureResampled (Slot&, const IRSource::Ptr& source, double rate, bool autoTrim);
        float currentLengthParam (int slot) const noexcept;

        Engine& engine;
        const ParamRefs& params;

        mutable juce::CriticalSection stateLock; // guards `slots` and the defaults; held briefly
        juce::CriticalSection buildLock;         // serialises rebuilds; may be held for a while
        std::array<Slot, kNumSlots> slots;
        double engineRate = 0.0;
        bool defaultNormalise = true, defaultAutoTrim = false;

        struct Job
        {
            int slot = 0;
            juce::File file;
            juce::uint32 requestId = 0;
        };

        juce::CriticalSection jobLock;
        std::vector<Job> jobs;
        std::array<std::atomic<juce::uint32>, kNumSlots> requestIds {};
        std::array<std::atomic<int>, kNumSlots> pendingLoads {};
        std::atomic<int> totalPending { 0 };
        std::atomic<double> longestSeconds { 0.0 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlotManager)
    };
}
