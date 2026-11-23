#include "SlotManager.h"

#include "IRSerialisation.h"

namespace irene
{
    const juce::Identifier SlotManager::treeType { "Slots" };

    namespace
    {
        const juce::Identifier kSlotType { "Slot" };
        const juce::Identifier kIndex { "index" };
        const juce::Identifier kStatus { "status" };
        const juce::Identifier kPath { "path" };
        const juce::Identifier kName { "name" };
        const juce::Identifier kHash { "hash" };
        const juce::Identifier kNormalise { "normalise" };
        const juce::Identifier kAutoTrim { "autoTrim" };
        const juce::Identifier kSampleRate { "sampleRate" };
        const juce::Identifier kNumChannels { "numChannels" };
        const juce::Identifier kNumSamples { "numSamples" };
        const juce::Identifier kAudio { "audio" };

        juce::String statusToString (SlotInfo::Status s)
        {
            switch (s)
            {
                case SlotInfo::Status::ready:   return "ready";
                case SlotInfo::Status::missing: return "missing";
                case SlotInfo::Status::empty:   break;
            }

            return "empty";
        }
    }

    //==============================================================================
    SlotManager::SlotManager (Engine& e, const ParamRefs& p)
        : juce::Thread ("IRene IR loader"), engine (e), params (p)
    {
        startThread();
    }

    SlotManager::~SlotManager()
    {
        signalThreadShouldExit();
        notify();
        stopThread (10000);
    }

    float SlotManager::currentLengthParam (int slot) const noexcept
    {
        return params.slots[static_cast<size_t> (slot)].length->load (std::memory_order_relaxed);
    }

    //==============================================================================
    void SlotManager::setEngineSampleRate (double sampleRate)
    {
        const juce::ScopedLock build (buildLock);

        {
            const juce::ScopedLock state (stateLock);

            if (std::abs (engineRate - sampleRate) < 1.0e-6)
                return;

            engineRate = sampleRate;
        }

        for (int i = 0; i < kNumSlots; ++i)
            if (getSource (i) != nullptr)
                rebuild (i);
    }

    bool SlotManager::ensureResampled (Slot& s, const IRSource::Ptr& source, double rate, bool autoTrim)
    {
        if (s.resampledSource == source && s.resampledRate == rate && s.resampledAutoTrim == autoTrim)
            return false;

        s.resampled = irproc::resampleSource (*source, rate, autoTrim);
        s.resampledSource = source;
        s.resampledRate = rate;
        s.resampledAutoTrim = autoTrim;
        return true;
    }

    void SlotManager::rebuild (int slot)
    {
        const juce::ScopedLock build (buildLock);
        auto& s = slots[static_cast<size_t> (slot)];

        IRSource::Ptr source;
        irproc::PrepareOptions options;

        {
            const juce::ScopedLock state (stateLock);
            source = s.source;
            options.targetRate = engineRate;
            options.normalise = s.normalise;
            options.autoTrim = s.autoTrim;
        }

        options.lengthPercent = currentLengthParam (slot);

        if (source == nullptr)
        {
            engine.getSlot (slot).setIR ({});

            const juce::ScopedLock state (stateLock);
            s.builtRate = 0.0;
            s.builtLength = -1.0f;
            s.builtNumSamples = 0;
            s.resampled = {};
            s.resampledSource = nullptr;
        }
        else if (options.targetRate > 0.0)
        {
            ensureResampled (s, source, options.targetRate, options.autoTrim);
            auto prepared = irproc::finishPrepared (s.resampled, *source, options);
            const auto numSamples = prepared.getNumSamples();

            engine.getSlot (slot).setIR (std::move (prepared));

            const juce::ScopedLock state (stateLock);
            s.builtRate = options.targetRate;
            s.builtLength = options.lengthPercent;
            s.builtNumSamples = numSamples;
        }

        {
            const juce::ScopedLock state (stateLock);
            auto longest = 0.0;

            for (const auto& other : slots)
                if (other.source != nullptr && other.builtRate > 0.0)
                    longest = juce::jmax (longest, other.builtNumSamples / other.builtRate);

            longestSeconds.store (longest, std::memory_order_relaxed);
        }

        ++buildGenerations[static_cast<size_t> (slot)];

        // If the engine rate isn't known yet, setEngineSampleRate() builds later.
        sendChange();
    }

    juce::AudioBuffer<float> SlotManager::getPreparedIR (int slot)
    {
        const juce::ScopedLock build (buildLock);
        auto& s = slots[static_cast<size_t> (slot)];

        IRSource::Ptr source;
        irproc::PrepareOptions options;

        {
            const juce::ScopedLock state (stateLock);
            source = s.source;
            options.targetRate = engineRate;
            options.normalise = s.normalise;
            options.autoTrim = s.autoTrim;
        }

        if (source == nullptr || options.targetRate <= 0.0)
            return {};

        options.lengthPercent = currentLengthParam (slot);
        ensureResampled (s, source, options.targetRate, options.autoTrim);
        return irproc::finishPrepared (s.resampled, *source, options);
    }

    //==============================================================================
    void SlotManager::installSource (int slot, IRSource::Ptr source, bool normalise, bool autoTrim)
    {
        if (source == nullptr)
        {
            resetSlot (slot, SlotInfo::Status::empty, {});
            return;
        }

        const juce::ScopedLock build (buildLock);

        {
            const juce::ScopedLock state (stateLock);
            auto& s = slots[static_cast<size_t> (slot)];
            s.status = SlotInfo::Status::ready;
            s.source = source;
            s.reference = source->reference;
            s.normalise = normalise;
            s.autoTrim = autoTrim;
            s.error.clear();
        }

        rebuild (slot);
    }

    void SlotManager::resetSlot (int slot, SlotInfo::Status status, const IRReference& reference)
    {
        const juce::ScopedLock build (buildLock);

        {
            const juce::ScopedLock state (stateLock);
            auto& s = slots[static_cast<size_t> (slot)];
            s.status = status;
            s.source = nullptr;
            s.reference = reference;
            s.error.clear();
        }

        rebuild (slot);
    }

    void SlotManager::loadFile (int slot, const juce::File& file)
    {
        const auto index = static_cast<size_t> (slot);
        Job job { slot, file, ++requestIds[index] };

        ++pendingLoads[index];
        ++totalPending;

        {
            const juce::ScopedLock lock (jobLock);
            jobs.push_back (std::move (job));
        }

        notify();
        sendChange();
    }

    void SlotManager::setSource (int slot, IRSource::Ptr source)
    {
        bool normalise, autoTrim;

        {
            const juce::ScopedLock state (stateLock);
            normalise = defaultNormalise;
            autoTrim = defaultAutoTrim;
        }

        setSource (slot, std::move (source), normalise, autoTrim);
    }

    void SlotManager::setSource (int slot, IRSource::Ptr source, bool normalise, bool autoTrim)
    {
        ++requestIds[static_cast<size_t> (slot)]; // supersedes any load still in progress
        installSource (slot, std::move (source), normalise, autoTrim);
    }

    void SlotManager::setMissing (int slot, const IRReference& reference)
    {
        ++requestIds[static_cast<size_t> (slot)];
        resetSlot (slot, SlotInfo::Status::missing, reference);
    }

    void SlotManager::clear (int slot)
    {
        ++requestIds[static_cast<size_t> (slot)];
        pushHistory (slot, {});
        resetSlot (slot, SlotInfo::Status::empty, {});
    }

    //==============================================================================
    void SlotManager::pushHistory (int slot, const juce::File& incoming)
    {
        const juce::ScopedLock state (stateLock);
        const auto& s = slots[static_cast<size_t> (slot)];
        auto& h = history[static_cast<size_t> (slot)];

        // Only real IRs and empty slots are worth returning to ("missing" isn't),
        // and loading the file that is already in the slot changes nothing.
        if (s.status == SlotInfo::Status::missing)
            return;

        if (s.status == SlotInfo::Status::ready && incoming != juce::File() && incoming.getFullPathName() == s.reference.path)
            return;

        if (s.status == SlotInfo::Status::empty && incoming == juce::File())
            return; // clearing an empty slot

        HistoryEntry entry;
        entry.source = s.status == SlotInfo::Status::ready ? s.source : nullptr;
        entry.normalise = s.normalise;
        entry.autoTrim = s.autoTrim;

        // don't stack two identical entries
        if (! h.empty() && h.back().source == entry.source
            && h.back().normalise == entry.normalise && h.back().autoTrim == entry.autoTrim)
            return;

        h.push_back (std::move (entry));

        if (static_cast<int> (h.size()) > kMaxHistory)
            h.erase (h.begin());
    }

    IRReference SlotManager::goBack (int slot)
    {
        const auto index = static_cast<size_t> (slot);
        Job job;

        {
            const juce::ScopedLock state (stateLock);
            auto& h = history[index];

            if (h.empty())
                return {};

            job.entry = std::move (h.back());
            h.pop_back();
        }

        job.slot = slot;
        job.restore = true;
        job.requestId = ++requestIds[index]; // supersedes any load still in progress

        const auto reference = job.entry.source != nullptr ? job.entry.source->reference : IRReference {};

        ++pendingLoads[index];
        ++totalPending;

        {
            const juce::ScopedLock lock (jobLock);
            jobs.push_back (std::move (job));
        }

        notify();
        sendChange();
        return reference;
    }

    bool SlotManager::canGoBack (int slot) const
    {
        const juce::ScopedLock state (stateLock);
        return ! history[static_cast<size_t> (slot)].empty();
    }

    void SlotManager::clearHistory (int slot)
    {
        {
            const juce::ScopedLock state (stateLock);
            history[static_cast<size_t> (slot)].clear();
        }

        sendChange();
    }

    void SlotManager::swap (int slotA, int slotB)
    {
        if (slotA == slotB)
            return;

        const juce::ScopedLock build (buildLock);

        ++requestIds[static_cast<size_t> (slotA)];
        ++requestIds[static_cast<size_t> (slotB)];

        {
            const juce::ScopedLock state (stateLock);
            std::swap (slots[static_cast<size_t> (slotA)], slots[static_cast<size_t> (slotB)]);
            std::swap (history[static_cast<size_t> (slotA)], history[static_cast<size_t> (slotB)]);
        }

        rebuild (slotA);
        rebuild (slotB);
    }

    void SlotManager::setSlotOptions (int slot, bool normalise, bool autoTrim)
    {
        {
            const juce::ScopedLock state (stateLock);
            auto& s = slots[static_cast<size_t> (slot)];

            if (s.normalise == normalise && s.autoTrim == autoTrim)
                return;

            s.normalise = normalise;
            s.autoTrim = autoTrim;
        }

        rebuild (slot);
    }

    void SlotManager::setLoadDefaults (bool normalise, bool autoTrim)
    {
        const juce::ScopedLock state (stateLock);
        defaultNormalise = normalise;
        defaultAutoTrim = autoTrim;
    }

    //==============================================================================
    SlotInfo SlotManager::getInfo (int slot) const
    {
        const auto index = static_cast<size_t> (slot);
        const juce::ScopedLock state (stateLock);
        const auto& s = slots[index];

        SlotInfo info;
        info.status = s.status;
        info.loading = pendingLoads[index].load() > 0;
        info.reference = s.reference;
        info.engineRate = engineRate;
        info.normalise = s.normalise;
        info.autoTrim = s.autoTrim;
        info.error = s.error;
        info.historySize = static_cast<int> (history[index].size());

        if (s.source != nullptr)
        {
            info.sourceRate = s.source->sampleRate;
            info.numChannels = s.source->audio.getNumChannels();
            info.sourceLengthSeconds = s.source->lengthSeconds();
            info.normalisationGainDb = s.source->normalisationGainDb;
            info.notice = s.source->getNotice();

            if (s.builtRate > 0.0)
                info.playedLengthSeconds = s.builtNumSamples / s.builtRate;
        }

        return info;
    }

    double SlotManager::getEngineSampleRate() const
    {
        const juce::ScopedLock state (stateLock);
        return engineRate;
    }

    IRSource::Ptr SlotManager::getSource (int slot) const
    {
        const juce::ScopedLock state (stateLock);
        return slots[static_cast<size_t> (slot)].source;
    }

    void SlotManager::waitForPendingLoads()
    {
        jassert (juce::Thread::getCurrentThreadId() != getThreadId());

        while (totalPending.load() > 0)
            juce::Thread::sleep (2);
    }

    //==============================================================================
    void SlotManager::run()
    {
        while (! threadShouldExit())
        {
            // 1. File loads
            for (;;)
            {
                Job job;

                {
                    const juce::ScopedLock lock (jobLock);

                    if (jobs.empty())
                        break;

                    job = std::move (jobs.front());
                    jobs.erase (jobs.begin());
                }

                const auto index = static_cast<size_t> (job.slot);
                const auto isCurrent = [&] { return job.requestId == requestIds[index].load(); };

                if (job.restore)
                {
                    if (isCurrent() && ! threadShouldExit())
                    {
                        if (job.entry.source != nullptr)
                            installSource (job.slot, job.entry.source, job.entry.normalise, job.entry.autoTrim);
                        else
                            resetSlot (job.slot, SlotInfo::Status::empty, {});
                    }
                }
                else if (isCurrent() && ! threadShouldExit())
                {
                    auto result = loadIRFile (job.file);

                    if (isCurrent())
                    {
                        if (result.source != nullptr)
                        {
                            bool normalise, autoTrim;

                            {
                                const juce::ScopedLock state (stateLock);
                                normalise = defaultNormalise;
                                autoTrim = defaultAutoTrim;
                            }

                            pushHistory (job.slot, job.file);
                            installSource (job.slot, result.source, normalise, autoTrim);

                            if (onFileLoaded != nullptr)
                                onFileLoaded (job.slot, *result.source);
                        }
                        else
                        {
                            const juce::ScopedLock state (stateLock);
                            slots[index].error = result.error;
                        }
                    }
                }

                --pendingLoads[index];
                --totalPending;
                sendChange();
            }

            // 2. Follow the "IR length" parameters
            for (int i = 0; i < kNumSlots && ! threadShouldExit(); ++i)
            {
                bool needsRebuild = false;

                {
                    const juce::ScopedLock state (stateLock);
                    const auto& s = slots[static_cast<size_t> (i)];
                    needsRebuild = s.source != nullptr && s.builtRate > 0.0
                                   && std::abs (s.builtLength - currentLengthParam (i)) > 0.01f;
                }

                if (needsRebuild)
                    rebuild (i);
            }

            wait (25);
        }
    }

    //==============================================================================
    SlotManager::Snapshot SlotManager::capture() const
    {
        Snapshot snapshot;
        const juce::ScopedLock state (stateLock);

        for (size_t i = 0; i < snapshot.size(); ++i)
        {
            const auto& s = slots[i];
            auto& out = snapshot[i];
            out.status = s.status;
            out.source = s.status == SlotInfo::Status::ready ? s.source : nullptr;
            out.reference = s.reference;
            out.normalise = s.normalise;
            out.autoTrim = s.autoTrim;
        }

        return snapshot;
    }

    void SlotManager::restoreAsync (const Snapshot& snapshot)
    {
        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto index = static_cast<size_t> (i);
            const auto& slot = snapshot[index];

            {
                const juce::ScopedLock state (stateLock);
                history[index].clear();
            }

            if (slot.status == SlotInfo::Status::ready && slot.source != nullptr)
            {
                Job job;
                job.slot = i;
                job.restore = true;
                job.entry.source = slot.source;
                job.entry.normalise = slot.normalise;
                job.entry.autoTrim = slot.autoTrim;
                job.requestId = ++requestIds[index];

                ++pendingLoads[index];
                ++totalPending;

                const juce::ScopedLock lock (jobLock);
                jobs.push_back (std::move (job));
            }
            else
            {
                ++requestIds[index];
                resetSlot (i, slot.status == SlotInfo::Status::missing ? SlotInfo::Status::missing : SlotInfo::Status::empty,
                           slot.status == SlotInfo::Status::missing ? slot.reference : IRReference {});
            }
        }

        notify();
        sendChange();
    }

    juce::ValueTree SlotManager::toValueTree (bool embedAudio) const
    {
        return snapshotToValueTree (capture(), embedAudio);
    }

    juce::ValueTree SlotManager::snapshotToValueTree (const Snapshot& snapshotToSave, bool embedAudio)
    {
        juce::ValueTree tree { treeType };

        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto& snapshot = snapshotToSave[static_cast<size_t> (i)];

            juce::ValueTree child { kSlotType };
            child.setProperty (kIndex, i, nullptr);
            child.setProperty (kStatus, statusToString (snapshot.status), nullptr);

            if (snapshot.status != SlotInfo::Status::empty)
            {
                child.setProperty (kPath, snapshot.reference.path, nullptr);
                child.setProperty (kName, snapshot.reference.name, nullptr);
                child.setProperty (kHash, snapshot.reference.hash, nullptr);
                child.setProperty (kNormalise, snapshot.normalise, nullptr);
                child.setProperty (kAutoTrim, snapshot.autoTrim, nullptr);
            }

            if (snapshot.source != nullptr)
            {
                child.setProperty (kSampleRate, snapshot.source->sampleRate, nullptr);
                child.setProperty (kNumChannels, snapshot.source->audio.getNumChannels(), nullptr);
                child.setProperty (kNumSamples, snapshot.source->audio.getNumSamples(), nullptr);

                if (embedAudio)
                    child.setProperty (kAudio, juce::var (encodeAudio (snapshot.source->audio)), nullptr);
            }

            tree.appendChild (child, nullptr);
        }

        return tree;
    }

    SlotManager::Snapshot SlotManager::readSnapshot (const juce::ValueTree& tree) const
    {
        Snapshot result;

        for (int i = 0; i < kNumSlots; ++i)
        {
            auto& out = result[static_cast<size_t> (i)];

            // Slots a project didn't have (e.g. 4 and 5 in a project from 0.2) stay empty.
            const auto child = tree.getChildWithProperty (kIndex, i);

            IRReference ref { child.getProperty (kPath).toString(),
                              child.getProperty (kName).toString(),
                              child.getProperty (kHash).toString() };

            out.normalise = static_cast<bool> (child.getProperty (kNormalise, true));
            out.autoTrim = static_cast<bool> (child.getProperty (kAutoTrim, false));

            if (! child.isValid() || child.getProperty (kStatus).toString() == "empty" || ref.isEmpty())
                continue;

            out.reference = ref;

            // 1. Embedded audio: independent of any file on disk.
            if (const auto* embedded = child.getProperty (kAudio).getBinaryData())
            {
                juce::AudioBuffer<float> audio;

                if (decodeAudio (*embedded,
                                 static_cast<int> (child.getProperty (kNumChannels, 0)),
                                 static_cast<int> (child.getProperty (kNumSamples, 0)),
                                 audio))
                {
                    if (auto source = IRSource::create (audio, static_cast<double> (child.getProperty (kSampleRate, 0.0)), ref))
                    {
                        out.status = SlotInfo::Status::ready;
                        out.source = source;
                        continue;
                    }
                }
            }

            // 2. The file at its last known path, 3. wherever the resolver finds it.
            juce::File file;

            if (juce::File::isAbsolutePath (ref.path) && juce::File (ref.path).existsAsFile())
                file = juce::File (ref.path);
            else if (fileResolver != nullptr)
                file = fileResolver (ref);

            if (file.existsAsFile())
            {
                if (auto loaded = loadIRFile (file); loaded.source != nullptr)
                {
                    out.status = SlotInfo::Status::ready;
                    out.source = loaded.source;
                    out.reference = loaded.source->reference;
                    continue;
                }
            }

            out.status = SlotInfo::Status::missing;
        }

        return result;
    }

    void SlotManager::fromValueTree (const juce::ValueTree& tree)
    {
        const auto snapshot = readSnapshot (tree);

        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto index = static_cast<size_t> (i);
            const auto& slot = snapshot[index];

            {
                const juce::ScopedLock state (stateLock);
                history[index].clear();
            }

            if (slot.status == SlotInfo::Status::ready)
            {
                setSource (i, slot.source, slot.normalise, slot.autoTrim);
            }
            else if (slot.status == SlotInfo::Status::missing)
            {
                setMissing (i, slot.reference);
            }
            else
            {
                ++requestIds[index];
                resetSlot (i, SlotInfo::Status::empty, {}); // not clear(): no history entry
            }
        }
    }
}
