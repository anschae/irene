#include "PresetBar.h"

namespace irene::ui
{
    PresetBar::PresetBar (IReneProcessor& p, ConfirmOverlay& d)
        : processor (p), presets (p.getPresets()), dialogs (d)
    {
        for (auto* b : { &prevButton, &nextButton, &nameButton, &saveButton, &saveAsButton, &moreButton })
            addAndMakeVisible (b);

        prevButton.setTooltip ("Previous preset");
        nextButton.setTooltip ("Next preset");
        nameButton.setTooltip ("Current preset. Click to choose another one.");
        saveButton.setTooltip ("Save the current settings to the current preset");
        saveAsButton.setTooltip ("Save the current settings as a new preset. \"Portable\" also stores the IR audio in the preset.");
        moreButton.setTooltip ("Rename or delete the current preset");

        prevButton.onClick = [this] { presets.step (-1); };
        nextButton.onClick = [this] { presets.step (1); };
        nameButton.onClick = [this] { showPresetMenu(); };
        saveButton.onClick = [this] { save(); };
        saveAsButton.onClick = [this] { saveAs(); };
        moreButton.onClick = [this] { showMoreMenu(); };

        presets.addChangeListener (this);
        refresh();
    }

    PresetBar::~PresetBar()
    {
        presets.removeChangeListener (this);
    }

    void PresetBar::refresh()
    {
        const auto name = presets.getCurrentName();
        const auto isFile = presets.exists (name);

        nameButton.setButtonText (name);
        saveButton.setEnabled (isFile);
        moreButton.setEnabled (isFile);
    }

    void PresetBar::report (const juce::String& error)
    {
        if (error.isNotEmpty())
            dialogs.inform ("Preset", error);
    }

    void PresetBar::showPresetMenu()
    {
        const auto names = presets.getNames();
        const auto current = presets.getCurrentName();

        juce::PopupMenu menu;
        menu.setLookAndFeel (&getLookAndFeel());
        menu.addItem (1, PresetManager::initName, true, current == PresetManager::initName);

        if (! names.isEmpty())
            menu.addSeparator();

        for (int i = 0; i < names.size(); ++i)
            menu.addItem (i + 2, names[i], true, names[i] == current);

        juce::Component::SafePointer<PresetBar> safe (this);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&nameButton).withMaximumNumColumns (3),
                            [safe, names] (int result)
                            {
                                if (safe == nullptr || result <= 0)
                                    return;

                                safe->report (safe->presets.load (result == 1 ? PresetManager::initName : names[result - 2]));
                            });
    }

    void PresetBar::showMoreMenu()
    {
        juce::PopupMenu menu;
        menu.setLookAndFeel (&getLookAndFeel());
        menu.addItem (1, "Rename...");
        menu.addItem (2, "Delete...");

        juce::Component::SafePointer<PresetBar> safe (this);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&moreButton),
                            [safe] (int result)
                            {
                                if (safe == nullptr)
                                    return;

                                if (result == 1)
                                    safe->rename();
                                else if (result == 2)
                                    safe->remove();
                            });
    }

    void PresetBar::save()
    {
        const auto name = presets.getCurrentName();

        if (! presets.exists (name))
        {
            saveAs();
            return;
        }

        // keeps the preset's "portable" choice
        report (presets.save (name, presets.isPortable (name)));
    }

    void PresetBar::saveAs()
    {
        const auto current = presets.getCurrentName();
        const auto suggestion = current == PresetManager::initName ? juce::String ("My preset") : current;
        juce::Component::SafePointer<PresetBar> safe (this);

        dialogs.prompt ("Save preset as",
                        "A normal preset remembers where each IR file is. A portable preset also contains the IR audio, so it works on another computer.",
                        suggestion, "Portable (embed IRs)", presets.exists (current) && presets.isPortable (current), "Save",
                        [safe] (bool confirmed, const juce::String& text, bool portable)
                        {
                            if (safe == nullptr || ! confirmed)
                                return;

                            const auto name = PresetManager::sanitiseName (text);

                            if (! safe->presets.exists (name))
                            {
                                safe->report (safe->presets.save (name, portable));
                                return;
                            }

                            // (deferred, so that the first dialog has closed before the next one opens)
                            juce::MessageManager::callAsync ([safe, name, portable]
                            {
                                if (safe == nullptr)
                                    return;

                                safe->dialogs.ask ("Replace preset?", "A preset called \"" + name + "\" already exists.", "Replace", "Cancel",
                                                   [safe, name, portable] (bool replace)
                                                   {
                                                       if (safe != nullptr && replace)
                                                           safe->report (safe->presets.save (name, portable));
                                                   });
                            });
                        });
    }

    void PresetBar::rename()
    {
        const auto current = presets.getCurrentName();
        juce::Component::SafePointer<PresetBar> safe (this);

        dialogs.prompt ("Rename preset", "New name for \"" + current + "\":", current, {}, false, "Rename",
                        [safe, current] (bool confirmed, const juce::String& text, bool)
                        {
                            if (safe == nullptr || ! confirmed)
                                return;

                            const auto error = safe->presets.rename (current, text);

                            if (error.isNotEmpty())
                                juce::MessageManager::callAsync ([safe, error] { if (safe != nullptr) safe->report (error); });
                        });
    }

    void PresetBar::remove()
    {
        const auto current = presets.getCurrentName();
        juce::Component::SafePointer<PresetBar> safe (this);

        dialogs.ask ("Delete preset?", "\"" + current + "\" will be deleted from the presets folder. The current settings stay as they are.",
                     "Delete", "Cancel",
                     [safe, current] (bool confirmed)
                     {
                         if (safe != nullptr && confirmed)
                         {
                             const auto error = safe->presets.remove (current);

                             if (error.isNotEmpty())
                                 juce::MessageManager::callAsync ([safe, error] { if (safe != nullptr) safe->report (error); });
                         }
                     });
    }

    void PresetBar::resized()
    {
        auto area = getLocalBounds();
        const auto gap = 4;

        moreButton.setBounds (area.removeFromRight (30));
        area.removeFromRight (gap);
        saveAsButton.setBounds (area.removeFromRight (78));
        area.removeFromRight (gap);
        saveButton.setBounds (area.removeFromRight (52));
        area.removeFromRight (gap * 2);

        prevButton.setBounds (area.removeFromLeft (26));
        area.removeFromLeft (gap);
        nextButton.setBounds (area.removeFromRight (26));
        area.removeFromRight (gap);
        nameButton.setBounds (area);
    }
}
