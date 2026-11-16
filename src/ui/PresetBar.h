#pragma once

#include "PluginProcessor.h"
#include "Widgets.h"

namespace irene::ui
{
    /** The preset controls in the header: previous / name (menu) / next, Save,
        Save as, and a menu with Rename and Delete. */
    class PresetBar final : public juce::Component,
                            private juce::ChangeListener
    {
    public:
        PresetBar (IReneProcessor&, ConfirmOverlay& dialogs);
        ~PresetBar() override;

        void resized() override;

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
        void refresh();
        void showPresetMenu();
        void showMoreMenu();
        void save();
        void saveAs();
        void rename();
        void remove();
        void report (const juce::String& error);

        IReneProcessor& processor;
        PresetManager& presets;
        ConfirmOverlay& dialogs;

        juce::TextButton prevButton { "<" }, nextButton { ">" }, nameButton, saveButton { "Save" }, saveAsButton { "Save as..." }, moreButton { "..." };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBar)
    };
}
