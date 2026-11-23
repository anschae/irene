#pragma once

#include "PluginProcessor.h"
#include "Widgets.h"
#include "library/IRLibrary.h"

namespace irene::ui
{
    /** The IR browser (left side of the editor).

        Locations at the top: all favourites, recent IRs, and the favourite
        folders from the settings. Below: the file list of the current view,
        with search, sorting and a star column.

        A single click (or the up/down arrow keys) loads a file into the target
        slot straight away; files can also be dragged onto any slot. The list
        is virtualised, so it stays fast with thousands of files. */
    class BrowserPanel final : public juce::Component,
                               private juce::TableListBoxModel,
                               private juce::TableHeaderComponent::Listener,
                               private Broadcaster::Listener,
                               private juce::Timer
    {
    public:
        BrowserPanel (IReneProcessor&, ConfirmOverlay&);
        ~BrowserPanel() override;

        /** A file was chosen for the target slot. `listContext` is the list it
            came from (full paths, in display order), used for prev/next. */
        std::function<void (const juce::File& file, const juce::StringArray& listContext)> onFileChosen;

        /** "Load into Slot n" from the right-click menu. */
        std::function<void (int slot, const juce::File& file, const juce::StringArray& listContext)> onLoadIntoSlot;

        std::function<void()> onOpenSettings;

        /** The "Solo target" button was switched (the processor flag is already set). */
        std::function<void()> onAuditionSoloChanged;

        /** Updates the "Solo target" button from the processor's flag. */
        void syncAuditionSolo();

        /** Right-click menus, exposed for tests: the menu for a favourite folder
            in the locations list, and for a row of the file list. */
        juce::PopupMenu createFolderMenu (const juce::String& favouriteFolder);
        juce::PopupMenu createRowMenu (int row);
        void handleFolderMenuResult (const juce::String& favouriteFolder, int result);
        void handleRowMenuResult (int row, int result);

        enum MenuItem
        {
            renameFolderItem = 1,
            resetFolderNameItem,
            openFolderItem,
            revealItem,
            rescanItem,
            toggleFavouriteItem,
            loadIntoSlotItem = 100 // + slot index
        };

        juce::TableHeaderComponent& getHeaderForTests() noexcept { return table.getHeader(); }
        juce::String getLocationLabel (int row) const;

        /** Full paths of the files in the current view, in display order. */
        juce::StringArray getCurrentFileList() const;

        /** Marks the row of the given path as selected (without loading it). */
        void showAsCurrent (const juce::String& path);

        /** Re-reads the browser state and rebuilds the list. */
        void refreshForTests() { rebuild(); }

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        struct Row
        {
            bool isFolder = false;
            juce::String path, name, hash;
            int numChannels = 0;
            double sampleRate = 0.0, seconds = 0.0;
            bool favourite = false;
            int slotMask = 0; // bit i set: loaded in slot i
        };

        /** The list of locations at the top. */
        class Locations final : public juce::ListBoxModel
        {
        public:
            explicit Locations (BrowserPanel& o) : owner (o) {}
            int getNumRows() override;
            void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
            void listBoxItemClicked (int row, const juce::MouseEvent&) override;

        private:
            BrowserPanel& owner;
        };

        /** A table that reports selection changes made with the keyboard. */
        class FileTable final : public juce::TableListBox
        {
        public:
            using juce::TableListBox::TableListBox;
            bool keyPressed (const juce::KeyPress&) override;
            std::function<void (int row)> onKeyboardSelection;
        };

        enum Column { starColumn = 1, nameColumn, channelsColumn, rateColumn, lengthColumn };

        // juce::TableListBoxModel
        int getNumRows() override;
        void paintRowBackground (juce::Graphics&, int row, int width, int height, bool selected) override;
        void paintCell (juce::Graphics&, int row, int column, int width, int height, bool selected) override;
        void cellClicked (int row, int column, const juce::MouseEvent&) override;
        void returnKeyPressed (int row) override;
        void sortOrderChanged (int newSortColumnId, bool isForwards) override;
        juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;
        juce::String getCellTooltip (int row, int column) override;

        void broadcasterChanged (Broadcaster*) override;
        void timerCallback() override;

        // juce::TableHeaderComponent::Listener
        void tableColumnsChanged (juce::TableHeaderComponent*) override;
        void tableColumnsResized (juce::TableHeaderComponent*) override;
        void tableSortOrderChanged (juce::TableHeaderComponent*) override {}

        void showFolderMenu (const juce::String& favouriteFolder);
        void showRowMenu (int row);
        void renameFolder (const juce::String& favouriteFolder);
        void saveColumnLayout();

        using BrowserState = IReneProcessor::BrowserState;

        void rebuild();
        void selectLocation (BrowserState::Location, const juce::String& rootFolder = {});
        void navigateTo (const juce::String& folder);
        void chooseRow (int row);
        void toggleFavourite (int row);
        void updateStatus();
        juce::String currentFolderLabel() const;

        IReneProcessor& processor;
        ConfirmOverlay& confirm;
        IRLibrary& library;
        GlobalSettings& settings;
        BrowserState& state;
        bool restoringColumns = false, columnsDirty = false;

        std::vector<Row> rows;
        juce::StringArray folders;
        Locations locationsModel { *this };

        juce::Label heading, pathLabel, status, emptyHint;
        juce::ListBox locationList;
        juce::TextButton upButton { "Up" }, subfoldersButton { "Subfolders" }, starredButton { juce::String::fromUTF8 ("\xe2\x98\x85 only") };
        juce::TextButton rescanButton { "Rescan" }, settingsButton { "Add folders..." }, soloButton { "Solo target" };
        juce::TextEditor search;
        FileTable table { "IR files", this };
        int scanAnimation = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrowserPanel)
    };
}
