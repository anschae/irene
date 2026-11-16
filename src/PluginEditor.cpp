#include "PluginEditor.h"

namespace irene
{
    IReneEditor::IReneEditor (IReneProcessor& p)
        : AudioProcessorEditor (p)
    {
        setSize (480, 240);
    }

    void IReneEditor::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colour (0xff1b1d21));

        auto area = getLocalBounds().reduced (24);

        g.setColour (juce::Colour (0xffe8e8ea));
        g.setFont (juce::FontOptions (34.0f, juce::Font::bold));
        g.drawText (kPluginName, area.removeFromTop (60), juce::Justification::centred);

        g.setColour (juce::Colour (0xff9aa0a8));
        g.setFont (juce::FontOptions (15.0f));
        g.drawText (juce::String ("v") + kVersionString + "  -  DSP core build (user interface follows in milestone 3)",
                    area.removeFromTop (30),
                    juce::Justification::centred);
    }
}
