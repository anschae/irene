// Developer tool: renders the plugin editor to PNG files, without a DAW.
//
//   IReneSnapshot <output-folder> [ir-file | favourite-folder ...]
//
// Build with -DIRENE_BUILD_TOOLS=ON. On Linux without a display, run it under
// xvfb-run.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <iostream>

#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace
{
    void pumpMessages (int milliseconds)
    {
        const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (milliseconds);

        while (juce::Time::getMillisecondCounter() < end)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        }
    }

    juce::Button* findButton (juce::Component& root, const juce::String& text)
    {
        for (auto* child : root.getChildren())
        {
            if (! child->isVisible())
                continue;

            if (auto* button = dynamic_cast<juce::Button*> (child); button != nullptr && button->getButtonText() == text && button->isVisible())
                return button;

            if (auto* found = findButton (*child, text))
                return found;
        }

        return nullptr;
    }

    bool save (juce::Component& component, const juce::File& file, float scale = 1.0f)
    {
        const auto image = component.createComponentSnapshot (component.getLocalBounds(), true, scale);
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat png;
        const auto ok = stream.openedOk() && png.writeImageToStream (image, stream);
        std::cout << (ok ? "wrote " : "FAILED ") << file.getFullPathName() << "  " << image.getWidth() << "x" << image.getHeight() << std::endl;
        return ok;
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc < 2)
    {
        std::cout << "usage: IReneSnapshot <output-folder> [ir-file ...]" << std::endl;
        return 1;
    }

    const auto out = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[1]));
    out.createDirectory();

    // Never touch the real user settings.
    irene::GlobalSettings::setDataDirectoryOverride (out.getChildFile ("data"));

    // Arguments: folders become favourite folders, files are loaded into the slots.
    juce::StringArray folders;
    juce::Array<juce::File> files;

    for (int i = 2; i < argc; ++i)
    {
        const auto item = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[i]));

        if (item.isDirectory())
            folders.add (item.getFullPathName());
        else
            files.add (item);
    }

    {
        irene::GlobalSettings::Shared settings;
        settings->update ([&] (auto& d)
        {
            d.favouriteFolders = folders;
            d.browserOpen = true;
            d.uiScale = 1.0f;

            // show a custom display name for the first favourite folder
            if (! folders.isEmpty())
                d.folderNames[folders[0]] = "Mesa 4x12 (renamed)";
        });
    }

    irene::IReneProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    pumpMessages (100);
    processor.getLibrary().waitForScan();

    for (int i = 0; i < files.size() && i < irene::kNumSlots; ++i)
        processor.getSlots().loadFile (i, files[i]);

    // a second IR in slot 1, so that its "back" button has something to go back to
    if (files.size() > 1)
    {
        processor.getSlots().waitForPendingLoads();
        processor.getSlots().loadFile (0, files[1]);
        processor.getSlots().waitForPendingLoads();
        processor.getSlots().loadFile (0, files[0]);
    }

    processor.getSlots().waitForPendingLoads();
    pumpMessages (100);

    // A few non-default settings, so that the display has something to show.
    const auto set = [&processor] (const char* id, float value)
    {
        if (auto* p = processor.getValueTreeState().getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    set ("in_hp_on", 1.0f);
    set ("in_hp_freq", 80.0f);
    set ("slot2_delay", 0.35f);
    set ("slot2_pan", 30.0f);
    set ("slot2_mid_on", 1.0f);
    set ("slot2_mid_freq", 2500.0f);
    set ("slot2_mid_gain", 7.0f);
    set ("slot2_mid_q", 1.6f);

    // Feed some audio so that the meters show something.
    {
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        juce::Random random (1);

        for (int block = 0; block < 40; ++block)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int n = 0; n < 512; ++n)
                    buffer.setSample (ch, n, 0.3f * (random.nextFloat() * 2.0f - 1.0f));

            processor.processBlock (buffer, midi);
            juce::Thread::sleep (2);
        }
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    pumpMessages (600);

    auto ok = save (*editor, out.getChildFile ("editor.png"));

    // Other filter tabs and the settings page
    if (auto* tab = findButton (*editor, "Slot 2"))
    {
        tab->triggerClick();
        pumpMessages (150);
        ok = save (*editor, out.getChildFile ("editor-slot2-filter.png")) && ok;
    }

    if (auto* settings = findButton (*editor, "Settings"))
    {
        settings->triggerClick();
        pumpMessages (150);
        ok = save (*editor, out.getChildFile ("settings.png")) && ok;

        if (auto* close = findButton (*editor, "Close"))
        {
            close->triggerClick();
            pumpMessages (100);
        }
    }

    if (auto* alignButton = findButton (*editor, "Align"))
    {
        alignButton->triggerClick();
        pumpMessages (150);
        ok = save (*editor, out.getChildFile ("align.png")) && ok;

        if (auto* okButton = findButton (*editor, "OK"))
        {
            okButton->triggerClick();
            pumpMessages (100);
        }
    }

    if (auto* exportButton = findButton (*editor, "Export IR..."))
    {
        exportButton->triggerClick();
        pumpMessages (150);
        ok = save (*editor, out.getChildFile ("export.png")) && ok;

        if (auto* close = findButton (*editor, "Close"))
        {
            close->triggerClick();
            pumpMessages (100);
        }
    }

    if (auto* solo = findButton (*editor, "Solo target"))
    {
        solo->triggerClick();
        pumpMessages (150);
        ok = save (*editor, out.getChildFile ("solo-target.png")) && ok;
        solo->triggerClick();
        pumpMessages (100);
    }

    if (auto* notesButton = findButton (*editor, "Notes"))
    {
        processor.setNotes ("Rhythm guitars, double-tracked L/R.\nSM57 cap edge + R121 blend, Align on slot 1.");
        notesButton->triggerClick();
        pumpMessages (150);
        ok = save (*editor, out.getChildFile ("notes.png")) && ok;

        if (auto* close = findButton (*editor, "Close"))
        {
            close->triggerClick();
            pumpMessages (100);
        }
    }

    // A wider browser (dragging its edge)
    if (auto* irene = dynamic_cast<irene::IReneEditor*> (editor.get()))
    {
        irene->setBrowserWidth (440, false);
        pumpMessages (150);
        ok = save (*editor, out.getChildFile ("browser-wide.png")) && ok;
        irene->setBrowserWidth (irene::GlobalSettings::kDefaultBrowserWidth, false);
        pumpMessages (100);
    }

    // A second size, to check the scaling
    editor->setSize (juce::roundToInt (editor->getWidth() * 1.5f), juce::roundToInt (editor->getHeight() * 1.5f));
    pumpMessages (150);
    ok = save (*editor, out.getChildFile ("editor-150.png")) && ok;

    editor.reset();
    return ok ? 0 : 1;
}
