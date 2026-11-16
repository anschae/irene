// Console runner for all juce::UnitTest instances linked into this target.
// Exit code is the number of failed checks (0 = success).

#include <juce_audio_processors/juce_audio_processors.h>

#include <iostream>

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

    std::cout << "\n==== " << passes << " checks passed, " << failures << " failed ====" << std::endl;
    return failures == 0 ? 0 : 1;
}
