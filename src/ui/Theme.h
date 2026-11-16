#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace irene::ui
{
    namespace colours
    {
        inline const juce::Colour background { 0xff17191c };
        inline const juce::Colour panel { 0xff202328 };
        inline const juce::Colour panelRaised { 0xff2a2e35 };
        inline const juce::Colour outline { 0xff3a3f48 };
        inline const juce::Colour text { 0xffe6e7ea };
        inline const juce::Colour textDim { 0xff9aa0a8 };
        inline const juce::Colour accent { 0xff4da3ff };
        inline const juce::Colour warning { 0xffffb84d };
        inline const juce::Colour danger { 0xffff6b5e };
        inline const juce::Colour ok { 0xff5fd38d };
        inline const juce::Colour mute { 0xffff6b5e };
        inline const juce::Colour solo { 0xffffd24d };
    }

    /** A distinct colour for each slot; works for any number of slots. */
    inline juce::Colour slotColour (int slotIndex)
    {
        static const std::array<juce::Colour, 6> base {
            juce::Colour (0xff4da3ff), juce::Colour (0xffff9f43), juce::Colour (0xff5fd38d),
            juce::Colour (0xffd48cff), juce::Colour (0xffff6b9a), juce::Colour (0xff4dd6d0)
        };

        const auto n = static_cast<int> (base.size());
        return base[static_cast<size_t> (slotIndex % n)].withRotatedHue (0.07f * static_cast<float> (slotIndex / n));
    }

    inline juce::Font font (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }

    /** Dark, flat look for all standard JUCE widgets. */
    class IReneLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        IReneLookAndFeel();

        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                               float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                                   bool highlighted, bool down) override;

        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
        juce::Font getLabelFont (juce::Label&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        juce::Font getPopupMenuFont() override;

        void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                           int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;

        /** Menus open inside the plugin window (in the scaled content component),
            so they follow the UI scale and never end up behind the host window. */
        juce::Component* getParentComponentForMenuOptions (const juce::PopupMenu::Options&) override;
    };

    /** Component ID of the scaled content component that hosts popup menus. */
    inline const juce::String kContentComponentId { "irene-content" };
}
