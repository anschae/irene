#pragma once

#include "PluginProcessor.h"
#include "Widgets.h"

namespace irene::ui
{
    /** A notepad: free text saved with the project (not in presets).
        Shown as an overlay inside the editor. */
    class NotesPanel final : public juce::Component
    {
    public:
        explicit NotesPanel (IReneProcessor&);

        std::function<void()> onClose;

        /** Called whenever the text changes between empty and not empty. */
        std::function<void (bool hasNotes)> onNotesChanged;

        void paint (juce::Graphics&) override;
        void resized() override;
        void visibilityChanged() override;
        void mouseDown (const juce::MouseEvent&) override {}
        bool keyPressed (const juce::KeyPress&) override;

        juce::TextEditor& getEditorForTests() noexcept { return text; }

    private:
        IReneProcessor& processor;
        juce::Rectangle<int> box;

        juce::Label title, hint;
        juce::TextEditor text;
        juce::TextButton closeButton { "Close" };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NotesPanel)
    };
}
