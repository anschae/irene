#include "GlobalSettings.h"

#include "Config.h"

namespace irene
{
    namespace
    {
        const juce::Identifier kRoot { "IReneSettings" };
        const juce::Identifier kVersion { "version" };
        const juce::Identifier kUiScale { "uiScale" };
        const juce::Identifier kAskBeforeResampling { "askBeforeResampling" };
        const juce::Identifier kDefaultNormalise { "defaultNormalise" };
        const juce::Identifier kDefaultAutoTrim { "defaultAutoTrim" };
        const juce::Identifier kBrowserOpen { "browserOpen" };
        const juce::Identifier kPresetsFolder { "presetsFolder" };
        const juce::Identifier kFavouriteFolders { "FavouriteFolders" };
        const juce::Identifier kFolder { "Folder" };
        const juce::Identifier kPath { "path" };

        juce::File& directoryOverride()
        {
            static juce::File dir;
            return dir;
        }
    }

    juce::File GlobalSettings::getDataDirectory()
    {
        if (directoryOverride() != juce::File())
            return directoryOverride();

        auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);

       #if JUCE_MAC
        base = base.getChildFile ("Application Support");
       #endif

        return base.getChildFile (kPluginName);
    }

    void GlobalSettings::setDataDirectoryOverride (const juce::File& directory)
    {
        directoryOverride() = directory;
    }

    //==============================================================================
    GlobalSettings::GlobalSettings()
        : file (getDataDirectory().getChildFile ("settings.xml"))
    {
        load();
        startTimer (1500);
    }

    GlobalSettings::~GlobalSettings()
    {
        stopTimer();
    }

    GlobalSettings::Data GlobalSettings::get() const
    {
        const juce::ScopedLock sl (lock);
        return data;
    }

    void GlobalSettings::set (const Data& newData)
    {
        {
            const juce::ScopedLock sl (lock);

            auto cleaned = newData;
            cleaned.uiScale = juce::jlimit (kMinUiScale, kMaxUiScale, cleaned.uiScale);
            cleaned.favouriteFolders.removeEmptyStrings();
            cleaned.favouriteFolders.removeDuplicates (false);

            if (cleaned == data)
                return;

            data = cleaned;
            save();
        }

        sendChangeMessage();
    }

    juce::File GlobalSettings::getPresetsFolder() const
    {
        const auto custom = get().presetsFolder;

        if (custom.isNotEmpty() && juce::File::isAbsolutePath (custom))
            return juce::File (custom);

        return getDataDirectory().getChildFile ("Presets");
    }

    //==============================================================================
    void GlobalSettings::load()
    {
        const juce::ScopedLock sl (lock);
        Data loaded;

        if (const auto xml = juce::parseXML (file))
        {
            const auto tree = juce::ValueTree::fromXml (*xml);

            if (tree.hasType (kRoot))
            {
                // Unknown properties from newer versions are simply ignored;
                // missing ones keep their defaults.
                loaded.uiScale = juce::jlimit (kMinUiScale, kMaxUiScale, static_cast<float> (tree.getProperty (kUiScale, loaded.uiScale)));
                loaded.askBeforeResampling = tree.getProperty (kAskBeforeResampling, loaded.askBeforeResampling);
                loaded.defaultNormalise = tree.getProperty (kDefaultNormalise, loaded.defaultNormalise);
                loaded.defaultAutoTrim = tree.getProperty (kDefaultAutoTrim, loaded.defaultAutoTrim);
                loaded.browserOpen = tree.getProperty (kBrowserOpen, loaded.browserOpen);
                loaded.presetsFolder = tree.getProperty (kPresetsFolder).toString();

                for (const auto& child : tree.getChildWithName (kFavouriteFolders))
                    if (child.hasType (kFolder) && child.getProperty (kPath).toString().isNotEmpty())
                        loaded.favouriteFolders.addIfNotAlreadyThere (child.getProperty (kPath).toString());
            }
        }

        data = loaded;
        lastKnownModification = file.getLastModificationTime();
    }

    void GlobalSettings::save() const
    {
        juce::ValueTree tree { kRoot };
        tree.setProperty (kVersion, kFileVersion, nullptr);
        tree.setProperty (kUiScale, data.uiScale, nullptr);
        tree.setProperty (kAskBeforeResampling, data.askBeforeResampling, nullptr);
        tree.setProperty (kDefaultNormalise, data.defaultNormalise, nullptr);
        tree.setProperty (kDefaultAutoTrim, data.defaultAutoTrim, nullptr);
        tree.setProperty (kBrowserOpen, data.browserOpen, nullptr);
        tree.setProperty (kPresetsFolder, data.presetsFolder, nullptr);

        juce::ValueTree folders { kFavouriteFolders };

        for (const auto& path : data.favouriteFolders)
        {
            juce::ValueTree folder { kFolder };
            folder.setProperty (kPath, path, nullptr);
            folders.appendChild (folder, nullptr);
        }

        tree.appendChild (folders, nullptr);

        file.getParentDirectory().createDirectory();

        // replaceWithText writes a temporary file and renames it over the target,
        // so other processes never see a half-written file.
        if (const auto xml = tree.createXml())
            file.replaceWithText (xml->toString());

        lastKnownModification = file.getLastModificationTime();
    }

    bool GlobalSettings::reloadIfChangedOnDisk()
    {
        Data before;

        {
            const juce::ScopedLock sl (lock);

            if (file.getLastModificationTime() == lastKnownModification)
                return false;

            before = data;
            load();

            if (data == before)
                return false;
        }

        sendChangeMessage();
        return true;
    }

    void GlobalSettings::timerCallback()
    {
        reloadIfChangedOnDisk();
    }
}
