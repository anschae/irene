#include "TestHelpers.h"

#include "dsp/Engine.h"
#include "ir/IRProcessing.h"

namespace
{
    using namespace irene;
    using namespace irene::test;

    constexpr double kFs = 48000.0;

    juce::AudioBuffer<float> copyOf (const juce::AudioBuffer<float>& b)
    {
        juce::AudioBuffer<float> c;
        c.makeCopyOf (b);
        return c;
    }

    class EngineTests final : public juce::UnitTest
    {
    public:
        EngineTests() : juce::UnitTest ("Engine", "IRene") {}

        /** Renders noise through slot 0 holding `ir` and compares with direct convolution. */
        void expectConvolutionCorrect (const juce::AudioBuffer<float>& ir, int signalLength, std::vector<int> blockSizes,
                                       int hostBlockSize, const juce::String& what)
        {
            Engine engine;
            engine.getSlot (0).setIR (copyOf (ir));
            engine.prepare (kFs, hostBlockSize, 2, 2);

            const auto input = makeNoise (2, signalLength, 100 + signalLength);
            const auto output = render (engine, input, {}, std::move (blockSizes));

            for (int ch = 0; ch < 2; ++ch)
            {
                const auto irChannel = std::min (ch, ir.getNumChannels() - 1);
                const auto expected = convolve (input.getReadPointer (ch), signalLength, ir.getReadPointer (irChannel), ir.getNumSamples());
                const auto error = errorDb (output.getReadPointer (ch), expected.data(), signalLength);
                expect (error < -90.0, what + ": error " + juce::String (error, 1) + " dB on channel " + juce::String (ch));
            }
        }

        void runTest() override
        {
            beginTest ("Nothing loaded: the dry signal passes through (100 % wet)");
            {
                Engine engine;
                engine.prepare (kFs, 256, 2, 2);

                const auto input = makeNoise (2, 2000, 1);
                const auto output = render (engine, input, {}, { 256 });

                for (int ch = 0; ch < 2; ++ch)
                    expect (errorDb (output.getReadPointer (ch), input.getReadPointer (ch), 2000) < -140.0);

                expect (! engine.hasAnyIR());

                for (int i = 0; i < kNumSlots; ++i)
                    expect (engine.getSlot (i).isIdle());

                // Muted slots are not "nothing loaded": a muted IR stays silent
                Engine muted;
                muted.getSlot (2).setIR (makeTestIR (1, 300, 2));
                muted.prepare (kFs, 256, 2, 2);
                EngineSettings settings;
                settings.slots[2].mute = true;
                expectEquals (render (muted, input, settings, { 256 }).getMagnitude (0, 2000), 0.0f);
                expect (muted.hasAnyIR());
            }

            beginTest ("Convolution matches direct convolution, zero latency");
            {
                // Short mono IR, typical block size
                expectConvolutionCorrect (makeTestIR (1, 500, 1), 6000, { 256 }, 256, "short mono IR");

                // Stereo IR: left and right are convolved separately
                expectConvolutionCorrect (makeTestIR (2, 1500, 2), 6000, { 512 }, 512, "stereo IR");

                // IR longer than the head partition (uses the non-uniform tail engine)
                expectConvolutionCorrect (makeTestIR (2, 9000, 3), 20000, { 480 }, 480, "long IR");

                // Block size 1, and sizes that change from call to call
                expectConvolutionCorrect (makeTestIR (1, 700, 4), 3000, { 1 }, 1, "block size 1");
                expectConvolutionCorrect (makeTestIR (2, 3000, 5), 12000, { 1, 64, 7, 512, 3, 129, 256, 33 }, 512, "varying block sizes");

                // Host blocks larger than announced, and larger than the internal chunk size
                expectConvolutionCorrect (makeTestIR (1, 2500, 6), 12000, { 4096, 1000, 3000 }, 128, "oversized host blocks");
            }

            beginTest ("Latency is zero");
            {
                Engine engine;
                expectEquals (engine.getLatencySamples(), 0);

                engine.getSlot (0).setIR (makeDelta (1, 4, 0, 0.5f));
                engine.prepare (kFs, 64, 2, 2);

                auto impulse = makeDelta (2, 256);
                const auto output = render (engine, impulse, {}, { 64 });

                expectWithinAbsoluteError (output.getSample (0, 0), 0.5f, 1.0e-6f); // arrives in the very first sample
                expect (output.getMagnitude (0, 1, 255) < 1.0e-6f);
            }

            beginTest ("Slots are summed; gain, phase, mute and solo");
            {
                const auto irA = makeTestIR (1, 300, 10);
                const auto irB = makeTestIR (1, 400, 11);
                const auto numSamples = 4000;
                const auto input = makeNoise (2, numSamples, 12);

                const auto a = convolve (input.getReadPointer (0), numSamples, irA.getReadPointer (0), 300);
                const auto b = convolve (input.getReadPointer (0), numSamples, irB.getReadPointer (0), 400);

                const auto run = [&] (const EngineSettings& settings)
                {
                    Engine engine;
                    engine.getSlot (0).setIR (copyOf (irA));
                    engine.getSlot (1).setIR (copyOf (irB));
                    engine.prepare (kFs, 256, 2, 2);
                    return render (engine, input, settings, { 256 });
                };

                const auto expectMix = [&] (const EngineSettings& settings, double gainA, double gainB, const juce::String& what)
                {
                    const auto output = run (settings);
                    std::vector<double> expected (static_cast<size_t> (numSamples));

                    for (size_t i = 0; i < expected.size(); ++i)
                        expected[i] = gainA * a[i] + gainB * b[i];

                    if (gainA == 0.0 && gainB == 0.0)
                        expect (output.getMagnitude (0, numSamples) < 1.0e-6f, what);
                    else
                        expect (errorDb (output.getReadPointer (0), expected.data(), numSamples) < -90.0, what);
                };

                EngineSettings s;
                expectMix (s, 1.0, 1.0, "plain sum");

                s.slots[0].gainDb = -6.0f;
                s.slots[1].gainDb = 6.0f;
                expectMix (s, 0.501187, 1.995262, "gains");

                s.slots[0].phaseInvert = true;
                expectMix (s, -0.501187, 1.995262, "phase invert");

                s = {};
                s.slots[0].gainDb = kSlotGainOffDb;
                expectMix (s, 0.0, 1.0, "gain at minimum is off");

                s = {};
                s.slots[1].mute = true;
                expectMix (s, 1.0, 0.0, "mute");

                s = {};
                s.slots[1].solo = true;
                expectMix (s, 0.0, 1.0, "solo");

                s.slots[0].solo = true;
                expectMix (s, 1.0, 1.0, "two slots soloed");

                s.slots[0].mute = true;
                expectMix (s, 0.0, 1.0, "mute wins over solo");

                s = {};
                s.slots[2].solo = true; // soloing an empty slot silences the others
                expectMix (s, 0.0, 0.0, "solo on an empty slot");
            }

            beginTest ("Pan: equal power for mono IRs, balance for stereo IRs");
            {
                const auto numSamples = 2000;
                const auto input = makeNoise (2, numSamples, 20);

                const auto measure = [&] (const juce::AudioBuffer<float>& ir, float pan)
                {
                    Engine engine;
                    engine.getSlot (0).setIR (copyOf (ir));
                    engine.prepare (kFs, 256, 2, 2);

                    EngineSettings s;
                    s.slots[0].pan = pan;
                    const auto output = render (engine, input, s, { 256 });

                    // level relative to the input, per channel
                    return std::array<double, 2> { rmsDb (output.getReadPointer (0), numSamples) - rmsDb (input.getReadPointer (0), numSamples),
                                                   rmsDb (output.getReadPointer (1), numSamples) - rmsDb (input.getReadPointer (1), numSamples) };
                };

                const auto mono = makeDelta (1, 4);
                const auto stereo = makeDelta (2, 4);

                // Centre: unity on both sides for both kinds of IR
                for (const auto* ir : { &mono, &stereo })
                {
                    const auto levels = measure (*ir, 0.0f);
                    expectWithinAbsoluteError (levels[0], 0.0, 0.01);
                    expectWithinAbsoluteError (levels[1], 0.0, 0.01);
                }

                // Mono IR hard left: +3 dB left, silence right. Total power is constant.
                auto levels = measure (mono, -1.0f);
                expectWithinAbsoluteError (levels[0], 3.01, 0.02);
                expect (levels[1] < -100.0);

                levels = measure (mono, 0.5f);
                const auto power = std::pow (10.0, levels[0] / 10.0) + std::pow (10.0, levels[1] / 10.0);
                expectWithinAbsoluteError (power, 2.0, 0.01);
                expect (levels[1] > levels[0]);

                // Stereo IR hard right: right stays at unity, left is silent
                levels = measure (stereo, 1.0f);
                expect (levels[0] < -100.0);
                expectWithinAbsoluteError (levels[1], 0.0, 0.01);

                // Stereo IR half left: left untouched, right attenuated
                levels = measure (stereo, -0.5f);
                expectWithinAbsoluteError (levels[0], 0.0, 0.01);
                expect (levels[1] < -1.0 && levels[1] > -12.0);
            }

            beginTest ("Slot delay shifts the IR; slot filter is applied");
            {
                const auto numSamples = 3000;
                const auto ir = makeTestIR (1, 200, 30);
                const auto input = makeNoise (2, numSamples, 31);

                Engine engine;
                engine.getSlot (0).setIR (copyOf (ir));
                engine.prepare (kFs, 256, 2, 2);

                EngineSettings s;
                s.slots[0].delayMs = 2.0f; // 96 samples at 48 kHz
                auto output = render (engine, input, s, { 256 });

                auto expected = convolve (input.getReadPointer (0), numSamples, ir.getReadPointer (0), 200);
                std::rotate (expected.rbegin(), expected.rbegin() + 96, expected.rend());
                std::fill (expected.begin(), expected.begin() + 96, 0.0);
                expect (errorDb (output.getReadPointer (0), expected.data(), numSamples) < -90.0);

                // Filter: a delta IR turns the slot into just its filter block
                Engine filterEngine;
                filterEngine.getSlot (0).setIR (makeDelta (1, 4));
                filterEngine.prepare (kFs, 256, 2, 2);

                EngineSettings fs;
                fs.slots[0].filter.hpOn = true;
                fs.slots[0].filter.hpFreq = 1000.0f;
                fs.slots[0].filter.bands[2] = { true, 6000.0f, 9.0f, 1.0f, true };

                auto impulse = makeDelta (2, 16384);
                output = render (filterEngine, impulse, fs, { 256 });

                for (const auto freq : { 200.0, 1000.0, 4000.0, 12000.0 })
                {
                    const auto measured = dtft (output.getReadPointer (0), 16384, freq, kFs);
                    const auto analytic = FilterBlock::response (fs.slots[0].filter, freq, kFs);
                    expect (std::abs (measured - analytic) < 0.002 * std::max (0.1, std::abs (analytic)));
                }
            }

            beginTest ("Dry/wet mix, input and output filter blocks, output gain");
            {
                const auto numSamples = 4000;
                const auto input = makeNoise (2, numSamples, 40);

                // With a unit-impulse IR, wet == dry, so every mix setting must return the
                // input exactly: any misalignment between the paths would show as comb filtering.
                for (const auto mix : { 0.0f, 0.3f, 0.5f, 1.0f })
                {
                    Engine engine;
                    engine.getSlot (0).setIR (makeDelta (1, 4));
                    engine.prepare (kFs, 256, 2, 2);

                    EngineSettings s;
                    s.mix = mix;
                    const auto output = render (engine, input, s, { 256 });
                    expect (errorDb (output.getReadPointer (0), input.getReadPointer (0), numSamples) < -120.0,
                            "mix " + juce::String (mix));
                }

                // Fully dry: IR is irrelevant, but the input filter still applies (dry is taken after it)
                {
                    Engine engine;
                    engine.getSlot (0).setIR (makeTestIR (1, 300, 41));
                    engine.prepare (kFs, 256, 2, 2);

                    EngineSettings s;
                    s.mix = 0.0f;
                    s.inputFilter.lpOn = true;
                    s.inputFilter.lpFreq = 500.0f;
                    s.outGainDb = -12.0f;

                    auto impulse = makeDelta (2, 16384);
                    const auto output = render (engine, impulse, s, { 256 });

                    for (const auto freq : { 100.0, 500.0, 2000.0 })
                    {
                        const auto measured = dtft (output.getReadPointer (1), 16384, freq, kFs);
                        const auto analytic = FilterBlock::response (s.inputFilter, freq, kFs) * 0.251189;
                        expect (std::abs (measured - analytic) < 0.002 * std::max (0.01, std::abs (analytic)));
                    }
                }

                // Output filter comes after the mix
                {
                    Engine engine;
                    engine.getSlot (0).setIR (makeDelta (1, 4, 0, 2.0f));
                    engine.prepare (kFs, 256, 2, 2);

                    EngineSettings s;
                    s.outputFilter.bands[1] = { true, 1000.0f, -10.0f, 1.0f, false };

                    auto impulse = makeDelta (2, 16384);
                    const auto output = render (engine, impulse, s, { 256 });
                    const auto measured = dtft (output.getReadPointer (0), 16384, 1000.0, kFs);
                    expectWithinAbsoluteError (toDb (std::abs (measured)), 6.02 - 10.0, 0.02);
                }
            }

            beginTest ("Mono and mono-to-stereo layouts");
            {
                const auto numSamples = 3000;
                const auto stereoIR = makeTestIR (2, 400, 50);
                const auto monoInput = makeNoise (1, numSamples, 51);

                const auto left = convolve (monoInput.getReadPointer (0), numSamples, stereoIR.getReadPointer (0), 400);
                const auto right = convolve (monoInput.getReadPointer (0), numSamples, stereoIR.getReadPointer (1), 400);

                // mono -> stereo: the mono input feeds both channels of a stereo IR
                {
                    Engine engine;
                    engine.getSlot (0).setIR (copyOf (stereoIR));
                    engine.prepare (kFs, 256, 1, 2);

                    juce::AudioBuffer<float> input (2, numSamples);
                    input.copyFrom (0, 0, monoInput, 0, 0, numSamples);

                    for (int i = 0; i < numSamples; ++i)
                        input.setSample (1, i, 99.0f); // garbage on the unused input channel must be ignored

                    const auto output = render (engine, input, {}, { 256 });
                    expect (errorDb (output.getReadPointer (0), left.data(), numSamples) < -90.0);
                    expect (errorDb (output.getReadPointer (1), right.data(), numSamples) < -90.0);
                }

                // mono -> mono: output is the mono sum (mid) and pan is ignored
                for (const auto pan : { 0.0f, -1.0f, 0.7f })
                {
                    Engine engine;
                    engine.getSlot (0).setIR (copyOf (stereoIR));
                    engine.prepare (kFs, 256, 1, 1);

                    EngineSettings s;
                    s.slots[0].pan = pan;
                    const auto output = render (engine, monoInput, s, { 256 });

                    std::vector<double> mid (static_cast<size_t> (numSamples));

                    for (size_t i = 0; i < mid.size(); ++i)
                        mid[i] = 0.5 * (left[i] + right[i]);

                    expect (errorDb (output.getReadPointer (0), mid.data(), numSamples) < -90.0);
                }

                // mono -> mono with a mono IR is plain convolution
                {
                    const auto monoIR = makeTestIR (1, 300, 52);
                    Engine engine;
                    engine.getSlot (0).setIR (copyOf (monoIR));
                    engine.prepare (kFs, 256, 1, 1);

                    const auto output = render (engine, monoInput, {}, { 100 });
                    const auto expected = convolve (monoInput.getReadPointer (0), numSamples, monoIR.getReadPointer (0), 300);
                    expect (errorDb (output.getReadPointer (0), expected.data(), numSamples) < -90.0);
                }
            }

            beginTest ("Clearing the last slot: back to the dry signal, then idle");
            {
                Engine engine;
                engine.getSlot (0).setIR (makeTestIR (1, 300, 60));
                engine.prepare (kFs, 256, 2, 2);
                expect (! engine.getSlot (0).isIdle());

                const auto input = makeNoise (2, 24000, 61);
                auto output = render (engine, input, {}, { 256 });
                expect (output.getMagnitude (0, 24000) > 0.01f);

                engine.getSlot (0).setIR ({});
                output = render (engine, input, {}, { 256 });
                expect (engine.getSlot (0).isIdle());
                expect (! engine.hasAnyIR());
                expect (errorDb (output.getReadPointer (0) + 12000, input.getReadPointer (0) + 12000, 12000) < -140.0);
            }

            beginTest ("Real-time IR switching: background load, crossfade, no clicks");
            {
                // Real-time mode: the new IR is built on JUCE's background thread and
                // crossfaded in. Feed a low sine so that any click would stand out.
                Engine engine;
                engine.prepare (kFs, 128, 2, 2);

                const auto sineStep = static_cast<float> (2.0 * std::numbers::pi * 100.0 / kFs);
                auto phase = 0.0;
                auto worstStep = 0.0f;
                auto lastSample = 0.0f;
                auto peakAfterSwitch = 0.0f;

                const auto runBlocks = [&] (int numBlocks)
                {
                    for (int b = 0; b < numBlocks; ++b)
                    {
                        juce::AudioBuffer<float> block (2, 128);

                        for (int i = 0; i < 128; ++i)
                        {
                            const auto v = static_cast<float> (std::sin (phase));
                            phase += 2.0 * std::numbers::pi * 100.0 / kFs;
                            block.setSample (0, i, v);
                            block.setSample (1, i, v);
                        }

                        engine.process (block, {}, false);

                        for (int i = 0; i < 128; ++i)
                        {
                            worstStep = std::max (worstStep, std::abs (block.getSample (0, i) - lastSample));
                            lastSample = block.getSample (0, i);
                            peakAfterSwitch = std::max (peakAfterSwitch, std::abs (lastSample));
                        }

                        // give the background thread time, as a real audio callback would
                        juce::Thread::sleep (1);
                    }
                };

                runBlocks (20);
                expectWithinAbsoluteError (peakAfterSwitch, 1.0f, 0.01f); // nothing loaded yet: dry

                // dry -> IR A (gain 1) -> IR B (gain -0.5, inverted) -> dry again
                engine.getSlot (0).setIR (makeDelta (1, 4, 0, 1.0f));
                runBlocks (150);
                expectWithinAbsoluteError (peakAfterSwitch, 1.0f, 0.01f);

                engine.getSlot (0).setIR (makeDelta (1, 4, 0, -0.5f));
                runBlocks (150);

                peakAfterSwitch = 0.0f;
                runBlocks (20);
                expectWithinAbsoluteError (peakAfterSwitch, 0.5f, 0.01f);

                engine.getSlot (0).setIR ({});
                runBlocks (150);

                peakAfterSwitch = 0.0f;
                runBlocks (20);
                expectWithinAbsoluteError (peakAfterSwitch, 1.0f, 0.01f); // dry again
                expect (engine.getSlot (0).isIdle());

                expect (worstStep < 3.0f * sineStep, "Largest step " + juce::String (worstStep, 5));
            }

            beginTest ("Parameter changes are smoothed (gain, pan, mix, mute, output gain)");
            {
                Engine engine;
                engine.getSlot (0).setIR (makeDelta (1, 4));
                engine.prepare (kFs, 64, 2, 2);

                const auto numSamples = 48000;
                juce::AudioBuffer<float> buffer (2, numSamples);
                const auto sineStep = static_cast<float> (2.0 * std::numbers::pi * 100.0 / kFs);

                for (int i = 0; i < numSamples; ++i)
                {
                    const auto v = static_cast<float> (std::sin (2.0 * std::numbers::pi * 100.0 * i / kFs));
                    buffer.setSample (0, i, v);
                    buffer.setSample (1, i, v);
                }

                std::array<EngineSettings, 6> sequence;
                sequence[1].slots[0].gainDb = -20.0f;
                sequence[2].slots[0].pan = -1.0f;
                sequence[2].slots[0].phaseInvert = true;
                sequence[3].mix = 0.2f;
                sequence[3].outGainDb = 3.0f;
                sequence[4].slots[0].mute = true;
                sequence[5].slots[0].delayMs = 5.0f;

                for (int pos = 0; pos < numSamples; pos += 64)
                {
                    juce::AudioBuffer<float> block (buffer.getArrayOfWritePointers(), 2, pos, 64);
                    engine.process (block, sequence[static_cast<size_t> (pos / 8000)], true);
                }

                // +3 dB at hard left and +3 dB of output gain steepen the sine a little
                expect (maxStep (buffer.getReadPointer (0), numSamples) < 2.5f * sineStep);
                expect (maxStep (buffer.getReadPointer (1), numSamples) < 2.5f * sineStep);
            }

            beginTest ("Peak meters");
            {
                Engine engine;
                engine.getSlot (0).setIR (makeDelta (1, 4, 0, 0.5f));
                engine.prepare (kFs, 256, 2, 2);

                juce::AudioBuffer<float> input (2, 512);
                input.clear();
                input.setSample (0, 100, -0.8f);
                input.setSample (1, 200, 0.4f);

                render (engine, input, {}, { 256 });

                expectWithinAbsoluteError (engine.readInputPeak (0), 0.8f, 1.0e-6f);
                expectWithinAbsoluteError (engine.readInputPeak (1), 0.4f, 1.0e-6f);
                expectWithinAbsoluteError (engine.readOutputPeak (0), 0.4f, 1.0e-5f);
                expectWithinAbsoluteError (engine.readOutputPeak (1), 0.2f, 1.0e-5f);
                expectEquals (engine.readInputPeak (0), 0.0f); // reading resets
            }
        }
    };

    EngineTests engineTests;
}
