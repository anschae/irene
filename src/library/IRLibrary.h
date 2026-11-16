#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

#include <unordered_map>

#include "app/GlobalSettings.h"
#include "ir/IRSource.h"

namespace irene
{
    /** What the library knows about one IR file. */
    struct IRFileEntry
    {
        juce::String path; // full path
        juce::String name; // file name with extension
        juce::String hash; // content hash (empty if not computed yet)
        juce::int64 size = 0;
        juce::int64 modified = 0; // milliseconds since the epoch
        int numChannels = 0;      // 0 = not a readable audio file
        double sampleRate = 0.0;
        juce::int64 lengthSamples = 0;

        bool isValidAudio() const noexcept { return numChannels > 0 && sampleRate > 0.0; }
        double lengthSeconds() const noexcept { return sampleRate > 0.0 ? static_cast<double> (lengthSamples) / sampleRate : 0.0; }
        IRReference reference() const { return { path, name, hash }; }

        static IRFileEntry fromSource (const IRSource&);
    };

    /** An immutable snapshot of the scan index: every audio file found in the
        favourite folders. Cheap to share between threads; the browser reads it
        without any locking. */
    class IRIndex
    {
    public:
        IRIndex() = default;
        explicit IRIndex (std::vector<IRFileEntry> entries); // sorts by path

        const std::vector<IRFileEntry>& all() const noexcept { return files; }
        int size() const noexcept { return static_cast<int> (files.size()); }

        const IRFileEntry* findByPath (const juce::String& path) const;
        const IRFileEntry* findByHash (const juce::String& hash) const;
        const IRFileEntry* findByName (const juce::String& fileName) const;

        /** Valid audio files directly in `folder`, or anywhere below it. */
        std::vector<const IRFileEntry*> filesIn (const juce::String& folder, bool includeSubfolders) const;

        /** Names of the direct subfolders of `folder` that contain IRs (at any depth). */
        juce::StringArray subfoldersOf (const juce::String& folder) const;

    private:
        std::pair<size_t, size_t> rangeBelow (const juce::String& folder) const;

        std::vector<IRFileEntry> files; // sorted by path
        std::unordered_map<juce::String, size_t> byHash;
    };

    /** The global IR library: favourites, recently used IRs and the scan index
        of the favourite folders.

        - Global: stored in IRene's data folder, identical in every DAW.
        - Shared: one object per process (IRLibrary::Shared), so a change shows
          up at once in every open instance. Changes from other processes are
          detected by watching the file's modification time.
        - IRs are identified by content hash plus last known path, so
          favourites survive moving or renaming files.
        - Folders are scanned on a background thread. Metadata is cached in an
          index file and only new or changed files are analysed again.
        - Files are written atomically.

        The file format carries a version number, and attributes this version
        doesn't know (e.g. future tags and ratings) are kept untouched.

        Sends a change message (message thread) when anything changes. */
    class IRLibrary final : public juce::ChangeBroadcaster,
                            private juce::Thread,
                            private juce::Timer,
                            private juce::ChangeListener
    {
    public:
        using Shared = juce::SharedResourcePointer<IRLibrary>;

        static constexpr int kFileVersion = 1;
        static constexpr int kMaxRecent = 50;

        IRLibrary();
        ~IRLibrary() override;

        //==============================================================================
        // Favourites
        bool isFavourite (const juce::String& hash) const;
        void setFavourite (const IRFileEntry& entry, bool shouldBeFavourite);
        std::vector<IRFileEntry> getFavourites() const;

        // Recently loaded IRs, newest first
        void addRecent (const IRFileEntry& entry);
        std::vector<IRFileEntry> getRecent() const;

        //==============================================================================
        // Scan index
        std::shared_ptr<const IRIndex> getIndex() const;
        juce::StringArray getFolders() const;

        /** Starts a scan of the favourite folders (only changed files are analysed). */
        void rescan();
        bool isScanning() const noexcept { return scanning.load(); }
        int getNumFilesSeenInScan() const noexcept { return filesSeen.load(); }

        /** Number of files that had to be opened and hashed so far (for tests). */
        int getNumFilesAnalysed() const noexcept { return filesAnalysed.load(); }

        /** Blocks until no scan is running or requested. Not for the message thread in real use. */
        void waitForScan();

        //==============================================================================
        /** Finds the file a reference points to: at its path, else by content
            hash, else by file name. Returns an invalid File if nothing matches. */
        juce::File resolve (const IRReference& reference) const;

        /** Reloads favourites and recents if another process changed the file. */
        bool reloadIfChangedOnDisk();

        juce::File getLibraryFile() const { return libraryFile; }
        juce::File getIndexFile() const { return indexFile; }

    private:
        void run() override;
        void timerCallback() override;
        void changeListenerCallback (juce::ChangeBroadcaster*) override;

        void loadLibraryFile();
        void saveLibraryFile();
        void loadIndexCache();
        void saveIndexCache (const IRIndex&) const;
        void publishIndex (std::vector<IRFileEntry> entries);
        void updateFavouritePaths (const IRIndex&);
        void rebuildFavouriteLookup();
        void setFolders (const juce::StringArray& newFolders);

        static IRFileEntry entryFromTree (const juce::ValueTree&);
        static void writeEntryToTree (juce::ValueTree&, const IRFileEntry&);

        GlobalSettings::Shared settings;
        const juce::File libraryFile, indexFile;

        mutable juce::CriticalSection lock;
        juce::ValueTree favourites, recent; // children of type "IR"
        std::unordered_map<juce::String, int> favouriteByHash;
        std::shared_ptr<const IRIndex> index;
        juce::StringArray folders;
        juce::Time lastKnownModification;

        std::atomic<bool> scanRequested { false }, scanning { false };
        std::atomic<int> filesSeen { 0 }, filesAnalysed { 0 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IRLibrary)
    };
}
