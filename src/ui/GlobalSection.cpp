#include "GlobalSection.h"

namespace irene::ui
{
    namespace
    {
        constexpr int kMeterRefreshHz = 30;
        constexpr float kDecayDbPerFrame = 1.2f;  // about 36 dB per second
        constexpr int kHoldFrames = kMeterRefreshHz; // 1 second
    }

    PeakMeter::PeakMeter (const juce::String& captionText)
        : caption (captionText)
    {
    }

    void PeakMeter::push (float left, float right, int numChannels)
    {
        channels = juce::jlimit (1, 2, numChannels);
        const std::array<float, 2> incoming { left, channels > 1 ? right : left };

        for (size_t ch = 0; ch < 2; ++ch)
        {
            const auto db = juce::Decibels::gainToDecibels (incoming[ch], -100.0f);
            levelDb[ch] = juce::jmax (db, levelDb[ch] - kDecayDbPerFrame);

            if (db >= holdDb[ch])
            {
                holdDb[ch] = db;
                holdCounter[ch] = kHoldFrames;
            }
            else if (--holdCounter[ch] <= 0)
            {
                holdDb[ch] = juce::jmax (levelDb[ch], holdDb[ch] - kDecayDbPerFrame);
            }
        }

        repaint();
    }

    void PeakMeter::paint (juce::Graphics& g)
    {
        auto area = getLocalBounds();

        g.setColour (colours::textDim);
        g.setFont (font (11.0f));
        g.drawText (caption, area.removeFromTop (14), juce::Justification::centred);

        const auto peakText = holdDb[0] <= -99.0f && holdDb[1] <= -99.0f
                                  ? juce::String ("-inf")
                                  : juce::String (juce::jmax (holdDb[0], holdDb[1]), 1);
        const auto clipped = juce::jmax (holdDb[0], holdDb[1]) > 0.0f;
        g.setColour (clipped ? colours::danger : colours::textDim);
        g.drawText (peakText, area.removeFromBottom (14), juce::Justification::centred);

        area.reduce (0, 2);
        const auto barWidth = juce::jmin (10, (area.getWidth() - 2) / 2);
        auto bars = area.withSizeKeepingCentre (barWidth * 2 + 2, area.getHeight());

        const auto toY = [&] (float db)
        {
            const auto t = juce::jlimit (0.0f, 1.0f, (db - kMinDb) / (kMaxDb - kMinDb));
            return static_cast<float> (bars.getBottom()) - t * static_cast<float> (bars.getHeight());
        };

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto bar = bars.removeFromLeft (barWidth).toFloat();
            bars.removeFromLeft (2);
            const auto index = static_cast<size_t> (ch);

            g.setColour (colours::background);
            g.fillRoundedRectangle (bar, 2.0f);

            const auto top = toY (levelDb[index]);

            if (top < bar.getBottom())
            {
                // green up to -12 dB, yellow to 0 dB, red above
                const auto drawPart = [&] (float fromDb, float toDb, juce::Colour c)
                {
                    const auto y1 = juce::jmax (top, toY (toDb));
                    const auto y2 = toY (fromDb);

                    if (y2 > y1)
                    {
                        g.setColour (c);
                        g.fillRect (juce::Rectangle<float> (bar.getX(), y1, bar.getWidth(), y2 - y1));
                    }
                };

                drawPart (kMinDb, -12.0f, colours::ok);
                drawPart (-12.0f, 0.0f, colours::warning);
                drawPart (0.0f, kMaxDb, colours::danger);
            }

            if (holdDb[index] > kMinDb)
            {
                g.setColour (holdDb[index] > 0.0f ? colours::danger : colours::text);
                g.fillRect (juce::Rectangle<float> (bar.getX(), toY (holdDb[index]) - 0.5f, bar.getWidth(), 1.5f));
            }
        }

        // 0 dB mark
        g.setColour (colours::outline.brighter (0.4f));
        const auto zeroY = toY (0.0f);
        g.drawHorizontalLine (juce::roundToInt (zeroY), static_cast<float> (area.getX()), static_cast<float> (area.getRight()));
    }

    //==============================================================================
    GlobalSection::GlobalSection (IReneProcessor& p)
        : processor (p)
    {
        inputMeter.setTooltip ("Input peak level (before the input filter block)");
        outputMeter.setTooltip ("Output peak level. Turns red above 0 dBFS.");
        addAndMakeVisible (inputMeter);
        addAndMakeVisible (outputMeter);

        mix.attach (processor.getValueTreeState(), ids::mix);
        outGain.attach (processor.getValueTreeState(), ids::outGain);
        mix.setTooltip ("Dry/wet mix. 100 % = only the IR signal. The dry signal is taken after the input filter block.");
        outGain.setTooltip ("Output gain, -24 to +24 dB. Double-click to reset.");
        addAndMakeVisible (mix);
        addAndMakeVisible (outGain);

        startTimerHz (kMeterRefreshHz);
    }

    void GlobalSection::timerCallback()
    {
        auto& engine = processor.getEngine();
        inputMeter.push (engine.readInputPeak (0), engine.readInputPeak (1), processor.getTotalNumInputChannels());
        outputMeter.push (engine.readOutputPeak (0), engine.readOutputPeak (1), processor.getTotalNumOutputChannels());
    }

    void GlobalSection::paint (juce::Graphics& g)
    {
        g.setColour (colours::panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f);
        g.setColour (colours::outline);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
    }

    void GlobalSection::resized()
    {
        auto area = getLocalBounds().reduced (10);

        outGain.setBounds (area.removeFromBottom (84));
        area.removeFromBottom (6);
        mix.setBounds (area.removeFromBottom (84));
        area.removeFromBottom (10);

        inputMeter.setBounds (area.removeFromLeft (area.getWidth() / 2));
        outputMeter.setBounds (area);
    }
}
