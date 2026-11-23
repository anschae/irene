#include "PluginProcessor.h"

#include "PluginEditor.h"

namespace irene
{
    const juce::Identifier IReneProcessor::stateType { "IReneState" };

    namespace
    {
        const juce::Identifier kStateVersionId { "stateVersion" };
        const juce::Identifier kPluginVersionId { "pluginVersion" };
        const juce::Identifier kParametersType { "Parameters" };
        const juce::Identifier kUiType { "UI" };
        const juce::Identifier kFilterTab { "filterTab" };
        const juce::Identifier kTargetSlot { "targetSlot" };
        const juce::Identifier kPresetName { "presetName" };
        const juce::Identifier kShowSlotCurves { "showSlotCurves" };
        const juce::Identifier kResponseChannel { "responseChannel" };
        const juce::Identifier kResponseSmoothing { "responseSmoothing" };
        const juce::Identifier kNotes { "notes" };
        const juce::Identifier kVisibleSlots { "visibleSlots" };
        const juce::Identifier kABType { "AB" };
        const juce::Identifier kSetupType { "Setup" };
        const juce::Identifier kActiveSetup { "active" };
        const juce::Identifier kLoudnessMatch { "loudnessMatch" };
        const juce::Identifier kReferenceLoudness { "referenceLoudness" };

        constexpr float kMaxMatchGainDb = 24.0f;

        /** Index of the Output filter tab in projects saved before 0.3 (three slots). */
        constexpr int kOldOutputTab = 4;

        ParamRefs makeParamRefs (juce::AudioProcessorValueTreeState& apvts)
        {
            ParamRefs refs;
            refs.attach (apvts);
            return refs;
        }
    }

    IReneProcessor::IReneProcessor()
        : AudioProcessor (BusesProperties()
                              .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, kParametersType, createParameterLayout()),
          paramRefs (makeParamRefs (apvts)),
          slotManager (engine, paramRefs),
          levelWorker (spectrumWorker,
                       { [this] { return readSettings (false); },
                         [this] { return slotManager.getEngineSampleRate(); },
                         [this] { return getTotalNumOutputChannels() > 1; } })
    {
        // Moved or renamed files are found again through the library's index.
        // (The library is shared and outlives the slot manager's thread.)
        slotManager.fileResolver = [lib = library] (const IRReference& ref) { return lib->resolve (ref); };
        slotManager.onFileLoaded = [lib = library] (int, const IRSource& source) { lib->addRecent (IRFileEntry::fromSource (source)); };

        applyGlobalSettings();
        globalSettings->addListener (this);
    }

    IReneProcessor::~IReneProcessor()
    {
        masterReference.clear();
        globalSettings->removeListener (this);
    }

    void IReneProcessor::broadcasterChanged (Broadcaster*)
    {
        applyGlobalSettings();
    }

    void IReneProcessor::applyGlobalSettings()
    {
        const auto d = globalSettings->get();
        slotManager.setLoadDefaults (d.defaultNormalise, d.defaultAutoTrim);
    }

    //==============================================================================
    void IReneProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        // Rebuild the IRs for the new rate first (from their originals), so that
        // the engine can install them synchronously while it prepares.
        slotManager.setEngineSampleRate (sampleRate);

        engine.prepare (sampleRate, samplesPerBlock, getTotalNumInputChannels(), getTotalNumOutputChannels());
        setLatencySamples (engine.getLatencySamples());
    }

    void IReneProcessor::releaseResources() {}

    bool IReneProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto in = layouts.getMainInputChannelSet();
        const auto out = layouts.getMainOutputChannelSet();
        const auto mono = juce::AudioChannelSet::mono();
        const auto stereo = juce::AudioChannelSet::stereo();

        return (in == mono && out == mono)
            || (in == mono && out == stereo)
            || (in == stereo && out == stereo);
    }

    double IReneProcessor::getTailLengthSeconds() const
    {
        return slotManager.getLongestIRSeconds() + kMaxDelayMs / 1000.0;
    }

    void IReneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        juce::ScopedNoDenormals noDenormals;

        if (! engine.isPrepared())
        {
            buffer.clear();
            return;
        }

        if (isNonRealtime())
        {
            // Offline rendering runs much faster than real time, so the level
            // worker's few milliseconds of delay could mean seconds of audio with
            // a stale auto gain (e.g. with automated mute or solo). Work it out
            // here instead. Not real-time code: the host isn't waiting for us.
            spectrumWorker.waitUntilUpToDate (2000);
            levelWorker.update();
        }

        engine.process (buffer, readSettings(), isNonRealtime());
    }

    EngineSettings IReneProcessor::readSettings (bool withDerivedGains) const noexcept
    {
        auto settings = paramRefs.read();

        if (auditionSolo.load (std::memory_order_relaxed))
            settings.auditionSlot = juce::jlimit (0, kNumSlots - 1, uiState.targetSlot.load (std::memory_order_relaxed));

        if (withDerivedGains)
        {
            settings.autoGainDb = settings.autoGainOn ? levelWorker.getAutoGainDb() : 0.0f;
            settings.matchGainDb = getMatchGainDb();
        }

        return settings;
    }

    void IReneProcessor::updateLevelsNow()
    {
        spectrumWorker.waitUntilUpToDate (5000);
        levelWorker.update();
    }

    //==============================================================================
    int IReneProcessor::highestUsedSlot() const
    {
        for (int i = kNumSlots; --i >= 0;)
            if (const auto info = slotManager.getInfo (i); info.status != SlotInfo::Status::empty || info.loading)
                return i;

        return -1;
    }

    int IReneProcessor::getVisibleSlots() const
    {
        return juce::jlimit (kDefaultVisibleSlots, kNumSlots, juce::jmax (visibleSlots.load(), highestUsedSlot() + 1));
    }

    bool IReneProcessor::addVisibleSlot()
    {
        const auto visible = getVisibleSlots();

        if (visible >= kNumSlots)
            return false;

        visibleSlots = visible + 1;
        uiChanges.sendChange();
        return true;
    }

    bool IReneProcessor::canHideLastSlot() const
    {
        const auto visible = getVisibleSlots();

        if (visible <= kDefaultVisibleSlots)
            return false;

        const auto info = slotManager.getInfo (visible - 1);
        return info.status == SlotInfo::Status::empty && ! info.loading;
    }

    bool IReneProcessor::hideLastSlot()
    {
        if (! canHideLastSlot())
            return false;

        const auto slot = getVisibleSlots() - 1;

        // Back to defaults, so that a hidden slot can't affect anything (a
        // forgotten Solo, for example) and comes back fresh.
        for (const auto& suffix : ids::slotParamSuffixes())
        {
            if (auto* param = apvts.getParameter (ids::slotPrefix (slot) + "_" + suffix))
            {
                if (! juce::exactlyEqual (param->getValue(), param->getDefaultValue()))
                {
                    param->beginChangeGesture();
                    param->setValueNotifyingHost (param->getDefaultValue());
                    param->endChangeGesture();
                }
            }
        }

        slotManager.clearHistory (slot);
        slotListContexts[static_cast<size_t> (slot)].clear();
        visibleSlots = slot;

        if (uiState.targetSlot.load() >= slot)
            uiState.targetSlot = slot - 1;

        if (uiState.filterTab.load() == slot + 1) // that slot's filter tab
            uiState.filterTab = 0;

        uiChanges.sendChange();
        return true;
    }

    //==============================================================================
    float IReneProcessor::getMatchGainDb() const noexcept
    {
        if (! loudnessMatch.load (std::memory_order_relaxed) || activeSetup.load (std::memory_order_relaxed) != 1
            || ! referenceLoudnessValid.load (std::memory_order_relaxed))
            return 0.0f;

        float live = 0.0f;

        if (! levelWorker.getWetLoudnessDb (live))
            return 0.0f;

        return juce::jlimit (-kMaxMatchGainDb, kMaxMatchGainDb, referenceLoudnessDb.load (std::memory_order_relaxed) - live);
    }

    float IReneProcessor::liveWetLoudness (bool& valid) const
    {
        float live = 0.0f;
        valid = levelWorker.getWetLoudnessDb (live);
        return live;
    }

    IReneProcessor::Setup IReneProcessor::captureSetup()
    {
        Setup setup;
        setup.parameters = apvts.copyState();
        setup.slots = slotManager.capture();
        setup.visibleSlots = getVisibleSlots();
        return setup;
    }

    void IReneProcessor::applySetup (const Setup& setup)
    {
        apvts.replaceState (setup.parameters.createCopy());
        slotManager.restoreAsync (setup.slots);
        visibleSlots = juce::jlimit (kDefaultVisibleSlots, kNumSlots, setup.visibleSlots);

        for (auto& context : slotListContexts)
            context.clear();
    }

    bool IReneProcessor::hasOtherSetup() const
    {
        const juce::ScopedLock sl (setupLock);
        return otherSetup.has_value();
    }

    void IReneProcessor::switchToSetup (int setup)
    {
        setup = juce::jlimit (0, 1, setup);

        if (setup == activeSetup.load())
            return;

        // Remember A's loudness: it is the reference B is matched to.
        if (activeSetup.load() == 0)
        {
            levelWorker.update();
            bool valid = false;
            const auto live = liveWetLoudness (valid);
            referenceLoudnessDb = live;
            referenceLoudnessValid = valid;
        }

        auto outgoing = captureSetup();

        {
            const juce::ScopedLock sl (setupLock);

            if (otherSetup.has_value())
            {
                auto incoming = std::move (*otherSetup);
                otherSetup = std::move (outgoing);
                applySetup (incoming);
            }
            else
            {
                // First switch: the other setup starts as a copy of this one.
                otherSetup = std::move (outgoing);
            }
        }

        activeSetup = setup;
        uiChanges.sendChange();
    }

    void IReneProcessor::copyActiveSetupToOther()
    {
        {
            const juce::ScopedLock sl (setupLock);
            otherSetup = captureSetup();
        }

        uiChanges.sendChange();
    }

    void IReneProcessor::setLoudnessMatch (bool shouldMatch)
    {
        loudnessMatch = shouldMatch;
        uiChanges.sendChange();
    }

    juce::ValueTree IReneProcessor::setupToValueTree (const Setup& setup, bool embedAudio) const
    {
        juce::ValueTree tree { kSetupType };
        tree.setProperty (kVisibleSlots, setup.visibleSlots, nullptr);
        tree.appendChild (setup.parameters.createCopy(), nullptr);
        tree.appendChild (SlotManager::snapshotToValueTree (setup.slots, embedAudio), nullptr);
        return tree;
    }

    IReneProcessor::Setup IReneProcessor::setupFromValueTree (const juce::ValueTree& tree) const
    {
        Setup setup;
        setup.parameters = tree.getChildWithName (kParametersType).createCopy();
        setup.slots = slotManager.readSnapshot (tree.getChildWithName (SlotManager::treeType));
        setup.visibleSlots = juce::jlimit (kDefaultVisibleSlots, kNumSlots, static_cast<int> (tree.getProperty (kVisibleSlots, kDefaultVisibleSlots)));
        return setup;
    }

    void IReneProcessor::migrateOldState (const juce::ValueTree& parameters)
    {
        // Saved before 0.3: no Auto gain parameter yet. Switch it off, so the
        // project or preset sounds exactly as it did.
        for (const auto& child : parameters)
            if (child.getProperty ("id").toString() == ids::autoGain)
                return;

        if (auto* param = apvts.getParameter (ids::autoGain))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (0.0f);
            param->endChangeGesture();
        }
    }

    juce::String IReneProcessor::getNotes() const
    {
        const juce::ScopedLock sl (notesLock);
        return notes;
    }

    void IReneProcessor::setNotes (const juce::String& newNotes)
    {
        const juce::ScopedLock sl (notesLock);
        notes = newNotes;
    }

    void IReneProcessor::processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&)
    {
        jassertfalse; // double precision is not supported
    }

    void IReneProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        // Latency is zero, so bypass is a plain pass-through. On mono -> stereo
        // the input is copied to both outputs.
        const auto numIn = getTotalNumInputChannels();
        const auto numOut = getTotalNumOutputChannels();

        if (numIn == 1 && numOut == 2 && buffer.getNumChannels() >= 2)
            buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());
    }

    juce::AudioProcessorEditor* IReneProcessor::createEditor()
    {
        return new IReneEditor (*this);
    }

    //==============================================================================
    void IReneProcessor::swapSlots (int slotA, int slotB)
    {
        if (slotA == slotB)
            return;

        for (const auto& suffix : ids::slotParamSuffixes())
        {
            auto* a = apvts.getParameter (ids::slotPrefix (slotA) + "_" + suffix);
            auto* b = apvts.getParameter (ids::slotPrefix (slotB) + "_" + suffix);

            if (a == nullptr || b == nullptr)
            {
                jassertfalse;
                continue;
            }

            const auto valueA = a->getValue();
            const auto valueB = b->getValue();

            if (juce::exactlyEqual (valueA, valueB))
                continue;

            a->beginChangeGesture();
            a->setValueNotifyingHost (valueB);
            a->endChangeGesture();

            b->beginChangeGesture();
            b->setValueNotifyingHost (valueA);
            b->endChangeGesture();
        }

        slotManager.swap (slotA, slotB);
        std::swap (slotListContexts[static_cast<size_t> (slotA)], slotListContexts[static_cast<size_t> (slotB)]);
    }

    align::Result IReneProcessor::alignSlots (int referenceSlot)
    {
        const auto rate = slotManager.getEngineSampleRate();

        if (rate <= 0.0)
        {
            align::Result notReady;
            notReady.message = "The audio engine isn't running yet. Start playback once (or wait until the DAW has activated the plugin) and try again.";
            return notReady;
        }

        // The IRs exactly as the slots play them: resampled, trimmed, auto-trimmed.
        std::array<align::SlotInput, kNumSlots> inputs;
        const auto settings = paramRefs.read();

        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto index = static_cast<size_t> (i);
            inputs[index].ir = slotManager.getPreparedIR (i);
            inputs[index].phaseInvert = settings.slots[index].phaseInvert;
            inputs[index].name = slotManager.getInfo (i).reference.name;
        }

        const auto result = align::computeAlignment (inputs, rate, referenceSlot);

        if (! result.ok)
            return result;

        const auto setParam = [this] (const juce::String& id, float realValue)
        {
            if (auto* param = apvts.getParameter (id))
            {
                const auto normalised = param->convertTo0to1 (realValue);

                if (! juce::approximatelyEqual (param->getValue(), normalised))
                {
                    param->beginChangeGesture();
                    param->setValueNotifyingHost (normalised);
                    param->endChangeGesture();
                }
            }
        };

        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto& slot = result.slots[static_cast<size_t> (i)];

            if (! slot.aligned)
                continue; // empty slots, and slots too different from the reference, keep their settings

            setParam (ids::slotParam (i, ids::delay), static_cast<float> (slot.delayMs));
            setParam (ids::slotParam (i, ids::phase), slot.phaseInvert ? 1.0f : 0.0f);
        }

        return result;
    }

    void IReneProcessor::setSlotListContext (int slot, const juce::StringArray& paths)
    {
        slotListContexts[static_cast<size_t> (slot)] = paths;
    }

    juce::File IReneProcessor::getAdjacentFile (int slot, int direction)
    {
        auto& context = slotListContexts[static_cast<size_t> (slot)];
        const auto current = slotManager.getInfo (slot).reference.path;

        if (current.isNotEmpty() && ! context.contains (current))
        {
            // Not from a browser list (or the list is stale): use the IR's own folder.
            context.clear();

            if (juce::File::isAbsolutePath (current))
            {
                for (const auto& f : juce::File (current).getParentDirectory().findChildFiles (juce::File::findFiles, false))
                    if (isSupportedIRFile (f) && ! f.isHidden())
                        context.add (f.getFullPathName());

                context.sortNatural();
            }
        }

        if (context.isEmpty())
            return {};

        const auto n = context.size();
        auto index = context.indexOf (current);

        if (index < 0)
            index = direction > 0 ? -1 : 0; // empty slot: start at the first / last entry

        const auto next = ((index + direction) % n + n) % n;

        if (context[next] == current)
            return {};

        return juce::File (context[next]);
    }

    //==============================================================================
    juce::ValueTree IReneProcessor::savePresetState (bool embedAudio)
    {
        juce::ValueTree state { stateType };
        state.setProperty (kStateVersionId, kStateVersion, nullptr);
        state.setProperty (kPluginVersionId, juce::String (kVersionString), nullptr);
        state.setProperty (kVisibleSlots, getVisibleSlots(), nullptr);
        state.appendChild (apvts.copyState(), nullptr);
        state.appendChild (slotManager.toValueTree (embedAudio), nullptr);
        return state;
    }

    void IReneProcessor::applyPresetState (const juce::ValueTree& state)
    {
        if (! state.hasType (stateType))
            return;

        // Parameters first: the slots read their "IR length" while they rebuild.
        if (const auto params = state.getChildWithName (kParametersType); params.isValid())
        {
            apvts.replaceState (params.createCopy());
            migrateOldState (params);
        }

        if (const auto slots = state.getChildWithName (SlotManager::treeType); slots.isValid())
            slotManager.fromValueTree (slots);

        // Older states have three slots; getVisibleSlots() shows more if they're used.
        visibleSlots = juce::jlimit (kDefaultVisibleSlots, kNumSlots, static_cast<int> (state.getProperty (kVisibleSlots, kDefaultVisibleSlots)));

        for (auto& context : slotListContexts)
            context.clear();

        uiChanges.sendChange();
    }

    void IReneProcessor::resetToDefaults()
    {
        for (auto* param : getParameters())
        {
            if (! juce::exactlyEqual (param->getValue(), param->getDefaultValue()))
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost (param->getDefaultValue());
                param->endChangeGesture();
            }
        }

        for (int i = 0; i < kNumSlots; ++i)
        {
            slotManager.clear (i);
            slotManager.clearHistory (i);
            slotListContexts[static_cast<size_t> (i)].clear();
        }

        visibleSlots = kDefaultVisibleSlots;
        uiChanges.sendChange();
    }

    juce::ValueTree IReneProcessor::saveState (bool embedAudio)
    {
        auto state = savePresetState (embedAudio);
        state.setProperty (kPresetName, presets.getCurrentName(), nullptr);

        juce::ValueTree ui { kUiType };
        ui.setProperty (kFilterTab, uiState.filterTab.load(), nullptr);
        ui.setProperty (kTargetSlot, uiState.targetSlot.load(), nullptr);
        ui.setProperty (kShowSlotCurves, uiState.showSlotCurves.load(), nullptr);
        ui.setProperty (kResponseChannel, uiState.responseChannel.load(), nullptr);
        ui.setProperty (kResponseSmoothing, uiState.responseSmoothing.load(), nullptr);

        if (const auto text = getNotes(); text.isNotEmpty())
            ui.setProperty (kNotes, text, nullptr);

        state.appendChild (ui, nullptr);

        juce::ValueTree ab { kABType };
        ab.setProperty (kActiveSetup, activeSetup.load(), nullptr);
        ab.setProperty (kLoudnessMatch, loudnessMatch.load(), nullptr);

        if (referenceLoudnessValid.load())
            ab.setProperty (kReferenceLoudness, referenceLoudnessDb.load(), nullptr);

        {
            const juce::ScopedLock sl (setupLock);

            if (otherSetup.has_value())
                ab.appendChild (setupToValueTree (*otherSetup, embedAudio), nullptr);
        }

        state.appendChild (ab, nullptr);
        return state;
    }

    void IReneProcessor::restoreState (const juce::ValueTree& state)
    {
        if (! state.hasType (stateType))
            return;

        applyPresetState (state);

        const auto stateVersion = static_cast<int> (state.getProperty (kStateVersionId, 1));

        if (const auto ui = state.getChildWithName (kUiType); ui.isValid())
        {
            auto tab = static_cast<int> (ui.getProperty (kFilterTab, 0));

            if (stateVersion < 2 && tab == kOldOutputTab)
                tab = kNumSlots + 1; // "Output" moved when slots 4 and 5 arrived

            uiState.filterTab = juce::jlimit (0, kNumSlots + 1, tab);
            uiState.targetSlot = juce::jlimit (0, kNumSlots - 1, static_cast<int> (ui.getProperty (kTargetSlot, 0)));
            uiState.showSlotCurves = static_cast<bool> (ui.getProperty (kShowSlotCurves, true));
            uiState.responseChannel = juce::jlimit (0, 2, static_cast<int> (ui.getProperty (kResponseChannel, 0)));
            uiState.responseSmoothing = juce::jlimit (0, 2, static_cast<int> (ui.getProperty (kResponseSmoothing, 0)));
        }

        // Older projects have no notes: they start empty.
        setNotes (state.getChildWithName (kUiType).getProperty (kNotes).toString());

        // A/B (older projects: A only)
        const auto ab = state.getChildWithName (kABType);
        activeSetup = juce::jlimit (0, 1, static_cast<int> (ab.getProperty (kActiveSetup, 0)));
        loudnessMatch = static_cast<bool> (ab.getProperty (kLoudnessMatch, false));
        referenceLoudnessValid = ab.hasProperty (kReferenceLoudness);
        referenceLoudnessDb = static_cast<float> (ab.getProperty (kReferenceLoudness, 0.0f));

        {
            const auto setupTree = ab.getChildWithName (kSetupType);
            std::optional<Setup> restored;

            if (setupTree.isValid())
                restored = setupFromValueTree (setupTree);

            const juce::ScopedLock sl (setupLock);
            otherSetup = std::move (restored);
        }

        uiChanges.sendChange();

        // Only a label: the project state itself is what counts.
        const auto presetName = state.getProperty (kPresetName).toString();

        if (juce::MessageManager::getInstance()->isThisTheMessageThread())
            presets.setCurrentName (presetName);
        else
            juce::MessageManager::callAsync ([weak = juce::WeakReference<IReneProcessor> (this), presetName]
            {
                if (weak != nullptr)
                    weak->presets.setCurrentName (presetName);
            });
    }

    void IReneProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        juce::MemoryOutputStream stream (destData, false);
        saveState (true).writeToStream (stream);
    }

    void IReneProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        if (data == nullptr || sizeInBytes <= 0)
            return;

        restoreState (juce::ValueTree::readFromData (data, static_cast<size_t> (sizeInBytes)));
    }
}
