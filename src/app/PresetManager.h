#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

namespace irene
{
    class IReneProcessor;

    /** User presets: one file per preset in the presets folder (see Settings).

        A preset stores all parameters plus, for each slot, the IR's path, file
        name and content hash. A "portable" preset embeds the IR audio as well,
        so it works on another computer.

        When a preset is loaded and an IR isn't at its path any more, it is
        looked up in the library by content hash, then by file name. If that
        fails too the slot is shown as "missing" and offers "Locate...".

        Sends a change message when the list or the current preset changes.
        Message thread only. */
    class PresetManager final : public juce::ChangeBroadcaster
    {
    public:
        explicit PresetManager (IReneProcessor&);

        static constexpr int kFileVersion = 1;
        static inline const juce::String fileExtension { ".irenepreset" };
        static inline const juce::String initName { "Init" };

        juce::File getFolder() const;
        juce::File getFile (const juce::String& name) const;

        /** Names of all presets in the folder, in natural order (without "Init"). */
        juce::StringArray getNames() const;
        bool exists (const juce::String& name) const;

        /** Name of the preset that was loaded or saved last ("Init" after loadInit). */
        juce::String getCurrentName() const { return currentName; }
        void setCurrentName (const juce::String& name);

        /** True if the named preset file embeds its IRs. */
        bool isPortable (const juce::String& name) const;

        //==============================================================================
        /** Writes the plugin's current settings to a preset (overwriting one of
            the same name). Returns an error message, or an empty string. */
        juce::String save (const juce::String& name, bool portable);

        /** Loads a preset. Returns an error message, or an empty string. */
        juce::String load (const juce::String& name);

        /** Resets all parameters to their defaults and clears all slots. */
        void loadInit();

        /** Loads the previous (-1) or next (+1) preset; "Init" is part of the cycle. */
        void step (int direction);

        juce::String rename (const juce::String& oldName, const juce::String& newName);
        juce::String remove (const juce::String& name);

        /** Removes characters that can't be used in a file name. */
        static juce::String sanitiseName (const juce::String& name);

    private:
        IReneProcessor& processor;
        juce::String currentName { initName };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
    };
}
