#include "BrowserPanel.h"

#include "SlotStrip.h"

namespace irene::ui
{
    namespace
    {
        constexpr int kNumFixedLocations = 2; // "All favourites", "Recent"
        const juce::String kStar = juce::String::fromUTF8 ("\xe2\x98\x85");
        const juce::String kStarOutline = juce::String::fromUTF8 ("\xe2\x98\x86");

       #if JUCE_MAC
        const juce::String kFileManager { "Finder" };
       #elif JUCE_WINDOWS
        const juce::String kFileManager { "Explorer" };
       #else
        const juce::String kFileManager { "the file manager" };
       #endif
    }

    //==============================================================================
    int BrowserPanel::Locations::getNumRows()
    {
        return kNumFixedLocations + owner.folders.size();
    }

    void BrowserPanel::Locations::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool)
    {
        using Location = BrowserState::Location;
        const auto& s = owner.state;

        juce::String text;
        auto active = false;

        if (row == 0)
        {
            text = kStar + "  All favourites";
            active = s.location == Location::allFavourites;
        }
        else if (row == 1)
        {
            text = "Recent";
            active = s.location == Location::recent;
        }
        else if (row - kNumFixedLocations < owner.folders.size())
        {
            const auto path = owner.folders[row - kNumFixedLocations];
            text = owner.getLocationLabel (row);
            active = s.location == Location::folder && s.rootFolder == path;
        }

        if (active)
        {
            g.setColour (colours::accent.withAlpha (0.25f));
            g.fillRoundedRectangle (juce::Rectangle<int> (width, height).toFloat().reduced (1.0f), 3.0f);
        }

        g.setColour (active ? colours::text : (row < kNumFixedLocations ? colours::text : colours::textDim.brighter (0.3f)));
        g.setFont (font (13.0f, row < kNumFixedLocations || active));
        g.drawText (text, 8, 0, width - 12, height, juce::Justification::centredLeft);
    }

    void BrowserPanel::Locations::listBoxItemClicked (int row, const juce::MouseEvent& e)
    {
        using Location = BrowserState::Location;

        if (e.mods.isPopupMenu())
        {
            if (row >= kNumFixedLocations && row - kNumFixedLocations < owner.folders.size())
                owner.showFolderMenu (owner.folders[row - kNumFixedLocations]);

            return;
        }

        if (row == 0)
            owner.selectLocation (Location::allFavourites);
        else if (row == 1)
            owner.selectLocation (Location::recent);
        else if (row - kNumFixedLocations < owner.folders.size())
            owner.selectLocation (Location::folder, owner.folders[row - kNumFixedLocations]);
    }

    //==============================================================================
    bool BrowserPanel::FileTable::keyPressed (const juce::KeyPress& key)
    {
        const auto before = getSelectedRow();
        const auto handled = juce::TableListBox::keyPressed (key);
        const auto after = getSelectedRow();

        const auto isNavigation = key.isKeyCode (juce::KeyPress::upKey) || key.isKeyCode (juce::KeyPress::downKey)
                                  || key.isKeyCode (juce::KeyPress::pageUpKey) || key.isKeyCode (juce::KeyPress::pageDownKey)
                                  || key.isKeyCode (juce::KeyPress::homeKey) || key.isKeyCode (juce::KeyPress::endKey);

        if (handled && isNavigation && after != before && after >= 0 && onKeyboardSelection)
            onKeyboardSelection (after);

        return handled;
    }

    //==============================================================================
    BrowserPanel::BrowserPanel (IReneProcessor& p, ConfirmOverlay& c)
        : processor (p), confirm (c), library (p.getLibrary()), settings (p.getGlobalSettings()), state (p.browserState)
    {
        heading.setText ("IR BROWSER", juce::dontSendNotification);
        heading.setFont (font (12.0f, true));
        heading.setColour (juce::Label::textColourId, colours::textDim);
        addAndMakeVisible (heading);

        locationList.setModel (&locationsModel);
        locationList.setRowHeight (22);
        locationList.setColour (juce::ListBox::backgroundColourId, colours::background);
        locationList.setTooltip ("Favourite folders are set up in Settings. Right-click a folder to rename it or open it in " + kFileManager + ".");
        addAndMakeVisible (locationList);

        styleCaption (pathLabel, 12.0f, colours::text, juce::Justification::centredLeft);
        addAndMakeVisible (pathLabel);

        upButton.setTooltip ("Go to the parent folder");
        upButton.onClick = [this]
        {
            if (state.currentFolder != state.rootFolder && juce::File::isAbsolutePath (state.currentFolder))
                navigateTo (juce::File (state.currentFolder).getParentDirectory().getFullPathName());
        };
        addAndMakeVisible (upButton);

        subfoldersButton.setClickingTogglesState (true);
        subfoldersButton.setTooltip ("Show the files of all subfolders in one flat list");
        subfoldersButton.onClick = [this]
        {
            state.includeSubfolders = subfoldersButton.getToggleState();
            rebuild();
        };
        addAndMakeVisible (subfoldersButton);

        starredButton.setClickingTogglesState (true);
        starredButton.setColour (juce::TextButton::buttonOnColourId, colours::solo);
        starredButton.setTooltip ("Show only the favourite IRs in this folder");
        starredButton.onClick = [this]
        {
            state.starredOnly = starredButton.getToggleState();
            rebuild();
        };
        addAndMakeVisible (starredButton);

        search.setTextToShowWhenEmpty ("Search", colours::textDim);
        search.setFont (font (13.0f));
        search.setTooltip ("Filter the current list by name");
        search.setText (state.search, false);
        search.onTextChange = [this]
        {
            state.search = search.getText();
            rebuild();
        };
        search.onEscapeKey = [this] { search.setText ({}, true); };
        search.onReturnKey = [this] { table.grabKeyboardFocus(); };
        addAndMakeVisible (search);

        // Columns can be resized by dragging their borders; Ch, Rate and Length
        // can be hidden by right-clicking the header. The layout is a global
        // setting, so every new instance shows the browser the same way.
        using Flags = juce::TableHeaderComponent::ColumnPropertyFlags;
        auto& header = table.getHeader();
        header.addColumn (kStar, starColumn, 24, 24, 24, Flags::visible);
        header.addColumn ("Name", nameColumn, 150, 60, -1, Flags::visible | Flags::resizable | Flags::sortable);
        header.addColumn ("Ch", channelsColumn, 30, 24, 70, Flags::visible | Flags::resizable | Flags::appearsOnColumnMenu);
        header.addColumn ("Rate", rateColumn, 42, 34, 90, Flags::visible | Flags::resizable | Flags::appearsOnColumnMenu);
        header.addColumn ("Length", lengthColumn, 54, 40, 110, Flags::visible | Flags::resizable | Flags::appearsOnColumnMenu);
        header.setStretchToFitActive (true);

        if (const auto saved = settings.get().browserColumns; saved.isNotEmpty())
        {
            const juce::ScopedValueSetter<bool> restoring (restoringColumns, true);
            header.restoreFromString (saved);

            // never lose the name column, whatever the file says
            header.setColumnVisible (starColumn, true);
            header.setColumnVisible (nameColumn, true);
        }

        header.setSortColumnId (nameColumn, state.sortForwards);
        header.addListener (this);
        header.setColour (juce::TableHeaderComponent::backgroundColourId, colours::panelRaised);
        header.setColour (juce::TableHeaderComponent::textColourId, colours::textDim);
        header.setColour (juce::TableHeaderComponent::outlineColourId, colours::outline);

        table.setRowHeight (21);
        table.setHeaderHeight (20);
        table.setColour (juce::ListBox::backgroundColourId, colours::background);
        table.setRowSelectedOnMouseDown (false); // so that rows can be dragged without loading them
        table.setMultipleSelectionEnabled (false);
        table.onKeyboardSelection = [this] (int row)
        {
            if (row >= 0 && row < static_cast<int> (rows.size()) && ! rows[static_cast<size_t> (row)].isFolder)
                chooseRow (row);
        };
        addAndMakeVisible (table);

        styleCaption (status, 11.0f, colours::textDim, juce::Justification::centredLeft);
        status.setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (status);

        rescanButton.setTooltip ("Look for new, changed and removed files in the favourite folders");
        rescanButton.onClick = [this] { library.rescan(); };
        addAndMakeVisible (rescanButton);

        soloButton.setClickingTogglesState (true);
        soloButton.setColour (juce::TextButton::buttonOnColourId, colours::solo);
        soloButton.setColour (juce::TextButton::textColourOnId, colours::background);
        soloButton.setTooltip ("While on, only the browser's target slot is heard, so you can judge the IR you are auditioning on its own. "
                               "Switch it off to hear the whole blend again. Turns off when the plugin window closes.");
        soloButton.onClick = [this]
        {
            processor.auditionSolo = soloButton.getToggleState();

            if (onAuditionSoloChanged)
                onAuditionSoloChanged();
        };
        addAndMakeVisible (soloButton);

        emptyHint.setFont (font (13.0f));
        emptyHint.setColour (juce::Label::textColourId, colours::textDim);
        emptyHint.setJustificationType (juce::Justification::centred);
        emptyHint.setInterceptsMouseClicks (false, false);
        addChildComponent (emptyHint);

        settingsButton.setTooltip ("Open Settings to add favourite folders");
        settingsButton.onClick = [this] { if (onOpenSettings) onOpenSettings(); };
        addChildComponent (settingsButton);

        library.addListener (this);
        settings.addListener (this); // folder display names
        processor.getSlots().addListener (this);
        startTimerHz (4);
        syncAuditionSolo();

        // First visit: start in the first favourite folder, if there is one.
        folders = library.getFolders();

        if (! state.initialised)
        {
            state.initialised = true;

            if (! folders.isEmpty())
            {
                state.location = BrowserState::Location::folder;
                state.rootFolder = state.currentFolder = folders[0];
            }
        }

        subfoldersButton.setToggleState (state.includeSubfolders, juce::dontSendNotification);
        starredButton.setToggleState (state.starredOnly, juce::dontSendNotification);
        rebuild();
    }

    BrowserPanel::~BrowserPanel()
    {
        saveColumnLayout();
        table.getHeader().removeListener (this);
        processor.getSlots().removeListener (this);
        settings.removeListener (this);
        library.removeListener (this);
        locationList.setModel (nullptr);
        table.setModel (nullptr);
    }

    //==============================================================================
    void BrowserPanel::broadcasterChanged (Broadcaster* source)
    {
        if (source != &processor.getSlots())
        {
            rebuild();
            return;
        }

        // A slot changed: only the "loaded in slot" dots need updating.
        std::unordered_map<juce::String, int> loaded;

        for (int i = 0; i < kNumSlots; ++i)
            if (const auto info = processor.getSlots().getInfo (i); info.status == SlotInfo::Status::ready)
                loaded[info.reference.path] |= 1 << i;

        for (auto& r : rows)
        {
            const auto it = loaded.find (r.path);
            r.slotMask = it != loaded.end() ? it->second : 0;
        }

        table.repaint();
    }

    void BrowserPanel::timerCallback()
    {
        ++scanAnimation;
        updateStatus();

        // Store the column layout once the user has finished changing it.
        if (columnsDirty && ! table.getHeader().isMouseButtonDown (true))
            saveColumnLayout();
    }

    void BrowserPanel::tableColumnsChanged (juce::TableHeaderComponent*)
    {
        // shown/hidden from the header's right-click menu
        if (! restoringColumns)
            columnsDirty = true;
    }

    void BrowserPanel::tableColumnsResized (juce::TableHeaderComponent* header)
    {
        // Only the user's own drags count. Resizing that follows from the
        // browser's width (stretch to fit) is not a new layout.
        if (! restoringColumns && header != nullptr && header->isMouseButtonDown (true))
            columnsDirty = true;
    }

    void BrowserPanel::saveColumnLayout()
    {
        if (! columnsDirty)
            return;

        columnsDirty = false;
        const auto layout = table.getHeader().toString();
        settings.update ([&layout] (auto& d) { d.browserColumns = layout; });
    }

    void BrowserPanel::syncAuditionSolo()
    {
        soloButton.setToggleState (processor.auditionSolo.load(), juce::dontSendNotification);
    }

    juce::String BrowserPanel::getLocationLabel (int row) const
    {
        if (row == 0)
            return kStar + "  All favourites";

        if (row == 1)
            return "Recent";

        const auto index = row - kNumFixedLocations;
        return juce::isPositiveAndBelow (index, folders.size()) ? settings.get().folderDisplayName (folders[index]) : juce::String();
    }

    //==============================================================================
    juce::PopupMenu BrowserPanel::createFolderMenu (const juce::String& path)
    {
        juce::PopupMenu menu;
        const auto d = settings.get();
        const auto hasCustomName = d.folderNames.count (path) > 0;
        const auto exists = juce::File::isAbsolutePath (path) && juce::File (path).isDirectory();

        menu.addSectionHeader (d.folderDisplayName (path));
        menu.addItem (renameFolderItem, "Rename...");
        menu.addItem (resetFolderNameItem, "Use the folder's own name (" + juce::File (path).getFileName() + ")", hasCustomName);
        menu.addSeparator();
        menu.addItem (openFolderItem, "Open in " + kFileManager, exists);
        menu.addItem (rescanItem, "Rescan all favourite folders");
        return menu;
    }

    void BrowserPanel::handleFolderMenuResult (const juce::String& path, int result)
    {
        switch (result)
        {
            case renameFolderItem:
                renameFolder (path);
                break;

            case resetFolderNameItem:
                settings.update ([&path] (auto& d) { d.folderNames.erase (path); });
                break;

            case openFolderItem:
                if (juce::File::isAbsolutePath (path) && juce::File (path).isDirectory())
                    juce::File (path).startAsProcess();
                break;

            case rescanItem:
                library.rescan();
                break;

            default:
                break;
        }
    }

    void BrowserPanel::showFolderMenu (const juce::String& path)
    {
        auto menu = createFolderMenu (path);
        menu.setLookAndFeel (&getLookAndFeel());
        juce::Component::SafePointer<BrowserPanel> safe (this);

        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                            [safe, path] (int result)
                            {
                                if (safe != nullptr && result > 0)
                                    safe->handleFolderMenuResult (path, result);
                            });
    }

    void BrowserPanel::renameFolder (const juce::String& path)
    {
        const auto ownName = juce::File::isAbsolutePath (path) ? juce::File (path).getFileName() : path;
        juce::Component::SafePointer<BrowserPanel> safe (this);

        confirm.prompt ("Rename favourite folder",
                        "The name shown in IRene for\n" + path + "\nThe folder itself is not renamed. Leave the field empty to show the folder's own name.",
                        settings.get().folderDisplayName (path), {}, false, "Rename",
                        [safe, path, ownName] (bool confirmed, const juce::String& text, bool)
                        {
                            if (safe == nullptr || ! confirmed)
                                return;

                            const auto name = text.trim();

                            safe->settings.update ([&] (auto& d)
                            {
                                if (name.isEmpty() || name == ownName)
                                    d.folderNames.erase (path);
                                else
                                    d.folderNames[path] = name;
                            });
                        });
    }

    juce::PopupMenu BrowserPanel::createRowMenu (int row)
    {
        juce::PopupMenu menu;

        if (! juce::isPositiveAndBelow (row, static_cast<int> (rows.size())))
            return menu;

        const auto& r = rows[static_cast<size_t> (row)];
        const juce::File file (r.path);

        if (r.isFolder)
        {
            menu.addItem (openFolderItem, "Open in " + kFileManager, file.isDirectory());
            return menu;
        }

        const auto target = processor.uiState.targetSlot.load();

        for (int i = 0; i < kNumSlots; ++i)
            menu.addItem (loadIntoSlotItem + i, "Load into Slot " + juce::String (i + 1) + (i == target ? "  (target)" : ""));

        menu.addSeparator();
        menu.addItem (toggleFavouriteItem, r.favourite ? "Remove from favourites" : "Add to favourites");
        menu.addItem (revealItem, "Show in " + kFileManager, file.existsAsFile());
        return menu;
    }

    void BrowserPanel::handleRowMenuResult (int row, int result)
    {
        if (! juce::isPositiveAndBelow (row, static_cast<int> (rows.size())))
            return;

        const auto r = rows[static_cast<size_t> (row)]; // copy: the actions may rebuild the list
        const juce::File file (r.path);

        if (result >= loadIntoSlotItem && result < loadIntoSlotItem + kNumSlots)
        {
            if (onLoadIntoSlot != nullptr && ! r.isFolder)
                onLoadIntoSlot (result - loadIntoSlotItem, file, getCurrentFileList());

            return;
        }

        switch (result)
        {
            case openFolderItem:
                if (file.isDirectory())
                    file.startAsProcess();
                break;

            case revealItem:
                if (file.existsAsFile())
                    file.revealToUser();
                break;

            case toggleFavouriteItem:
                toggleFavourite (row);
                break;

            default:
                break;
        }
    }

    void BrowserPanel::showRowMenu (int row)
    {
        auto menu = createRowMenu (row);

        if (menu.getNumItems() == 0)
            return;

        menu.setLookAndFeel (&getLookAndFeel());
        const auto path = rows[static_cast<size_t> (row)].path;
        juce::Component::SafePointer<BrowserPanel> safe (this);

        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                            [safe, path] (int result)
                            {
                                if (safe == nullptr || result <= 0)
                                    return;

                                // the list may have changed while the menu was open
                                for (size_t i = 0; i < safe->rows.size(); ++i)
                                    if (safe->rows[i].path == path)
                                        return safe->handleRowMenuResult (static_cast<int> (i), result);
                            });
    }

    void BrowserPanel::updateStatus()
    {
        auto numFiles = 0;

        for (const auto& r : rows)
            numFiles += r.isFolder ? 0 : 1;

        auto text = juce::String (numFiles) + (numFiles == 1 ? " file" : " files");

        if (library.isScanning())
            text = "Scanning" + juce::String::repeatedString (".", 1 + scanAnimation % 3)
                   + "  " + juce::String (library.getNumFilesSeenInScan()) + " checked";

        status.setText (text, juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, library.isScanning() ? colours::accent : colours::textDim);
    }

    juce::String BrowserPanel::currentFolderLabel() const
    {
        using Location = BrowserState::Location;

        if (state.location == Location::allFavourites)
            return "All favourites";

        if (state.location == Location::recent)
            return "Recently loaded";

        if (! juce::File::isAbsolutePath (state.rootFolder))
            return {};

        const juce::File root (state.rootFolder);
        auto label = settings.get().folderDisplayName (state.rootFolder);

        if (state.currentFolder != state.rootFolder && juce::File::isAbsolutePath (state.currentFolder))
            label << " / " << juce::File (state.currentFolder).getRelativePathFrom (root).replaceCharacter ('\\', '/').replace ("/", " / ");

        return label;
    }

    void BrowserPanel::selectLocation (BrowserState::Location location, const juce::String& rootFolder)
    {
        state.location = location;
        state.rootFolder = state.currentFolder = (location == BrowserState::Location::folder ? rootFolder : juce::String());
        rebuild();
    }

    void BrowserPanel::navigateTo (const juce::String& folder)
    {
        // never leave the favourite folder
        if (folder != state.rootFolder && ! folder.startsWith (state.rootFolder))
            return;

        state.currentFolder = folder;
        table.deselectAllRows();
        rebuild();
    }

    //==============================================================================
    void BrowserPanel::rebuild()
    {
        using Location = BrowserState::Location;

        folders = library.getFolders();

        if (state.location == Location::folder && ! folders.contains (state.rootFolder))
        {
            // the folder was removed from the settings
            state.location = folders.isEmpty() ? Location::allFavourites : Location::folder;
            state.rootFolder = state.currentFolder = folders.isEmpty() ? juce::String() : folders[0];
        }

        const auto selectedPath = table.getSelectedRow() >= 0 && table.getSelectedRow() < static_cast<int> (rows.size())
                                      ? rows[static_cast<size_t> (table.getSelectedRow())].path
                                      : juce::String();

        const auto index = library.getIndex();
        const auto inFolder = state.location == Location::folder;
        const auto needle = state.search.trim();

        std::unordered_map<juce::String, int> loaded;

        for (int i = 0; i < kNumSlots; ++i)
            if (const auto info = processor.getSlots().getInfo (i); info.status == SlotInfo::Status::ready)
                loaded[info.reference.path] |= 1 << i;

        std::vector<Row> folderRows, fileRows;

        const auto addFile = [&] (const IRFileEntry& e)
        {
            if (needle.isNotEmpty() && ! e.name.containsIgnoreCase (needle))
                return;

            Row r;
            r.path = e.path;
            r.name = e.name;
            r.hash = e.hash;
            r.numChannels = e.numChannels;
            r.sampleRate = e.sampleRate;
            r.seconds = e.lengthSeconds();
            r.favourite = library.isFavourite (e.hash);

            if (const auto it = loaded.find (e.path); it != loaded.end())
                r.slotMask = it->second;

            fileRows.push_back (std::move (r));
        };

        if (inFolder)
        {
            if (! state.includeSubfolders && needle.isEmpty() && ! state.starredOnly)
            {
                for (const auto& name : index->subfoldersOf (state.currentFolder))
                {
                    Row r;
                    r.isFolder = true;
                    r.name = name;
                    r.path = juce::File (state.currentFolder).getChildFile (name).getFullPathName();
                    folderRows.push_back (std::move (r));
                }
            }

            // Searching and "starred only" look through the subfolders as well,
            // so nothing is hidden one level down.
            const auto deep = state.includeSubfolders || needle.isNotEmpty() || state.starredOnly;

            for (const auto* e : index->filesIn (state.currentFolder, deep))
                if (! state.starredOnly || library.isFavourite (e->hash))
                    addFile (*e);
        }
        else
        {
            for (auto e : (state.location == Location::allFavourites ? library.getFavourites() : library.getRecent()))
            {
                // prefer fresh metadata from the index when the file is known there
                if (const auto* known = index->findByPath (e.path))
                {
                    const auto hash = e.hash;
                    e = *known;
                    e.hash = hash;
                }

                addFile (e);
            }
        }

        // "Recent" keeps its own order (newest first); everything else sorts by name.
        if (state.location != Location::recent)
        {
            const auto forwards = state.sortForwards;
            std::sort (fileRows.begin(), fileRows.end(), [forwards] (const Row& a, const Row& b)
            {
                const auto c = a.name.compareNatural (b.name);
                return forwards ? c < 0 : c > 0;
            });
        }

        rows = std::move (folderRows);
        rows.insert (rows.end(), std::make_move_iterator (fileRows.begin()), std::make_move_iterator (fileRows.end()));

        table.updateContent();

        if (selectedPath.isNotEmpty())
            showAsCurrent (selectedPath);

        table.repaint();
        locationList.updateContent();
        locationList.repaint();

        pathLabel.setText (currentFolderLabel(), juce::dontSendNotification);
        upButton.setEnabled (inFolder && state.currentFolder != state.rootFolder);
        subfoldersButton.setEnabled (inFolder);
        starredButton.setEnabled (inFolder);

        const auto noFolders = folders.isEmpty() && inFolder;
        juce::String hint;

        if (folders.isEmpty() && state.location != Location::recent && rows.empty())
            hint = "No favourite folders yet.\nAdd the folders that hold your IRs.";
        else if (rows.empty())
            hint = library.isScanning() ? "Scanning..." : (needle.isNotEmpty() ? "Nothing matches the search." : "Nothing here.");

        emptyHint.setText (hint, juce::dontSendNotification);
        emptyHint.setVisible (hint.isNotEmpty());
        settingsButton.setVisible (folders.isEmpty() && rows.empty() && state.location != Location::recent);
        juce::ignoreUnused (noFolders);

        updateStatus();
        resized();
    }

    juce::StringArray BrowserPanel::getCurrentFileList() const
    {
        juce::StringArray list;

        for (const auto& r : rows)
            if (! r.isFolder)
                list.add (r.path);

        return list;
    }

    void BrowserPanel::showAsCurrent (const juce::String& path)
    {
        for (size_t i = 0; i < rows.size(); ++i)
        {
            if (rows[i].path == path)
            {
                table.selectRow (static_cast<int> (i), true);
                return;
            }
        }

        table.deselectAllRows();
    }

    void BrowserPanel::chooseRow (int row)
    {
        if (row < 0 || row >= static_cast<int> (rows.size()))
            return;

        const auto r = rows[static_cast<size_t> (row)]; // copy: the callback may rebuild the list

        if (r.isFolder)
        {
            navigateTo (r.path);
            return;
        }

        if (onFileChosen)
            onFileChosen (juce::File (r.path), getCurrentFileList());
    }

    void BrowserPanel::toggleFavourite (int row)
    {
        if (row < 0 || row >= static_cast<int> (rows.size()) || rows[static_cast<size_t> (row)].isFolder)
            return;

        const auto& r = rows[static_cast<size_t> (row)];

        IRFileEntry entry;
        entry.path = r.path;
        entry.name = r.name;
        entry.hash = r.hash.isNotEmpty() ? r.hash : hashFile (juce::File (r.path));
        entry.numChannels = r.numChannels;
        entry.sampleRate = r.sampleRate;
        entry.lengthSamples = static_cast<juce::int64> (r.seconds * r.sampleRate);

        // Ask the library rather than the row: the row is only refreshed later.
        library.setFavourite (entry, ! library.isFavourite (entry.hash)); // triggers a rebuild via the change message
    }

    //==============================================================================
    int BrowserPanel::getNumRows()
    {
        return static_cast<int> (rows.size());
    }

    void BrowserPanel::paintRowBackground (juce::Graphics& g, int row, int, int, bool selected)
    {
        if (selected)
            g.fillAll (colours::accent.withAlpha (0.28f));
        else if (row % 2 == 1)
            g.fillAll (juce::Colours::white.withAlpha (0.018f));
    }

    void BrowserPanel::paintCell (juce::Graphics& g, int row, int column, int width, int height, bool)
    {
        if (row < 0 || row >= static_cast<int> (rows.size()))
            return;

        const auto& r = rows[static_cast<size_t> (row)];
        g.setFont (font (13.0f));

        if (r.isFolder)
        {
            if (column == nameColumn)
            {
                g.setColour (colours::accent.brighter (0.2f));
                g.drawText (juce::String::fromUTF8 ("\xe2\x96\xb8 ") + r.name, 4, 0, width - 6, height, juce::Justification::centredLeft);
            }

            return;
        }

        switch (column)
        {
            case starColumn:
                g.setColour (r.favourite ? colours::solo : colours::outline.brighter (0.2f));
                g.setFont (font (14.0f));
                g.drawText (r.favourite ? kStar : kStarOutline, 0, 0, width, height, juce::Justification::centred);
                break;

            case nameColumn:
            {
                auto textWidth = width - 6;

                // a dot in the slot's colour for each slot this IR is loaded in
                for (int i = kNumSlots; --i >= 0;)
                {
                    if ((r.slotMask & (1 << i)) != 0)
                    {
                        textWidth -= 9;
                        g.setColour (slotColour (i));
                        g.fillEllipse (static_cast<float> (textWidth) + 3.0f, static_cast<float> (height) * 0.5f - 3.0f, 6.0f, 6.0f);
                    }
                }

                g.setColour (colours::text);
                g.drawText (r.name, 4, 0, textWidth - 2, height, juce::Justification::centredLeft);
                break;
            }

            case channelsColumn:
                g.setColour (colours::textDim);
                g.drawText (r.numChannels > 1 ? "St" : "M", 0, 0, width, height, juce::Justification::centred);
                break;

            case rateColumn:
                g.setColour (colours::textDim);
                g.drawText (r.sampleRate > 0.0 ? formatRate (r.sampleRate) : juce::String(), 0, 0, width - 2, height, juce::Justification::centredRight);
                break;

            case lengthColumn:
                g.setColour (colours::textDim);
                g.drawText (r.sampleRate > 0.0 ? formatLength (r.seconds) : juce::String(), 0, 0, width - 4, height, juce::Justification::centredRight);
                break;

            default:
                break;
        }
    }

    void BrowserPanel::cellClicked (int row, int column, const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu())
        {
            showRowMenu (row);
            return;
        }

        if (column == starColumn)
            toggleFavourite (row);
        else
            chooseRow (row);
    }

    void BrowserPanel::returnKeyPressed (int row)
    {
        chooseRow (row);
    }

    void BrowserPanel::sortOrderChanged (int, bool isForwards)
    {
        if (state.sortForwards != isForwards)
        {
            state.sortForwards = isForwards;
            rebuild();
        }
    }

    juce::var BrowserPanel::getDragSourceDescription (const juce::SparseSet<int>& selectedRows)
    {
        if (selectedRows.isEmpty())
            return {};

        const auto row = selectedRows[0];

        if (row < 0 || row >= static_cast<int> (rows.size()) || rows[static_cast<size_t> (row)].isFolder)
            return {};

        return SlotStrip::browserDragPrefix + rows[static_cast<size_t> (row)].path;
    }

    juce::String BrowserPanel::getCellTooltip (int row, int column)
    {
        if (row < 0 || row >= static_cast<int> (rows.size()))
            return {};

        if (column == starColumn && ! rows[static_cast<size_t> (row)].isFolder)
            return "Click to add or remove this IR as a favourite";

        return rows[static_cast<size_t> (row)].path + "\n(right-click for more)";
    }

    //==============================================================================
    void BrowserPanel::paint (juce::Graphics& g)
    {
        g.setColour (colours::panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f);
        g.setColour (colours::outline);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
    }

    void BrowserPanel::resized()
    {
        auto area = getLocalBounds().reduced (8);

        heading.setBounds (area.removeFromTop (16));
        area.removeFromTop (2);

        const auto locationRows = juce::jlimit (kNumFixedLocations, 6, kNumFixedLocations + folders.size());
        locationList.setBounds (area.removeFromTop (locationRows * 22 + 2));
        area.removeFromTop (6);

        auto pathRow = area.removeFromTop (22);
        upButton.setBounds (pathRow.removeFromLeft (34));
        pathRow.removeFromLeft (6);
        pathLabel.setBounds (pathRow);
        area.removeFromTop (4);

        auto filterRow = area.removeFromTop (22);
        starredButton.setBounds (filterRow.removeFromRight (58));
        filterRow.removeFromRight (4);
        subfoldersButton.setBounds (filterRow.removeFromRight (78));
        filterRow.removeFromRight (4);
        search.setBounds (filterRow);
        area.removeFromTop (4);

        auto bottom = area.removeFromBottom (22);
        rescanButton.setBounds (bottom.removeFromRight (62));
        bottom.removeFromRight (4);
        soloButton.setBounds (bottom.removeFromRight (80));
        bottom.removeFromRight (6);
        status.setBounds (bottom);
        area.removeFromBottom (4);

        table.setBounds (area);

        auto hintArea = area.reduced (10).withTrimmedTop (24);
        emptyHint.setBounds (hintArea.removeFromTop (44));
        settingsButton.setBounds (hintArea.removeFromTop (26).withSizeKeepingCentre (120, 24));
    }
}
