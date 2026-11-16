#include "TestHelpers.h"

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "library/IRLibrary.h"

namespace
{
    using namespace irene;
    using namespace irene::test;

    void pump (int milliseconds = 50)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
    }

    /** Makes the library pick up the current favourite folders and finish scanning. */
    void settle (IRLibrary& library)
    {
        pump();
        library.waitForScan();
        pump();
    }

    void setFolders (const juce::StringArray& folders)
    {
        GlobalSettings::Shared settings;
        settings->update ([&] (auto& d) { d.favouriteFolders = folders; });
    }

    void resetLibraryFiles()
    {
        const auto dir = GlobalSettings::getDataDirectory();
        dir.getChildFile ("library.xml").deleteFile();
        dir.getChildFile ("index.bin").deleteFile();
        GlobalSettings::Shared()->set ({});
        pump();
    }

    /** Different seeds give different content, hence different hashes. */
    juce::File makeIRFile (const juce::File& file, int seed, double rate = 48000.0, int channels = 1, int length = 600)
    {
        file.getParentDirectory().createDirectory();
        writeWav (file, makeTestIR (channels, length, seed), rate, 24);
        return file;
    }

    IRFileEntry entryFor (const juce::File& file)
    {
        const auto info = probeIRFile (file);
        IRFileEntry e;
        e.path = file.getFullPathName();
        e.name = file.getFileName();
        e.hash = hashFile (file);
        e.numChannels = info.numChannels;
        e.sampleRate = info.sampleRate;
        e.lengthSamples = info.lengthInSamples;
        return e;
    }

    class LibraryTests final : public juce::UnitTest
    {
    public:
        LibraryTests() : juce::UnitTest ("IR library", "IRene") {}

        void runTest() override
        {
            beginTest ("Scanning favourite folders builds the index");
            {
                resetLibraryFiles();
                TempFolder temp;
                const auto root = temp.folder.getChildFile ("IRs");

                makeIRFile (root.getChildFile ("b top.wav"), 1);
                makeIRFile (root.getChildFile ("A top.wav"), 2, 96000.0, 2, 1200);
                makeIRFile (root.getChildFile ("Mesa").getChildFile ("57 edge.wav"), 3);
                makeIRFile (root.getChildFile ("Mesa").getChildFile ("121 cone.wav"), 4, 44100.0);
                makeIRFile (root.getChildFile ("Mesa").getChildFile ("Room").getChildFile ("far.wav"), 5);
                makeIRFile (root.getChildFile ("Marshall").getChildFile ("421.wav"), 6);
                root.getChildFile ("Empty").createDirectory();
                root.getChildFile ("readme.txt").replaceWithText ("not an IR");
                root.getChildFile ("broken.wav").replaceWithText ("not audio either");

                setFolders ({ root.getFullPathName() });
                IRLibrary library;
                settle (library);

                expect (! library.isScanning());
                const auto index = library.getIndex();
                expectEquals (library.getNumFilesAnalysed(), 7); // 6 IRs + the broken .wav; the .txt is skipped

                // Folder listing
                auto top = index->filesIn (root.getFullPathName(), false);
                expectEquals (static_cast<int> (top.size()), 2); // broken.wav is not listed
                expectEquals (static_cast<int> (index->filesIn (root.getFullPathName(), true).size()), 6);
                expectEquals (index->subfoldersOf (root.getFullPathName()).joinIntoString ("|"), juce::String ("Marshall|Mesa")); // "Empty" has no IRs
                expectEquals (index->subfoldersOf (root.getChildFile ("Mesa").getFullPathName()).joinIntoString ("|"), juce::String ("Room"));
                expectEquals (static_cast<int> (index->filesIn (root.getChildFile ("Mesa").getFullPathName(), false).size()), 2);
                expectEquals (static_cast<int> (index->filesIn (root.getChildFile ("Mesa").getFullPathName(), true).size()), 3);
                expectEquals (static_cast<int> (index->filesIn (root.getChildFile ("Mes").getFullPathName(), true).size()), 0); // no prefix mix-ups

                // Metadata and hash
                const auto stereoFile = root.getChildFile ("A top.wav");
                const auto* entry = index->findByPath (stereoFile.getFullPathName());
                expect (entry != nullptr);

                if (entry != nullptr)
                {
                    expectEquals (entry->numChannels, 2);
                    expectEquals (entry->sampleRate, 96000.0);
                    expectEquals (static_cast<int> (entry->lengthSamples), 1200);
                    expectEquals (entry->hash, hashFile (stereoFile));
                    expectEquals (entry->name, juce::String ("A top.wav"));
                    expect (index->findByHash (entry->hash) == entry);
                }

                expect (index->findByName ("421.WAV") != nullptr); // by name, ignoring case
                expect (index->findByPath (root.getChildFile ("nope.wav").getFullPathName()) == nullptr);

                // --- Only changed files are analysed again -------------------------
                library.rescan();
                settle (library);
                expectEquals (library.getNumFilesAnalysed(), 7);

                const auto changed = root.getChildFile ("b top.wav");
                makeIRFile (changed, 99, 48000.0, 1, 900);
                changed.setLastModificationTime (juce::Time::getCurrentTime() + juce::RelativeTime::seconds (10));
                makeIRFile (root.getChildFile ("Marshall").getChildFile ("new.wav"), 7);
                root.getChildFile ("Mesa").getChildFile ("121 cone.wav").deleteFile();

                library.rescan();
                settle (library);
                expectEquals (library.getNumFilesAnalysed(), 9); // the changed and the new file

                const auto after = library.getIndex();
                expectEquals (static_cast<int> (after->filesIn (root.getFullPathName(), true).size()), 6);
                expect (after->findByPath (root.getChildFile ("Mesa").getChildFile ("121 cone.wav").getFullPathName()) == nullptr);
                expectEquals (static_cast<int> (after->findByPath (changed.getFullPathName())->lengthSamples), 900);
                expectEquals (after->findByPath (changed.getFullPathName())->hash, hashFile (changed));

                // --- The index is cached on disk -------------------------------------
                expect (library.getIndexFile().existsAsFile());
                {
                    IRLibrary second;
                    expectEquals (static_cast<int> (second.getIndex()->filesIn (root.getFullPathName(), true).size()), 6); // available at once
                    settle (second);
                    expectEquals (second.getNumFilesAnalysed(), 0); // nothing had to be opened again
                }

                // --- Removing the folder empties the index ----------------------------
                setFolders ({});
                settle (library);
                expectEquals (library.getIndex()->size(), 0);
            }

            beginTest ("Favourites: stored globally, survive moving and renaming");
            {
                resetLibraryFiles();
                TempFolder temp;
                const auto root = temp.folder.getChildFile ("IRs");
                const auto fileA = makeIRFile (root.getChildFile ("a.wav"), 11);
                const auto fileB = makeIRFile (root.getChildFile ("pack").getChildFile ("b.wav"), 12);
                const auto outside = makeIRFile (temp.folder.getChildFile ("elsewhere").getChildFile ("c.wav"), 13);

                setFolders ({ root.getFullPathName() });
                IRLibrary library;
                settle (library);

                const auto hashA = hashFile (fileA);
                const auto hashB = hashFile (fileB);
                expect (! library.isFavourite (hashA));

                library.setFavourite (entryFor (fileA), true);
                library.setFavourite (entryFor (fileB), true);
                library.setFavourite (entryFor (outside), true); // not in a favourite folder: still allowed
                library.setFavourite (entryFor (fileA), true);   // twice is harmless

                expect (library.isFavourite (hashA) && library.isFavourite (hashB));
                expectEquals (static_cast<int> (library.getFavourites().size()), 3);

                // The file has a version, and a second instance sees the same favourites
                const auto xml = juce::parseXML (library.getLibraryFile());
                expect (xml != nullptr && xml->getIntAttribute ("version") == IRLibrary::kFileVersion);

                {
                    IRLibrary other;
                    expect (other.isFavourite (hashA));
                    expectEquals (static_cast<int> (other.getFavourites().size()), 3);
                }

                // Rename one file and move the other: the stars follow the content
                const auto renamedA = root.getChildFile ("a (renamed).wav");
                const auto movedB = root.getChildFile ("other pack").getChildFile ("b.wav");
                movedB.getParentDirectory().createDirectory();
                expect (fileA.moveFileTo (renamedA));
                expect (fileB.moveFileTo (movedB));

                library.rescan();
                settle (library);

                expect (library.isFavourite (hashA) && library.isFavourite (hashB));
                juce::StringArray paths;

                for (const auto& f : library.getFavourites())
                    paths.add (f.path);

                expect (paths.contains (renamedA.getFullPathName()));
                expect (paths.contains (movedB.getFullPathName()));
                expect (paths.contains (outside.getFullPathName()));
                expect (! paths.contains (fileA.getFullPathName()));

                // A copy doesn't steal the favourite: the original keeps its path
                expect (renamedA.copyFileTo (root.getChildFile ("0 copy of a.wav")));
                library.rescan();
                settle (library);
                paths.clear();

                for (const auto& f : library.getFavourites())
                    paths.add (f.path);

                expect (paths.contains (renamedA.getFullPathName()));

                // Un-star
                library.setFavourite (entryFor (movedB), false);
                expect (! library.isFavourite (hashB));
                expectEquals (static_cast<int> (library.getFavourites().size()), 2);

                setFolders ({});
                settle (library);
            }

            beginTest ("Library file: unknown attributes (future tags, ratings) are preserved");
            {
                resetLibraryFiles();
                const auto file = GlobalSettings::getDataDirectory().getChildFile ("library.xml");

                // A file as a future version might write it
                file.replaceWithText (R"(<?xml version="1.0" encoding="UTF-8"?>
<IReneLibrary version="2" futureGlobalOption="yes">
  <Favourites>
    <IR path="/irs/old.wav" name="old.wav" hash="aaaa-10" channels="1" sampleRate="48000" lengthSamples="100" rating="5" tags="bright;tight">
      <Note text="great on rhythm"/>
    </IR>
  </Favourites>
  <Recent/>
</IReneLibrary>)");

                IRLibrary library;
                expect (library.isFavourite ("aaaa-10"));

                IRFileEntry another;
                another.path = "/irs/new.wav";
                another.name = "new.wav";
                another.hash = "bbbb-20";
                another.numChannels = 2;
                another.sampleRate = 44100.0;
                library.setFavourite (another, true); // forces a save

                const auto text = file.loadFileAsString();
                expect (text.contains ("rating=\"5\""));
                expect (text.contains ("tags=\"bright;tight\""));
                expect (text.contains ("great on rhythm"));
                expect (text.contains ("bbbb-20"));

                // A damaged file doesn't break anything
                file.replaceWithText ("garbage");
                IRLibrary damaged;
                expectEquals (static_cast<int> (damaged.getFavourites().size()), 0);
            }

            beginTest ("Recent: newest first, at most 50, no duplicates");
            {
                resetLibraryFiles();
                IRLibrary library;

                for (int i = 0; i < 60; ++i)
                {
                    IRFileEntry e;
                    e.path = "/irs/file" + juce::String (i) + ".wav";
                    e.name = "file" + juce::String (i) + ".wav";
                    e.hash = "hash" + juce::String (i);
                    library.addRecent (e);
                }

                auto recent = library.getRecent();
                expectEquals (static_cast<int> (recent.size()), IRLibrary::kMaxRecent);
                expectEquals (recent.front().name, juce::String ("file59.wav"));
                expectEquals (recent.back().name, juce::String ("file10.wav"));

                // Loading an older one again moves it to the front
                IRFileEntry again;
                again.path = "/irs/file30.wav";
                again.name = "file30.wav";
                again.hash = "hash30";
                library.addRecent (again);

                recent = library.getRecent();
                expectEquals (static_cast<int> (recent.size()), IRLibrary::kMaxRecent);
                expectEquals (recent.front().name, juce::String ("file30.wav"));
                expectEquals (recent[1].name, juce::String ("file59.wav"));

                IRLibrary other;
                expectEquals (other.getRecent().front().name, juce::String ("file30.wav"));
            }

            beginTest ("Changes show up in other instances and other processes");
            {
                resetLibraryFiles();

                // Same process: every plugin instance shares one library object
                IRLibrary::Shared first, second;
                expect (&*first == &*second);

                IRFileEntry e;
                e.path = "/irs/shared.wav";
                e.name = "shared.wav";
                e.hash = "shared-1";
                first->setFavourite (e, true);
                expect (second->isFavourite ("shared-1"));

                // Another process (simulated by a separate object) changes the file
                {
                    IRLibrary otherProcess;
                    IRFileEntry f;
                    f.path = "/irs/from-other-daw.wav";
                    f.name = "from-other-daw.wav";
                    f.hash = "other-2";
                    otherProcess.setFavourite (f, true);
                }

                first->getLibraryFile().setLastModificationTime (juce::Time::getCurrentTime() + juce::RelativeTime::seconds (10));
                expect (first->reloadIfChangedOnDisk());
                expect (first->isFavourite ("other-2"));
                expect (first->isFavourite ("shared-1"));
                expect (! first->reloadIfChangedOnDisk());

                first->setFavourite (e, false);
                e.hash = "other-2";
                first->setFavourite (e, false);
            }

            beginTest ("Resolving references: path, then content hash, then name");
            {
                resetLibraryFiles();
                TempFolder temp;
                const auto root = temp.folder.getChildFile ("IRs");
                const auto file = makeIRFile (root.getChildFile ("orig").getChildFile ("cab.wav"), 21);
                const auto hash = hashFile (file);

                setFolders ({ root.getFullPathName() });
                IRLibrary library;
                settle (library);

                // 1. the path still exists
                expectEquals (library.resolve ({ file.getFullPathName(), "cab.wav", hash }).getFullPathName(), file.getFullPathName());

                // 2. moved and renamed: found by hash
                const auto moved = root.getChildFile ("moved").getChildFile ("cab renamed.wav");
                moved.getParentDirectory().createDirectory();
                expect (file.moveFileTo (moved));
                library.rescan();
                settle (library);
                expectEquals (library.resolve ({ file.getFullPathName(), "cab.wav", hash }).getFullPathName(), moved.getFullPathName());

                // 3. content changed (e.g. re-exported) but same name: found by name
                expectEquals (library.resolve ({ "/old/disk/cab renamed.wav", "cab renamed.wav", "different-hash" }).getFullPathName(),
                              moved.getFullPathName());

                // 4. nothing matches
                expect (library.resolve ({ "/old/disk/unknown.wav", "unknown.wav", "nope" }) == juce::File());
                expect (library.resolve ({}) == juce::File());

                setFolders ({});
                settle (library);
            }

            beginTest ("Plugin integration: recents, prev/next, moved files in a saved state");
            {
                resetLibraryFiles();
                TempFolder temp;
                const auto root = temp.folder.getChildFile ("IRs");
                juce::Array<juce::File> files;

                for (int i = 1; i <= 4; ++i)
                    files.add (makeIRFile (root.getChildFile ("mic " + juce::String (i) + ".wav"), 30 + i));

                root.getChildFile ("notes.txt").replaceWithText ("x");

                setFolders ({ root.getFullPathName() });

                IReneProcessor processor;
                auto& library = processor.getLibrary();
                settle (library);

                // Loading a file puts it into "Recent"
                processor.getSlots().loadFile (0, files[1]);
                processor.getSlots().waitForPendingLoads();
                expectEquals (library.getRecent().front().path, files[1].getFullPathName());
                expectEquals (library.getRecent().front().hash, hashFile (files[1]));

                // prev/next without a browser list: steps through the IR's folder, wrapping around
                expectEquals (processor.getAdjacentFile (0, 1).getFileName(), juce::String ("mic 3.wav"));
                expectEquals (processor.getAdjacentFile (0, -1).getFileName(), juce::String ("mic 1.wav"));

                processor.getSlots().loadFile (0, files[3]);
                processor.getSlots().waitForPendingLoads();
                expectEquals (processor.getAdjacentFile (0, 1).getFileName(), juce::String ("mic 1.wav"));

                // with a list from the browser: steps through that list
                processor.setSlotListContext (0, { files[3].getFullPathName(), files[0].getFullPathName() });
                expectEquals (processor.getAdjacentFile (0, 1).getFileName(), juce::String ("mic 1.wav"));
                expectEquals (processor.getAdjacentFile (0, -1).getFileName(), juce::String ("mic 1.wav"));

                // an empty slot has nothing to step to
                expect (processor.getAdjacentFile (2, 1) == juce::File());

                // A state saved without audio still finds a file that was moved, via the index
                const auto state = processor.saveState (false);
                const auto moved = root.getChildFile ("sub").getChildFile ("mic four.wav");
                moved.getParentDirectory().createDirectory();
                expect (files[3].moveFileTo (moved));
                library.rescan();
                settle (library);

                IReneProcessor restored;
                restored.restoreState (state);
                const auto info = restored.getSlots().getInfo (0);
                expect (info.status == SlotInfo::Status::ready);
                expectEquals (info.reference.path, moved.getFullPathName());

                setFolders ({});
                settle (library);
            }

            beginTest ("Browser panel: views, search, auditioning");
            {
                resetLibraryFiles();
                TempFolder temp;
                const auto root = temp.folder.getChildFile ("IRs");

                for (int i = 0; i < 12; ++i)
                    makeIRFile (root.getChildFile ("cab " + juce::String (i).paddedLeft ('0', 2) + ".wav"), 50 + i);

                makeIRFile (root.getChildFile ("Deep").getChildFile ("hidden gem.wav"), 70);
                setFolders ({ root.getFullPathName() });

                IReneProcessor processor;
                processor.prepareToPlay (48000.0, 256);
                settle (processor.getLibrary());

                std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
                pump (100);

                auto* irene = dynamic_cast<IReneEditor*> (editor.get());
                expect (irene != nullptr);

                if (irene == nullptr)
                    return;

                // The browser opens in the first favourite folder
                expect (processor.browserState.location == IReneProcessor::BrowserState::Location::folder);
                expectEquals (processor.browserState.currentFolder, root.getFullPathName());

                auto* browser = irene->getBrowserForTests();
                expectEquals (browser->getCurrentFileList().size(), 12);

                // Draws without problems
                editor->createComponentSnapshot (editor->getLocalBounds());

                // Simulate what a click on a row does: the file goes to the target slot
                processor.uiState.targetSlot = 1;
                const auto list = browser->getCurrentFileList();
                browser->onFileChosen (juce::File (list[4]), list);
                processor.getSlots().waitForPendingLoads();
                pump();
                expectEquals (processor.getSlots().getInfo (1).reference.name, juce::String ("cab 04.wav"));
                expect (processor.getSlots().getInfo (0).status == SlotInfo::Status::empty);

                // ...and the slot's next button continues in that list
                expectEquals (processor.getAdjacentFile (1, 1).getFileName(), juce::String ("cab 05.wav"));

                // Search is a case-insensitive substring match and looks into subfolders
                processor.browserState.search = "GEM";
                browser->refreshForTests();
                expectEquals (browser->getCurrentFileList().size(), 1);
                expect (browser->getCurrentFileList()[0].endsWith ("hidden gem.wav"));

                processor.browserState.search = "cab 1";
                browser->refreshForTests();
                expectEquals (browser->getCurrentFileList().size(), 2); // cab 10, cab 11

                // Flat list of all subfolders
                processor.browserState.search = {};
                processor.browserState.includeSubfolders = true;
                browser->refreshForTests();
                expectEquals (browser->getCurrentFileList().size(), 13);

                // Starred only
                processor.getLibrary().setFavourite (entryFor (juce::File (list[2])), true);
                processor.getLibrary().setFavourite (entryFor (root.getChildFile ("Deep").getChildFile ("hidden gem.wav")), true);
                processor.browserState.includeSubfolders = false;
                processor.browserState.starredOnly = true;
                pump();
                browser->refreshForTests();
                expectEquals (browser->getCurrentFileList().size(), 2);

                // All favourites and Recent
                processor.browserState.starredOnly = false;
                processor.browserState.location = IReneProcessor::BrowserState::Location::allFavourites;
                browser->refreshForTests();
                expectEquals (browser->getCurrentFileList().size(), 2);

                processor.browserState.location = IReneProcessor::BrowserState::Location::recent;
                browser->refreshForTests();
                expectEquals (browser->getCurrentFileList().size(), 1);
                expect (browser->getCurrentFileList()[0].endsWith ("cab 04.wav"));

                // Descending sort
                processor.browserState.location = IReneProcessor::BrowserState::Location::folder;
                processor.browserState.sortForwards = false;
                browser->refreshForTests();
                expect (browser->getCurrentFileList()[0].endsWith ("cab 11.wav"));

                editor->createComponentSnapshot (editor->getLocalBounds());
                editor.reset();

                processor.getLibrary().setFavourite (entryFor (juce::File (list[2])), false);
                processor.getLibrary().setFavourite (entryFor (root.getChildFile ("Deep").getChildFile ("hidden gem.wav")), false);
                setFolders ({});
                settle (processor.getLibrary());
            }

            beginTest ("A large library stays fast");
            {
                // 5000 entries: building the index and listing a folder must be quick
                // enough to do on every keystroke in the search field.
                std::vector<IRFileEntry> entries;
                const auto sep = juce::File::getSeparatorString();
                const auto base = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("big").getFullPathName();

                for (int i = 0; i < 5000; ++i)
                {
                    IRFileEntry e;
                    e.name = "IR " + juce::String (i) + ".wav";
                    e.path = base + sep + "pack" + juce::String (i % 50) + sep + e.name;
                    e.hash = "h" + juce::String (i);
                    e.numChannels = 1;
                    e.sampleRate = 48000.0;
                    e.lengthSamples = 4800;
                    entries.push_back (e);
                }

                const auto start = juce::Time::getMillisecondCounterHiRes();
                const IRIndex index (std::move (entries));
                const auto all = index.filesIn (base, true);
                const auto subfolders = index.subfoldersOf (base);
                const auto one = index.filesIn (base + sep + "pack7", false);
                const auto elapsed = juce::Time::getMillisecondCounterHiRes() - start;

                expectEquals (static_cast<int> (all.size()), 5000);
                expectEquals (subfolders.size(), 50);
                expectEquals (static_cast<int> (one.size()), 100);
                expect (index.findByHash ("h4321") != nullptr);
                expect (elapsed < 500.0, "Index operations took " + juce::String (elapsed, 1) + " ms");
            }
        }
    };

    LibraryTests libraryTests;
}
