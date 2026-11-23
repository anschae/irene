#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/BrowserPanel.h"
#include "ui/ExportPanel.h"
#include "ui/FilterEditor.h"
#include "ui/GlobalSection.h"
#include "ui/NotesPanel.h"
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
                              private Broadcaster::Listener,
                              private juce::Timer
    {
    public:
        explicit IReneEditor (IReneProcessor&);
        ~IReneEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        /** Size of the UI at 100 % (the browser adds to the width when open).
            The width grows with the number of slots. */
        static constexpr int kGap = 8;
        static constexpr int kStripWidth = 315;
        static constexpr int kGlobalWidth = 130;
        static constexpr int kMargin = 10;
        static constexpr int kBaseWidth = 2 * kMargin + kNumSlots * kStripWidth + (kNumSlots - 1) * kGap + kGap + kGlobalWidth;
        static constexpr int kBaseHeight = 702;
        static constexpr int kDisplayHeight = 190;

        int getBaseWidth() const noexcept { return kBaseWidth + (browserOpen ? browserWidth + kGap : 0); }

        /** Width of the IR browser at 100 %. Changing it makes the window wider
            or narrower at the same scale. With `remember` the width is stored
            in the global settings (used by every new instance). */
        int getBrowserWidth() const noexcept { return browserWidth; }
        void setBrowserWidth (int width, bool remember);

        /** Loads a file into a slot, asking first if the "ask before resampling"
            setting is on and the sample rates differ. */
        void requestLoad (int slot, const juce::File& file);

        bool isBrowserOpen() const noexcept { return browserOpen; }
        ui::BrowserPanel* getBrowserForTests() noexcept { return &content.browser; }
        ui::ResponseDisplay* getDisplayForTests() noexcept { return &content.display; }
        ui::ExportPanel* getExportPanelForTests() noexcept { return &content.exportPanel; }
        ui::NotesPanel* getNotesPanelForTests() noexcept { return &content.notesPanel; }
        ui::SlotStrip* getStripForTests (int slot) noexcept { return content.strips[slot]; }
        juce::TextButton& getNotesButtonForTests() noexcept { return content.notesButton; }

    private:
        /** The thin strip between the browser and the rest: drag it to change
            the browser's width. */
        class BrowserResizer final : public juce::Component,
                                     public juce::SettableTooltipClient
        {
        public:
            explicit BrowserResizer (IReneEditor& e) : editor (e)
            {
                setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
                setRepaintsOnMouseActivity (true);
                setTooltip ("Drag to change the width of the IR browser");
            }

            void paint (juce::Graphics&) override;
            void mouseDown (const juce::MouseEvent&) override { startWidth = editor.getBrowserWidth(); }
            void mouseDrag (const juce::MouseEvent& e) override { editor.setBrowserWidth (startWidth + e.getDistanceFromDragStartX(), false); }
            void mouseUp (const juce::MouseEvent&) override { editor.setBrowserWidth (editor.getBrowserWidth(), true); }
            void mouseDoubleClick (const juce::MouseEvent&) override { editor.setBrowserWidth (GlobalSettings::kDefaultBrowserWidth, true); }

        private:
            IReneEditor& editor;
            int startWidth = 0;
        };

        /** Everything visible, laid out at 100 % and scaled by the editor. */
        class Content final : public juce::Component
        {
        public:
            Content (IReneEditor&, IReneProcessor&);

            void paint (juce::Graphics&) override;
            void resized() override;

            IReneEditor& editor;
            juce::Label title, version;
            juce::TextButton settingsButton { "Settings" }, browserButton { "Browser" }, exportButton { "Export IR..." }, notesButton { "Notes" };
            ui::ConfirmOverlay confirm;
            ui::PresetBar presetBar;
            ui::BrowserPanel browser;
            BrowserResizer browserResizer;
            ui::ResponseDisplay display;
            ui::FilterEditor filterEditor;
            juce::OwnedArray<ui::SlotStrip> strips;
            ui::GlobalSection globalSection;
            ui::SettingsPanel settingsPanel;
            ui::ExportPanel exportPanel;
            ui::NotesPanel notesPanel;
        };

        void broadcasterChanged (Broadcaster*) override;
        void timerCallback() override;
        void applyScale (float scale);
        void applyGlobalSettings();
        void browseForFile (int slot);
        void selectTargetSlot (int slot);
        void loadFromList (int slot, const juce::File& file);
        void stepSlot (int slot, int direction);
        void toggleSlotFavourite (int slot);
        void alignToSlot (int slot);
        void refreshStrips();
        void goBack (int slot);
        void auditionSoloChanged();
        void updateNotesButton();

        IReneProcessor& plugin;
        ui::IReneLookAndFeel lookAndFeel;
        Content content;
        juce::TooltipWindow tooltips { this, 700 };
        std::unique_ptr<juce::FileChooser> chooser;
        juce::File lastBrowseFolder;
        float currentScale = 1.0f;
        bool applyingScale = false, constructed = false, browserOpen = true;
        int browserWidth = GlobalSettings::kDefaultBrowserWidth;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IReneEditor)
    };
}
