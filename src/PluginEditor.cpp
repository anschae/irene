#include "PluginEditor.h"

namespace irene
{
    using namespace ui;

    //==============================================================================
    IReneEditor::Content::Content (IReneEditor& e, IReneProcessor& p)
        : editor (e), presetBar (p, confirm), browser (p, confirm), browserResizer (e), display (p), filterEditor (p), globalSection (p),
          settingsPanel (confirm), exportPanel (p), notesPanel (p)
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
        addAndMakeVisible (presetBar);

        browserButton.setClickingTogglesState (true);
        browserButton.setTooltip ("Show or hide the IR browser");
        addAndMakeVisible (browserButton);
        addAndMakeVisible (browser);
        addAndMakeVisible (browserResizer);

        browser.onOpenSettings = [this] { settingsButton.triggerClick(); };

        addAndMakeVisible (display);
        addAndMakeVisible (filterEditor);
        addAndMakeVisible (globalSection);

        for (int i = 0; i < kNumSlots; ++i)
            addAndMakeVisible (strips.add (new SlotStrip (p, i)));

        addChildComponent (settingsPanel);
        settingsPanel.onClose = [this] { settingsPanel.setVisible (false); };

        exportButton.setTooltip ("Render the current setup into a single impulse response file");
        exportButton.onClick = [this]
        {
            exportPanel.setVisible (true);
            exportPanel.toFront (true);
        };
        addAndMakeVisible (exportButton);

        addChildComponent (exportPanel);
        exportPanel.onClose = [this] { exportPanel.setVisible (false); };

        notesButton.setTooltip ("Notes about this sound, saved with the project");
        notesButton.setColour (juce::TextButton::buttonOnColourId, colours::accent.withAlpha (0.45f));
        notesButton.onClick = [this]
        {
            notesPanel.setVisible (true);
            notesPanel.toFront (true);
        };
        addAndMakeVisible (notesButton);

        addChildComponent (notesPanel);
        notesPanel.onClose = [this] { notesPanel.setVisible (false); };

        addChildComponent (confirm);
    }

    void IReneEditor::Content::paint (juce::Graphics& g)
    {
        g.fillAll (colours::background);
    }

    void IReneEditor::Content::resized()
    {
        auto area = getLocalBounds().reduced (kMargin);

        auto header = area.removeFromTop (30);
        title.setBounds (header.removeFromLeft (74));
        version.setBounds (header.removeFromLeft (60).withTrimmedBottom (5));
        settingsButton.setBounds (header.removeFromRight (90).reduced (0, 2));
        header.removeFromRight (6);
        browserButton.setBounds (header.removeFromRight (90).reduced (0, 2));
        header.removeFromRight (6);
        exportButton.setBounds (header.removeFromRight (96).reduced (0, 2));
        header.removeFromRight (6);
        notesButton.setBounds (header.removeFromRight (70).reduced (0, 2));
        header.removeFromLeft (20);
        header.removeFromRight (20);
        presetBar.setBounds (header.removeFromLeft (juce::jmin (header.getWidth(), 520)).reduced (0, 2));
        area.removeFromTop (8);

        browser.setVisible (editor.isBrowserOpen());
        browserResizer.setVisible (editor.isBrowserOpen());

        if (editor.isBrowserOpen())
        {
            browser.setBounds (area.removeFromLeft (editor.getBrowserWidth()));
            browserResizer.setBounds (area.removeFromLeft (kGap).withTrimmedTop (20).withTrimmedBottom (20));
        }

        globalSection.setBounds (area.removeFromRight (kGlobalWidth));
        area.removeFromRight (kGap);

        display.setBounds (area.removeFromTop (kDisplayHeight));
        area.removeFromTop (8);

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
        exportPanel.setBounds (getLocalBounds());
        notesPanel.setBounds (getLocalBounds());
        confirm.setBounds (getLocalBounds());
    }

    void IReneEditor::BrowserResizer::paint (juce::Graphics& g)
    {
        if (! isMouseOverOrDragging())
            return;

        // a grip: three dots, shown while the mouse is over the strip
        g.setColour (colours::accent.withAlpha (isMouseButtonDown() ? 0.9f : 0.6f));
        const auto x = static_cast<float> (getWidth()) * 0.5f;
        const auto y = static_cast<float> (getHeight()) * 0.5f;

        for (int i = -1; i <= 1; ++i)
            g.fillEllipse (x - 1.5f, y + static_cast<float> (i) * 7.0f - 1.5f, 3.0f, 3.0f);

        g.setColour (colours::accent.withAlpha (0.25f));
        g.fillRect (getLocalBounds().withSizeKeepingCentre (2, getHeight()));
    }

    void IReneEditor::setBrowserWidth (int width, bool remember)
    {
        width = juce::jlimit (GlobalSettings::kMinBrowserWidth, GlobalSettings::kMaxBrowserWidth, width);

        if (width != browserWidth)
        {
            browserWidth = width;
            applyScale (currentScale);
        }

        if (remember)
            plugin.getGlobalSettings().update ([width] (auto& d) { d.browserWidth = width; });
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
            strip->onFileChosen = [this, i] (const juce::File& f) { loadFromList (i, f); };
            strip->onSelected = [this, i] { selectTargetSlot (i); };
            strip->onStep = [this, i] (int direction) { stepSlot (i, direction); };
            strip->onToggleFavourite = [this, i] { toggleSlotFavourite (i); };
            strip->onAlign = [this, i] { alignToSlot (i); };
            strip->onGoBack = [this, i] { goBack (i); };
        }

        content.browser.onLoadIntoSlot = [this] (int slot, const juce::File& file, const juce::StringArray& list)
        {
            plugin.setSlotListContext (slot, list);
            requestLoad (slot, file);
        };

        content.browser.onAuditionSoloChanged = [this] { auditionSoloChanged(); };
        content.notesPanel.onNotesChanged = [this] (bool) { updateNotesButton(); };

        content.browser.onFileChosen = [this] (const juce::File& file, const juce::StringArray& list)
        {
            const auto target = plugin.uiState.targetSlot.load();
            plugin.setSlotListContext (target, list);
            requestLoad (target, file);
        };

        content.browserButton.onClick = [this]
        {
            const auto open = content.browserButton.getToggleState();
            plugin.getGlobalSettings().update ([open] (auto& d) { d.browserOpen = open; });
        };

        const auto showBlock = [this] (int tab)
        {
            const auto colour = FilterEditor::tabColour (tab);
            content.display.setHighlightedBlock (tab, colour == colours::text ? colours::accent : colour);
        };

        content.filterEditor.selectTab (plugin.uiState.filterTab.load());
        showBlock (content.filterEditor.getSelectedTab());
        content.filterEditor.onTabChanged = [this, showBlock] (int tab)
        {
            plugin.uiState.filterTab = tab;
            showBlock (tab);
        };
        selectTargetSlot (plugin.uiState.targetSlot.load());

        setResizable (true, true);

        plugin.getGlobalSettings().addListener (this);
        plugin.getLibrary().addListener (this);
        plugin.getSlots().addListener (this);

        browserOpen = plugin.getGlobalSettings().get().browserOpen;
        browserWidth = plugin.getGlobalSettings().get().browserWidth;
        content.browserButton.setToggleState (browserOpen, juce::dontSendNotification);
        applyScale (plugin.getGlobalSettings().get().uiScale);
        refreshStrips();
        updateNotesButton();
        constructed = true;
    }

    IReneEditor::~IReneEditor()
    {
        stopTimer();

        // "Solo target" belongs to the open window: never leave the plugin
        // playing a single slot with nothing on screen to say so.
        plugin.auditionSolo = false;

        plugin.getSlots().removeListener (this);
        plugin.getLibrary().removeListener (this);
        plugin.getGlobalSettings().removeListener (this);
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

        const auto baseWidth = getBaseWidth();
        getConstrainer()->setFixedAspectRatio (static_cast<double> (baseWidth) / kBaseHeight);
        setResizeLimits (juce::roundToInt (static_cast<float> (baseWidth) * GlobalSettings::kMinUiScale),
                         juce::roundToInt (kBaseHeight * GlobalSettings::kMinUiScale),
                         juce::roundToInt (static_cast<float> (baseWidth) * GlobalSettings::kMaxUiScale),
                         juce::roundToInt (kBaseHeight * GlobalSettings::kMaxUiScale));

        setSize (juce::roundToInt (static_cast<float> (baseWidth) * scale), juce::roundToInt (kBaseHeight * scale));
        resized();
    }

    void IReneEditor::resized()
    {
        // The window may have been resized by dragging its corner: derive the
        // scale from the new width (the aspect ratio is fixed).
        const auto scale = static_cast<float> (getWidth()) / static_cast<float> (getBaseWidth());

        content.setTransform (juce::AffineTransform::scale (scale));
        content.setBounds (0, 0, getBaseWidth(), kBaseHeight);
        content.resized();

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

    void IReneEditor::broadcasterChanged (Broadcaster* source)
    {
        if (source == &plugin.getGlobalSettings())
            applyGlobalSettings();
        else
            refreshStrips();
    }

    void IReneEditor::applyGlobalSettings()
    {
        const auto d = plugin.getGlobalSettings().get();
        const auto browserChanged = d.browserOpen != browserOpen;

        browserOpen = d.browserOpen;
        content.browserButton.setToggleState (browserOpen, juce::dontSendNotification);

        if (! browserOpen && plugin.auditionSolo.load())
        {
            plugin.auditionSolo = false;
            content.browser.syncAuditionSolo();
            auditionSoloChanged();
        }

        if (browserChanged || (std::abs (d.uiScale - currentScale) > 0.001f && ! isTimerRunning()))
            applyScale (browserChanged ? currentScale : d.uiScale);
    }

    void IReneEditor::auditionSoloChanged()
    {
        for (auto* strip : content.strips)
            strip->repaint();
    }

    void IReneEditor::updateNotesButton()
    {
        content.notesButton.setToggleState (plugin.getNotes().isNotEmpty(), juce::dontSendNotification);
        content.notesButton.setButtonText (plugin.getNotes().isNotEmpty() ? juce::String::fromUTF8 ("Notes \xe2\x80\xa2") : juce::String ("Notes"));
    }

    void IReneEditor::goBack (int slot)
    {
        const auto restored = plugin.getSlots().goBack (slot);

        if (slot == plugin.uiState.targetSlot.load() && restored.path.isNotEmpty())
            content.browser.showAsCurrent (restored.path);
    }

    void IReneEditor::refreshStrips()
    {
        updateNotesButton(); // the project may have been reloaded
        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto info = plugin.getSlots().getInfo (i);
            content.strips[i]->setFavourite (info.status == SlotInfo::Status::ready && plugin.getLibrary().isFavourite (info.reference.hash));
            content.strips[i]->setStepEnabled (info.status == SlotInfo::Status::ready);
        }
    }

    void IReneEditor::loadFromList (int slot, const juce::File& file)
    {
        // Dropped onto a slot: if the file is in the browser's current list,
        // prev/next will step through that list; otherwise through its folder.
        const auto list = content.browser.getCurrentFileList();
        plugin.setSlotListContext (slot, list.contains (file.getFullPathName()) ? list : juce::StringArray());
        requestLoad (slot, file);
    }

    void IReneEditor::stepSlot (int slot, int direction)
    {
        const auto file = plugin.getAdjacentFile (slot, direction);

        if (file == juce::File())
            return;

        requestLoad (slot, file);

        if (slot == plugin.uiState.targetSlot.load())
            content.browser.showAsCurrent (file.getFullPathName());
    }

    void IReneEditor::alignToSlot (int slot)
    {
        const auto result = plugin.alignSlots (slot);

        content.confirm.inform (result.ok ? "Aligned to Slot " + juce::String (slot + 1) : juce::String ("Nothing was changed"),
                                result.message);
    }

    void IReneEditor::toggleSlotFavourite (int slot)
    {
        if (const auto source = plugin.getSlots().getSource (slot))
        {
            const auto entry = IRFileEntry::fromSource (*source);
            plugin.getLibrary().setFavourite (entry, ! plugin.getLibrary().isFavourite (entry.hash));
        }
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
