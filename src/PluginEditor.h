#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/BrowserPanel.h"
#include "ui/FilterEditor.h"
#include "ui/GlobalSection.h"
#include "ui/PresetBar.h"
#include "ui/ResponseDisplay.h"
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

        /** Size of the UI at 100 % (the browser adds to the width when open). */
        static constexpr int kBaseWidth = 1120;
        static constexpr int kBaseHeight = 702;
        static constexpr int kDisplayHeight = 190;
        static constexpr int kBrowserWidth = 300;
        static constexpr int kGap = 8;

        int getBaseWidth() const noexcept { return kBaseWidth + (browserOpen ? kBrowserWidth + kGap : 0); }

        /** Loads a file into a slot, asking first if the "ask before resampling"
            setting is on and the sample rates differ. */
        void requestLoad (int slot, const juce::File& file);

        bool isBrowserOpen() const noexcept { return browserOpen; }
        ui::BrowserPanel* getBrowserForTests() noexcept { return &content.browser; }
        ui::ResponseDisplay* getDisplayForTests() noexcept { return &content.display; }

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
            juce::TextButton settingsButton { "Settings" }, browserButton { "Browser" };
            ui::ConfirmOverlay confirm;
            ui::PresetBar presetBar;
            ui::BrowserPanel browser;
            ui::ResponseDisplay display;
            ui::FilterEditor filterEditor;
            juce::OwnedArray<ui::SlotStrip> strips;
            ui::GlobalSection globalSection;
            ui::SettingsPanel settingsPanel;
        };

        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;
        void applyScale (float scale);
        void applyGlobalSettings();
        void browseForFile (int slot);
        void selectTargetSlot (int slot);
        void loadFromList (int slot, const juce::File& file);
        void stepSlot (int slot, int direction);
        void toggleSlotFavourite (int slot);
        void refreshStrips();

        IReneProcessor& plugin;
        ui::IReneLookAndFeel lookAndFeel;
        Content content;
        juce::TooltipWindow tooltips { this, 700 };
        std::unique_ptr<juce::FileChooser> chooser;
        juce::File lastBrowseFolder;
        float currentScale = 1.0f;
        bool applyingScale = false, constructed = false, browserOpen = true;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IReneEditor)
    };
}
