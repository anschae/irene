#include "BrowserPanel.h"

#include "SlotStrip.h"

namespace irene::ui
{
    namespace
    {
        constexpr int kNumFixedLocations = 2; // "All favourites", "Recent"
        const juce::String kStar = juce::String::fromUTF8 ("\xe2\x98\x85");
        const juce::String kStarOutline = juce::String::fromUTF8 ("\xe2\x98\x86");
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
            text = juce::File::isAbsolutePath (path) ? juce::File (path).getFileName() : path;
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

    void BrowserPanel::Locations::listBoxItemClicked (int row, const juce::MouseEvent&)
    {
        using Location = BrowserState::Location;

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
    BrowserPanel::BrowserPanel (IReneProcessor& p)
        : processor (p), library (p.getLibrary()), state (p.browserState)
    {
        heading.setText ("IR BROWSER", juce::dontSendNotification);
        heading.setFont (font (12.0f, true));
        heading.setColour (juce::Label::textColourId, colours::textDim);
        addAndMakeVisible (heading);

        locationList.setModel (&locationsModel);
        locationList.setRowHeight (22);
        locationList.setColour (juce::ListBox::backgroundColourId, colours::background);
        locationList.setTooltip ("Favourite folders are set up in Settings");
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

        auto& header = table.getHeader();
        header.addColumn (kStar, starColumn, 24, 24, 24, juce::TableHeaderComponent::notResizableOrSortable);
        header.addColumn ("Name", nameColumn, 150, 60, -1, juce::TableHeaderComponent::notResizable | juce::TableHeaderComponent::sortable | juce::TableHeaderComponent::visible);
        header.addColumn ("Ch", channelsColumn, 30, 30, 30, juce::TableHeaderComponent::notResizableOrSortable);
        header.addColumn ("Rate", rateColumn, 42, 42, 42, juce::TableHeaderComponent::notResizableOrSortable);
        header.addColumn ("Length", lengthColumn, 54, 54, 54, juce::TableHeaderComponent::notResizableOrSortable);
        header.setStretchToFitActive (true);
        header.setSortColumnId (nameColumn, state.sortForwards);
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
        addAndMakeVisible (status);

        rescanButton.setTooltip ("Look for new, changed and removed files in the favourite folders");
        rescanButton.onClick = [this] { library.rescan(); };
        addAndMakeVisible (rescanButton);

        emptyHint.setFont (font (13.0f));
        emptyHint.setColour (juce::Label::textColourId, colours::textDim);
        emptyHint.setJustificationType (juce::Justification::centred);
        emptyHint.setInterceptsMouseClicks (false, false);
        addChildComponent (emptyHint);

        settingsButton.setTooltip ("Open Settings to add favourite folders");
        settingsButton.onClick = [this] { if (onOpenSettings) onOpenSettings(); };
        addChildComponent (settingsButton);

        library.addListener (this);
        processor.getSlots().addListener (this);
        startTimerHz (4);

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
        processor.getSlots().removeListener (this);
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
        auto label = root.getFileName();

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

        library.setFavourite (entry, ! r.favourite); // triggers a rebuild via the change message
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
            return;

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

        return rows[static_cast<size_t> (row)].path;
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
        bottom.removeFromRight (6);
        status.setBounds (bottom);
        area.removeFromBottom (4);

        table.setBounds (area);

        auto hintArea = area.reduced (10).withTrimmedTop (24);
        emptyHint.setBounds (hintArea.removeFromTop (44));
        settingsButton.setBounds (hintArea.removeFromTop (26).withSizeKeepingCentre (120, 24));
    }
}
