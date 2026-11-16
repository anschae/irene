#pragma once

#include "PluginProcessor.h"
#include "Widgets.h"

namespace irene::ui
{
    /** One shared editor for the five filter blocks. Tabs choose which block
        (Input / Slot 1..N / Output) the controls are connected to. */
    class FilterEditor final : public juce::Component
    {
    public:
        explicit FilterEditor (IReneProcessor&);

        /** Tab index: 0 = Input, 1..kNumSlots = slots, kNumSlots + 1 = Output. */
        static constexpr int kNumTabs = kNumSlots + 2;
        void selectTab (int tabIndex);
        int getSelectedTab() const noexcept { return selectedTab; }

        std::function<void (int tabIndex)> onTabChanged;

        static juce::String tabName (int tabIndex);
        static juce::String tabPrefix (int tabIndex);
        static juce::Colour tabColour (int tabIndex);

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        struct CutSection
        {
            CutSection (const juce::String& onText, const juce::String& caption) : on (onText), freq (caption) {}
            ParamToggle on;
            Knob freq;
            ParamChoice slope;
        };

        struct BandSection
        {
            explicit BandSection (const juce::String& onText) : on (onText) {}
            ParamToggle on;
            Knob freq { "FREQ" }, gain { "GAIN" }, q { "Q" };
            ParamChoice type;
            bool hasType = false;
        };

        IReneProcessor& processor;
        int selectedTab = -1;

        juce::OwnedArray<juce::TextButton> tabs;
        ParamToggle bypass { "Bypass", colours::warning };
        CutSection highPass { "HP", "FREQ" }, lowPass { "LP", "FREQ" };
        juce::OwnedArray<BandSection> bands;
        std::vector<juce::Rectangle<int>> sectionBounds;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilterEditor)
    };
}
