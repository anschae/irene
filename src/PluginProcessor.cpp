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
    }

    IReneProcessor::~IReneProcessor() = default;

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
    }

    //==============================================================================
    juce::ValueTree IReneProcessor::saveState (bool embedAudio)
    {
        juce::ValueTree state { stateType };
        state.setProperty (kStateVersionId, kStateVersion, nullptr);
        state.setProperty (kPluginVersionId, juce::String (kVersionString), nullptr);
        state.appendChild (apvts.copyState(), nullptr);
        state.appendChild (slotManager.toValueTree (embedAudio), nullptr);
        return state;
    }

    void IReneProcessor::restoreState (const juce::ValueTree& state)
    {
        if (! state.hasType (stateType))
            return;

        // Parameters first: the slots read their "IR length" while they rebuild.
        if (const auto params = state.getChildWithName (kParametersType); params.isValid())
            apvts.replaceState (params.createCopy());

        if (const auto slots = state.getChildWithName (SlotManager::treeType); slots.isValid())
            slotManager.fromValueTree (slots);
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
