#pragma once

#include "PluginProcessor.h"
#include "Widgets.h"
#include "export/Exporter.h"

namespace irene::ui
{
    /** "Export as single IR": options, a save dialog, and a progress bar while
        the IR is rendered on a background thread. Shown as an overlay. */
    class ExportPanel final : public juce::Component,
                              private juce::Timer
    {
    public:
        explicit ExportPanel (IReneProcessor&);
        ~ExportPanel() override;

        std::function<void()> onClose;

        void paint (juce::Graphics&) override;
        void resized() override;
        void visibilityChanged() override;
        void mouseDown (const juce::MouseEvent&) override {}
        bool keyPressed (const juce::KeyPress&) override;

        /** The options as currently set in the panel. */
        ExportOptions readOptions() const;

        /** Starts an export to the given file (normally called after the save dialog). */
        void startExport (const juce::File& destination);
        bool isExporting() const noexcept { return job != nullptr; }

    private:
        class Job;

        void timerCallback() override;
        void chooseFileAndExport();
        void refresh();
        void setStatus (const juce::String& text, juce::Colour colour);

        IReneProcessor& processor;
        juce::Rectangle<int> box;

        juce::Label title, hint, channelsLabel, rateLabel, formatLabel, lengthLabel, msLabel, status;
        juce::ToggleButton includeFilters { "Include the input and output filter blocks" };
        juce::ToggleButton includeOutputGain { "Include the output gain" };
        juce::ToggleButton normalise { "Normalise the peak to -0.3 dBFS" };
        juce::ComboBox channels, rate, format, lengthMode;
        juce::TextEditor lengthMs;
        juce::TextButton exportButton { "Export..." }, closeButton { "Close" };

        double progressValue = 0.0;
        juce::ProgressBar progressBar { progressValue };

        std::unique_ptr<juce::FileChooser> chooser;
        std::unique_ptr<Job> job;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExportPanel)
    };
}
