#include "PresetManager.h"

#include "PluginProcessor.h"

namespace irene
{
    namespace
    {
        const juce::Identifier kPresetRoot { "IRenePreset" };
        const juce::Identifier kVersion { "version" };
        const juce::Identifier kName { "name" };
        const juce::Identifier kPortable { "portable" };

        juce::ValueTree readPresetFile (const juce::File& file)
        {
            juce::FileInputStream in (file);

            if (! in.openedOk())
                return {};

            juce::GZIPDecompressorInputStream unzip (in);
            auto tree = juce::ValueTree::readFromStream (unzip);
            return tree.hasType (kPresetRoot) ? tree : juce::ValueTree();
        }
    }

    PresetManager::PresetManager (IReneProcessor& p)
        : processor (p)
    {
    }

    juce::File PresetManager::getFolder() const
    {
        return processor.getGlobalSettings().getPresetsFolder();
    }

    juce::File PresetManager::getFile (const juce::String& name) const
    {
        return getFolder().getChildFile (sanitiseName (name) + fileExtension);
    }

    juce::String PresetManager::sanitiseName (const juce::String& name)
    {
        return juce::File::createLegalFileName (name.trim()).trim().trimCharactersAtEnd (".");
    }

    juce::StringArray PresetManager::getNames() const
    {
        juce::StringArray names;

        for (const auto& f : getFolder().findChildFiles (juce::File::findFiles, false, "*" + fileExtension))
            names.add (f.getFileNameWithoutExtension());

        names.sortNatural();
        return names;
    }

    bool PresetManager::exists (const juce::String& name) const
    {
        return name.isNotEmpty() && getFile (name).existsAsFile();
    }

    void PresetManager::setCurrentName (const juce::String& name)
    {
        const auto newName = name.isEmpty() ? initName : name;

        if (currentName != newName)
        {
            currentName = newName;
            sendChangeMessage();
        }
    }

    bool PresetManager::isPortable (const juce::String& name) const
    {
        return static_cast<bool> (readPresetFile (getFile (name)).getProperty (kPortable, false));
    }

    //==============================================================================
    juce::String PresetManager::save (const juce::String& rawName, bool portable)
    {
        const auto name = sanitiseName (rawName);

        if (name.isEmpty())
            return "Please enter a name for the preset.";

        if (name.equalsIgnoreCase (initName))
            return "\"" + initName + "\" is the built-in empty preset. Please choose another name.";

        const auto folder = getFolder();

        if (! folder.isDirectory() && folder.createDirectory().failed())
            return "The presets folder could not be created:\n" + folder.getFullPathName();

        juce::ValueTree preset { kPresetRoot };
        preset.setProperty (kVersion, kFileVersion, nullptr);
        preset.setProperty (kName, name, nullptr);
        preset.setProperty (kPortable, portable, nullptr);
        preset.appendChild (processor.savePresetState (portable), nullptr);

        const auto file = getFile (name);
        juce::TemporaryFile temp (file);

        {
            juce::FileOutputStream out (temp.getFile());

            if (! out.openedOk())
                return "The preset could not be written to:\n" + file.getFullPathName();

            juce::GZIPCompressorOutputStream zip (out, 6);
            preset.writeToStream (zip);
        }

        if (! temp.overwriteTargetFileWithTemporary())
            return "The preset could not be written to:\n" + file.getFullPathName();

        currentName = name;
        sendChangeMessage();
        return {};
    }

    juce::String PresetManager::load (const juce::String& name)
    {
        if (name == initName || name.isEmpty())
        {
            loadInit();
            return {};
        }

        const auto file = getFile (name);

        if (! file.existsAsFile())
            return "The preset \"" + name + "\" doesn't exist any more.";

        const auto preset = readPresetFile (file);
        const auto state = preset.getChildWithName (IReneProcessor::stateType);

        if (! state.isValid())
            return "\"" + file.getFileName() + "\" is not a valid IRene preset.";

        processor.applyPresetState (state);
        currentName = name;
        sendChangeMessage();
        return {};
    }

    void PresetManager::loadInit()
    {
        processor.resetToDefaults();
        currentName = initName;
        sendChangeMessage();
    }

    void PresetManager::step (int direction)
    {
        juce::StringArray all { initName };
        all.addArray (getNames());

        auto index = all.indexOf (currentName);

        if (index < 0)
            index = 0;

        const auto n = all.size();
        load (all[((index + direction) % n + n) % n]);
    }

    juce::String PresetManager::rename (const juce::String& oldName, const juce::String& rawNewName)
    {
        const auto newName = sanitiseName (rawNewName);

        if (newName.isEmpty())
            return "Please enter a name for the preset.";

        if (newName.equalsIgnoreCase (initName))
            return "\"" + initName + "\" is the built-in empty preset. Please choose another name.";

        const auto from = getFile (oldName);
        const auto to = getFile (newName);

        if (! from.existsAsFile())
            return "The preset \"" + oldName + "\" doesn't exist any more.";

        if (from == to)
            return {};

        // (a pure change of upper/lower case is fine on case-insensitive file systems)
        if (to.existsAsFile() && ! from.getFullPathName().equalsIgnoreCase (to.getFullPathName()))
            return "A preset called \"" + newName + "\" already exists.";

        if (! from.moveFileTo (to))
            return "The preset could not be renamed.";

        if (currentName == oldName)
            currentName = newName;

        sendChangeMessage();
        return {};
    }

    juce::String PresetManager::remove (const juce::String& name)
    {
        const auto file = getFile (name);

        if (! file.existsAsFile())
            return "The preset \"" + name + "\" doesn't exist any more.";

        if (! file.deleteFile())
            return "The preset could not be deleted.";

        // The settings stay as they are; they just no longer belong to a preset file.
        sendChangeMessage();
        return {};
    }
}
