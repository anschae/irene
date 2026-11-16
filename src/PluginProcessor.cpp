#include "PluginProcessor.h"

#include "PluginEditor.h"

namespace irene
{
    namespace
    {
        const juce::Identifier kStateRootId { "IReneState" };
        const juce::Identifier kStateVersionId { "stateVersion" };
        const juce::Identifier kPluginVersionId { "pluginVersion" };
    }

    IReneProcessor::IReneProcessor()
        : AudioProcessor (BusesProperties()
                              .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
    }

    void IReneProcessor::prepareToPlay (double, int)
    {
        setLatencySamples (0);
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

    void IReneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        juce::ScopedNoDenormals noDenormals;

        const auto numIn = getTotalNumInputChannels();
        const auto numOut = getTotalNumOutputChannels();
        const auto numSamples = buffer.getNumSamples();

        // mono -> stereo: duplicate the input onto the second output channel.
        const bool monoToStereo = numIn == 1 && numOut == 2 && buffer.getNumChannels() >= 2;

        if (monoToStereo)
            buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

        // Clear any output channel that has no corresponding input.
        for (auto ch = monoToStereo ? 2 : numIn; ch < juce::jmin (numOut, buffer.getNumChannels()); ++ch)
            buffer.clear (ch, 0, numSamples);
    }

    void IReneProcessor::processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&)
    {
        jassertfalse; // double precision is not supported
    }

    juce::AudioProcessorEditor* IReneProcessor::createEditor()
    {
        return new IReneEditor (*this);
    }

    void IReneProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        juce::ValueTree state { kStateRootId };
        state.setProperty (kStateVersionId, kStateVersion, nullptr);
        state.setProperty (kPluginVersionId, juce::String (kVersionString), nullptr);

        juce::MemoryOutputStream stream (destData, false);
        state.writeToStream (stream);
    }

    void IReneProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        if (data == nullptr || sizeInBytes <= 0)
            return;

        const auto state = juce::ValueTree::readFromData (data, static_cast<size_t> (sizeInBytes));

        if (! state.hasType (kStateRootId))
            return;

        // Nothing to restore yet (milestone 1).
    }
}
