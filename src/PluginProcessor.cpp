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
          slotManager (engine, paramRefs)
    {
        // Moved or renamed files are found again through the library's index.
        // (The library is shared and outlives the slot manager's thread.)
        slotManager.fileResolver = [lib = library] (const IRReference& ref) { return lib->resolve (ref); };
        slotManager.onFileLoaded = [lib = library] (int, const IRSource& source) { lib->addRecent (IRFileEntry::fromSource (source)); };

        applyGlobalSettings();
        globalSettings->addChangeListener (this);
    }

    IReneProcessor::~IReneProcessor()
    {
        masterReference.clear();
        globalSettings->removeChangeListener (this);
    }

    void IReneProcessor::changeListenerCallback (juce::ChangeBroadcaster*)
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

        engine.process (buffer, paramRefs.read(), isNonRealtime());
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
            apvts.replaceState (params.createCopy());

        if (const auto slots = state.getChildWithName (SlotManager::treeType); slots.isValid())
            slotManager.fromValueTree (slots);

        for (auto& context : slotListContexts)
            context.clear();
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
            slotListContexts[static_cast<size_t> (i)].clear();
        }
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
        state.appendChild (ui, nullptr);
        return state;
    }

    void IReneProcessor::restoreState (const juce::ValueTree& state)
    {
        if (! state.hasType (stateType))
            return;

        applyPresetState (state);

        if (const auto ui = state.getChildWithName (kUiType); ui.isValid())
        {
            uiState.filterTab = juce::jlimit (0, kNumSlots + 1, static_cast<int> (ui.getProperty (kFilterTab, 0)));
            uiState.targetSlot = juce::jlimit (0, kNumSlots - 1, static_cast<int> (ui.getProperty (kTargetSlot, 0)));
            uiState.showSlotCurves = static_cast<bool> (ui.getProperty (kShowSlotCurves, true));
            uiState.responseChannel = juce::jlimit (0, 2, static_cast<int> (ui.getProperty (kResponseChannel, 0)));
            uiState.responseSmoothing = juce::jlimit (0, 2, static_cast<int> (ui.getProperty (kResponseSmoothing, 0)));
        }

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
