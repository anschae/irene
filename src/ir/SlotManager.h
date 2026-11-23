#pragma once

#include <juce_events/juce_events.h>

#include "IRProcessing.h"
#include "app/Broadcaster.h"
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
        int historySize = 0;       // how many earlier IRs "back" can return to

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
    class SlotManager final : public Broadcaster,
                              private juce::Thread
    {
    public:
        SlotManager (Engine& engine, const ParamRefs& params);
        ~SlotManager() override;

        // (juce::Thread has listener functions of its own)
        using Broadcaster::addListener;
        using Broadcaster::removeListener;

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

        /** Empties the slot. The IR that was in it can be brought back with goBack(). */
        void clear (int slot);

        //==============================================================================
        /** Audition history: every IR replaced by loadFile() or clear() is
            remembered (up to kMaxHistory per slot), so that goBack() can return
            to it. Restoring a project or preset starts a fresh history.

            goBack() puts the previous IR back in the background and returns the
            reference it restores (empty if the slot becomes empty, or if there
            was nothing to go back to). */
        static constexpr int kMaxHistory = 25;
        IRReference goBack (int slot);
        bool canGoBack (int slot) const;
        void clearHistory (int slot);

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

        /** Changes every time the IR played by a slot changes (new file, sample
            rate, length, options, cleared). Lock-free. */
        juce::uint32 getBuildGeneration (int slot) const noexcept { return buildGenerations[static_cast<size_t> (slot)].load(); }

        /** The rate the IRs are currently prepared for (0 before the first prepareToPlay). */
        double getEngineSampleRate() const;

        /** Length of the longest IR as played, in seconds. Lock-free. */
        double getLongestIRSeconds() const noexcept { return longestSeconds.load (std::memory_order_relaxed); }

        /** Blocks until all background loads requested so far have finished. */
        void waitForPendingLoads();

        //==============================================================================
        /** Serialises all slots. With embedAudio the original audio is included,
            so the state is self-contained. */
        juce::ValueTree toValueTree (bool embedAudio) const;

        //==============================================================================
        /** Everything a slot holds (its IR, not its parameters), e.g. for A/B. */
        struct SlotSnapshot
        {
            SlotInfo::Status status = SlotInfo::Status::empty;
            IRSource::Ptr source;  // set if status == ready
            IRReference reference; // also set if status == missing
            bool normalise = true, autoTrim = false;
        };

        using Snapshot = std::array<SlotSnapshot, kNumSlots>;

        Snapshot capture() const;

        /** Puts a snapshot back. Returns at once: the IRs are prepared on the
            loader thread and crossfaded in. The audition history is cleared. */
        void restoreAsync (const Snapshot&);

        /** Serialisation of a snapshot (the same format as toValueTree). */
        static juce::ValueTree snapshotToValueTree (const Snapshot&, bool embedAudio);

        /** Reads a snapshot from a tree written by toValueTree/snapshotToValueTree.
            Blocking: IRs without embedded audio are read from disk. Doesn't
            change any slot. */
        Snapshot readSnapshot (const juce::ValueTree&) const;

        /** Restores all slots (blocking). Slots without embedded audio are loaded
            from their path if the file exists, otherwise marked as missing. */
        void fromValueTree (const juce::ValueTree& tree);

        /** Optional: finds a moved or renamed file for a reference whose path no
            longer exists (by content hash, then by name). Returns an invalid File
            if nothing matches. Set once, before the manager is used. */
        std::function<juce::File (const IRReference&)> fileResolver;

        /** Optional: called on the loader thread after a file was loaded into a
            slot by loadFile(). Set once, before the manager is used. */
        std::function<void (int slot, const IRSource&)> onFileLoaded;

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
        void pushHistory (int slot, const juce::File& incoming); // stateLock must not be held

        struct HistoryEntry
        {
            IRSource::Ptr source; // null = the slot was empty
            bool normalise = true, autoTrim = false;
        };

        Engine& engine;
        const ParamRefs& params;

        mutable juce::CriticalSection stateLock; // guards `slots` and the defaults; held briefly
        juce::CriticalSection buildLock;         // serialises rebuilds; may be held for a while
        std::array<Slot, kNumSlots> slots;
        std::array<std::vector<HistoryEntry>, kNumSlots> history; // guarded by stateLock; newest last
        double engineRate = 0.0;
        bool defaultNormalise = true, defaultAutoTrim = false;

        struct Job
        {
            int slot = 0;
            juce::File file;
            juce::uint32 requestId = 0;

            // Going back in the history: install this instead of loading `file`.
            bool restore = false;
            HistoryEntry entry;
        };

        juce::CriticalSection jobLock;
        std::vector<Job> jobs;
        std::array<std::atomic<juce::uint32>, kNumSlots> requestIds {};
        std::array<std::atomic<int>, kNumSlots> pendingLoads {};
        std::array<std::atomic<juce::uint32>, kNumSlots> buildGenerations {};
        std::atomic<int> totalPending { 0 };
        std::atomic<double> longestSeconds { 0.0 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlotManager)
    };
}
