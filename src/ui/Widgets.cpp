#include "Widgets.h"

namespace irene::ui
{
    void styleCaption (juce::Label& label, float height, juce::Colour colour, juce::Justification justification)
    {
        label.setFont (font (height));
        label.setColour (juce::Label::textColourId, colour);
        label.setJustificationType (justification);
        label.setInterceptsMouseClicks (false, false);
        label.setMinimumHorizontalScale (0.7f);
    }

    //==============================================================================
    Knob::Knob (const juce::String& captionText)
    {
        caption.setText (captionText, juce::dontSendNotification);
        styleCaption (caption);
        addAndMakeVisible (caption);

        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 15);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        addAndMakeVisible (slider);
    }

    void Knob::attach (APVTS& apvts, const juce::String& parameterId)
    {
        attachment.reset();
        attachment = std::make_unique<APVTS::SliderAttachment> (apvts, parameterId, slider);
    }

    void Knob::setColour (juce::Colour colour)
    {
        slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
    }

    void Knob::setTooltip (const juce::String& tip)
    {
        slider.setTooltip (tip);
    }

    void Knob::resized()
    {
        // caption (13) + knob (square, as large as fits) + value (15), centred vertically
        auto area = getLocalBounds();
        const auto knobSize = juce::jmax (16, juce::jmin (area.getWidth(), area.getHeight() - 28));
        area = area.withSizeKeepingCentre (area.getWidth(), juce::jmin (area.getHeight(), knobSize + 28));

        caption.setBounds (area.removeFromTop (13));
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, area.getWidth(), 15);
        slider.setBounds (area);
    }

    //==============================================================================
    ParamToggle::ParamToggle (const juce::String& text, juce::Colour onColour)
        : juce::TextButton (text)
    {
        setClickingTogglesState (true);
        setColour (juce::TextButton::buttonOnColourId, onColour);
    }

    void ParamToggle::attach (APVTS& apvts, const juce::String& parameterId)
    {
        attachment.reset();
        attachment = std::make_unique<APVTS::ButtonAttachment> (apvts, parameterId, *this);
    }

    void ParamChoice::attach (APVTS& apvts, const juce::String& parameterId)
    {
        attachment.reset();
        clear (juce::dontSendNotification);

        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (parameterId)))
            addItemList (choice->choices, 1);

        attachment = std::make_unique<APVTS::ComboBoxAttachment> (apvts, parameterId, *this);
    }

    //==============================================================================
    ConfirmOverlay::ConfirmOverlay()
    {
        addAndMakeVisible (confirmButton);
        addAndMakeVisible (cancelButton);
        confirmButton.setColour (juce::TextButton::buttonColourId, colours::accent);
        confirmButton.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
        confirmButton.onClick = [this] { finish (true); };
        cancelButton.onClick = [this] { finish (false); };
        setWantsKeyboardFocus (true);
        setVisible (false);
    }

    void ConfirmOverlay::ask (const juce::String& newTitle, const juce::String& newMessage,
                              const juce::String& confirmText, const juce::String& cancelText,
                              std::function<void (bool)> callback)
    {
        // A question that is still open is answered with "cancel".
        if (isVisible())
            finish (false);

        title = newTitle;
        message = newMessage;
        confirmButton.setButtonText (confirmText);
        cancelButton.setButtonText (cancelText);
        onResult = std::move (callback);

        setVisible (true);
        toFront (true);
        resized();
        repaint();
    }

    void ConfirmOverlay::finish (bool confirmed)
    {
        setVisible (false);

        if (auto callback = std::exchange (onResult, nullptr))
            callback (confirmed);
    }

    bool ConfirmOverlay::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey)
        {
            finish (false);
            return true;
        }

        if (key == juce::KeyPress::returnKey)
        {
            finish (true);
            return true;
        }

        return true; // swallow everything else while the question is open
    }

    void ConfirmOverlay::resized()
    {
        box = juce::Rectangle<int> (440, 170).withCentre (getLocalBounds().getCentre());
        auto buttons = box.reduced (18).removeFromBottom (30);
        confirmButton.setBounds (buttons.removeFromRight (130));
        buttons.removeFromRight (10);
        cancelButton.setBounds (buttons.removeFromRight (110));
    }

    void ConfirmOverlay::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colours::black.withAlpha (0.6f));

        g.setColour (colours::panel);
        g.fillRoundedRectangle (box.toFloat(), 8.0f);
        g.setColour (colours::outline);
        g.drawRoundedRectangle (box.toFloat(), 8.0f, 1.0f);

        auto area = box.reduced (18);
        g.setColour (colours::text);
        g.setFont (font (17.0f, true));
        g.drawText (title, area.removeFromTop (26), juce::Justification::centredLeft);

        area.removeFromBottom (38);
        g.setColour (colours::textDim);
        g.setFont (font (14.0f));
        g.drawFittedText (message, area, juce::Justification::topLeft, 4);
    }
}
