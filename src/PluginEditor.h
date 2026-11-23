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

        /** Size of the UI at 100 %. The width depends on how many slots are
            shown (up to three full-width strips; four or five narrower ones)
            and on the browser. */
        static constexpr int kGap = 8;
        static constexpr int kStripWidth = 315;        // up to three slots
        static constexpr int kCompactStripWidth = 250; // four or five slots
        static constexpr int kAddSlotWidth = 26;       // the "+" column
        static constexpr int kGlobalWidth = 130;
        static constexpr int kMargin = 10;
        static constexpr int kBaseHeight = 702;
        static constexpr int kDisplayHeight = 190;

        static constexpr int stripWidthFor (int visibleSlots) noexcept { return visibleSlots <= 3 ? kStripWidth : kCompactStripWidth; }

        static constexpr int slotAreaWidth (int visibleSlots) noexcept
        {
            return visibleSlots * stripWidthFor (visibleSlots) + (visibleSlots - 1) * kGap
                   + (visibleSlots < kNumSlots ? kGap + kAddSlotWidth : 0);
        }

        /** Width at 100 % without the browser. */
        static constexpr int baseWidthFor (int visibleSlots) noexcept
        {
            return 2 * kMargin + slotAreaWidth (visibleSlots) + kGap + kGlobalWidth;
        }

        int getBaseWidth() const noexcept { return baseWidthFor (visibleSlots) + (browserOpen ? browserWidth + kGap : 0); }
        int getVisibleSlots() const noexcept { return visibleSlots; }

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
        juce::Button& getAddSlotButtonForTests() noexcept { return content.addSlotButton; }
        ui::FilterEditor* getFilterEditorForTests() noexcept { return &content.filterEditor; }
        ui::GlobalSection* getGlobalSectionForTests() noexcept { return &content.globalSection; }

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
            ui::PlusButton addSlotButton;
            juce::TextButton setupA { "A" }, setupB { "B" }, copySetup { "A > B" }, matchButton { "Match" };
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
        void updateVisibleSlots();
        void updateSetupButtons();
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
        int visibleSlots = kDefaultVisibleSlots;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IReneEditor)
    };
}
