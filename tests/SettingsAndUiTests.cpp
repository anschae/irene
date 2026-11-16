#include "TestHelpers.h"

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "app/GlobalSettings.h"

#include <thread>

namespace
{
    using namespace irene;
    using namespace irene::test;

    void pumpMessages (int milliseconds = 60)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
    }

    /** Fraction of pixels that differ clearly from the top-left pixel: a cheap
        "something was drawn" check. */
    double drawnFraction (const juce::Image& image)
    {
        const auto reference = image.getPixelAt (0, 0);
        auto different = 0;
        auto total = 0;

        for (int y = 0; y < image.getHeight(); y += 3)
            for (int x = 0; x < image.getWidth(); x += 3)
            {
                const auto c = image.getPixelAt (x, y);
                different += (std::abs (c.getRed() - reference.getRed()) + std::abs (c.getGreen() - reference.getGreen())
                              + std::abs (c.getBlue() - reference.getBlue())) > 24 ? 1 : 0;
                ++total;
            }

        return static_cast<double> (different) / std::max (1, total);
    }

    class SettingsAndUiTests final : public juce::UnitTest
    {
    public:
        SettingsAndUiTests() : juce::UnitTest ("Settings and UI", "IRene") {}

        void runTest() override
        {
            beginTest ("Global settings: defaults, save, reload");
            {
                const auto file = GlobalSettings::getDataDirectory().getChildFile ("settings.xml");
                file.deleteFile();

                // a path that is absolute on every platform
                const auto customPresets = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("somewhere").getChildFile ("presets");

                {
                    GlobalSettings settings;
                    const auto d = settings.get();
                    expectEquals (d.uiScale, 1.0f);
                    expect (! d.askBeforeResampling);
                    expect (d.defaultNormalise);
                    expect (! d.defaultAutoTrim); // auto-trim must be off by default
                    expect (d.favouriteFolders.isEmpty());
                    expectEquals (settings.getPresetsFolder().getFullPathName(),
                                  GlobalSettings::getDataDirectory().getChildFile ("Presets").getFullPathName());

                    settings.update ([&customPresets] (auto& s)
                    {
                        s.uiScale = 1.5f;
                        s.askBeforeResampling = true;
                        s.defaultAutoTrim = true;
                        s.defaultNormalise = false;
                        s.presetsFolder = customPresets.getFullPathName();
                        s.favouriteFolders = { "/irs/b", "/irs/a", "/irs/b", "" };
                    });

                    expect (file.existsAsFile());
                }

                // A new object (e.g. the plugin in another DAW) sees the same data
                GlobalSettings settings;
                auto d = settings.get();
                expectEquals (d.uiScale, 1.5f);
                expect (d.askBeforeResampling && d.defaultAutoTrim && ! d.defaultNormalise);
                expectEquals (d.presetsFolder, customPresets.getFullPathName());
                expectEquals (d.favouriteFolders.joinIntoString ("|"), juce::String ("/irs/b|/irs/a")); // order kept, duplicates and blanks removed
                expectEquals (settings.getPresetsFolder().getFullPathName(), customPresets.getFullPathName());

                // The file carries a version number
                const auto xml = juce::parseXML (file);
                expect (xml != nullptr);

                if (xml != nullptr)
                    expectEquals (xml->getIntAttribute ("version"), GlobalSettings::kFileVersion);

                // Out-of-range values are clamped
                settings.update ([] (auto& s) { s.uiScale = 9.0f; });
                expectEquals (settings.get().uiScale, GlobalSettings::kMaxUiScale);

                // A change made by another process is noticed
                {
                    GlobalSettings other;
                    other.update ([] (auto& s) { s.uiScale = 0.75f; s.favouriteFolders.add ("/irs/c"); });
                }

                file.setLastModificationTime (juce::Time::getCurrentTime() + juce::RelativeTime::seconds (5));
                expect (settings.reloadIfChangedOnDisk());
                expectEquals (settings.get().uiScale, 0.75f);
                expectEquals (settings.get().favouriteFolders.size(), 3);
                expect (! settings.reloadIfChangedOnDisk()); // nothing new

                // A damaged file falls back to the defaults instead of failing
                file.replaceWithText ("<<<< not xml");
                GlobalSettings damaged;
                expectEquals (damaged.get().uiScale, 1.0f);
                expect (damaged.get().favouriteFolders.isEmpty());

                file.deleteFile();
            }

            beginTest ("Load defaults follow the global settings");
            {
                GlobalSettings::Shared settings;
                settings->set ({});

                IReneProcessor processor;
                processor.getSlots().setSource (0, makeSource (makeTestIR (1, 300, 1, 50), 48000.0));
                expect (processor.getSlots().getInfo (0).normalise);
                expect (! processor.getSlots().getInfo (0).autoTrim);

                settings->update ([] (auto& s) { s.defaultNormalise = false; s.defaultAutoTrim = true; });
                pumpMessages();

                processor.getSlots().setSource (1, makeSource (makeTestIR (1, 300, 2, 50), 48000.0));
                expect (! processor.getSlots().getInfo (1).normalise);
                expect (processor.getSlots().getInfo (1).autoTrim);

                // the slot loaded earlier keeps the options it was loaded with
                expect (processor.getSlots().getInfo (0).normalise);

                settings->set ({});
                pumpMessages();
            }

            beginTest ("Editor: opens, scales, draws, closes");
            {
                GlobalSettings::Shared settings;
                settings->set ({});

                IReneProcessor processor;
                processor.prepareToPlay (48000.0, 256);
                processor.getSlots().setSource (0, makeSource (makeTestIR (2, 4800, 3), 96000.0, "SM57 cap edge.wav"));
                processor.getSlots().setMissing (2, { "/gone/R121.wav", "R121.wav", "abc" });

                std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
                expect (editor != nullptr);

                if (editor == nullptr)
                    return;

                pumpMessages();
                auto* irene = dynamic_cast<IReneEditor*> (editor.get());
                expect (irene != nullptr && irene->isBrowserOpen());
                const auto baseWidth = irene != nullptr ? irene->getBaseWidth() : 0;
                expectEquals (baseWidth, IReneEditor::kBaseWidth + IReneEditor::kBrowserWidth + IReneEditor::kGap);
                expectEquals (editor->getWidth(), baseWidth);
                expectEquals (editor->getHeight(), IReneEditor::kBaseHeight);

                auto image = editor->createComponentSnapshot (editor->getLocalBounds());
                expect (drawnFraction (image) > 0.05, "The editor drew (almost) nothing");

                // The UI scale setting resizes the window
                settings->update ([] (auto& s) { s.uiScale = 1.5f; });
                pumpMessages();
                expectEquals (editor->getWidth(), juce::roundToInt (static_cast<float> (baseWidth) * 1.5f));
                expectEquals (editor->getHeight(), juce::roundToInt (IReneEditor::kBaseHeight * 1.5f));
                image = editor->createComponentSnapshot (editor->getLocalBounds());
                expect (drawnFraction (image) > 0.05);

                // Resizing the window (host or corner drag) is remembered as the scale
                editor->setSize (juce::roundToInt (static_cast<float> (baseWidth) * 1.25f), juce::roundToInt (IReneEditor::kBaseHeight * 1.25f));
                pumpMessages (600);
                expectWithinAbsoluteError (settings->get().uiScale, 1.25f, 0.01f);

                // Closing the browser makes the window narrower at the same scale
                settings->update ([] (auto& s) { s.browserOpen = false; });
                pumpMessages();
                expectEquals (editor->getWidth(), juce::roundToInt (IReneEditor::kBaseWidth * 1.25f));
                editor->createComponentSnapshot (editor->getLocalBounds());

                // Parameter changes while the editor is open
                for (const auto* id : { "slot1_gain", "slot2_pan", "in_hp_on", "out_mid_gain", "mix" })
                    processor.getValueTreeState().getParameter (id)->setValueNotifyingHost (0.3f);

                processor.swapSlots (0, 1);
                processor.getSlots().clear (1);
                pumpMessages();
                editor->createComponentSnapshot (editor->getLocalBounds());

                editor.reset();
                settings->set ({});
                pumpMessages();
            }

            beginTest ("Editor state is saved with the project");
            {
                IReneProcessor processor;
                processor.uiState.filterTab = 2;
                processor.uiState.targetSlot = kNumSlots - 1;

                juce::MemoryBlock state;
                processor.getStateInformation (state);

                IReneProcessor restored;
                restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
                expectEquals (restored.uiState.filterTab.load(), 2);
                expectEquals (restored.uiState.targetSlot.load(), kNumSlots - 1);

                // The editor opens on that tab
                std::unique_ptr<juce::AudioProcessorEditor> editor (restored.createEditor());
                pumpMessages();
                editor.reset();
                expectEquals (restored.uiState.filterTab.load(), 2);
            }

            beginTest ("Broadcaster: asynchronous, coalesced, safe to add/remove/destroy at any time");
            {
                struct Counter final : Broadcaster::Listener
                {
                    void broadcasterChanged (Broadcaster* source) override
                    {
                        ++calls;
                        lastSource = source;

                        if (removeFrom != nullptr)
                            removeFrom->removeListener (this);
                    }

                    int calls = 0;
                    Broadcaster* lastSource = nullptr;
                    Broadcaster* removeFrom = nullptr;
                };

                Counter a, b;

                {
                    Broadcaster broadcaster;
                    broadcaster.addListener (&a);
                    broadcaster.addListener (&b);
                    broadcaster.addListener (&a); // twice is once

                    broadcaster.sendChange();
                    broadcaster.sendChange();
                    expectEquals (a.calls, 0); // never synchronous
                    pumpMessages();
                    expectEquals (a.calls, 1); // coalesced
                    expectEquals (b.calls, 1);
                    expect (a.lastSource == &broadcaster);

                    // a listener may remove itself during the callback
                    a.removeFrom = &broadcaster;
                    broadcaster.sendChange();
                    pumpMessages();
                    broadcaster.sendChange();
                    pumpMessages();
                    expectEquals (a.calls, 2);
                    expectEquals (b.calls, 3);

                    // sending from another thread
                    std::thread ([&broadcaster] { broadcaster.sendChange(); }).join();
                    pumpMessages();
                    expectEquals (b.calls, 4);

                    broadcaster.sendChange(); // still queued when the broadcaster dies
                }

                pumpMessages();
                expectEquals (b.calls, 4);
            }

            beginTest ("Plugin instances can be created and destroyed on another thread");
            {
                // Some hosts do this. The shared settings and library must cope with
                // instances coming and going while the message thread is busy.
                IReneProcessor keepAlive; // holds the shared objects, as an open project would
                std::atomic<bool> done { false };

                std::thread worker ([&done]
                {
                    for (int i = 0; i < 12; ++i)
                    {
                        IReneProcessor processor;
                        processor.prepareToPlay (48000.0, 128);
                        processor.getSlots().setSource (0, makeSource (makeTestIR (1, 200, i + 1), 48000.0));

                        juce::MemoryBlock state;
                        processor.getStateInformation (state);
                        processor.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
                    }

                    done = true;
                });

                GlobalSettings::Shared settings;

                for (int i = 0; ! done.load(); ++i)
                {
                    // keep the shared objects broadcasting while instances register and unregister
                    settings->update ([i] (auto& d) { d.defaultAutoTrim = (i % 2) == 0; });
                    pumpMessages (5);
                }

                worker.join();
                settings->set ({});
                pumpMessages();
                expect (done.load());
            }

            beginTest ("File probing reads only the header");
            {
                TempFolder temp;
                const auto file = temp.folder.getChildFile ("probe.wav");
                expect (writeWav (file, makeTestIR (2, 1234, 4), 88200.0, 24));

                const auto info = probeIRFile (file);
                expect (info.valid);
                expectEquals (info.sampleRate, 88200.0);
                expectEquals (info.numChannels, 2);
                expectEquals (static_cast<int> (info.lengthInSamples), 1234);

                expect (! probeIRFile (temp.folder.getChildFile ("nope.wav")).valid);

                const auto fake = temp.folder.getChildFile ("fake.aiff");
                fake.replaceWithText ("not audio");
                expect (! probeIRFile (fake).valid);
            }
        }
    };

    SettingsAndUiTests settingsAndUiTests;
}
