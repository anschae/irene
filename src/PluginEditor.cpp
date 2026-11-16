#include "PluginEditor.h"

namespace irene
{
    using namespace ui;

    //==============================================================================
    IReneEditor::Content::Content (IReneEditor& e, IReneProcessor& p)
        : editor (e), filterEditor (p), globalSection (p)
    {
        setComponentID (kContentComponentId);

        title.setText (kPluginName, juce::dontSendNotification);
        title.setFont (font (22.0f, true));
        title.setColour (juce::Label::textColourId, colours::text);
        addAndMakeVisible (title);

        version.setText (juce::String ("v") + kVersionString, juce::dontSendNotification);
        styleCaption (version, 11.0f, colours::textDim, juce::Justification::bottomLeft);
        addAndMakeVisible (version);

        settingsButton.setTooltip ("Favourite folders, presets folder, loading defaults and UI scale");
        settingsButton.onClick = [this]
        {
            settingsPanel.setVisible (true);
            settingsPanel.toFront (true);
        };
        addAndMakeVisible (settingsButton);

        addAndMakeVisible (filterEditor);
        addAndMakeVisible (globalSection);

        for (int i = 0; i < kNumSlots; ++i)
            addAndMakeVisible (strips.add (new SlotStrip (p, i)));

        addChildComponent (settingsPanel);
        settingsPanel.onClose = [this] { settingsPanel.setVisible (false); };

        addChildComponent (confirm);
    }

    void IReneEditor::Content::paint (juce::Graphics& g)
    {
        g.fillAll (colours::background);
    }

    void IReneEditor::Content::resized()
    {
        auto area = getLocalBounds().reduced (10);

        auto header = area.removeFromTop (30);
        title.setBounds (header.removeFromLeft (74));
        version.setBounds (header.removeFromLeft (60).withTrimmedBottom (5));
        settingsButton.setBounds (header.removeFromRight (90).reduced (0, 2));
        area.removeFromTop (8);

        globalSection.setBounds (area.removeFromRight (130));
        area.removeFromRight (8);

        filterEditor.setBounds (area.removeFromTop (176));
        area.removeFromTop (8);

        const auto gap = 8;
        const auto stripWidth = (area.getWidth() - (kNumSlots - 1) * gap) / kNumSlots;

        for (auto* strip : strips)
        {
            strip->setBounds (area.removeFromLeft (stripWidth));
            area.removeFromLeft (gap);
        }

        settingsPanel.setBounds (getLocalBounds());
        confirm.setBounds (getLocalBounds());
    }

    //==============================================================================
    IReneEditor::IReneEditor (IReneProcessor& p)
        : AudioProcessorEditor (p), plugin (p), content (*this, p)
    {
        setLookAndFeel (&lookAndFeel);
        tooltips.setLookAndFeel (&lookAndFeel);
        addAndMakeVisible (content);

        for (int i = 0; i < kNumSlots; ++i)
        {
            auto* strip = content.strips[i];
            strip->onBrowse = [this, i] { browseForFile (i); };
            strip->onFileChosen = [this, i] (const juce::File& f) { requestLoad (i, f); };
            strip->onSelected = [this, i] { selectTargetSlot (i); };
            strip->setBrowserControlsVisible (false); // arrive with the IR browser
        }

        content.filterEditor.selectTab (plugin.uiState.filterTab.load());
        content.filterEditor.onTabChanged = [this] (int tab) { plugin.uiState.filterTab = tab; };
        selectTargetSlot (plugin.uiState.targetSlot.load());

        setResizable (true, true);
        getConstrainer()->setFixedAspectRatio (static_cast<double> (kBaseWidth) / kBaseHeight);
        setResizeLimits (juce::roundToInt (kBaseWidth * GlobalSettings::kMinUiScale),
                         juce::roundToInt (kBaseHeight * GlobalSettings::kMinUiScale),
                         juce::roundToInt (kBaseWidth * GlobalSettings::kMaxUiScale),
                         juce::roundToInt (kBaseHeight * GlobalSettings::kMaxUiScale));

        plugin.getGlobalSettings().addChangeListener (this);
        applyScale (plugin.getGlobalSettings().get().uiScale);
        constructed = true;
    }

    IReneEditor::~IReneEditor()
    {
        stopTimer();
        plugin.getGlobalSettings().removeChangeListener (this);
        tooltips.setLookAndFeel (nullptr);
        setLookAndFeel (nullptr);
    }

    void IReneEditor::paint (juce::Graphics& g)
    {
        g.fillAll (colours::background);
    }

    void IReneEditor::applyScale (float scale)
    {
        scale = juce::jlimit (GlobalSettings::kMinUiScale, GlobalSettings::kMaxUiScale, scale);
        const juce::ScopedValueSetter<bool> guard (applyingScale, true);
        currentScale = scale;
        setSize (juce::roundToInt (kBaseWidth * scale), juce::roundToInt (kBaseHeight * scale));
        resized();
    }

    void IReneEditor::resized()
    {
        // The window may have been resized by dragging its corner: derive the
        // scale from the new width (the aspect ratio is fixed).
        const auto scale = static_cast<float> (getWidth()) / static_cast<float> (kBaseWidth);

        content.setTransform (juce::AffineTransform::scale (scale));
        content.setBounds (0, 0, kBaseWidth, kBaseHeight);

        if (constructed && ! applyingScale && getWidth() > 0 && std::abs (scale - currentScale) > 0.001f)
        {
            currentScale = scale;
            startTimer (400); // remember the new size once the drag has settled
        }
    }

    void IReneEditor::timerCallback()
    {
        stopTimer();
        const auto scale = currentScale;
        plugin.getGlobalSettings().update ([scale] (auto& d) { d.uiScale = scale; });
    }

    void IReneEditor::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        const auto wanted = plugin.getGlobalSettings().get().uiScale;

        if (std::abs (wanted - currentScale) > 0.001f && ! isTimerRunning())
            applyScale (wanted);
    }

    //==============================================================================
    void IReneEditor::selectTargetSlot (int slot)
    {
        slot = juce::jlimit (0, kNumSlots - 1, slot);
        plugin.uiState.targetSlot = slot;

        for (int i = 0; i < kNumSlots; ++i)
            content.strips[i]->setSelected (i == slot);
    }

    void IReneEditor::browseForFile (int slot)
    {
        auto start = lastBrowseFolder;

        if (const auto current = plugin.getSlots().getInfo (slot).reference.path;
            juce::File::isAbsolutePath (current) && juce::File (current).getParentDirectory().isDirectory())
            start = juce::File (current).getParentDirectory();

        if (! start.isDirectory())
            start = juce::File::getSpecialLocation (juce::File::userHomeDirectory);

        chooser = std::make_unique<juce::FileChooser> ("Load an impulse response into slot " + juce::String (slot + 1),
                                                       start, "*.wav;*.wave;*.aif;*.aiff");
        juce::Component::SafePointer<IReneEditor> safe (this);

        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe, slot] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();

                                  if (safe == nullptr || ! file.existsAsFile())
                                      return;

                                  safe->lastBrowseFolder = file.getParentDirectory();
                                  safe->requestLoad (slot, file);
                              });
    }

    void IReneEditor::requestLoad (int slot, const juce::File& file)
    {
        const auto engineRate = plugin.getEngine().isPrepared() ? plugin.getEngine().getSampleRate() : 0.0;

        if (plugin.getGlobalSettings().get().askBeforeResampling && engineRate > 0.0)
        {
            const auto info = probeIRFile (file);

            if (info.valid && std::abs (info.sampleRate - engineRate) > 0.5)
            {
                juce::Component::SafePointer<IReneEditor> safe (this);

                content.confirm.ask ("Resample this IR?",
                                     file.getFileName() + " is " + formatRate (info.sampleRate) + "Hz, but the session runs at "
                                         + formatRate (engineRate) + "Hz. It has to be converted to be played correctly.",
                                     "Resample", "Cancel",
                                     [safe, slot, file] (bool confirmed)
                                     {
                                         if (safe != nullptr && confirmed)
                                             safe->plugin.getSlots().loadFile (slot, file);
                                     });
                return;
            }
        }

        plugin.getSlots().loadFile (slot, file);
    }
}
