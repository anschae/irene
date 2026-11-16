#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

namespace irene
{
    /** User settings that are the same in every DAW and every plugin instance.

        Stored as a small XML file in the user's application-data folder:
          macOS    ~/Library/Application Support/IRene/
          Windows  %APPDATA%\IRene\
          Linux    ~/.config/IRene/

        - One object is shared by all instances in a process
          (use GlobalSettings::Shared).
        - Changes made by another process (another DAW, the standalone app) are
          noticed by watching the file's modification time.
        - The file is written atomically (temp file, then rename).

        Sends a change message on the message thread whenever the data changes. */
    class GlobalSettings final : public juce::ChangeBroadcaster,
                                 private juce::Timer
    {
    public:
        using Shared = juce::SharedResourcePointer<GlobalSettings>;

        static constexpr int kFileVersion = 1;
        static constexpr float kMinUiScale = 0.75f;
        static constexpr float kMaxUiScale = 2.0f;

        struct Data
        {
            float uiScale = 1.0f;
            bool askBeforeResampling = false;
            bool defaultNormalise = true;
            bool defaultAutoTrim = false;
            bool browserOpen = true;
            juce::String presetsFolder;        // empty = default location
            juce::StringArray favouriteFolders; // in display order

            bool operator== (const Data&) const = default;
        };

        GlobalSettings();
        ~GlobalSettings() override;

        Data get() const;
        void set (const Data& newData);

        /** Convenience: read, modify, write. */
        template <typename Fn>
        void update (Fn&& modify)
        {
            auto d = get();
            modify (d);
            set (d);
        }

        juce::File getPresetsFolder() const;
        juce::File getFile() const { return file; }

        /** Reloads if another process changed the file. Returns true if the data changed. */
        bool reloadIfChangedOnDisk();

        //==============================================================================
        /** The folder that holds all of IRene's global files. */
        static juce::File getDataDirectory();

        /** Redirects getDataDirectory(), so tests never touch the real settings. */
        static void setDataDirectoryOverride (const juce::File& directory);

    private:
        void timerCallback() override;
        void load();
        void save() const;

        juce::File file;
        mutable juce::CriticalSection lock;
        Data data;
        mutable juce::Time lastKnownModification;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlobalSettings)
    };
}
