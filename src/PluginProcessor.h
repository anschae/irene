#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Config.h"
#include "dsp/Engine.h"
#include "ir/SlotManager.h"
#include "params/Parameters.h"

namespace irene
{
    /** The plugin. Supported bus layouts: mono->mono, mono->stereo, stereo->stereo. */
    class IReneProcessor final : public juce::AudioProcessor
    {
    public:
        IReneProcessor();
        ~IReneProcessor() override;

        //==============================================================================
        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
        void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        bool supportsDoublePrecisionProcessing() const override { return false; }

        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override { return true; }

        const juce::String getName() const override { return kPluginName; }
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        bool isMidiEffect() const override { return false; }
        double getTailLengthSeconds() const override;

        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock& destData) override;
        void setStateInformation (const void* data, int sizeInBytes) override;

        //==============================================================================
        juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return apvts; }
        const ParamRefs& getParamRefs() const noexcept { return paramRefs; }
        SlotManager& getSlots() noexcept { return slotManager; }
        Engine& getEngine() noexcept { return engine; }

        /** Exchanges the IR and every setting of two slots. Message thread. */
        void swapSlots (int slotA, int slotB);

        /** The complete state as a tree (parameters + slots).
            @param embedAudio  include the IR audio, making the state self-contained */
        juce::ValueTree saveState (bool embedAudio);
        void restoreState (const juce::ValueTree& state);

        static const juce::Identifier stateType;

    private:
        juce::AudioProcessorValueTreeState apvts;
        ParamRefs paramRefs;
        Engine engine;
        SlotManager slotManager;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IReneProcessor)
    };
}
