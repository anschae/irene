#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Theme.h"

namespace irene::ui
{
    using APVTS = juce::AudioProcessorValueTreeState;

    /** Rotary control with a caption above and an editable value below. */
    class Knob final : public juce::Component
    {
    public:
        explicit Knob (const juce::String& caption);

        /** Connects the knob to a parameter (replacing any earlier connection). */
        void attach (APVTS&, const juce::String& parameterId);
        void setColour (juce::Colour);
        void setTooltip (const juce::String&);

        void resized() override;

        juce::Slider slider;

    private:
        juce::Label caption;
        std::unique_ptr<APVTS::SliderAttachment> attachment;
    };

    /** A latching text button bound to a bool parameter. */
    class ParamToggle final : public juce::TextButton
    {
    public:
        ParamToggle (const juce::String& text, juce::Colour onColour = colours::accent);
        void attach (APVTS&, const juce::String& parameterId);

    private:
        std::unique_ptr<APVTS::ButtonAttachment> attachment;
    };

    /** A drop-down bound to a choice parameter. */
    class ParamChoice final : public juce::ComboBox
    {
    public:
        ParamChoice() = default;
        void attach (APVTS&, const juce::String& parameterId);

    private:
        std::unique_ptr<APVTS::ComboBoxAttachment> attachment;
    };

    /** A small button showing a "go back" arrow (drawn, so it doesn't depend
        on which symbols the system font has). */
    class BackButton final : public juce::TextButton
    {
    public:
        BackButton() = default;
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

    /** A simple in-window dialog: a question with two buttons, a message with
        one, or a prompt with a text field and an optional checkbox. Covers its
        parent while visible. Used instead of native dialogs, which behave
        differently from host to host. */
    class ConfirmOverlay final : public juce::Component
    {
    public:
        ConfirmOverlay();

        /** Yes/no question. */
        void ask (const juce::String& title, const juce::String& message,
                  const juce::String& confirmText, const juce::String& cancelText,
                  std::function<void (bool confirmed)> callback);

        /** Just a message with an OK button. */
        void inform (const juce::String& title, const juce::String& message);

        /** Text input, optionally with a checkbox (pass an empty checkboxText for none). */
        void prompt (const juce::String& title, const juce::String& message, const juce::String& initialText,
                     const juce::String& checkboxText, bool checkboxState, const juce::String& confirmText,
                     std::function<void (bool confirmed, const juce::String& text, bool checked)> callback);

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override {}
        bool keyPressed (const juce::KeyPress&) override;

    private:
        void show();
        void finish (bool confirmed);

        juce::String title, message;
        juce::TextButton confirmButton, cancelButton;
        juce::TextEditor textField;
        juce::ToggleButton checkbox;
        std::function<void (bool, const juce::String&, bool)> onResult;
        juce::Rectangle<int> box;
    };

    /** Small caption-style label. */
    void styleCaption (juce::Label&, float height = 11.0f, juce::Colour colour = colours::textDim,
                       juce::Justification = juce::Justification::centred);
}
