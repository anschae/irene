#include "ExportPanel.h"

#include "SlotStrip.h"

namespace irene::ui
{
    /** Renders and writes the file on its own thread. */
    class ExportPanel::Job final : public juce::Thread
    {
    public:
        Job (ExportSnapshot s, ExportOptions o, juce::File f)
            : juce::Thread ("IRene export"), snapshot (std::move (s)), options (o), file (std::move (f))
        {
            startThread (juce::Thread::Priority::normal);
        }

        ~Job() override
        {
            stopThread (15000);
        }

        void run() override
        {
            error = exportToFile (snapshot, options, file, [this] (float p)
            {
                progress = p;
                return ! threadShouldExit();
            });

            finished = true;
        }

        const ExportSnapshot snapshot;
        const ExportOptions options;
        const juce::File file;
        std::atomic<float> progress { 0.0f };
        std::atomic<bool> finished { false };
        juce::String error; // valid once finished
    };

    //==============================================================================
    ExportPanel::ExportPanel (IReneProcessor& p)
        : processor (p)
    {
        title.setText ("Export as single IR", juce::dontSendNotification);
        title.setFont (font (20.0f, true));
        addAndMakeVisible (title);

        hint.setText ("Renders everything you hear from the slots (IRs, length, normalisation, delay, filters, phase, gain, pan, "
                      "mute and solo) into one impulse response file. The dry/wet mix is ignored.",
                      juce::dontSendNotification);
        hint.setFont (font (12.5f));
        hint.setColour (juce::Label::textColourId, colours::textDim);
        hint.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (hint);

        const auto addLabel = [this] (juce::Label& label, const juce::String& text)
        {
            label.setText (text, juce::dontSendNotification);
            label.setFont (font (13.0f));
            label.setColour (juce::Label::textColourId, colours::text);
            addAndMakeVisible (label);
        };

        addLabel (channelsLabel, "Channels");
        addLabel (rateLabel, "Sample rate");
        addLabel (formatLabel, "Format");
        addLabel (lengthLabel, "Length");
        addLabel (msLabel, "ms");

        includeFilters.setTooltip ("If off, only the slots are exported (each with its own filter block)");
        includeOutputGain.setTooltip ("If on, the Output knob's gain is baked into the IR");
        normalise.setTooltip ("Scales the exported IR so that its highest sample is at -0.3 dBFS");

        channels.addItem ("Stereo", 1);
        channels.addItem ("Mono (as on a mono track: pan ignored)", 2);
        channels.setTooltip ("Stereo keeps left and right separate. Mono gives what the plugin produces on a mono track.");

        rate.addItem ("Session rate", 1);
        rate.addItem ("44.1 kHz", 2);
        rate.addItem ("48 kHz", 3);
        rate.addItem ("96 kHz", 4);
        rate.setTooltip ("The IRs are converted from their original files straight to this rate");

        format.addItem ("24-bit WAV", 1);
        format.addItem ("32-bit float WAV", 2);
        format.setTooltip ("32-bit float can hold peaks above 0 dBFS; 24-bit can't");

        lengthMode.addItem ("Auto (until the tail is 90 dB down)", 1);
        lengthMode.addItem ("Fixed length", 2);
        lengthMode.setTooltip ("The end of the IR always gets a short fade-out");
        lengthMode.onChange = [this] { refresh(); };

        lengthMs.setInputRestrictions (6, "0123456789.");
        lengthMs.setFont (font (13.0f));
        lengthMs.setTooltip ("Length of the exported IR in milliseconds (1 to 12000)");

        for (auto* c : std::initializer_list<juce::Component*> { &includeFilters, &includeOutputGain, &normalise, &channels, &rate, &format,
                                                                  &lengthMode, &lengthMs, &exportButton, &closeButton, &status })
            addAndMakeVisible (c);

        addChildComponent (progressBar);
        progressBar.setPercentageDisplay (true);
        progressBar.setColour (juce::ProgressBar::backgroundColourId, colours::background);
        progressBar.setColour (juce::ProgressBar::foregroundColourId, colours::accent);

        status.setFont (font (12.5f));
        status.setJustificationType (juce::Justification::topLeft);
        status.setMinimumHorizontalScale (0.7f);

        exportButton.setColour (juce::TextButton::buttonColourId, colours::accent);
        exportButton.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
        exportButton.setTooltip ("Choose where to save the file and start the export");
        exportButton.onClick = [this] { chooseFileAndExport(); };
        closeButton.onClick = [this]
        {
            if (job != nullptr)
            {
                // "Cancel": stop the render; nothing is written
                job.reset();
                stopTimer();
                setStatus ("Cancelled.", colours::textDim);
                refresh();
            }
            else if (onClose)
            {
                onClose();
            }
        };

        // start from the options used last time in this instance
        const auto& o = processor.exportOptions;
        includeFilters.setToggleState (o.includeInputOutputFilters, juce::dontSendNotification);
        includeOutputGain.setToggleState (o.includeOutputGain, juce::dontSendNotification);
        normalise.setToggleState (o.normalisePeak, juce::dontSendNotification);
        channels.setSelectedId (o.stereo ? 1 : 2, juce::dontSendNotification);
        rate.setSelectedId (juce::approximatelyEqual (o.sampleRate, 44100.0) ? 2 : (juce::approximatelyEqual (o.sampleRate, 48000.0) ? 3 : (juce::approximatelyEqual (o.sampleRate, 96000.0) ? 4 : 1)),
                            juce::dontSendNotification);
        format.setSelectedId (o.bitDepth == 32 ? 2 : 1, juce::dontSendNotification);
        lengthMode.setSelectedId (o.autoLength ? 1 : 2, juce::dontSendNotification);
        lengthMs.setText (juce::String (juce::roundToInt (o.lengthMs)), false);

        setWantsKeyboardFocus (true);
        setVisible (false);
        refresh();
    }

    ExportPanel::~ExportPanel()
    {
        stopTimer();
        job.reset(); // cancels a running export
    }

    ExportOptions ExportPanel::readOptions() const
    {
        ExportOptions o;
        o.includeInputOutputFilters = includeFilters.getToggleState();
        o.includeOutputGain = includeOutputGain.getToggleState();
        o.normalisePeak = normalise.getToggleState();
        o.stereo = channels.getSelectedId() != 2;

        switch (rate.getSelectedId())
        {
            case 2:  o.sampleRate = 44100.0; break;
            case 3:  o.sampleRate = 48000.0; break;
            case 4:  o.sampleRate = 96000.0; break;
            default: o.sampleRate = 0.0; break;
        }

        o.bitDepth = format.getSelectedId() == 2 ? 32 : 24;
        o.autoLength = lengthMode.getSelectedId() != 2;
        o.lengthMs = juce::jlimit (ExportOptions::kMinLengthMs, ExportOptions::kMaxLengthMs, lengthMs.getText().getDoubleValue());
        return o;
    }

    void ExportPanel::refresh()
    {
        const auto busy = job != nullptr;
        const auto fixed = lengthMode.getSelectedId() == 2;

        lengthMs.setEnabled (fixed && ! busy);
        msLabel.setEnabled (fixed);
        exportButton.setEnabled (! busy);
        exportButton.setButtonText (busy ? "Exporting..." : "Export...");
        closeButton.setButtonText (busy ? "Cancel" : "Close");
        progressBar.setVisible (busy);

        const auto engineRate = processor.getEngine().isPrepared() ? processor.getEngine().getSampleRate() : 48000.0;
        const auto sessionText = "Session rate (" + formatRate (engineRate) + "Hz)";
        rate.changeItemText (1, sessionText);

        if (rate.getSelectedId() == 1)
            rate.setText (sessionText, juce::dontSendNotification);
    }

    void ExportPanel::setStatus (const juce::String& text, juce::Colour colour)
    {
        status.setText (text, juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, colour);
    }

    void ExportPanel::visibilityChanged()
    {
        if (isVisible())
        {
            if (job == nullptr)
                setStatus ({}, colours::textDim);

            refresh();
        }
    }

    void ExportPanel::chooseFileAndExport()
    {
        processor.exportOptions = readOptions();

        if (! ExportSnapshot::capture (processor).hasAnyIR())
        {
            setStatus ("There is nothing to export: no slot has an IR loaded.", colours::warning);
            return;
        }

        auto start = processor.lastExportFolder;

        if (! start.isDirectory())
            start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

        chooser = std::make_unique<juce::FileChooser> ("Export as single IR", start.getChildFile ("IRene export.wav"), "*.wav");
        juce::Component::SafePointer<ExportPanel> safe (this);

        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe] (const juce::FileChooser& fc)
                              {
                                  if (safe == nullptr || fc.getResult() == juce::File())
                                      return;

                                  safe->startExport (fc.getResult().withFileExtension ("wav"));
                              });
    }

    void ExportPanel::startExport (const juce::File& destination)
    {
        if (job != nullptr)
            return;

        processor.exportOptions = readOptions();
        processor.lastExportFolder = destination.getParentDirectory();

        // The snapshot is taken here, on the message thread; the render itself
        // doesn't touch the plugin again.
        job = std::make_unique<Job> (ExportSnapshot::capture (processor), processor.exportOptions, destination);
        progressValue = 0.0;
        setStatus ("Rendering...", colours::textDim);
        refresh();
        startTimerHz (20);
    }

    void ExportPanel::timerCallback()
    {
        if (job == nullptr)
        {
            stopTimer();
            return;
        }

        progressValue = job->progress.load();

        if (! job->finished.load())
            return;

        const auto error = job->error;
        const auto file = job->file;
        job.reset();
        stopTimer();

        if (error.isEmpty())
            setStatus ("Saved: " + file.getFullPathName(), colours::ok);
        else
            setStatus (error, error == "Cancelled." ? colours::textDim : colours::danger);

        refresh();
    }

    bool ExportPanel::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey)
        {
            closeButton.triggerClick();
            return true;
        }

        return false;
    }

    void ExportPanel::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colours::black.withAlpha (0.6f));
        g.setColour (colours::panel);
        g.fillRoundedRectangle (box.toFloat(), 8.0f);
        g.setColour (colours::outline);
        g.drawRoundedRectangle (box.toFloat(), 8.0f, 1.0f);
    }

    void ExportPanel::resized()
    {
        box = getLocalBounds().withSizeKeepingCentre (juce::jmin (560, getWidth() - 40), juce::jmin (470, getHeight() - 30));
        auto area = box.reduced (20, 16);

        title.setBounds (area.removeFromTop (28));
        hint.setBounds (area.removeFromTop (50));
        area.removeFromTop (4);

        includeFilters.setBounds (area.removeFromTop (24));
        includeOutputGain.setBounds (area.removeFromTop (24));
        area.removeFromTop (8);

        const auto row = [&area] (juce::Label& label, juce::Component& control, int controlWidth)
        {
            auto r = area.removeFromTop (24);
            label.setBounds (r.removeFromLeft (110));
            control.setBounds (r.removeFromLeft (controlWidth));
            area.removeFromTop (6);
            return r;
        };

        row (channelsLabel, channels, 300);
        row (rateLabel, rate, 200);
        row (formatLabel, format, 200);
        auto rest = row (lengthLabel, lengthMode, 270);
        rest.removeFromLeft (8);
        lengthMs.setBounds (rest.removeFromLeft (64));
        rest.removeFromLeft (4);
        msLabel.setBounds (rest.removeFromLeft (30));

        normalise.setBounds (area.removeFromTop (24));
        area.removeFromTop (8);

        auto buttons = area.removeFromBottom (30);
        exportButton.setBounds (buttons.removeFromRight (130));
        buttons.removeFromRight (10);
        closeButton.setBounds (buttons.removeFromRight (100));
        area.removeFromBottom (8);

        progressBar.setBounds (area.removeFromTop (20));
        area.removeFromTop (4);
        status.setBounds (area);
    }
}
