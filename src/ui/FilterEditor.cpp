#include "FilterEditor.h"

namespace irene::ui
{
    juce::String FilterEditor::tabName (int tab)
    {
        if (tab == 0)
            return "Input";

        if (tab == kNumTabs - 1)
            return "Output";

        return "Slot " + juce::String (tab);
    }

    juce::String FilterEditor::tabPrefix (int tab)
    {
        if (tab == 0)
            return ids::inputPrefix;

        if (tab == kNumTabs - 1)
            return ids::outputPrefix;

        return ids::slotPrefix (tab - 1);
    }

    juce::Colour FilterEditor::tabColour (int tab)
    {
        return (tab == 0 || tab == kNumTabs - 1) ? colours::text : slotColour (tab - 1);
    }

    FilterEditor::FilterEditor (IReneProcessor& p)
        : processor (p)
    {
        for (int t = 0; t < kNumTabs; ++t)
        {
            auto* tab = tabs.add (new juce::TextButton (tabName (t)));
            tab->setClickingTogglesState (true);
            tab->setRadioGroupId (0x1f17);
            tab->setColour (juce::TextButton::buttonOnColourId, tabColour (t));
            tab->setTooltip ("Edit the filter block of: " + tabName (t));
            tab->onClick = [this, t] { if (tabs[t]->getToggleState()) selectTab (t); };
            addAndMakeVisible (tab);
        }

        bypass.setTooltip ("Bypass this whole filter block");
        addAndMakeVisible (bypass);

        const auto addCut = [this] (CutSection& cut, const juce::String& what)
        {
            cut.on.setTooltip (what + " on/off");
            cut.freq.setTooltip (what + " frequency, 20 Hz to 20 kHz");
            cut.slope.setTooltip (what + " slope: 12 or 24 dB per octave");
            addAndMakeVisible (cut.on);
            addAndMakeVisible (cut.freq);
            addAndMakeVisible (cut.slope);
        };

        addCut (highPass, "High-pass");
        addCut (lowPass, "Low-pass");

        const std::array<const char*, kNumBands> names { "LOW", "MID", "HIGH" };

        for (int b = 0; b < kNumBands; ++b)
        {
            auto* band = bands.add (new BandSection (names[static_cast<size_t> (b)]));
            band->hasType = b != 1;
            const juce::String label (names[static_cast<size_t> (b)]);

            band->on.setTooltip (label + " band on/off");
            band->freq.setTooltip (label + " band frequency, 20 Hz to 20 kHz");
            band->gain.setTooltip (label + " band gain, -18 to +18 dB. Double-click to reset.");
            band->q.setTooltip (label + " band Q (width), 0.3 to 10");
            band->type.setTooltip (label + " band shape: peak or shelf");

            addAndMakeVisible (band->on);
            addAndMakeVisible (band->freq);
            addAndMakeVisible (band->gain);
            addAndMakeVisible (band->q);

            if (band->hasType)
                addAndMakeVisible (band->type);
        }

        selectTab (0);
    }

    void FilterEditor::setVisibleSlots (int numSlots)
    {
        numSlots = juce::jlimit (1, kNumSlots, numSlots);

        if (numSlots == visibleSlots)
            return;

        visibleSlots = numSlots;

        for (int t = 1; t <= kNumSlots; ++t)
            tabs[t]->setVisible (t <= visibleSlots);

        if (selectedTab > visibleSlots && selectedTab <= kNumSlots)
            selectTab (0);

        resized();
    }

    void FilterEditor::selectTab (int tab)
    {
        tab = juce::jlimit (0, kNumTabs - 1, tab);

        if (tab == selectedTab)
            return;

        selectedTab = tab;
        tabs[tab]->setToggleState (true, juce::dontSendNotification);

        auto& apvts = processor.getValueTreeState();
        const auto p = tabPrefix (tab) + "_";
        const auto colour = tabColour (tab) == colours::text ? colours::accent : tabColour (tab);

        bypass.attach (apvts, p + "filter_bypass");

        highPass.on.attach (apvts, p + "hp_on");
        highPass.freq.attach (apvts, p + "hp_freq");
        highPass.slope.attach (apvts, p + "hp_slope");
        lowPass.on.attach (apvts, p + "lp_on");
        lowPass.freq.attach (apvts, p + "lp_freq");
        lowPass.slope.attach (apvts, p + "lp_slope");

        for (auto* cut : { &highPass, &lowPass })
        {
            cut->on.setColour (juce::TextButton::buttonOnColourId, colour);
            cut->freq.setColour (colour);
        }

        for (int b = 0; b < kNumBands; ++b)
        {
            auto& band = *bands[b];
            const auto bp = p + ids::bandNames[static_cast<size_t> (b)] + "_";

            band.on.attach (apvts, bp + "on");
            band.freq.attach (apvts, bp + "freq");
            band.gain.attach (apvts, bp + "gain");
            band.q.attach (apvts, bp + "q");

            if (band.hasType)
                band.type.attach (apvts, bp + "type");

            band.on.setColour (juce::TextButton::buttonOnColourId, colour);

            for (auto* k : { &band.freq, &band.gain, &band.q })
                k->setColour (colour);
        }

        repaint();

        if (onTabChanged)
            onTabChanged (tab);
    }

    void FilterEditor::paint (juce::Graphics& g)
    {
        g.setColour (colours::panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f);
        g.setColour (colours::outline);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);

        g.setColour (colours::panelRaised.withAlpha (0.45f));

        for (const auto& r : sectionBounds)
            g.fillRoundedRectangle (r.toFloat(), 5.0f);
    }

    void FilterEditor::resized()
    {
        auto area = getLocalBounds().reduced (10, 8);

        auto tabRow = area.removeFromTop (24);
        bypass.setBounds (tabRow.removeFromRight (80));
        tabRow.removeFromRight (12);

        const auto shownTabs = visibleSlots + 2;
        const auto tabWidth = juce::jmin (96, (tabRow.getWidth() - (shownTabs - 1) * 4) / shownTabs);

        for (auto* tab : tabs)
        {
            if (! tab->isVisible())
                continue;

            tab->setBounds (tabRow.removeFromLeft (tabWidth));
            tabRow.removeFromLeft (4);
        }

        area.removeFromTop (8);

        // Sections: HP | LOW | MID | HIGH | LP. Cuts hold 1 knob, bands 3.
        const auto gap = 8;
        const auto unit = (area.getWidth() - 4 * gap) / 13; // 2 + 3 + 3 + 3 + 2 units
        sectionBounds.clear();

        const auto layoutCut = [&] (CutSection& cut, juce::Rectangle<int> r)
        {
            sectionBounds.push_back (r);
            r.reduce (8, 6);
            auto top = r.removeFromTop (22);
            cut.on.setBounds (top.removeFromLeft (38));
            top.removeFromLeft (4);
            cut.slope.setBounds (top);
            r.removeFromTop (4);
            cut.freq.setBounds (r);
        };

        const auto layoutBand = [&] (BandSection& band, juce::Rectangle<int> r)
        {
            sectionBounds.push_back (r);
            r.reduce (8, 6);
            auto top = r.removeFromTop (22);
            band.on.setBounds (top.removeFromLeft (54));

            if (band.hasType)
                band.type.setBounds (top.removeFromRight (84));

            r.removeFromTop (4);
            const auto w = r.getWidth() / 3;
            band.freq.setBounds (r.removeFromLeft (w));
            band.gain.setBounds (r.removeFromLeft (w));
            band.q.setBounds (r);
        };

        layoutCut (highPass, area.removeFromLeft (unit * 2));
        area.removeFromLeft (gap);

        for (auto* band : bands)
        {
            layoutBand (*band, area.removeFromLeft (unit * 3));
            area.removeFromLeft (gap);
        }

        layoutCut (lowPass, area);
    }
}
