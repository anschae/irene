#include "TestHelpers.h"

#include "PluginProcessor.h"
#include "app/PresetManager.h"

namespace
{
    using namespace irene;
    using namespace irene::test;

    void pump (int milliseconds = 50)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
    }

    void setParam (IReneProcessor& p, const juce::String& id, float realValue)
    {
        auto* param = p.getValueTreeState().getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }

    float getParam (IReneProcessor& p, const juce::String& id)
    {
        return p.getValueTreeState().getRawParameterValue (id)->load();
    }

    juce::AudioBuffer<float> renderOffline (IReneProcessor& p, const juce::AudioBuffer<float>& input)
    {
        p.setNonRealtime (true);
        p.prepareToPlay (48000.0, 256);

        juce::AudioBuffer<float> output;
        output.makeCopyOf (input);
        juce::MidiBuffer midi;

        for (int pos = 0; pos < output.getNumSamples(); pos += 256)
        {
            const auto n = std::min (256, output.getNumSamples() - pos);
            juce::AudioBuffer<float> block (output.getArrayOfWritePointers(), output.getNumChannels(), pos, n);
            p.processBlock (block, midi);
        }

        return output;
    }

    juce::File makeIRFile (const juce::File& file, int seed, int length = 800)
    {
        file.getParentDirectory().createDirectory();
        writeWav (file, makeTestIR (1, length, seed), 48000.0, 32);
        return file;
    }

    class PresetTests final : public juce::UnitTest
    {
    public:
        PresetTests() : juce::UnitTest ("Presets", "IRene") {}

        void runTest() override
        {
            TempFolder temp;
            const auto presetsFolder = temp.folder.getChildFile ("My Presets");
            const auto irFolder = temp.folder.getChildFile ("IRs");

            GlobalSettings::Shared settings;
            settings->set ({});
            settings->update ([&] (auto& d)
            {
                d.presetsFolder = presetsFolder.getFullPathName();
                d.favouriteFolders = { irFolder.getFullPathName() };
            });

            const auto fileA = makeIRFile (irFolder.getChildFile ("a.wav"), 1);
            const auto fileB = makeIRFile (irFolder.getChildFile ("b.wav"), 2);
            const auto fileC = makeIRFile (irFolder.getChildFile ("c.wav"), 3);
            const auto input = makeNoise (2, 4000, 9);

            const auto settleLibrary = [] (IReneProcessor& p)
            {
                pump();
                p.getLibrary().rescan();
                p.getLibrary().waitForScan();
                pump();
            };

            beginTest ("Save and load: parameters and IRs come back");
            {
                IReneProcessor processor;
                auto& presets = processor.getPresets();
                settleLibrary (processor);

                expectEquals (presets.getFolder().getFullPathName(), presetsFolder.getFullPathName());
                expectEquals (presets.getCurrentName(), PresetManager::initName);
                expect (presets.getNames().isEmpty());

                processor.getSlots().loadFile (0, fileA);
                processor.getSlots().loadFile (2, fileB);
                processor.getSlots().waitForPendingLoads();
                setParam (processor, "slot1_gain", -6.0f);
                setParam (processor, "slot3_pan", 50.0f);
                setParam (processor, "slot3_delay", 1.5f);
                setParam (processor, "in_hp_on", 1.0f);
                setParam (processor, "in_hp_freq", 120.0f);
                setParam (processor, "mix", 90.0f);

                expectEquals (presets.save ("Rhythm 1", false), juce::String());
                expect (presets.getFile ("Rhythm 1").existsAsFile()); // the folder is created on demand
                expectEquals (presets.getCurrentName(), juce::String ("Rhythm 1"));
                expect (! presets.isPortable ("Rhythm 1"));

                const auto reference = renderOffline (processor, input);

                // Change everything, then load the preset again
                presets.loadInit();
                expectEquals (presets.getCurrentName(), PresetManager::initName);
                expectEquals (getParam (processor, "slot1_gain"), 0.0f);
                expectEquals (getParam (processor, "in_hp_on"), 0.0f);
                expectEquals (getParam (processor, "mix"), 100.0f);

                for (int i = 0; i < kNumSlots; ++i)
                    expect (processor.getSlots().getInfo (i).status == SlotInfo::Status::empty);

                expectEquals (presets.load ("Rhythm 1"), juce::String());
                expectWithinAbsoluteError (getParam (processor, "slot1_gain"), -6.0f, 1.0e-4f);
                expectWithinAbsoluteError (getParam (processor, "in_hp_freq"), 120.0f, 1.0e-2f);
                expectEquals (processor.getSlots().getInfo (0).reference.name, juce::String ("a.wav"));
                expect (processor.getSlots().getInfo (1).status == SlotInfo::Status::empty);
                expectEquals (processor.getSlots().getInfo (2).reference.name, juce::String ("b.wav"));

                const auto again = renderOffline (processor, input);
                expect (errorDb (again.getReadPointer (0), reference.getReadPointer (0), 4000) < -120.0);
                expect (errorDb (again.getReadPointer (1), reference.getReadPointer (1), 4000) < -120.0);

                // The preset name travels with the DAW project
                juce::MemoryBlock state;
                processor.getStateInformation (state);
                IReneProcessor other;
                other.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
                expectEquals (other.getPresets().getCurrentName(), juce::String ("Rhythm 1"));
            }

            beginTest ("List, previous/next, rename, delete");
            {
                IReneProcessor processor;
                auto& presets = processor.getPresets();

                expectEquals (presets.save ("Lead 10", false), juce::String());
                expectEquals (presets.save ("Lead 2", false), juce::String());
                expectEquals (presets.getNames().joinIntoString ("|"), juce::String ("Lead 2|Lead 10|Rhythm 1")); // natural order

                // Stepping cycles through Init and all presets, in both directions
                presets.loadInit();
                presets.step (1);
                expectEquals (presets.getCurrentName(), juce::String ("Lead 2"));
                presets.step (1);
                expectEquals (presets.getCurrentName(), juce::String ("Lead 10"));
                presets.step (1);
                presets.step (1);
                expectEquals (presets.getCurrentName(), PresetManager::initName);
                presets.step (-1);
                expectEquals (presets.getCurrentName(), juce::String ("Rhythm 1"));

                // Rename
                expect (presets.rename ("Rhythm 1", "Lead 2").isNotEmpty()); // name taken
                expect (presets.rename ("Rhythm 1", "   ").isNotEmpty());
                expect (presets.rename ("Rhythm 1", "Init").isNotEmpty());
                expectEquals (presets.rename ("Rhythm 1", "Rhythm: crunch/2"), juce::String()); // illegal characters are removed
                expect (! presets.exists ("Rhythm 1"));
                expectEquals (presets.getCurrentName(), PresetManager::sanitiseName ("Rhythm: crunch/2"));
                expect (presets.exists (presets.getCurrentName()));
                expectEquals (presets.rename (presets.getCurrentName(), "Rhythm 1"), juce::String());

                // Save rules
                expect (presets.save ("", false).isNotEmpty());
                expect (presets.save ("Init", false).isNotEmpty());

                // Delete
                expectEquals (presets.remove ("Lead 10"), juce::String());
                expect (presets.remove ("Lead 10").isNotEmpty());
                expectEquals (presets.remove ("Lead 2"), juce::String());
                expectEquals (presets.getNames().joinIntoString ("|"), juce::String ("Rhythm 1"));

                // Loading something that isn't there, or isn't a preset, changes nothing
                setParam (processor, "out_gain", 5.0f);
                expect (presets.load ("Nope").isNotEmpty());
                presets.getFile ("Broken").replaceWithText ("not a preset");
                expect (presets.load ("Broken").isNotEmpty());
                expectWithinAbsoluteError (getParam (processor, "out_gain"), 5.0f, 1.0e-4f);
                presets.getFile ("Broken").deleteFile();
            }

            beginTest ("Missing files: found by path, then by hash, then by name, else marked missing");
            {
                // The preset "Rhythm 1" refers to a.wav (slot 1) and b.wav (slot 3). Add c.wav in slot 2.
                {
                    IReneProcessor processor;
                    settleLibrary (processor);
                    expectEquals (processor.getPresets().load ("Rhythm 1"), juce::String());
                    processor.getSlots().loadFile (1, fileC);
                    processor.getSlots().waitForPendingLoads();
                    expectEquals (processor.getPresets().save ("Three mics", false), juce::String());
                }

                const auto hashA = hashFile (fileA);

                // a.wav: moved AND renamed          -> found by content hash
                // b.wav: replaced by a re-export with the same name in another folder -> found by name
                // c.wav: gone                        -> missing
                const auto movedA = irFolder.getChildFile ("archive").getChildFile ("57 renamed.wav");
                movedA.getParentDirectory().createDirectory();
                expect (fileA.moveFileTo (movedA));

                expect (fileB.deleteFile());
                const auto newB = makeIRFile (irFolder.getChildFile ("new takes").getChildFile ("b.wav"), 77);
                expect (fileC.deleteFile());

                IReneProcessor processor;
                settleLibrary (processor);
                expectEquals (processor.getPresets().load ("Three mics"), juce::String());

                const auto slotA = processor.getSlots().getInfo (0);
                expect (slotA.status == SlotInfo::Status::ready);
                expectEquals (slotA.reference.path, movedA.getFullPathName());
                expectEquals (slotA.reference.hash, hashA);

                const auto slotB = processor.getSlots().getInfo (2);
                expect (slotB.status == SlotInfo::Status::ready);
                expectEquals (slotB.reference.path, newB.getFullPathName());

                const auto slotC = processor.getSlots().getInfo (1);
                expect (slotC.status == SlotInfo::Status::missing);
                expectEquals (slotC.reference.name, juce::String ("c.wav"));
                expectEquals (slotC.reference.path, fileC.getFullPathName()); // kept, so "Locate..." knows what to ask for

                // The parameters are restored regardless
                expectWithinAbsoluteError (getParam (processor, "slot1_gain"), -6.0f, 1.0e-4f);

                // A missing slot is silent, doesn't block the others, and "Locate" is a normal load
                const auto output = renderOffline (processor, input);
                expect (output.getMagnitude (0, 4000) > 0.001f);

                const auto relocated = makeIRFile (temp.folder.getChildFile ("found it").getChildFile ("c again.wav"), 3);
                processor.getSlots().loadFile (1, relocated);
                processor.getSlots().waitForPendingLoads();
                expect (processor.getSlots().getInfo (1).status == SlotInfo::Status::ready);

                // Saving the preset again stores the new locations
                expectEquals (processor.getPresets().save ("Three mics", false), juce::String());
                IReneProcessor another;
                expectEquals (another.getPresets().load ("Three mics"), juce::String());
                expectEquals (another.getSlots().getInfo (0).reference.path, movedA.getFullPathName());
                expectEquals (another.getSlots().getInfo (1).reference.path, relocated.getFullPathName());
            }

            beginTest ("Portable presets embed the IRs");
            {
                const auto file = makeIRFile (temp.folder.getChildFile ("usb stick").getChildFile ("portable.wav"), 42, 1500);
                juce::AudioBuffer<float> reference;
                juce::int64 normalSize = 0;

                {
                    IReneProcessor processor;
                    processor.getSlots().loadFile (0, file);
                    processor.getSlots().waitForPendingLoads();
                    setParam (processor, "slot1_length", 80.0f);

                    expectEquals (processor.getPresets().save ("Normal", false), juce::String());
                    expectEquals (processor.getPresets().save ("To go", true), juce::String());
                    expect (processor.getPresets().isPortable ("To go"));
                    expect (! processor.getPresets().isPortable ("Normal"));

                    normalSize = processor.getPresets().getFile ("Normal").getSize();
                    expect (processor.getPresets().getFile ("To go").getSize() > normalSize + 2000); // the audio is inside

                    reference = renderOffline (processor, input);
                }

                // On "another computer": the IR file doesn't exist
                expect (file.deleteFile());

                IReneProcessor processor;
                expectEquals (processor.getPresets().load ("To go"), juce::String());
                expect (processor.getSlots().getInfo (0).status == SlotInfo::Status::ready);
                expectEquals (processor.getSlots().getInfo (0).reference.name, juce::String ("portable.wav"));

                const auto output = renderOffline (processor, input);
                expect (errorDb (output.getReadPointer (0), reference.getReadPointer (0), 4000) < -120.0);

                // The normal preset can't find it
                expectEquals (processor.getPresets().load ("Normal"), juce::String());
                expect (processor.getSlots().getInfo (0).status == SlotInfo::Status::missing);
            }

            settings->set ({});
            pump();
        }
    };

    PresetTests presetTests;
}
