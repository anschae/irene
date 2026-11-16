#include "SlotStrip.h"

namespace irene::ui
{
    juce::String formatRate (double sampleRate)
    {
        const auto k = sampleRate / 1000.0;
        return (std::abs (k - std::round (k)) < 0.005 ? juce::String (juce::roundToInt (k)) : juce::String (k, 1)) + "k";
    }

    juce::String formatLength (double seconds)
    {
        return seconds < 1.0 ? juce::String (juce::roundToInt (seconds * 1000.0)) + " ms"
                             : juce::String (seconds, 2) + " s";
    }

    SlotStrip::SlotStrip (IReneProcessor& p, int slotIndex)
        : processor (p), slot (slotIndex), colour (slotColour (slotIndex))
    {
        auto& apvts = processor.getValueTreeState();

        title.setText ("SLOT " + juce::String (slot + 1), juce::dontSendNotification);
        title.setFont (font (12.0f, true));
        title.setColour (juce::Label::textColourId, colour);
        title.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (title);

        name.setFont (font (15.0f, true));
        name.setMinimumHorizontalScale (0.75f);
        name.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (name);

        styleCaption (info, 12.0f, colours::textDim, juce::Justification::centredLeft);
        styleCaption (notice, 11.0f, colours::warning, juce::Justification::centredLeft);
        styleCaption (delayReadout, 11.0f, colours::textDim, juce::Justification::centred);
        addAndMakeVisible (info);
        addAndMakeVisible (notice);
        addAndMakeVisible (delayReadout);

        for (auto* b : { &favouriteButton, &prevButton, &nextButton, &loadButton, &clearButton, &swapButton, &normaliseButton, &autoTrimButton, &alignButton })
            addAndMakeVisible (b);

        alignButton.setTooltip ("Time-align the other slots to this one: sets the Delay of every loaded slot (and flips polarity where needed) "
                                "so that all microphones arrive together.");
        alignButton.onClick = [this] { if (onAlign) onAlign(); };

        favouriteButton.setTooltip ("Mark the loaded IR as a favourite");
        favouriteButton.setColour (juce::TextButton::buttonOnColourId, colours::solo);
        favouriteButton.onClick = [this] { if (onToggleFavourite) onToggleFavourite(); };

        prevButton.setTooltip ("Load the previous IR from the list this IR came from");
        nextButton.setTooltip ("Load the next IR from the list this IR came from");
        prevButton.onClick = [this] { if (onStep) onStep (-1); };
        nextButton.onClick = [this] { if (onStep) onStep (1); };

        loadButton.setTooltip ("Load a WAV or AIFF impulse response into this slot. You can also drop a file here.");
        loadButton.onClick = [this] { if (onBrowse) onBrowse(); };

        clearButton.setTooltip ("Remove the IR from this slot");
        clearButton.onClick = [this] { processor.getSlots().clear (slot); };

        swapButton.setTooltip ("Exchange this slot's IR and all its settings with another slot");
        swapButton.onClick = [this] { showSwapMenu(); };

        normaliseButton.setTooltip ("Loudness normalisation for this IR: different IRs play at a similar loudness. Does not change the Gain control.");
        normaliseButton.setColour (juce::TextButton::buttonOnColourId, colour);
        normaliseButton.onClick = [this]
        {
            const auto i = processor.getSlots().getInfo (slot);
            processor.getSlots().setSlotOptions (slot, ! i.normalise, i.autoTrim);
        };

        autoTrimButton.setTooltip ("Remove silence at the start of this IR. Leave off for multi-mic packs: their start delays keep the mics in phase.");
        autoTrimButton.setColour (juce::TextButton::buttonOnColourId, colour);
        autoTrimButton.onClick = [this]
        {
            const auto i = processor.getSlots().getInfo (slot);
            processor.getSlots().setSlotOptions (slot, i.normalise, ! i.autoTrim);
        };

        for (auto* k : { &gain, &pan, &delay, &length })
        {
            k->setColour (colour);
            addAndMakeVisible (k);
        }

        gain.attach (apvts, ids::slotParam (slot, ids::gain));
        pan.attach (apvts, ids::slotParam (slot, ids::pan));
        delay.attach (apvts, ids::slotParam (slot, ids::delay));
        length.attach (apvts, ids::slotParam (slot, ids::length));

        gain.setTooltip ("Slot level, -60 dB (off) to +12 dB. Double-click to reset.");
        pan.setTooltip ("Pan for mono IRs (equal power), balance for stereo IRs. Ignored on a mono track.");
        delay.setTooltip ("Delays this slot by up to 10 ms to time-align microphones. Shown in milliseconds and samples.");
        length.setTooltip ("Shortens the IR (percentage of its full length) with a short fade-out.");

        delay.slider.onValueChange = [this] { updateDelayReadout(); };

        addAndMakeVisible (phase);
        addAndMakeVisible (mute);
        addAndMakeVisible (solo);
        phase.attach (apvts, ids::slotParam (slot, ids::phase));
        mute.attach (apvts, ids::slotParam (slot, ids::mute));
        solo.attach (apvts, ids::slotParam (slot, ids::solo));
        phase.setTooltip ("Invert the polarity of this slot");
        mute.setTooltip ("Mute this slot");
        solo.setTooltip ("Solo this slot (several slots can be soloed)");

        // A click anywhere in the strip, including on its controls, makes it the
        // browser's target slot.
        addMouseListener (this, true);

        processor.getSlots().addListener (this);
        refresh();
    }

    SlotStrip::~SlotStrip()
    {
        processor.getSlots().removeListener (this);
    }

    void SlotStrip::setSelected (bool shouldBeSelected)
    {
        if (selected != shouldBeSelected)
        {
            selected = shouldBeSelected;
            repaint();
        }
    }

    void SlotStrip::setFavourite (bool isFavourite)
    {
        favourite = isFavourite;
        favouriteButton.setToggleState (isFavourite, juce::dontSendNotification);
    }

    void SlotStrip::setStepEnabled (bool canStep)
    {
        prevButton.setEnabled (canStep);
        nextButton.setEnabled (canStep);
    }

    void SlotStrip::setBrowserControlsVisible (bool shouldBeVisible)
    {
        browserControls = shouldBeVisible;

        for (auto* b : { &favouriteButton, &prevButton, &nextButton })
            b->setVisible (shouldBeVisible);

        resized();
        repaint();
    }

    void SlotStrip::setDragHover (bool shouldHover)
    {
        if (dragHover != shouldHover)
        {
            dragHover = shouldHover;
            repaint();
        }
    }

    void SlotStrip::refresh()
    {
        const auto i = processor.getSlots().getInfo (slot);
        const auto hasIR = i.status == SlotInfo::Status::ready;
        const auto missing = i.status == SlotInfo::Status::missing;

        if (hasIR)
        {
            name.setText (i.reference.name, juce::dontSendNotification);
            name.setColour (juce::Label::textColourId, colours::text);
            name.setTooltip (i.reference.path);

            juce::StringArray parts;
            parts.add (i.isResampled() ? formatRate (i.sourceRate) + juce::String::fromUTF8 (" \xe2\x86\x92 ") + formatRate (i.engineRate)
                                       : formatRate (i.sourceRate));
            parts.add (i.numChannels > 1 ? "stereo" : "mono");
            parts.add (formatLength (i.playedLengthSeconds > 0.0 ? i.playedLengthSeconds : i.sourceLengthSeconds));

            if (i.normalise)
                parts.add ((i.normalisationGainDb >= 0.0f ? "+" : "") + juce::String (i.normalisationGainDb, 1) + " dB norm");

            info.setText (parts.joinIntoString (juce::String::fromUTF8 ("  \xc2\xb7  ")), juce::dontSendNotification);
        }
        else if (missing)
        {
            name.setText ("missing: " + i.reference.name, juce::dontSendNotification);
            name.setColour (juce::Label::textColourId, colours::warning);
            name.setTooltip (i.reference.path);
            info.setText ("The file could not be found.", juce::dontSendNotification);
        }
        else
        {
            name.setText (i.loading ? "loading..." : "empty", juce::dontSendNotification);
            name.setColour (juce::Label::textColourId, colours::textDim);
            name.setTooltip ({});
            info.setText ("Drop a WAV or AIFF file here", juce::dontSendNotification);
        }

        notice.setColour (juce::Label::textColourId, i.error.isNotEmpty() ? colours::danger : colours::warning);
        notice.setText (i.error.isNotEmpty() ? i.error : i.notice, juce::dontSendNotification);

        loadButton.setButtonText (missing ? "Locate..." : "Load...");
        clearButton.setEnabled (hasIR || missing);
        favouriteButton.setEnabled (hasIR);
        normaliseButton.setEnabled (hasIR);
        autoTrimButton.setEnabled (hasIR);
        alignButton.setEnabled (hasIR);
        normaliseButton.setToggleState (hasIR && i.normalise, juce::dontSendNotification);
        autoTrimButton.setToggleState (hasIR && i.autoTrim, juce::dontSendNotification);

        updateDelayReadout();
    }

    void SlotStrip::updateDelayReadout()
    {
        const auto rate = processor.getEngine().isPrepared() ? processor.getEngine().getSampleRate() : 0.0;

        if (rate > 0.0)
            delayReadout.setText (juce::String (delay.slider.getValue() * 0.001 * rate, 1) + " smp", juce::dontSendNotification);
        else
            delayReadout.setText ({}, juce::dontSendNotification);
    }

    void SlotStrip::showSwapMenu()
    {
        juce::PopupMenu menu;
        menu.setLookAndFeel (&getLookAndFeel());

        for (int other = 0; other < kNumSlots; ++other)
            if (other != slot)
                menu.addItem (other + 1, "Swap with Slot " + juce::String (other + 1));

        juce::Component::SafePointer<SlotStrip> safe (this);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&swapButton),
                            [safe] (int result)
                            {
                                if (safe != nullptr && result > 0)
                                    safe->processor.swapSlots (safe->slot, result - 1);
                            });
    }

    //==============================================================================
    void SlotStrip::mouseDown (const juce::MouseEvent&)
    {
        if (onSelected)
            onSelected();
    }

    bool SlotStrip::isInterestedInFileDrag (const juce::StringArray& files)
    {
        for (const auto& f : files)
            if (isSupportedIRFile (juce::File (f)))
                return true;

        return false;
    }

    void SlotStrip::filesDropped (const juce::StringArray& files, int, int)
    {
        setDragHover (false);

        for (const auto& f : files)
        {
            if (isSupportedIRFile (juce::File (f)))
            {
                if (onFileChosen)
                    onFileChosen (juce::File (f));

                return;
            }
        }
    }

    bool SlotStrip::isInterestedInDragSource (const SourceDetails& details)
    {
        return details.description.toString().startsWith (browserDragPrefix);
    }

    void SlotStrip::itemDropped (const SourceDetails& details)
    {
        setDragHover (false);
        const auto path = details.description.toString().fromFirstOccurrenceOf (browserDragPrefix, false, false);

        if (juce::File::isAbsolutePath (path) && onFileChosen)
            onFileChosen (juce::File (path));
    }

    //==============================================================================
    void SlotStrip::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

        g.setColour (colours::panel);
        g.fillRoundedRectangle (bounds, 6.0f);

        // slot colour bar on the left
        g.setColour (colour);
        g.fillRoundedRectangle (bounds.withWidth (4.0f), 2.0f);

        if (dragHover)
        {
            g.setColour (colour.withAlpha (0.12f));
            g.fillRoundedRectangle (bounds, 6.0f);
        }

        const auto highlighted = dragHover || (selected && browserControls);
        g.setColour (highlighted ? colour : colours::outline);
        g.drawRoundedRectangle (bounds, 6.0f, highlighted ? 2.0f : 1.0f);

        if (selected && browserControls)
        {
            g.setColour (colour);
            g.setFont (font (10.0f, true));
            g.drawText ("BROWSER TARGET", getLocalBounds().reduced (12, 9).removeFromTop (14), juce::Justification::topRight);
        }
    }

    void SlotStrip::resized()
    {
        auto area = getLocalBounds().reduced (12, 8);
        area.removeFromLeft (2);

        title.setBounds (area.removeFromTop (16));

        auto nameRow = area.removeFromTop (24);

        if (browserControls)
        {
            favouriteButton.setBounds (nameRow.removeFromLeft (24).reduced (0, 1));
            nameRow.removeFromLeft (6);
            nextButton.setBounds (nameRow.removeFromRight (24).reduced (0, 1));
            nameRow.removeFromRight (3);
            prevButton.setBounds (nameRow.removeFromRight (24).reduced (0, 1));
            nameRow.removeFromRight (6);
        }

        name.setBounds (nameRow);

        info.setBounds (area.removeFromTop (18));
        notice.setBounds (area.removeFromTop (15));
        area.removeFromTop (3);

        auto buttons = area.removeFromTop (24);
        const auto gap = 5;
        const auto buttonWidth = (buttons.getWidth() - 4 * gap) / 5;

        for (auto* b : { &loadButton, &clearButton, &swapButton, &normaliseButton, &autoTrimButton })
        {
            b->setBounds (buttons.removeFromLeft (buttonWidth));
            buttons.removeFromLeft (gap);
        }

        area.removeFromTop (8);

        auto bottom = area.removeFromBottom (26);
        const auto toggleWidth = (bottom.getWidth() - 3 * gap) / 4;
        phase.setBounds (bottom.removeFromLeft (toggleWidth));
        bottom.removeFromLeft (gap);
        mute.setBounds (bottom.removeFromLeft (toggleWidth));
        bottom.removeFromLeft (gap);
        solo.setBounds (bottom.removeFromLeft (toggleWidth));
        bottom.removeFromLeft (gap);
        alignButton.setBounds (bottom);

        area.removeFromBottom (6);
        auto readoutRow = area.removeFromBottom (13);

        const auto knobWidth = area.getWidth() / 4;
        gain.setBounds (area.removeFromLeft (knobWidth));
        pan.setBounds (area.removeFromLeft (knobWidth));
        const auto delayBounds = area.removeFromLeft (knobWidth);
        delay.setBounds (delayBounds);
        length.setBounds (area);

        delayReadout.setBounds (readoutRow.withX (delayBounds.getX()).withWidth (delayBounds.getWidth()));
    }
}
