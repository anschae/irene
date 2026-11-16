#pragma once

#include "PluginProcessor.h"
#include "Widgets.h"

namespace irene::ui
{
    juce::String formatRate (double sampleRate);     // "44.1k"
    juce::String formatLength (double seconds);      // "185 ms" / "1.25 s"

    /** The controls of one IR slot. Created in a loop over kNumSlots. */
    class SlotStrip final : public juce::Component,
                            public juce::FileDragAndDropTarget,
                            public juce::DragAndDropTarget,
                            private Broadcaster::Listener
    {
    public:
        SlotStrip (IReneProcessor&, int slotIndex);
        ~SlotStrip() override;

        /** Set by the editor. */
        std::function<void()> onBrowse;                       // "Load..." clicked
        std::function<void (const juce::File&)> onFileChosen; // file dropped onto the strip
        std::function<void()> onSelected;                     // strip clicked
        std::function<void (int direction)> onStep;           // prev/next IR (-1 / +1)
        std::function<void()> onToggleFavourite;

        void setSelected (bool shouldBeSelected);
        void setFavourite (bool isFavourite);
        void setStepEnabled (bool canStep);

        /** Shows or hides the controls that need the IR browser (favourite, prev/next). */
        void setBrowserControlsVisible (bool shouldBeVisible);
        void refresh();

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;

        bool isInterestedInFileDrag (const juce::StringArray& files) override;
        void filesDropped (const juce::StringArray& files, int, int) override;
        void fileDragEnter (const juce::StringArray&, int, int) override { setDragHover (true); }
        void fileDragExit (const juce::StringArray&) override { setDragHover (false); }

        // drags from the built-in browser (description = full path of the file)
        bool isInterestedInDragSource (const SourceDetails&) override;
        void itemDropped (const SourceDetails&) override;
        void itemDragEnter (const SourceDetails&) override { setDragHover (true); }
        void itemDragExit (const SourceDetails&) override { setDragHover (false); }

        /** Prefix that marks a drag from the IR browser. */
        static inline const juce::String browserDragPrefix { "irene-file:" };

    private:
        void broadcasterChanged (Broadcaster*) override { refresh(); }
        void setDragHover (bool);
        void updateDelayReadout();
        void showSwapMenu();

        IReneProcessor& processor;
        const int slot;
        const juce::Colour colour;
        bool selected = false, dragHover = false, favourite = false, browserControls = true;

        juce::Label title, name, info, notice, delayReadout;
        juce::TextButton favouriteButton { juce::String::fromUTF8 ("\xe2\x98\x85") }, prevButton { "<" }, nextButton { ">" };
        juce::TextButton loadButton { "Load..." }, clearButton { "Clear" }, swapButton { "Swap" };
        juce::TextButton normaliseButton { "Norm" }, autoTrimButton { "Trim" };
        Knob gain { "GAIN" }, pan { "PAN" }, delay { "DELAY" }, length { "LENGTH" };
        ParamToggle phase { juce::String::fromUTF8 ("\xc3\x98"), colours::accent };
        ParamToggle mute { "M", colours::mute };
        ParamToggle solo { "S", colours::solo };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlotStrip)
    };
}
