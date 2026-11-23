#include "NotesPanel.h"

namespace irene::ui
{
    NotesPanel::NotesPanel (IReneProcessor& p)
        : processor (p)
    {
        title.setText ("Notes", juce::dontSendNotification);
        title.setFont (font (20.0f, true));
        addAndMakeVisible (title);

        hint.setText ("Anything you want to remember about this sound. Saved with the DAW project (not in presets).",
                      juce::dontSendNotification);
        styleCaption (hint, 12.0f, colours::textDim, juce::Justification::centredLeft);
        addAndMakeVisible (hint);

        text.setMultiLine (true, true);
        text.setReturnKeyStartsNewLine (true);
        text.setTabKeyUsedAsCharacter (false);
        text.setScrollbarsShown (true);
        text.setFont (font (14.0f));
        text.setTextToShowWhenEmpty ("e.g. which cab and mics, what it's used for in the mix...", colours::textDim);
        text.setColour (juce::TextEditor::backgroundColourId, colours::background);
        text.setColour (juce::TextEditor::outlineColourId, colours::outline);
        text.setColour (juce::TextEditor::focusedOutlineColourId, colours::accent);
        text.setIndents (8, 8);
        text.onTextChange = [this]
        {
            const auto hadNotes = processor.getNotes().isNotEmpty();
            processor.setNotes (text.getText());

            if (hadNotes != text.getText().isNotEmpty() && onNotesChanged)
                onNotesChanged (text.getText().isNotEmpty());
        };
        text.onEscapeKey = [this] { closeButton.triggerClick(); };
        addAndMakeVisible (text);

        closeButton.setTooltip ("Close the notes (they are saved as you type)");
        closeButton.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (closeButton);
    }

    void NotesPanel::visibilityChanged()
    {
        if (! isVisible())
            return;

        // The project may have been reloaded since the panel was last open.
        text.setText (processor.getNotes(), juce::dontSendNotification);
        text.moveCaretToEnd();

        if (isShowing())
            text.grabKeyboardFocus();
    }

    bool NotesPanel::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey)
        {
            closeButton.triggerClick();
            return true;
        }

        return false;
    }

    void NotesPanel::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colours::black.withAlpha (0.6f));
        g.setColour (colours::panel);
        g.fillRoundedRectangle (box.toFloat(), 8.0f);
        g.setColour (colours::outline);
        g.drawRoundedRectangle (box.toFloat(), 8.0f, 1.0f);
    }

    void NotesPanel::resized()
    {
        box = getLocalBounds().withSizeKeepingCentre (juce::jmin (640, getWidth() - 40), juce::jmin (480, getHeight() - 30));
        auto area = box.reduced (20, 16);

        auto top = area.removeFromTop (28);
        closeButton.setBounds (top.removeFromRight (80));
        title.setBounds (top);
        hint.setBounds (area.removeFromTop (20));
        area.removeFromTop (8);
        text.setBounds (area);
    }
}
