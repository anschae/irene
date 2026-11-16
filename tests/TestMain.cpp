// Console runner for all juce::UnitTest instances linked into this target.
// Exit code is the number of failed checks (0 = success).

#include <juce_audio_processors/juce_audio_processors.h>

#include <iostream>

#include "app/GlobalSettings.h"

namespace
{
    class ConsoleRunner final : public juce::UnitTestRunner
    {
        void logMessage (const juce::String& message) override
        {
            std::cout << message << std::endl;
        }
    };
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // Keep the tests away from the user's real settings, favourites and presets.
    const auto dataDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("IReneTestData_" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
    dataDir.createDirectory();
    irene::GlobalSettings::setDataDirectoryOverride (dataDir);

    ConsoleRunner runner;
    runner.setAssertOnFailure (false);
    runner.runAllTests();

    int failures = 0;
    int passes = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        if (const auto* result = runner.getResult (i))
        {
            failures += result->failures;
            passes += result->passes;
        }
    }

    dataDir.deleteRecursively();

    std::cout << "\n==== " << passes << " checks passed, " << failures << " failed ====" << std::endl;
    return failures == 0 ? 0 : 1;
}
