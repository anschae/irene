#pragma once

#include "Widgets.h"
#include "app/GlobalSettings.h"

namespace irene::ui
{
    /** The settings page. Shown as an overlay inside the editor. Everything here
        is global: shared by all instances and all DAWs. */
    class SettingsPanel final : public juce::Component,
                                private juce::ListBoxModel,
                                private juce::ChangeListener
    {
    public:
        SettingsPanel();
        ~SettingsPanel() override;

        std::function<void()> onClose;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override {}
        bool keyPressed (const juce::KeyPress&) override;

    private:
        int getNumRows() override;
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
        void selectedRowsChanged (int) override { updateButtons(); }
        void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

        void refresh();
        void updateButtons();
        void addFolder();
        void removeFolder();
        void moveFolder (int delta);
        void choosePresetsFolder();

        GlobalSettings::Shared settings;
        juce::StringArray folders;
        juce::Rectangle<int> box;

        juce::Label title, loadingHeading, scaleHeading, presetsHeading, foldersHeading, presetsPath, hint;
        juce::TextButton closeButton { "Close" };
        juce::ToggleButton askResample { "Ask before resampling an IR to the session's sample rate" };
        juce::ToggleButton normalise { "Loudness normalisation for newly loaded IRs" };
        juce::ToggleButton autoTrim { "Auto-trim leading silence for newly loaded IRs" };
        juce::ComboBox scale;
        juce::TextButton choosePresets { "Choose..." }, defaultPresets { "Use default" };
        juce::ListBox folderList;
        juce::TextButton addButton { "Add..." }, removeButton { "Remove" }, upButton { "Move up" }, downButton { "Move down" };
        std::unique_ptr<juce::FileChooser> chooser;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsPanel)
    };

    /** The UI scale steps offered in the settings, in percent. */
    inline constexpr std::array<int, 6> kUiScaleSteps { 75, 100, 125, 150, 175, 200 };
}
