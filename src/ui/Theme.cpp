#include "Theme.h"

namespace irene::ui
{
    IReneLookAndFeel::IReneLookAndFeel()
    {
        using namespace colours;

        setColour (juce::ResizableWindow::backgroundColourId, background);
        setColour (juce::DocumentWindow::backgroundColourId, background);

        setColour (juce::Label::textColourId, text);
        setColour (juce::Label::textWhenEditingColourId, text);
        setColour (juce::Label::backgroundWhenEditingColourId, panelRaised);
        setColour (juce::Label::outlineWhenEditingColourId, accent);

        setColour (juce::TextButton::buttonColourId, panelRaised);
        setColour (juce::TextButton::buttonOnColourId, accent);
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, juce::Colours::black);

        setColour (juce::ToggleButton::textColourId, text);
        setColour (juce::ToggleButton::tickColourId, accent);
        setColour (juce::ToggleButton::tickDisabledColourId, textDim);

        setColour (juce::Slider::rotarySliderFillColourId, accent);
        setColour (juce::Slider::rotarySliderOutlineColourId, outline);
        setColour (juce::Slider::thumbColourId, text);
        setColour (juce::Slider::trackColourId, accent);
        setColour (juce::Slider::backgroundColourId, outline);
        setColour (juce::Slider::textBoxTextColourId, text);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, accent.withAlpha (0.4f));

        setColour (juce::TextEditor::backgroundColourId, panelRaised);
        setColour (juce::TextEditor::textColourId, text);
        setColour (juce::TextEditor::outlineColourId, outline);
        setColour (juce::TextEditor::focusedOutlineColourId, accent);
        setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.4f));
        setColour (juce::CaretComponent::caretColourId, text);

        setColour (juce::ComboBox::backgroundColourId, panelRaised);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::outlineColourId, outline);
        setColour (juce::ComboBox::arrowColourId, textDim);
        setColour (juce::ComboBox::focusedOutlineColourId, accent);

        setColour (juce::PopupMenu::backgroundColourId, panelRaised);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
        setColour (juce::PopupMenu::headerTextColourId, textDim);

        setColour (juce::ListBox::backgroundColourId, panel);
        setColour (juce::ListBox::textColourId, text);
        setColour (juce::ListBox::outlineColourId, outline);

        setColour (juce::ScrollBar::thumbColourId, outline.brighter (0.3f));
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff0f1012));
        setColour (juce::TooltipWindow::textColourId, text);
        setColour (juce::TooltipWindow::outlineColourId, outline);
    }

    void IReneLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                             float startAngle, float endAngle, juce::Slider& slider)
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const auto thickness = juce::jmax (2.5f, radius * 0.18f);
        const auto arcRadius = radius - thickness * 0.5f;
        const auto angle = startAngle + sliderPos * (endAngle - startAngle);
        const auto enabled = slider.isEnabled();
        const auto fill = slider.findColour (juce::Slider::rotarySliderFillColourId).withMultipliedAlpha (enabled ? 1.0f : 0.35f);

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
        g.setColour (slider.findColour (juce::Slider::rotarySliderOutlineColourId));
        g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Bipolar controls (range symmetric around zero) fill from the centre.
        const auto range = slider.getRange();
        const auto bipolar = range.getStart() < 0.0 && std::abs (range.getStart() + range.getEnd()) < 1.0e-6;
        const auto fromAngle = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

        if (std::abs (angle - fromAngle) > 0.001f)
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                 juce::jmin (fromAngle, angle), juce::jmax (fromAngle, angle), true);
            g.setColour (fill);
            g.strokePath (value, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        const auto knobRadius = arcRadius - thickness * 1.1f;

        if (knobRadius > 2.0f)
        {
            g.setColour (colours::panelRaised.brighter (slider.isMouseOverOrDragging() ? 0.15f : 0.0f));
            g.fillEllipse (juce::Rectangle<float> (knobRadius * 2.0f, knobRadius * 2.0f).withCentre (centre));

            const auto pointer = centre.getPointOnCircumference (knobRadius * 0.85f, angle);
            const auto pointerStart = centre.getPointOnCircumference (knobRadius * 0.35f, angle);
            g.setColour (enabled ? colours::text : colours::textDim);
            g.drawLine ({ pointerStart, pointer }, juce::jmax (1.5f, thickness * 0.5f));
        }
    }

    void IReneLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                                 bool highlighted, bool down)
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        auto colour = backgroundColour.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.4f);

        if (down)
            colour = colour.brighter (0.25f);
        else if (highlighted)
            colour = colour.brighter (0.12f);

        g.setColour (colour);
        g.fillRoundedRectangle (bounds, 4.0f);

        if (! button.getToggleState())
        {
            g.setColour (colours::outline);
            g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
        }
    }

    juce::Font IReneLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
    {
        return font (juce::jmin (13.0f, static_cast<float> (buttonHeight) * 0.6f));
    }

    juce::Font IReneLookAndFeel::getLabelFont (juce::Label& label)
    {
        return label.getFont();
    }

    juce::Font IReneLookAndFeel::getComboBoxFont (juce::ComboBox&)
    {
        return font (13.0f);
    }

    juce::Font IReneLookAndFeel::getPopupMenuFont()
    {
        return font (14.0f);
    }

    juce::Component* IReneLookAndFeel::getParentComponentForMenuOptions (const juce::PopupMenu::Options& options)
    {
        if (auto* parent = options.getParentComponent())
            return parent;

        for (auto* c = options.getTargetComponent(); c != nullptr; c = c->getParentComponent())
            if (c->getComponentID() == kContentComponentId)
                return c;

        return nullptr;
    }

    void IReneLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                         int, int, int, int, juce::ComboBox& box)
    {
        const auto bounds = juce::Rectangle<int> (width, height).toFloat().reduced (0.5f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (box.findColour (box.hasKeyboardFocus (true) ? juce::ComboBox::focusedOutlineColourId : juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

        juce::Path arrow;
        const auto cx = static_cast<float> (width) - 11.0f;
        const auto cy = static_cast<float> (height) * 0.5f;
        arrow.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId).withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.3f));
        g.fillPath (arrow);
    }
}
