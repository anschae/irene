#pragma once

#include "PluginProcessor.h"
#include "Widgets.h"
#include "analysis/Response.h"

namespace irene::ui
{
    /** The frequency response display: the combined magnitude response of the
        wet path (20 Hz - 20 kHz, log axis), optionally each slot's own curve,
        and the curve of the filter block that is selected in the filter editor.

        Recomputed on the message thread, at most 30 times per second, and only
        when something changed. The IR spectra come from a background thread. */
    class ResponseDisplay final : public juce::Component,
                                  private juce::Timer
    {
    public:
        explicit ResponseDisplay (IReneProcessor&);

        /** Which filter block to highlight: 0 = Input, 1..kNumSlots = slots, last = Output. */
        void setHighlightedBlock (int tabIndex, juce::Colour colour);

        void paint (juce::Graphics&) override;
        void resized() override;

        static constexpr float kTopDb = 24.0f;
        static constexpr float kBottomDb = -60.0f;

        /** For tests: recomputes immediately and returns the combined curve. */
        const analysis::ResponseCurves& updateNowForTests();
        analysis::SpectrumWorker& getWorkerForTests() noexcept { return worker; }

    private:
        void timerCallback() override;
        void recompute();
        juce::Path makePath (const std::vector<float>& db) const;
        float xForFrequency (double freq) const;
        float yForDb (float db) const;

        IReneProcessor& processor;
        analysis::SpectrumWorker worker;

        juce::ComboBox channelBox, smoothingBox;
        juce::TextButton slotsButton { "Slot curves" };

        juce::Rectangle<float> plot;
        analysis::ResponseCurves curves;
        std::vector<float> filterCurve;
        bool filterCurveVisible = false;

        // what the current curves were computed from
        EngineSettings lastSettings;
        double lastRate = 0.0;
        bool lastStereo = true;
        int lastChannel = -1, lastSmoothing = -1, lastBlock = -1;
        bool dirty = true;

        int highlightedBlock = 0;
        juce::Colour highlightColour { colours::accent };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResponseDisplay)
    };
}
