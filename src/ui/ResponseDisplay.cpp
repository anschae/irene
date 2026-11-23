#include "ResponseDisplay.h"

namespace irene::ui
{
    namespace
    {
        constexpr int kRefreshHz = 30;

        const FilterBlockSettings& blockSettings (const EngineSettings& s, int tab)
        {
            if (tab <= 0)
                return s.inputFilter;

            if (tab > kNumSlots)
                return s.outputFilter;

            return s.slots[static_cast<size_t> (tab - 1)].filter;
        }
    }

    ResponseDisplay::ResponseDisplay (IReneProcessor& p)
        : processor (p), worker (p.getSpectra())
    {
        channelBox.addItem ("Mid (L+R)", 1);
        channelBox.addItem ("Left", 2);
        channelBox.addItem ("Right", 3);
        channelBox.setSelectedId (processor.uiState.responseChannel.load() + 1, juce::dontSendNotification);
        channelBox.setTooltip ("Which output channel the display shows");
        channelBox.onChange = [this]
        {
            processor.uiState.responseChannel = juce::jmax (0, channelBox.getSelectedId() - 1);
            dirty = true;
        };
        addAndMakeVisible (channelBox);

        smoothingBox.addItem ("No smoothing", 1);
        smoothingBox.addItem ("1/6 octave", 2);
        smoothingBox.addItem ("1/3 octave", 3);
        smoothingBox.setSelectedId (processor.uiState.responseSmoothing.load() + 1, juce::dontSendNotification);
        smoothingBox.setTooltip ("Smooths the displayed curves. Does not change the sound.");
        smoothingBox.onChange = [this]
        {
            processor.uiState.responseSmoothing = juce::jmax (0, smoothingBox.getSelectedId() - 1);
            dirty = true;
        };
        addAndMakeVisible (smoothingBox);

        slotsButton.setClickingTogglesState (true);
        slotsButton.setToggleState (processor.uiState.showSlotCurves.load(), juce::dontSendNotification);
        slotsButton.setTooltip ("Also show each slot's own response in its colour");
        slotsButton.onClick = [this]
        {
            processor.uiState.showSlotCurves = slotsButton.getToggleState();
            repaint();
        };
        addAndMakeVisible (slotsButton);

        worker.addListener (this);
        startTimerHz (kRefreshHz);
    }

    ResponseDisplay::~ResponseDisplay()
    {
        stopTimer();
        worker.removeListener (this);
    }

    void ResponseDisplay::setHighlightedBlock (int tabIndex, juce::Colour colour)
    {
        highlightedBlock = tabIndex;
        highlightColour = colour;
        dirty = true;
    }

    //==============================================================================
    void ResponseDisplay::timerCallback()
    {
        const auto settings = processor.readSettings();
        const auto rate = processor.getEngine().isPrepared() ? processor.getEngine().getSampleRate() : 48000.0;
        const auto stereo = processor.getTotalNumOutputChannels() > 1;

        if (! dirty && settings == lastSettings && juce::exactlyEqual (rate, lastRate) && stereo == lastStereo)
            return;

        lastSettings = settings;
        lastRate = rate;
        lastStereo = stereo;
        recompute();
        repaint();
    }

    void ResponseDisplay::recompute()
    {
        dirty = false;

        analysis::ResponseInput input;
        input.settings = lastSettings;
        input.sampleRate = lastRate;
        input.stereoOutput = lastStereo;

        for (int i = 0; i < kNumSlots; ++i)
            input.slots[static_cast<size_t> (i)] = worker.getSpectrum (i);

        const auto channel = static_cast<analysis::Channel> (juce::jlimit (0, 2, processor.uiState.responseChannel.load()));
        const auto smoothing = static_cast<analysis::Smoothing> (juce::jlimit (0, 2, processor.uiState.responseSmoothing.load()));

        curves = analysis::computeResponse (input, worker.getFrequencies(), channel, smoothing);
        filterCurve = analysis::computeFilterCurve (blockSettings (lastSettings, highlightedBlock), lastRate, worker.getFrequencies());

        filterCurveVisible = false;

        for (const auto db : filterCurve)
            if (std::abs (db) > 0.02f)
                filterCurveVisible = true;
    }

    const analysis::ResponseCurves& ResponseDisplay::updateNowForTests()
    {
        lastSettings = processor.readSettings();
        lastRate = processor.getEngine().isPrepared() ? processor.getEngine().getSampleRate() : 48000.0;
        lastStereo = processor.getTotalNumOutputChannels() > 1;
        recompute();
        return curves;
    }

    //==============================================================================
    float ResponseDisplay::xForFrequency (double freq) const
    {
        const auto t = std::log (freq / analysis::kMinDisplayFreq) / std::log (analysis::kMaxDisplayFreq / analysis::kMinDisplayFreq);
        return plot.getX() + static_cast<float> (t) * plot.getWidth();
    }

    float ResponseDisplay::yForDb (float db) const
    {
        return plot.getY() + (kTopDb - db) / (kTopDb - kBottomDb) * plot.getHeight();
    }

    juce::Path ResponseDisplay::makePath (const std::vector<float>& db) const
    {
        juce::Path path;
        const auto& frequencies = worker.getFrequencies();

        if (db.size() != frequencies.size())
            return path;

        for (size_t i = 0; i < db.size(); ++i)
        {
            // keep the path finite and close to the plot; the clip region cuts the rest
            const auto y = yForDb (juce::jlimit (kBottomDb - 12.0f, kTopDb + 12.0f, db[i]));
            const auto x = xForFrequency (frequencies[i]);

            if (i == 0)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }

        return path;
    }

    void ResponseDisplay::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (colours::panel);
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (colours::outline);
        g.drawRoundedRectangle (bounds, 6.0f, 1.0f);

        g.setColour (colours::background);
        g.fillRect (plot);

        // --- grid --------------------------------------------------------------
        g.setFont (font (10.0f));

        for (const auto freq : { 20.0, 30.0, 40.0, 50.0, 60.0, 70.0, 80.0, 90.0, 100.0, 200.0, 300.0, 400.0, 500.0, 600.0, 700.0, 800.0, 900.0,
                                 1000.0, 2000.0, 3000.0, 4000.0, 5000.0, 6000.0, 7000.0, 8000.0, 9000.0, 10000.0, 20000.0 })
        {
            const auto x = xForFrequency (freq);
            const auto major = juce::approximatelyEqual (freq, 100.0) || juce::approximatelyEqual (freq, 1000.0) || juce::approximatelyEqual (freq, 10000.0);
            g.setColour (colours::outline.withAlpha (major ? 0.9f : 0.35f));
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        }

        for (const auto freq : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        {
            const auto text = freq >= 1000.0 ? juce::String (juce::roundToInt (freq / 1000.0)) + "k" : juce::String (juce::roundToInt (freq));
            const auto x = xForFrequency (freq);
            g.setColour (colours::textDim);
            g.drawText (text, juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 1.0f, 40.0f, 12.0f),
                        freq <= 20.0 ? juce::Justification::centredRight : (freq >= 20000.0 ? juce::Justification::centredLeft : juce::Justification::centred));
        }

        for (auto db = kTopDb; db >= kBottomDb; db -= 12.0f)
        {
            const auto y = yForDb (db);
            g.setColour (colours::outline.withAlpha (juce::approximatelyEqual (db, 0.0f) ? 1.0f : 0.4f));
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
            g.setColour (colours::textDim);
            g.drawText ((db > 0.0f ? "+" : "") + juce::String (juce::roundToInt (db)),
                        juce::Rectangle<float> (0.0f, y - 6.0f, plot.getX() - 4.0f, 12.0f), juce::Justification::centredRight);
        }

        // --- curves ------------------------------------------------------------
        {
            const juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (plot.toNearestInt());

            if (filterCurveVisible)
            {
                // the selected filter block on its own, relative to the 0 dB line
                auto outline = makePath (filterCurve);
                auto fill = outline;
                fill.lineTo (plot.getRight(), yForDb (0.0f));
                fill.lineTo (plot.getX(), yForDb (0.0f));
                fill.closeSubPath();

                g.setColour (highlightColour.withAlpha (0.12f));
                g.fillPath (fill);

                juce::Path dashed;
                const float dashes[] { 5.0f, 4.0f };
                juce::PathStrokeType (1.3f).createDashedStroke (dashed, outline, dashes, 2);
                g.setColour (highlightColour.withAlpha (0.9f));
                g.fillPath (dashed);
            }

            if (curves.anything)
            {
                if (processor.uiState.showSlotCurves.load())
                {
                    for (int i = 0; i < kNumSlots; ++i)
                    {
                        const auto& slot = curves.slotDb[static_cast<size_t> (i)];

                        if (! slot.empty())
                        {
                            g.setColour (slotColour (i).withAlpha (0.8f));
                            g.strokePath (makePath (slot), juce::PathStrokeType (1.2f));
                        }
                    }
                }

                g.setColour (colours::text);
                g.strokePath (makePath (curves.combinedDb), juce::PathStrokeType (2.0f, juce::PathStrokeType::curved));
            }
        }

        if (! curves.anything)
        {
            g.setColour (colours::textDim);
            g.setFont (font (14.0f));
            g.drawText ("Load an IR to see the frequency response of the wet signal", plot, juce::Justification::centred);
        }
    }

    void ResponseDisplay::resized()
    {
        auto area = getLocalBounds().reduced (8, 6);

        auto controls = area.removeFromTop (22);
        slotsButton.setBounds (controls.removeFromRight (88));
        controls.removeFromRight (6);
        smoothingBox.setBounds (controls.removeFromRight (118));
        controls.removeFromRight (6);
        channelBox.setBounds (controls.removeFromRight (100));

        area.removeFromTop (4);
        area.removeFromLeft (26);  // dB labels
        area.removeFromBottom (13); // frequency labels
        plot = area.toFloat();
    }
}
