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

    /** A simple in-window question ("message", two buttons). Covers its parent
        while visible. Used instead of native dialogs, which behave differently
        from host to host. */
    class ConfirmOverlay final : public juce::Component
    {
    public:
        ConfirmOverlay();

        void ask (const juce::String& title, const juce::String& message,
                  const juce::String& confirmText, const juce::String& cancelText,
                  std::function<void (bool confirmed)> callback);

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override {}
        bool keyPressed (const juce::KeyPress&) override;

    private:
        void finish (bool confirmed);

        juce::String title, message;
        juce::TextButton confirmButton, cancelButton;
        std::function<void (bool)> onResult;
        juce::Rectangle<int> box;
    };

    /** Small caption-style label. */
    void styleCaption (juce::Label&, float height = 11.0f, juce::Colour colour = colours::textDim,
                       juce::Justification = juce::Justification::centred);
}
