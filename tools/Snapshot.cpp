// Developer tool: renders the plugin editor to PNG files, without a DAW.
//
//   IReneSnapshot <output-folder> [ir-file ...]
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

    irene::IReneProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    for (int i = 2; i < argc && i - 2 < irene::kNumSlots; ++i)
        processor.getSlots().loadFile (i - 2, juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[i])));

    processor.getSlots().waitForPendingLoads();

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
    pumpMessages (300);

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

    // A second size, to check the scaling
    editor->setSize (juce::roundToInt (editor->getWidth() * 1.5f), juce::roundToInt (editor->getHeight() * 1.5f));
    pumpMessages (150);
    ok = save (*editor, out.getChildFile ("editor-150.png")) && ok;

    editor.reset();
    return ok ? 0 : 1;
}
