#include "PluginProcessor.h"

namespace
{
    using Layout = juce::AudioProcessor::BusesLayout;

    Layout makeLayout (const juce::AudioChannelSet& in, const juce::AudioChannelSet& out)
    {
        Layout layout;
        layout.inputBuses.add (in);
        layout.outputBuses.add (out);
        return layout;
    }

    class PassThroughTests final : public juce::UnitTest
    {
    public:
        PassThroughTests() : juce::UnitTest ("Skeleton pass-through", "IRene") {}

        void runTest() override
        {
            const auto mono = juce::AudioChannelSet::mono();
            const auto stereo = juce::AudioChannelSet::stereo();

            beginTest ("Bus layouts");
            {
                irene::IReneProcessor processor;
                expect (processor.checkBusesLayoutSupported (makeLayout (mono, mono)));
                expect (processor.checkBusesLayoutSupported (makeLayout (mono, stereo)));
                expect (processor.checkBusesLayoutSupported (makeLayout (stereo, stereo)));
                expect (! processor.checkBusesLayoutSupported (makeLayout (stereo, mono)));
                expect (! processor.checkBusesLayoutSupported (makeLayout (juce::AudioChannelSet::create5point1(), stereo)));
            }

            beginTest ("Stereo passes through unchanged at varying block sizes");
            {
                irene::IReneProcessor processor;
                expect (processor.setBusesLayout (makeLayout (stereo, stereo)));
                processor.prepareToPlay (48000.0, 512);
                expectEquals (processor.getLatencySamples(), 0);

                auto random = getRandom();
                juce::MidiBuffer midi;

                for (const int blockSize : { 1, 7, 64, 512, 3, 256 })
                {
                    juce::AudioBuffer<float> buffer (2, blockSize);

                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < blockSize; ++i)
                            buffer.setSample (ch, i, random.nextFloat() * 2.0f - 1.0f);

                    juce::AudioBuffer<float> reference;
                    reference.makeCopyOf (buffer);

                    processor.processBlock (buffer, midi);

                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < blockSize; ++i)
                            expectEquals (buffer.getSample (ch, i), reference.getSample (ch, i));
                }
            }

            beginTest ("Mono in, stereo out duplicates the input");
            {
                irene::IReneProcessor processor;
                expect (processor.setBusesLayout (makeLayout (mono, stereo)));
                processor.prepareToPlay (44100.0, 128);

                juce::AudioBuffer<float> buffer (2, 128);
                buffer.clear();

                auto random = getRandom();

                for (int i = 0; i < 128; ++i)
                    buffer.setSample (0, i, random.nextFloat() * 2.0f - 1.0f);

                // Garbage on the second channel must be overwritten.
                for (int i = 0; i < 128; ++i)
                    buffer.setSample (1, i, 123.0f);

                juce::MidiBuffer midi;
                processor.processBlock (buffer, midi);

                for (int i = 0; i < 128; ++i)
                    expectEquals (buffer.getSample (1, i), buffer.getSample (0, i));
            }

            beginTest ("State round trip does not crash and ignores foreign data");
            {
                irene::IReneProcessor processor;

                juce::MemoryBlock state;
                processor.getStateInformation (state);
                expect (state.getSize() > 0);

                processor.setStateInformation (state.getData(), static_cast<int> (state.getSize()));

                const char garbage[] = "this is not a valid state";
                processor.setStateInformation (garbage, static_cast<int> (sizeof (garbage)));
                processor.setStateInformation (nullptr, 0);
            }
        }
    };

    PassThroughTests passThroughTests;
}
