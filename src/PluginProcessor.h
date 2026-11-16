#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Config.h"
#include "analysis/TimeAlign.h"
#include "app/GlobalSettings.h"
#include "app/PresetManager.h"
#include "dsp/Engine.h"
#include "export/Exporter.h"
#include "ir/SlotManager.h"
#include "library/IRLibrary.h"
#include "params/Parameters.h"

namespace irene
{
    /** The plugin. Supported bus layouts: mono->mono, mono->stereo, stereo->stereo. */
    class IReneProcessor final : public juce::AudioProcessor,
                                 private Broadcaster::Listener
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

        /** Time-aligns all loaded slots to the given slot: sets their Delay
            parameters (and polarity where needed) so that their direct sound
            arrives together. Returns what was done; if `ok` is false nothing was
            changed and `message` says why. Message thread. */
        align::Result alignSlots (int referenceSlot);

        /** The complete state as a tree (parameters + slots).
            @param embedAudio  include the IR audio, making the state self-contained */
        juce::ValueTree saveState (bool embedAudio);
        void restoreState (const juce::ValueTree& state);

        /** The part of the state that a preset stores: parameters and slots. */
        juce::ValueTree savePresetState (bool embedAudio);
        void applyPresetState (const juce::ValueTree& state);

        /** All parameters back to their defaults, all slots empty. Message thread. */
        void resetToDefaults();

        PresetManager& getPresets() noexcept { return presets; }

        static const juce::Identifier stateType;

        //==============================================================================
        GlobalSettings& getGlobalSettings() noexcept { return *globalSettings; }

        /** Editor state that is saved with the project (not automatable). */
        struct UiState
        {
            std::atomic<int> filterTab { 0 };  // 0 = Input, 1..kNumSlots = slots, last = Output
            std::atomic<int> targetSlot { 0 }; // the browser's target slot

            // frequency display
            std::atomic<bool> showSlotCurves { true };
            std::atomic<int> responseChannel { 0 };   // 0 = Mid, 1 = Left, 2 = Right
            std::atomic<int> responseSmoothing { 0 }; // 0 = none, 1 = 1/6 octave, 2 = 1/3 octave
        };

        UiState uiState;

        //==============================================================================
        IRLibrary& getLibrary() noexcept { return *library; }

        /** Where the browser is, kept here so it survives closing the editor.
            Message thread only; not saved with the project. */
        struct BrowserState
        {
            enum class Location { allFavourites, recent, folder };

            bool initialised = false;
            Location location = Location::allFavourites;
            juce::String rootFolder, currentFolder; // for Location::folder
            bool includeSubfolders = false, starredOnly = false, sortForwards = true;
            juce::String search;
        };

        BrowserState browserState;

        /** "Export as single IR" options used last in this instance. Message thread. */
        ExportOptions exportOptions;
        juce::File lastExportFolder;

        /** Remembers the list a slot's IR was picked from (full paths), for the
            slot's prev/next buttons. Message thread. */
        void setSlotListContext (int slot, const juce::StringArray& paths);

        /** The file before/after the slot's current IR in its list. Without a
            list, the other IR files in the same folder are used. Returns an
            invalid File if there is nothing to step to. Message thread. */
        juce::File getAdjacentFile (int slot, int direction);

    private:
        void broadcasterChanged (Broadcaster*) override;
        void applyGlobalSettings();

        GlobalSettings::Shared globalSettings;
        IRLibrary::Shared library;
        std::array<juce::StringArray, kNumSlots> slotListContexts;
        juce::AudioProcessorValueTreeState apvts;
        ParamRefs paramRefs;
        Engine engine;
        SlotManager slotManager;
        PresetManager presets { *this };

        JUCE_DECLARE_WEAK_REFERENCEABLE (IReneProcessor)
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IReneProcessor)
    };
}
