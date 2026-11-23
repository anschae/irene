#include "TestHelpers.h"

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "app/GlobalSettings.h"

// Tests for the browser and workflow features added after 0.1.0:
// folder display names, browser layout, audition history ("back"),
// the browser's "Solo target" mode, notes and the mono export default.

namespace
{
    using namespace irene;
    using namespace irene::test;

    void pump (int milliseconds = 60)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
    }

    void setParam (IReneProcessor& p, const juce::String& id, float realValue)
    {
        auto* param = p.getValueTreeState().getParameter (id);
        jassert (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }

    juce::AudioBuffer<float> renderProcessor (IReneProcessor& p, const juce::AudioBuffer<float>& input, int blockSize)
    {
        juce::AudioBuffer<float> output;
        output.makeCopyOf (input);
        juce::MidiBuffer midi;

        for (int pos = 0; pos < output.getNumSamples(); pos += blockSize)
        {
            const auto n = std::min (blockSize, output.getNumSamples() - pos);
            juce::AudioBuffer<float> block (output.getArrayOfWritePointers(), output.getNumChannels(), pos, n);
            p.processBlock (block, midi);
        }

        return output;
    }

    /** The IDs of the menu's enabled items. */
    juce::Array<int> enabledItems (const juce::PopupMenu& menu)
    {
        juce::Array<int> ids;

        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            if (it.getItem().itemID > 0 && it.getItem().isEnabled)
                ids.add (it.getItem().itemID);

        return ids;
    }

    class WorkflowTests final : public juce::UnitTest
    {
    public:
        WorkflowTests() : juce::UnitTest ("Browser and workflow", "IRene") {}

        void runTest() override
        {
            beginTest ("Settings: folder display names, browser width and columns");
            {
                const auto file = GlobalSettings::getDataDirectory().getChildFile ("settings.xml");
                file.deleteFile();

                TempFolder temp;
                const auto folderA = temp.folder.getChildFile ("48").getFullPathName();
                const auto folderB = temp.folder.getChildFile ("Mesa").getFullPathName();

                {
                    GlobalSettings settings;
                    const auto d = settings.get();
                    expectEquals (d.browserWidth, GlobalSettings::kDefaultBrowserWidth);
                    expect (d.browserColumns.isEmpty());
                    expectEquals (d.folderDisplayName (folderA), juce::String ("48")); // no custom name: the folder's own

                    settings.update ([&] (auto& s)
                    {
                        s.favouriteFolders = { folderA, folderB };
                        s.folderNames[folderA] = "  Mesa 4x12 V30  ";
                        s.folderNames[folderB] = "   ";                 // blank: no custom name
                        s.folderNames["/not/a/favourite"] = "Orphan"; // not in the list: dropped
                        s.browserWidth = 420;
                        s.browserColumns = "<TABLELAYOUT/>";
                    });

                    const auto saved = settings.get();
                    expectEquals (saved.folderDisplayName (folderA), juce::String ("Mesa 4x12 V30"));
                    expectEquals (saved.folderDisplayName (folderB), juce::String ("Mesa"));
                    expectEquals (static_cast<int> (saved.folderNames.size()), 1);
                }

                // Another instance (or another DAW) reads the same
                {
                    GlobalSettings settings;
                    auto d = settings.get();
                    expectEquals (d.folderDisplayName (folderA), juce::String ("Mesa 4x12 V30"));
                    expectEquals (d.browserWidth, 420);
                    expectEquals (d.browserColumns, juce::String ("<TABLELAYOUT/>"));

                    // Removing a folder drops its name; widths are clamped
                    settings.update ([&] (auto& s)
                    {
                        s.favouriteFolders.removeString (folderA);
                        s.browserWidth = 5000;
                    });

                    d = settings.get();
                    expect (d.folderNames.empty());
                    expectEquals (d.browserWidth, GlobalSettings::kMaxBrowserWidth);

                    settings.update ([] (auto& s) { s.browserWidth = 10; });
                    expectEquals (settings.get().browserWidth, GlobalSettings::kMinBrowserWidth);
                }

                file.deleteFile();
            }

            beginTest ("Audition history: back to the previous IR");
            {
                TempFolder temp;
                juce::Array<juce::File> files;

                for (int i = 0; i < 3; ++i)
                {
                    files.add (temp.folder.getChildFile ("ir" + juce::String (i) + ".wav"));
                    expect (writeWav (files.getLast(), makeTestIR (1, 400 + i, 20 + i), 48000.0));
                }

                IReneProcessor processor;
                auto& slots = processor.getSlots();
                processor.prepareToPlay (48000.0, 256);

                expect (! slots.canGoBack (0));
                expect (slots.goBack (0).isEmpty()); // nothing to go back to: nothing happens

                const auto load = [&] (int slot, const juce::File& f)
                {
                    slots.loadFile (slot, f);
                    slots.waitForPendingLoads();
                };

                const auto back = [&] (int slot)
                {
                    const auto ref = slots.goBack (slot);
                    slots.waitForPendingLoads();
                    return ref;
                };

                load (0, files[0]);
                load (0, files[1]);
                load (0, files[1]); // the same file again: no new entry
                load (0, files[2]);
                expectEquals (slots.getInfo (0).historySize, 3); // empty, ir0, ir1

                // Going back restores the earlier IR, with its own per-slot options
                slots.setSlotOptions (0, false, true);
                expectEquals (back (0).name, juce::String ("ir1.wav"));
                expectEquals (slots.getInfo (0).reference.name, juce::String ("ir1.wav"));
                expect (slots.getInfo (0).normalise); // ir1 was loaded with the defaults

                expectEquals (back (0).name, juce::String ("ir0.wav"));
                expectEquals (slots.getInfo (0).reference.name, juce::String ("ir0.wav"));

                // ...and finally to the empty slot it started as
                expect (back (0).isEmpty());
                expect (slots.getInfo (0).status == SlotInfo::Status::empty);
                expect (! slots.canGoBack (0));

                // Clear can be undone
                load (1, files[2]);
                slots.clear (1);
                expect (slots.getInfo (1).status == SlotInfo::Status::empty);
                expectEquals (back (1).name, juce::String ("ir2.wav"));
                expect (slots.getInfo (1).status == SlotInfo::Status::ready);

                // A failed load leaves the history alone
                const auto before = slots.getInfo (1).historySize;
                load (1, temp.folder.getChildFile ("missing.wav"));
                expectEquals (slots.getInfo (1).historySize, before);

                // The history travels with the IR when slots are swapped
                load (0, files[0]);
                load (0, files[1]);
                const auto historyOf0 = slots.getInfo (0).historySize;
                const auto historyOf1 = slots.getInfo (1).historySize;
                processor.swapSlots (0, 1);
                expectEquals (slots.getInfo (1).historySize, historyOf0);
                expectEquals (slots.getInfo (0).historySize, historyOf1);
                expectEquals (back (1).name, juce::String ("ir0.wav"));

                // The history is bounded
                for (int i = 0; i < SlotManager::kMaxHistory + 10; ++i)
                    load (2, files[i % 3]);

                expectEquals (slots.getInfo (2).historySize, SlotManager::kMaxHistory);

                // Restoring a project starts a fresh history (no entries for the restore itself)
                juce::MemoryBlock state;
                processor.getStateInformation (state);
                processor.setStateInformation (state.getData(), static_cast<int> (state.getSize()));

                for (int i = 0; i < kNumSlots; ++i)
                    expect (! slots.canGoBack (i), "slot " + juce::String (i + 1));

                // So does "Init"
                load (0, files[2]);
                processor.resetToDefaults();
                expect (! slots.canGoBack (0));
            }

            beginTest ("Solo target: only the browser's target slot is heard");
            {
                const auto irA = makeTestIR (1, 300, 31);
                const auto irB = makeTestIR (1, 300, 32);

                IReneProcessor processor;
                expect (processor.setBusesLayout (makeLayout (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo())));
                processor.getSlots().setSource (0, makeSource (irA, 48000.0, "a.wav"), false, false);
                processor.getSlots().setSource (1, makeSource (irB, 48000.0, "b.wav"), false, false);
                processor.setNonRealtime (true);
                processor.prepareToPlay (48000.0, 256);

                // Slot 2 is muted and slot 1 soloed: normally only slot 1 is heard...
                setParam (processor, "slot2_mute", 1.0f);
                setParam (processor, "slot1_solo", 1.0f);

                const auto input = makeNoise (2, 3000, 7);
                const auto expectedA = convolve (input.getReadPointer (0), 3000, irA.getReadPointer (0), 300);
                const auto expectedB = convolve (input.getReadPointer (0), 3000, irB.getReadPointer (0), 300);

                auto output = renderProcessor (processor, input, 256);
                expect (errorDb (output.getReadPointer (0), expectedA.data(), 3000) < -80.0);

                // ...but with "Solo target" on and slot 2 as the target, only slot 2 plays
                processor.uiState.targetSlot = 1;
                processor.auditionSolo = true;
                expectEquals (processor.readSettings().auditionSlot, 1);
                expect (! processor.readSettings().isSlotAudible (0));
                expect (processor.readSettings().isSlotAudible (1));
                expectEquals (processor.getParamRefs().read().auditionSlot, -1); // export ignores it

                processor.prepareToPlay (48000.0, 256); // start from a clean state: no gain ramps
                output = renderProcessor (processor, input, 256);
                expect (errorDb (output.getReadPointer (0), expectedB.data(), 3000) < -80.0);

                // It follows the target slot
                processor.uiState.targetSlot = 0;
                expectEquals (processor.readSettings().auditionSlot, 0);

                processor.auditionSolo = false;
                expectEquals (processor.readSettings().auditionSlot, -1);
            }

            beginTest ("Notes are saved with the project, not with presets");
            {
                IReneProcessor processor;
                expect (processor.getNotes().isEmpty());

                const auto text = juce::String::fromUTF8 ("Rhythm guitars, L/R.\nSM57 + R121 \xe2\x80\x93 Mesa V30");
                processor.setNotes (text);

                juce::MemoryBlock state;
                processor.getStateInformation (state);

                IReneProcessor restored;
                restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
                expectEquals (restored.getNotes(), text);

                // A preset carries no notes, and applying one keeps the project's notes
                const auto preset = processor.savePresetState (false);
                expect (! preset.toXmlString().contains ("Rhythm guitars"));
                restored.applyPresetState (preset);
                expectEquals (restored.getNotes(), text);

                // Older projects (no notes) load with empty notes
                IReneProcessor empty;
                juce::MemoryBlock emptyState;
                empty.getStateInformation (emptyState);
                restored.setStateInformation (emptyState.getData(), static_cast<int> (emptyState.getSize()));
                expect (restored.getNotes().isEmpty());
            }

            beginTest ("Export is mono by default");
            {
                const ExportOptions options;
                expect (! options.stereo);

                IReneProcessor processor;
                expect (! processor.exportOptions.stereo);
            }

            beginTest ("Editor: browser width, columns, folder names, menus, notes, Solo target");
            {
                GlobalSettings::Shared settings;
                settings->set ({});

                TempFolder temp;
                const auto irFolder = temp.folder.getChildFile ("48");
                irFolder.createDirectory();
                const auto irFile = irFolder.getChildFile ("Mesa SM57.wav");
                expect (writeWav (irFile, makeTestIR (1, 600, 41), 48000.0));

                settings->update ([&] (auto& s) { s.favouriteFolders = { irFolder.getFullPathName() }; });

                IReneProcessor processor;
                processor.prepareToPlay (48000.0, 256);
                processor.getLibrary().rescan();
                processor.getLibrary().waitForScan();
                pump();

                std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
                auto* irene = dynamic_cast<IReneEditor*> (editor.get());
                expect (irene != nullptr);

                if (irene == nullptr)
                    return;

                pump();
                auto* browser = irene->getBrowserForTests();
                browser->refreshForTests();

                // --- Width: dragging the edge makes the window wider at the same scale
                expectEquals (irene->getBrowserWidth(), GlobalSettings::kDefaultBrowserWidth);
                irene->setBrowserWidth (450, false);
                expectEquals (editor->getWidth(), IReneEditor::kBaseWidth + 450 + IReneEditor::kGap);
                expectEquals (browser->getWidth(), 450);
                expectEquals (settings->get().browserWidth, GlobalSettings::kDefaultBrowserWidth); // not yet stored
                irene->setBrowserWidth (450, true);                                                 // mouse released
                expectEquals (settings->get().browserWidth, 450);
                irene->setBrowserWidth (2000, true);
                expectEquals (irene->getBrowserWidth(), GlobalSettings::kMaxBrowserWidth);
                irene->setBrowserWidth (450, true);
                editor->createComponentSnapshot (editor->getLocalBounds());

                // --- Folder display names
                expectEquals (browser->getLocationLabel (2), juce::String ("48"));
                settings->update ([&] (auto& s) { s.folderNames[irFolder.getFullPathName()] = "Mesa 4x12"; });
                pump();
                expectEquals (browser->getLocationLabel (2), juce::String ("Mesa 4x12"));

                auto items = enabledItems (browser->createFolderMenu (irFolder.getFullPathName()));
                expect (items.contains (ui::BrowserPanel::renameFolderItem));
                expect (items.contains (ui::BrowserPanel::resetFolderNameItem));
                expect (items.contains (ui::BrowserPanel::openFolderItem));

                browser->handleFolderMenuResult (irFolder.getFullPathName(), ui::BrowserPanel::resetFolderNameItem);
                pump();
                expectEquals (browser->getLocationLabel (2), juce::String ("48"));
                expect (! enabledItems (browser->createFolderMenu (irFolder.getFullPathName())).contains (ui::BrowserPanel::resetFolderNameItem));

                // --- Right-click on a file: load into any slot
                expectEquals (browser->getCurrentFileList().size(), 1);
                items = enabledItems (browser->createRowMenu (0));

                for (int i = 0; i < kNumSlots; ++i)
                    expect (items.contains (ui::BrowserPanel::loadIntoSlotItem + i));

                expect (items.contains (ui::BrowserPanel::toggleFavouriteItem));
                expect (items.contains (ui::BrowserPanel::revealItem));

                browser->handleRowMenuResult (0, ui::BrowserPanel::loadIntoSlotItem + kNumSlots - 1);
                processor.getSlots().waitForPendingLoads();
                expectEquals (processor.getSlots().getInfo (kNumSlots - 1).reference.name, juce::String ("Mesa SM57.wav"));

                browser->handleRowMenuResult (0, ui::BrowserPanel::toggleFavouriteItem);
                expect (processor.getLibrary().isFavourite (hashFile (irFile)));
                browser->handleRowMenuResult (0, ui::BrowserPanel::toggleFavouriteItem);
                expect (! processor.getLibrary().isFavourite (hashFile (irFile)));

                // --- The back button follows the slot's history
                pump();
                processor.getSlots().goBack (kNumSlots - 1);
                processor.getSlots().waitForPendingLoads();
                expect (processor.getSlots().getInfo (kNumSlots - 1).status == SlotInfo::Status::empty);

                // --- Columns: hiding one is remembered for the next instance
                auto& header = browser->getHeaderForTests();
                expect (header.isColumnVisible (4)); // Rate
                header.setColumnVisible (4, false);

                // --- Notes
                auto* notes = irene->getNotesPanelForTests();
                notes->setVisible (true);
                notes->getEditorForTests().setText ("Doubled rhythm", true);
                pump(); // the text editor reports changes asynchronously
                expectEquals (processor.getNotes(), juce::String ("Doubled rhythm"));
                expect (irene->getNotesButtonForTests().getToggleState());
                editor->createComponentSnapshot (editor->getLocalBounds());
                notes->setVisible (false);

                // --- Solo target: switched off when the window closes
                processor.auditionSolo = true;
                editor->createComponentSnapshot (editor->getLocalBounds()); // draws the "not heard" overlays
                editor.reset();
                pump();
                expect (! processor.auditionSolo.load());
                expect (settings->get().browserColumns.isNotEmpty());

                // A new window opens with the stored width and columns
                editor.reset (processor.createEditor());
                irene = dynamic_cast<IReneEditor*> (editor.get());
                pump();
                expectEquals (irene->getBrowserWidth(), 450);
                expect (! irene->getBrowserForTests()->getHeaderForTests().isColumnVisible (4));
                expect (irene->getBrowserForTests()->getHeaderForTests().isColumnVisible (3));
                expect (irene->getBrowserForTests()->getHeaderForTests().isColumnVisible (2)); // Name
                expect (irene->getNotesButtonForTests().getToggleState()); // the notes are still there

                // Closing the browser switches Solo target off too
                processor.auditionSolo = true;
                settings->update ([] (auto& s) { s.browserOpen = false; });
                pump();
                expect (! processor.auditionSolo.load());

                editor.reset();
                settings->set ({});
                pump();
            }
        }
    };

    WorkflowTests workflowTests;
}
