#pragma once

#include "PluginProcessor.h"
#include "Widgets.h"

namespace irene::ui
{
    /** A pair of peak meters (left/right) with peak hold. */
    class PeakMeter final : public juce::Component,
                            public juce::SettableTooltipClient
    {
    public:
        explicit PeakMeter (const juce::String& caption);

        /** Feed with the peaks (linear gain) measured since the last call. */
        void push (float left, float right, int numChannels);

        void paint (juce::Graphics&) override;

    private:
        static constexpr float kMinDb = -60.0f;
        static constexpr float kMaxDb = 6.0f;

        juce::String caption;
        std::array<float, 2> levelDb { -100.0f, -100.0f }, holdDb { -100.0f, -100.0f };
        std::array<int, 2> holdCounter { 0, 0 };
        int channels = 2;
    };

    /** The global controls on the right: meters, auto gain, dry/wet mix, output gain. */
    class GlobalSection final : public juce::Component,
                                private juce::Timer
    {
    public:
        explicit GlobalSection (IReneProcessor&);

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void timerCallback() override;

        IReneProcessor& processor;
        PeakMeter inputMeter { "IN" }, outputMeter { "OUT" };
        Knob mix { "MIX" }, outGain { "OUTPUT" };
        ParamToggle autoGain { "Auto gain", colours::accent };
        juce::Label autoGainReadout, matchReadout;

    public:
        juce::String getAutoGainTextForTests() const { return autoGainReadout.getText(); }
        juce::String getMatchTextForTests() const { return matchReadout.getText(); }

    private:

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlobalSection)
    };
}
