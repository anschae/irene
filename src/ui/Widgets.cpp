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
        addChildComponent (textField);
        addChildComponent (checkbox);

        confirmButton.setColour (juce::TextButton::buttonColourId, colours::accent);
        confirmButton.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
        confirmButton.onClick = [this] { finish (true); };
        cancelButton.onClick = [this] { finish (false); };

        textField.setFont (font (15.0f));
        textField.onReturnKey = [this] { finish (true); };
        textField.onEscapeKey = [this] { finish (false); };

        setWantsKeyboardFocus (true);
        setVisible (false);
    }

    void ConfirmOverlay::ask (const juce::String& newTitle, const juce::String& newMessage,
                              const juce::String& confirmText, const juce::String& cancelText,
                              std::function<void (bool)> callback)
    {
        // A dialog that is still open is answered with "cancel".
        if (isVisible())
            finish (false);

        title = newTitle;
        message = newMessage;
        confirmButton.setButtonText (confirmText);
        cancelButton.setButtonText (cancelText);
        cancelButton.setVisible (true);
        textField.setVisible (false);
        checkbox.setVisible (false);

        onResult = [cb = std::move (callback)] (bool confirmed, const juce::String&, bool)
        {
            if (cb)
                cb (confirmed);
        };

        show();
    }

    void ConfirmOverlay::inform (const juce::String& newTitle, const juce::String& newMessage)
    {
        ask (newTitle, newMessage, "OK", {}, nullptr);
        cancelButton.setVisible (false);
    }

    void ConfirmOverlay::prompt (const juce::String& newTitle, const juce::String& newMessage, const juce::String& initialText,
                                 const juce::String& checkboxText, bool checkboxState, const juce::String& confirmText,
                                 std::function<void (bool, const juce::String&, bool)> callback)
    {
        if (isVisible())
            finish (false);

        title = newTitle;
        message = newMessage;
        confirmButton.setButtonText (confirmText);
        cancelButton.setButtonText ("Cancel");
        cancelButton.setVisible (true);

        textField.setText (initialText, false);
        textField.setVisible (true);
        checkbox.setButtonText (checkboxText);
        checkbox.setToggleState (checkboxState, juce::dontSendNotification);
        checkbox.setVisible (checkboxText.isNotEmpty());

        onResult = std::move (callback);
        show();

        textField.grabKeyboardFocus();
        textField.selectAll();
    }

    void ConfirmOverlay::show()
    {
        setVisible (true);
        toFront (true);
        resized();
        repaint();
    }

    void ConfirmOverlay::finish (bool confirmed)
    {
        setVisible (false);

        if (auto callback = std::exchange (onResult, nullptr))
            callback (confirmed, textField.getText(), checkbox.getToggleState());
    }

    bool ConfirmOverlay::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey)
            finish (false);
        else if (key == juce::KeyPress::returnKey)
            finish (true);

        return true; // swallow everything else while the dialog is open
    }

    void ConfirmOverlay::resized()
    {
        const auto hasText = textField.isVisible();
        const auto hasCheckbox = checkbox.isVisible();

        box = juce::Rectangle<int> (460, 170 + (hasText ? 34 : 0) + (hasCheckbox ? 28 : 0)).withCentre (getLocalBounds().getCentre());
        auto area = box.reduced (18);

        auto buttons = area.removeFromBottom (30);
        confirmButton.setBounds (buttons.removeFromRight (130));
        buttons.removeFromRight (10);
        cancelButton.setBounds (buttons.removeFromRight (110));
        area.removeFromBottom (10);

        if (hasCheckbox)
            checkbox.setBounds (area.removeFromBottom (26));

        if (hasText)
        {
            area.removeFromBottom (4);
            textField.setBounds (area.removeFromBottom (28));
        }
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

        area.removeFromBottom (40 + (textField.isVisible() ? 34 : 0) + (checkbox.isVisible() ? 28 : 0));
        g.setColour (colours::textDim);
        g.setFont (font (14.0f));
        g.drawFittedText (message, area, juce::Justification::topLeft, 4);
    }

    //==============================================================================
    void BackButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
    {
        getLookAndFeel().drawButtonBackground (g, *this, findColour (juce::TextButton::buttonColourId), highlighted, down);

        // An arrow that curves back to the left.
        const auto area = getLocalBounds().toFloat().reduced (getWidth() * 0.27f, getHeight() * 0.3f);
        juce::Path arc;
        arc.addCentredArc (area.getCentreX(), area.getBottom() - area.getHeight() * 0.5f,
                           area.getWidth() * 0.45f, area.getHeight() * 0.45f, 0.0f,
                           juce::MathConstants<float>::pi * 1.55f, juce::MathConstants<float>::pi * 2.5f, true);

        const auto colour = isEnabled() ? colours::text : colours::textDim.withAlpha (0.5f);
        g.setColour (colour);
        g.strokePath (arc, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // arrow head at the start of the arc (left side)
        const auto start = arc.getPointAlongPath (0.0f);
        const auto size = area.getHeight() * 0.42f;
        juce::Path head;
        head.addTriangle (start.x - size * 0.6f, start.y, start.x + size * 0.45f, start.y - size * 0.55f,
                          start.x + size * 0.45f, start.y + size * 0.55f);
        g.fillPath (head);
    }
}
