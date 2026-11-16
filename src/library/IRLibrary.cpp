#include "IRLibrary.h"

namespace irene
{
    namespace
    {
        const juce::Identifier kLibraryRoot { "IReneLibrary" };
        const juce::Identifier kIndexRoot { "IReneIndex" };
        const juce::Identifier kVersion { "version" };
        const juce::Identifier kFavourites { "Favourites" };
        const juce::Identifier kRecent { "Recent" };
        const juce::Identifier kEntry { "IR" };

        const juce::Identifier kPath { "path" };
        const juce::Identifier kName { "name" };
        const juce::Identifier kHash { "hash" };
        const juce::Identifier kSize { "size" };
        const juce::Identifier kModified { "modified" };
        const juce::Identifier kChannels { "channels" };
        const juce::Identifier kRate { "sampleRate" };
        const juce::Identifier kLength { "lengthSamples" };

        juce::String withTrailingSeparator (const juce::String& folder)
        {
            return folder.endsWithChar (juce::File::getSeparatorChar()) ? folder : folder + juce::File::getSeparatorString();
        }

        juce::ValueTree findChildWithHash (const juce::ValueTree& parent, const juce::String& hash)
        {
            return hash.isEmpty() ? juce::ValueTree() : parent.getChildWithProperty (kHash, hash);
        }
    }

    //==============================================================================
    IRFileEntry IRFileEntry::fromSource (const IRSource& source)
    {
        IRFileEntry e;
        e.path = source.reference.path;
        e.name = source.reference.name;
        e.hash = source.reference.hash;
        e.numChannels = source.audio.getNumChannels();
        e.sampleRate = source.sampleRate;
        e.lengthSamples = source.audio.getNumSamples();
        return e;
    }

    IRFileEntry IRLibrary::entryFromTree (const juce::ValueTree& t)
    {
        IRFileEntry e;
        e.path = t.getProperty (kPath).toString();
        e.name = t.getProperty (kName).toString();
        e.hash = t.getProperty (kHash).toString();
        e.size = static_cast<juce::int64> (t.getProperty (kSize, 0));
        e.modified = static_cast<juce::int64> (t.getProperty (kModified, 0));
        e.numChannels = t.getProperty (kChannels, 0);
        e.sampleRate = t.getProperty (kRate, 0.0);
        e.lengthSamples = static_cast<juce::int64> (t.getProperty (kLength, 0));
        return e;
    }

    void IRLibrary::writeEntryToTree (juce::ValueTree& t, const IRFileEntry& e)
    {
        // Only the properties this version knows are written; anything else
        // already on the node (e.g. tags from a newer version) is left alone.
        t.setProperty (kPath, e.path, nullptr);
        t.setProperty (kName, e.name, nullptr);
        t.setProperty (kHash, e.hash, nullptr);
        t.setProperty (kSize, e.size, nullptr);
        t.setProperty (kModified, e.modified, nullptr);
        t.setProperty (kChannels, e.numChannels, nullptr);
        t.setProperty (kRate, e.sampleRate, nullptr);
        t.setProperty (kLength, e.lengthSamples, nullptr);
    }

    //==============================================================================
    IRIndex::IRIndex (std::vector<IRFileEntry> entries)
        : files (std::move (entries))
    {
        std::sort (files.begin(), files.end(), [] (const auto& a, const auto& b) { return a.path < b.path; });

        byHash.reserve (files.size());

        for (size_t i = 0; i < files.size(); ++i)
            if (files[i].hash.isNotEmpty() && files[i].isValidAudio())
                byHash.emplace (files[i].hash, i);
    }

    const IRFileEntry* IRIndex::findByPath (const juce::String& path) const
    {
        const auto it = std::lower_bound (files.begin(), files.end(), path,
                                          [] (const IRFileEntry& e, const juce::String& p) { return e.path < p; });

        return it != files.end() && it->path == path ? &*it : nullptr;
    }

    const IRFileEntry* IRIndex::findByHash (const juce::String& hash) const
    {
        const auto it = byHash.find (hash);
        return it != byHash.end() ? &files[it->second] : nullptr;
    }

    const IRFileEntry* IRIndex::findByName (const juce::String& fileName) const
    {
        for (const auto& f : files)
            if (f.isValidAudio() && f.name.equalsIgnoreCase (fileName))
                return &f;

        return nullptr;
    }

    std::pair<size_t, size_t> IRIndex::rangeBelow (const juce::String& folder) const
    {
        const auto prefix = withTrailingSeparator (folder);
        const auto first = std::lower_bound (files.begin(), files.end(), prefix,
                                             [] (const IRFileEntry& e, const juce::String& p) { return e.path < p; });
        auto last = first;

        while (last != files.end() && last->path.startsWith (prefix))
            ++last;

        return { static_cast<size_t> (first - files.begin()), static_cast<size_t> (last - files.begin()) };
    }

    std::vector<const IRFileEntry*> IRIndex::filesIn (const juce::String& folder, bool includeSubfolders) const
    {
        std::vector<const IRFileEntry*> result;
        const auto prefixLength = withTrailingSeparator (folder).length();
        const auto [first, last] = rangeBelow (folder);

        for (auto i = first; i < last; ++i)
        {
            const auto& f = files[i];

            if (! f.isValidAudio())
                continue;

            if (includeSubfolders || ! f.path.substring (prefixLength).containsChar (juce::File::getSeparatorChar()))
                result.push_back (&f);
        }

        return result;
    }

    juce::StringArray IRIndex::subfoldersOf (const juce::String& folder) const
    {
        juce::StringArray result;
        const auto prefixLength = withTrailingSeparator (folder).length();
        const auto [first, last] = rangeBelow (folder);

        for (auto i = first; i < last; ++i)
        {
            if (! files[i].isValidAudio())
                continue;

            const auto rest = files[i].path.substring (prefixLength);
            const auto separator = rest.indexOfChar (juce::File::getSeparatorChar());

            if (separator > 0)
            {
                const auto name = rest.substring (0, separator);

                // entries are sorted, so equal names are adjacent
                if (result.isEmpty() || result[result.size() - 1] != name)
                    result.addIfNotAlreadyThere (name);
            }
        }

        result.sortNatural();
        return result;
    }

    //==============================================================================
    IRLibrary::IRLibrary()
        : juce::Thread ("IRene library scanner"),
          libraryFile (GlobalSettings::getDataDirectory().getChildFile ("library.xml")),
          indexFile (GlobalSettings::getDataDirectory().getChildFile ("index.bin")),
          favourites (kFavourites),
          recent (kRecent),
          index (std::make_shared<IRIndex>())
    {
        loadLibraryFile();
        loadIndexCache();

        folders = settings->get().favouriteFolders;
        settings->addListener (this);

        startThread (juce::Thread::Priority::low);
        rescan();
        startTimer (1500);
    }

    IRLibrary::~IRLibrary()
    {
        stopTimer();
        settings->removeListener (this);
        signalThreadShouldExit();
        notify();
        stopThread (10000);
    }

    //==============================================================================
    void IRLibrary::rebuildFavouriteLookup()
    {
        favouriteByHash.clear();

        for (int i = 0; i < favourites.getNumChildren(); ++i)
            favouriteByHash[favourites.getChild (i).getProperty (kHash).toString()] = i;
    }

    bool IRLibrary::isFavourite (const juce::String& hash) const
    {
        if (hash.isEmpty())
            return false;

        const juce::ScopedLock sl (lock);
        return favouriteByHash.count (hash) > 0;
    }

    void IRLibrary::setFavourite (const IRFileEntry& entry, bool shouldBeFavourite)
    {
        if (entry.hash.isEmpty())
            return;

        {
            const juce::ScopedLock sl (lock);
            auto existing = findChildWithHash (favourites, entry.hash);

            if (shouldBeFavourite == existing.isValid())
                return;

            if (shouldBeFavourite)
            {
                juce::ValueTree node { kEntry };
                writeEntryToTree (node, entry);
                favourites.appendChild (node, nullptr);
            }
            else
            {
                favourites.removeChild (existing, nullptr);
            }

            rebuildFavouriteLookup();
            saveLibraryFile();
        }

        sendChange();
    }

    std::vector<IRFileEntry> IRLibrary::getFavourites() const
    {
        const juce::ScopedLock sl (lock);
        std::vector<IRFileEntry> result;
        result.reserve (static_cast<size_t> (favourites.getNumChildren()));

        for (const auto& child : favourites)
            result.push_back (entryFromTree (child));

        return result;
    }

    void IRLibrary::addRecent (const IRFileEntry& entry)
    {
        if (entry.path.isEmpty())
            return;

        {
            const juce::ScopedLock sl (lock);

            // Already the newest entry: nothing to do (keeps rapid re-loads cheap).
            if (recent.getNumChildren() > 0 && recent.getChild (0).getProperty (kPath).toString() == entry.path
                && recent.getChild (0).getProperty (kHash).toString() == entry.hash)
                return;

            for (int i = recent.getNumChildren(); --i >= 0;)
            {
                const auto child = recent.getChild (i);

                if (child.getProperty (kPath).toString() == entry.path
                    || (entry.hash.isNotEmpty() && child.getProperty (kHash).toString() == entry.hash))
                    recent.removeChild (i, nullptr);
            }

            juce::ValueTree node { kEntry };
            writeEntryToTree (node, entry);
            recent.addChild (node, 0, nullptr);

            while (recent.getNumChildren() > kMaxRecent)
                recent.removeChild (recent.getNumChildren() - 1, nullptr);

            saveLibraryFile();
        }

        sendChange();
    }

    std::vector<IRFileEntry> IRLibrary::getRecent() const
    {
        const juce::ScopedLock sl (lock);
        std::vector<IRFileEntry> result;

        for (const auto& child : recent)
            result.push_back (entryFromTree (child));

        return result;
    }

    //==============================================================================
    void IRLibrary::loadLibraryFile()
    {
        const juce::ScopedLock sl (lock);

        favourites = juce::ValueTree (kFavourites);
        recent = juce::ValueTree (kRecent);

        if (const auto xml = juce::parseXML (libraryFile))
        {
            const auto tree = juce::ValueTree::fromXml (*xml);

            if (tree.hasType (kLibraryRoot))
            {
                if (auto f = tree.getChildWithName (kFavourites); f.isValid())
                    favourites = f.createCopy();

                if (auto r = tree.getChildWithName (kRecent); r.isValid())
                    recent = r.createCopy();
            }
        }

        rebuildFavouriteLookup();
        lastKnownModification = libraryFile.getLastModificationTime();
    }

    void IRLibrary::saveLibraryFile()
    {
        // called with the lock held
        juce::ValueTree tree { kLibraryRoot };
        tree.setProperty (kVersion, kFileVersion, nullptr);
        tree.appendChild (favourites.createCopy(), nullptr);
        tree.appendChild (recent.createCopy(), nullptr);

        libraryFile.getParentDirectory().createDirectory();

        if (const auto xml = tree.createXml())
            libraryFile.replaceWithText (xml->toString()); // temp file + rename

        lastKnownModification = libraryFile.getLastModificationTime();
    }

    bool IRLibrary::reloadIfChangedOnDisk()
    {
        {
            const juce::ScopedLock sl (lock);

            if (libraryFile.getLastModificationTime() == lastKnownModification)
                return false;

            loadLibraryFile();
        }

        sendChange();
        return true;
    }

    void IRLibrary::timerCallback()
    {
        reloadIfChangedOnDisk();
    }

    //==============================================================================
    void IRLibrary::loadIndexCache()
    {
        juce::FileInputStream in (indexFile);

        if (! in.openedOk())
            return;

        juce::GZIPDecompressorInputStream unzip (in);
        const auto tree = juce::ValueTree::readFromStream (unzip);

        if (! tree.hasType (kIndexRoot) || static_cast<int> (tree.getProperty (kVersion, 0)) != kFileVersion)
            return; // unknown format: it's only a cache, so just rebuild it

        std::vector<IRFileEntry> entries;
        entries.reserve (static_cast<size_t> (tree.getNumChildren()));

        for (const auto& child : tree)
            entries.push_back (entryFromTree (child));

        const juce::ScopedLock sl (lock);
        index = std::make_shared<IRIndex> (std::move (entries));
    }

    void IRLibrary::saveIndexCache (const IRIndex& snapshot) const
    {
        juce::ValueTree tree { kIndexRoot };
        tree.setProperty (kVersion, kFileVersion, nullptr);

        for (const auto& e : snapshot.all())
        {
            juce::ValueTree node { kEntry };
            writeEntryToTree (node, e);
            tree.appendChild (node, nullptr);
        }

        indexFile.getParentDirectory().createDirectory();
        juce::TemporaryFile temp (indexFile);

        {
            juce::FileOutputStream out (temp.getFile());

            if (! out.openedOk())
                return;

            juce::GZIPCompressorOutputStream zip (out, 5);
            tree.writeToStream (zip);
        }

        temp.overwriteTargetFileWithTemporary();
    }

    std::shared_ptr<const IRIndex> IRLibrary::getIndex() const
    {
        const juce::ScopedLock sl (lock);
        return index;
    }

    juce::StringArray IRLibrary::getFolders() const
    {
        const juce::ScopedLock sl (lock);
        return folders;
    }

    void IRLibrary::publishIndex (std::vector<IRFileEntry> entries)
    {
        auto snapshot = std::make_shared<IRIndex> (std::move (entries));

        {
            const juce::ScopedLock sl (lock);
            index = std::move (snapshot);
        }

        sendChange();
    }

    void IRLibrary::setFolders (const juce::StringArray& newFolders)
    {
        {
            const juce::ScopedLock sl (lock);

            if (folders == newFolders)
                return;

            folders = newFolders;
        }

        rescan();
    }

    void IRLibrary::broadcasterChanged (Broadcaster*)
    {
        setFolders (settings->get().favouriteFolders);
    }

    void IRLibrary::rescan()
    {
        scanRequested = true;
        scanning = true;
        notify();
        sendChange();
    }

    void IRLibrary::waitForScan()
    {
        jassert (juce::Thread::getCurrentThreadId() != getThreadId());

        while (scanning.load() && ! threadShouldExit())
            juce::Thread::sleep (3);
    }

    //==============================================================================
    void IRLibrary::run()
    {
        while (! threadShouldExit())
        {
            if (! scanRequested.exchange (false))
            {
                scanning = false;
                wait (500);
                continue;
            }

            scanning = true;
            filesSeen = 0;

            const auto roots = getFolders();
            const auto previous = getIndex();
            std::vector<IRFileEntry> found;
            auto lastPublish = juce::Time::getMillisecondCounter();
            auto aborted = false;

            for (const auto& root : roots)
            {
                if (! juce::File::isAbsolutePath (root))
                    continue;

                const juce::File folder (root);

                if (! folder.isDirectory())
                    continue;

                for (const auto& item : juce::RangedDirectoryIterator (folder, true, "*", juce::File::findFiles))
                {
                    if (threadShouldExit() || scanRequested.load())
                    {
                        aborted = true;
                        break;
                    }

                    const auto file = item.getFile();

                    if (! isSupportedIRFile (file) || file.isHidden())
                        continue;

                    IRFileEntry entry;
                    entry.path = file.getFullPathName();
                    entry.name = file.getFileName();
                    entry.size = item.getFileSize();
                    entry.modified = item.getModificationTime().toMilliseconds();

                    const auto* known = previous->findByPath (entry.path);

                    if (known != nullptr && known->size == entry.size && known->modified == entry.modified && known->hash.isNotEmpty())
                    {
                        entry = *known; // unchanged: reuse the cached analysis
                    }
                    else
                    {
                        const auto info = probeIRFile (file);
                        entry.numChannels = info.valid ? info.numChannels : 0;
                        entry.sampleRate = info.valid ? info.sampleRate : 0.0;
                        entry.lengthSamples = info.valid ? info.lengthInSamples : 0;
                        entry.hash = hashFile (file);
                        ++filesAnalysed;
                    }

                    found.push_back (std::move (entry));
                    ++filesSeen;

                    // Let results appear while a long first scan is still running.
                    if (juce::Time::getMillisecondCounter() - lastPublish > 700)
                    {
                        lastPublish = juce::Time::getMillisecondCounter();
                        // What has been found so far, plus everything known from the
                        // previous index that this scan hasn't reached yet.
                        auto partial = found;
                        partial.insert (partial.end(), previous->all().begin(), previous->all().end());
                        std::stable_sort (partial.begin(), partial.end(), [] (const auto& x, const auto& y) { return x.path < y.path; });
                        partial.erase (std::unique (partial.begin(), partial.end(), [] (const auto& x, const auto& y) { return x.path == y.path; }),
                                       partial.end());
                        publishIndex (std::move (partial));
                    }
                }

                if (aborted)
                    break;
            }

            if (aborted)
                continue; // a new request (or shutdown) takes over

            // Several roots may overlap (a folder and one of its subfolders).
            std::sort (found.begin(), found.end(), [] (const auto& a, const auto& b) { return a.path < b.path; });
            found.erase (std::unique (found.begin(), found.end(), [] (const auto& a, const auto& b) { return a.path == b.path; }), found.end());

            publishIndex (std::move (found));

            const auto snapshot = getIndex();
            saveIndexCache (*snapshot);
            updateFavouritePaths (*snapshot);

            if (! scanRequested.load())
                scanning = false;

            sendChange();
        }

        scanning = false;
    }

    void IRLibrary::updateFavouritePaths (const IRIndex& snapshot)
    {
        // A favourite whose file has moved or been renamed is recognised by its
        // content hash; remember where it lives now.
        auto changed = false;

        {
            const juce::ScopedLock sl (lock);

            for (auto* list : { &favourites, &recent })
            {
                for (auto child : *list)
                {
                    const auto path = child.getProperty (kPath).toString();
                    const auto hash = child.getProperty (kHash).toString();
                    const auto* byHash = snapshot.findByHash (hash);

                    if (byHash == nullptr || byHash->path == path)
                        continue;

                    // Only follow the file if the old location is really gone
                    // (otherwise it's a copy, and the favourite stays where it is).
                    if (snapshot.findByPath (path) != nullptr
                        || (juce::File::isAbsolutePath (path) && juce::File (path).existsAsFile()))
                        continue;

                    child.setProperty (kPath, byHash->path, nullptr);
                    child.setProperty (kName, byHash->name, nullptr);
                    changed = true;
                }
            }

            if (changed)
                saveLibraryFile();
        }

        if (changed)
            sendChange();
    }

    //==============================================================================
    juce::File IRLibrary::resolve (const IRReference& reference) const
    {
        if (juce::File::isAbsolutePath (reference.path))
        {
            const juce::File file (reference.path);

            if (file.existsAsFile())
                return file;
        }

        const auto snapshot = getIndex();

        if (reference.hash.isNotEmpty())
        {
            if (const auto* e = snapshot->findByHash (reference.hash))
                if (juce::File (e->path).existsAsFile())
                    return juce::File (e->path);

            // Favourites and recents can live outside the scanned folders.
            const juce::ScopedLock sl (lock);

            for (const auto* list : { &favourites, &recent })
            {
                const auto node = findChildWithHash (*list, reference.hash);
                const auto path = node.getProperty (kPath).toString();

                if (node.isValid() && juce::File::isAbsolutePath (path) && juce::File (path).existsAsFile())
                    return juce::File (path);
            }
        }

        if (reference.name.isNotEmpty())
            if (const auto* e = snapshot->findByName (reference.name))
                if (juce::File (e->path).existsAsFile())
                    return juce::File (e->path);

        return {};
    }
}
