#include "SettingsPanel.h"

namespace irene::ui
{
    namespace
    {
        void styleHeading (juce::Label& label, const juce::String& text)
        {
            label.setText (text, juce::dontSendNotification);
            label.setFont (font (12.0f, true));
            label.setColour (juce::Label::textColourId, colours::textDim);
        }
    }

    SettingsPanel::SettingsPanel()
    {
        title.setText ("Settings", juce::dontSendNotification);
        title.setFont (font (20.0f, true));
        addAndMakeVisible (title);

        hint.setText ("These settings are shared by all IRene instances, in every DAW.", juce::dontSendNotification);
        styleCaption (hint, 12.0f, colours::textDim, juce::Justification::centredLeft);
        addAndMakeVisible (hint);

        styleHeading (loadingHeading, "IR LOADING");
        styleHeading (scaleHeading, "UI SCALE");
        styleHeading (presetsHeading, "PRESETS FOLDER");
        styleHeading (foldersHeading, "FAVOURITE FOLDERS (shown at the top of the IR browser)");

        for (auto* l : { &loadingHeading, &scaleHeading, &presetsHeading, &foldersHeading, &presetsPath })
            addAndMakeVisible (l);

        presetsPath.setFont (font (13.0f));
        presetsPath.setMinimumHorizontalScale (0.6f);

        closeButton.setTooltip ("Close the settings (changes are saved immediately)");
        closeButton.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (closeButton);

        askResample.setTooltip ("If on, a dialog asks before an IR with a different sample rate is converted. If off, IRs are always converted automatically.");
        normalise.setTooltip ("Default for new IRs. Each slot remembers its own setting, so existing projects don't change.");
        autoTrim.setTooltip ("Default for new IRs. Leave off for multi-mic IR packs: their start delays keep the mics in phase.");

        askResample.onClick = [this] { settings->update ([this] (auto& d) { d.askBeforeResampling = askResample.getToggleState(); }); };
        normalise.onClick = [this] { settings->update ([this] (auto& d) { d.defaultNormalise = normalise.getToggleState(); }); };
        autoTrim.onClick = [this] { settings->update ([this] (auto& d) { d.defaultAutoTrim = autoTrim.getToggleState(); }); };

        for (auto* b : { &askResample, &normalise, &autoTrim })
            addAndMakeVisible (b);

        for (const auto percent : kUiScaleSteps)
            scale.addItem (juce::String (percent) + " %", percent);

        scale.setTooltip ("Size of the plugin window. You can also drag the window's bottom-right corner.");
        scale.onChange = [this]
        {
            if (const auto id = scale.getSelectedId(); id > 0)
                settings->update ([id] (auto& d) { d.uiScale = static_cast<float> (id) / 100.0f; });
        };
        addAndMakeVisible (scale);

        choosePresets.setTooltip ("Choose the folder where presets are stored");
        defaultPresets.setTooltip ("Store presets in IRene's own settings folder again");
        choosePresets.onClick = [this] { choosePresetsFolder(); };
        defaultPresets.onClick = [this] { settings->update ([] (auto& d) { d.presetsFolder.clear(); }); };
        addAndMakeVisible (choosePresets);
        addAndMakeVisible (defaultPresets);

        folderList.setModel (this);
        folderList.setRowHeight (22);
        folderList.setOutlineThickness (1);
        addAndMakeVisible (folderList);

        addButton.setTooltip ("Add a folder that contains impulse responses");
        removeButton.setTooltip ("Remove the selected folder from the list (no files are deleted)");
        upButton.setTooltip ("Move the selected folder up in the list");
        downButton.setTooltip ("Move the selected folder down in the list");
        addButton.onClick = [this] { addFolder(); };
        removeButton.onClick = [this] { removeFolder(); };
        upButton.onClick = [this] { moveFolder (-1); };
        downButton.onClick = [this] { moveFolder (1); };

        for (auto* b : { &addButton, &removeButton, &upButton, &downButton })
            addAndMakeVisible (b);

        setWantsKeyboardFocus (true);
        settings->addListener (this);
        refresh();
    }

    SettingsPanel::~SettingsPanel()
    {
        settings->removeListener (this);
    }

    void SettingsPanel::refresh()
    {
        const auto d = settings->get();

        askResample.setToggleState (d.askBeforeResampling, juce::dontSendNotification);
        normalise.setToggleState (d.defaultNormalise, juce::dontSendNotification);
        autoTrim.setToggleState (d.defaultAutoTrim, juce::dontSendNotification);

        const auto percent = juce::roundToInt (d.uiScale * 100.0f);
        scale.setSelectedId (0, juce::dontSendNotification);

        for (const auto step : kUiScaleSteps)
            if (step == percent)
                scale.setSelectedId (step, juce::dontSendNotification);

        if (scale.getSelectedId() == 0)
            scale.setText (juce::String (percent) + " %", juce::dontSendNotification);

        presetsPath.setText (settings->getPresetsFolder().getFullPathName() + (d.presetsFolder.isEmpty() ? "  (default)" : ""),
                             juce::dontSendNotification);
        defaultPresets.setEnabled (d.presetsFolder.isNotEmpty());

        folders = d.favouriteFolders;
        folderList.updateContent();
        folderList.repaint();
        updateButtons();
    }

    void SettingsPanel::updateButtons()
    {
        const auto row = folderList.getSelectedRow();
        const auto valid = row >= 0 && row < folders.size();
        removeButton.setEnabled (valid);
        upButton.setEnabled (valid && row > 0);
        downButton.setEnabled (valid && row < folders.size() - 1);
    }

    void SettingsPanel::addFolder()
    {
        chooser = std::make_unique<juce::FileChooser> ("Add a folder with impulse responses",
                                                       folders.isEmpty() ? juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                                                         : juce::File (folders[folders.size() - 1]).getParentDirectory());
        juce::Component::SafePointer<SettingsPanel> safe (this);

        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safe] (const juce::FileChooser& fc)
                              {
                                  if (safe == nullptr || ! fc.getResult().isDirectory())
                                      return;

                                  const auto path = fc.getResult().getFullPathName();
                                  safe->settings->update ([&path] (auto& d) { d.favouriteFolders.addIfNotAlreadyThere (path); });
                              });
    }

    void SettingsPanel::removeFolder()
    {
        const auto row = folderList.getSelectedRow();

        if (row >= 0 && row < folders.size())
            settings->update ([row] (auto& d) { d.favouriteFolders.remove (row); });
    }

    void SettingsPanel::moveFolder (int delta)
    {
        const auto row = folderList.getSelectedRow();
        const auto target = row + delta;

        if (row < 0 || row >= folders.size() || target < 0 || target >= folders.size())
            return;

        settings->update ([row, target] (auto& d) { d.favouriteFolders.move (row, target); });
        folderList.selectRow (target);
    }

    void SettingsPanel::choosePresetsFolder()
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose the presets folder", settings->getPresetsFolder());
        juce::Component::SafePointer<SettingsPanel> safe (this);

        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safe] (const juce::FileChooser& fc)
                              {
                                  if (safe == nullptr || ! fc.getResult().isDirectory())
                                      return;

                                  const auto path = fc.getResult().getFullPathName();
                                  safe->settings->update ([&path] (auto& d) { d.presetsFolder = path; });
                              });
    }

    //==============================================================================
    int SettingsPanel::getNumRows()
    {
        return folders.size();
    }

    void SettingsPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
    {
        if (row < 0 || row >= folders.size())
            return;

        if (selected)
            g.fillAll (colours::accent.withAlpha (0.3f));

        const auto exists = juce::File::isAbsolutePath (folders[row]) && juce::File (folders[row]).isDirectory();
        g.setColour (exists ? colours::text : colours::warning);
        g.setFont (font (13.0f));
        g.drawText (folders[row] + (exists ? "" : "   (not found)"), 8, 0, width - 16, height, juce::Justification::centredLeft);
    }

    bool SettingsPanel::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey)
        {
            if (onClose)
                onClose();

            return true;
        }

        return false;
    }

    void SettingsPanel::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colours::black.withAlpha (0.6f));
        g.setColour (colours::panel);
        g.fillRoundedRectangle (box.toFloat(), 8.0f);
        g.setColour (colours::outline);
        g.drawRoundedRectangle (box.toFloat(), 8.0f, 1.0f);
    }

    void SettingsPanel::resized()
    {
        box = getLocalBounds().withSizeKeepingCentre (juce::jmin (680, getWidth() - 40), juce::jmin (520, getHeight() - 30));
        auto area = box.reduced (20, 16);

        auto top = area.removeFromTop (28);
        closeButton.setBounds (top.removeFromRight (80));
        title.setBounds (top);
        hint.setBounds (area.removeFromTop (18));
        area.removeFromTop (10);

        loadingHeading.setBounds (area.removeFromTop (18));
        askResample.setBounds (area.removeFromTop (24));
        normalise.setBounds (area.removeFromTop (24));
        autoTrim.setBounds (area.removeFromTop (24));
        area.removeFromTop (10);

        auto row = area.removeFromTop (24);
        scaleHeading.setBounds (row.removeFromLeft (120));
        scale.setBounds (row.removeFromLeft (100));
        area.removeFromTop (12);

        presetsHeading.setBounds (area.removeFromTop (18));
        row = area.removeFromTop (24);
        defaultPresets.setBounds (row.removeFromRight (100));
        row.removeFromRight (6);
        choosePresets.setBounds (row.removeFromRight (90));
        row.removeFromRight (8);
        presetsPath.setBounds (row);
        area.removeFromTop (12);

        foldersHeading.setBounds (area.removeFromTop (18));
        auto buttons = area.removeFromBottom (26);
        area.removeFromBottom (6);
        folderList.setBounds (area);

        for (auto* b : { &addButton, &removeButton, &upButton, &downButton })
        {
            b->setBounds (buttons.removeFromLeft (100));
            buttons.removeFromLeft (6);
        }
    }
}
