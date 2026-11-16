#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/FilterEditor.h"
#include "ui/GlobalSection.h"
#include "ui/SettingsPanel.h"
#include "ui/SlotStrip.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace irene
{
    /** The plugin window.

        All controls live in a fixed-size "content" component that is scaled as a
        whole, so the UI can be shown at 75-200 % without any layout changes. The
        scale is a global setting and is remembered. */
    class IReneEditor final : public juce::AudioProcessorEditor,
                              public juce::DragAndDropContainer,
                              private juce::ChangeListener,
                              private juce::Timer
    {
    public:
        explicit IReneEditor (IReneProcessor&);
        ~IReneEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        /** Size of the UI at 100 %. */
        static constexpr int kBaseWidth = 1120;
        static constexpr int kBaseHeight = 504;

        /** Loads a file into a slot, asking first if the "ask before resampling"
            setting is on and the sample rates differ. */
        void requestLoad (int slot, const juce::File& file);

    private:
        /** Everything visible, laid out at 100 % and scaled by the editor. */
        class Content final : public juce::Component
        {
        public:
            Content (IReneEditor&, IReneProcessor&);

            void paint (juce::Graphics&) override;
            void resized() override;

            IReneEditor& editor;
            juce::Label title, version;
            juce::TextButton settingsButton { "Settings" };
            ui::FilterEditor filterEditor;
            juce::OwnedArray<ui::SlotStrip> strips;
            ui::GlobalSection globalSection;
            ui::SettingsPanel settingsPanel;
            ui::ConfirmOverlay confirm;
        };

        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;
        void applyScale (float scale);
        void browseForFile (int slot);
        void selectTargetSlot (int slot);

        IReneProcessor& plugin;
        ui::IReneLookAndFeel lookAndFeel;
        Content content;
        juce::TooltipWindow tooltips { this, 700 };
        std::unique_ptr<juce::FileChooser> chooser;
        juce::File lastBrowseFolder;
        float currentScale = 1.0f;
        bool applyingScale = false, constructed = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IReneEditor)
    };
}
