#include "TestHelpers.h"

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "analysis/TimeAlign.h"

namespace
{
    using namespace irene;
    using namespace irene::test;

    constexpr double kFs = 48000.0;

    /** A cabinet-like IR defined in continuous time, so that it can be sampled
        with any delay, including fractions of a sample, without interpolation
        error: a few decaying resonances with a soft onset. */
    juce::AudioBuffer<float> makeMicIR (double delaySamples, double sampleRate = kFs, int numChannels = 1,
                                        float gain = 1.0f, int lengthSamples = 4096)
    {
        struct Resonance { double freq, decay, amplitude, phase; };

        static const std::array<Resonance, 6> resonances {
            Resonance { 110.0, 0.012, 0.9, 0.3 }, Resonance { 420.0, 0.006, 0.7, 1.1 }, Resonance { 950.0, 0.004, 0.8, 2.0 },
            Resonance { 2100.0, 0.0025, 1.0, 0.7 }, Resonance { 3400.0, 0.002, 0.6, 4.0 }, Resonance { 5200.0, 0.0015, 0.3, 5.1 }
        };

        juce::AudioBuffer<float> ir (numChannels, lengthSamples);
        ir.clear();

        for (int n = 0; n < lengthSamples; ++n)
        {
            const auto t = (n - delaySamples) / sampleRate;

            if (t <= 0.0)
                continue;

            const auto onset = 1.0 - std::exp (-t / 0.00025); // soft start: keeps the IR band-limited
            auto value = 0.0;

            for (const auto& r : resonances)
                value += r.amplitude * std::exp (-t / r.decay) * std::sin (2.0 * std::numbers::pi * r.freq * t + r.phase);

            for (int ch = 0; ch < numChannels; ++ch)
                ir.setSample (ch, n, gain * static_cast<float> (0.2 * onset * onset * value));
        }

        return ir;
    }

    void setParam (IReneProcessor& p, const juce::String& id, float realValue)
    {
        auto* param = p.getValueTreeState().getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }

    float getParam (IReneProcessor& p, const juce::String& id)
    {
        return p.getValueTreeState().getRawParameterValue (id)->load();
    }

    juce::AudioBuffer<float> renderProcessor (IReneProcessor& p, const juce::AudioBuffer<float>& input)
    {
        juce::AudioBuffer<float> output;
        output.makeCopyOf (input);
        juce::MidiBuffer midi;

        for (int pos = 0; pos < output.getNumSamples(); pos += 256)
        {
            const auto n = std::min (256, output.getNumSamples() - pos);
            juce::AudioBuffer<float> block (output.getArrayOfWritePointers(), output.getNumChannels(), pos, n);
            p.processBlock (block, midi);
        }

        return output;
    }

    /** Level of the plugin's output for a fixed noise input, in dB (after things have settled). */
    double outputLevelDb (IReneProcessor& p)
    {
        const auto input = makeNoise (2, 24000, 4242);
        const auto output = renderProcessor (p, input);
        return rmsDb (output.getReadPointer (0) + 8000, 16000);
    }

    class TimeAlignTests final : public juce::UnitTest
    {
    public:
        TimeAlignTests() : juce::UnitTest ("Time alignment", "IRene") {}

        void runTest() override
        {
            beginTest ("Cross-correlation finds whole-sample offsets, in both directions");
            {
                const auto reference = makeMicIR (5.0);

                for (const auto offset : { 0, 1, 7, 37, 150, 400 })
                {
                    auto pair = align::correlate (reference, makeMicIR (5.0 + offset), kFs);
                    expect (pair.valid);
                    expectWithinAbsoluteError (pair.lagSamples, static_cast<double> (offset), 0.02);
                    expect (! pair.inverted);
                    expect (pair.match > 0.99f);

                    // the other way round: `other` arrives earlier, so the lag is negative
                    pair = align::correlate (makeMicIR (5.0 + offset), reference, kFs);
                    expectWithinAbsoluteError (pair.lagSamples, -static_cast<double> (offset), 0.02);
                }
            }

            beginTest ("Offsets between samples are found to a fraction of a sample");
            {
                const auto reference = makeMicIR (10.0);

                for (const auto offset : { 0.25, 0.5, 3.3, 12.4, 96.75, -4.6 })
                {
                    const auto pair = align::correlate (reference, makeMicIR (10.0 + offset), kFs);
                    expect (pair.valid);
                    expectWithinAbsoluteError (pair.lagSamples, offset, 0.05, "offset " + juce::String (offset));
                }

                // also at other sample rates
                for (const auto rate : { 44100.0, 96000.0, 192000.0 })
                {
                    const auto pair = align::correlate (makeMicIR (20.0, rate), makeMicIR (20.0 + 17.3, rate), rate);
                    expectWithinAbsoluteError (pair.lagSamples, 17.3, 0.05, "rate " + juce::String (rate));
                }
            }

            beginTest ("Polarity: a flipped IR is recognised, a normal one is never flipped");
            {
                const auto reference = makeMicIR (5.0);

                auto pair = align::correlate (reference, makeMicIR (5.0 + 22.0, kFs, 1, -0.7f), kFs);
                expect (pair.valid && pair.inverted);
                expectWithinAbsoluteError (pair.lagSamples, 22.0, 0.02);
                expect (pair.match > 0.99f);

                for (const auto offset : { 0.0, 3.5, 60.0 })
                    expect (! align::correlate (reference, makeMicIR (5.0 + offset, kFs, 1, 0.3f), kFs).inverted);
            }

            beginTest ("Works for a different-sounding microphone, stereo IRs and odd inputs");
            {
                const auto reference = makeMicIR (5.0);

                // "Another mic": the same event through a zero-phase tone change (a symmetric
                // 5-tap smoothing filter, centred, so it adds no delay of its own), plus level and delay.
                const auto delayed = makeMicIR (5.0 + 31.0);
                juce::AudioBuffer<float> darker (1, delayed.getNumSamples());
                darker.clear();
                const std::array<float, 5> taps { 0.1f, 0.25f, 0.3f, 0.25f, 0.1f };

                for (int n = 2; n < delayed.getNumSamples() - 2; ++n)
                {
                    auto v = 0.0f;

                    for (int k = -2; k <= 2; ++k)
                        v += taps[static_cast<size_t> (k + 2)] * delayed.getSample (0, n + k);

                    darker.setSample (0, n, 0.4f * v);
                }

                auto pair = align::correlate (reference, darker, kFs);
                expect (pair.valid && ! pair.inverted);
                expectWithinAbsoluteError (pair.lagSamples, 31.0, 0.1);
                expect (pair.match > 0.8f && pair.match < 0.9999f); // similar, not identical

                // stereo against mono
                pair = align::correlate (reference, makeMicIR (5.0 + 9.0, kFs, 2), kFs);
                expectWithinAbsoluteError (pair.lagSamples, 9.0, 0.02);

                // silence and empty buffers can't be aligned, and don't crash
                juce::AudioBuffer<float> silence (1, 512);
                silence.clear();
                expect (! align::correlate (reference, silence, kFs).valid);
                expect (! align::correlate (silence, reference, kFs).valid);
                expect (! align::correlate (reference, juce::AudioBuffer<float>(), kFs).valid);

                // a very long IR is handled (only its start is analysed)
                pair = align::correlate (makeMicIR (5.0, kFs, 1, 1.0f, 200000), makeMicIR (5.0 + 44.0, kFs, 1, 1.0f, 150000), kFs);
                expectWithinAbsoluteError (pair.lagSamples, 44.0, 0.02);
            }

            beginTest ("Alignment plan: everything meets the slot that arrives last");
            {
                // arrival times: slot 1 at 30 samples, slot 2 at 60 (latest), slot 3 at 16 and flipped
                std::array<align::SlotInput, kNumSlots> slots;
                slots[0].ir = makeMicIR (30.0);
                slots[1].ir = makeMicIR (60.0);
                slots[2].ir = makeMicIR (16.0, kFs, 1, -1.0f);
                slots[0].name = "SM57.wav";

                for (int reference = 0; reference < 3; ++reference)
                {
                    const auto result = align::computeAlignment (slots, kFs, reference);
                    expect (result.ok, result.message);

                    // whichever slot is the reference, the delays are the same
                    expectWithinAbsoluteError (result.slots[0].delayMs, 30.0 / 48.0, 0.002);
                    expectWithinAbsoluteError (result.slots[1].delayMs, 0.0, 0.002);
                    expectWithinAbsoluteError (result.slots[2].delayMs, 44.0 / 48.0, 0.002);

                    // polarity follows the reference: slots 1 and 2 agree, slot 3 is the odd one out
                    expect (result.slots[0].phaseInvert == result.slots[1].phaseInvert);
                    expect (result.slots[2].phaseInvert != result.slots[0].phaseInvert);
                    expect (! result.slots[static_cast<size_t> (reference)].polarityChanged); // the reference is never flipped
                    expect (result.slots[static_cast<size_t> (reference)].isReference);
                    expect (result.message.contains ("reference"));
                }

                expect (align::computeAlignment (slots, kFs, 0).message.contains ("SM57.wav"));
                expect (align::computeAlignment (slots, kFs, 0).message.contains ("polarity flipped"));

                // The reference's current polarity setting is kept and the others follow it
                slots[0].phaseInvert = true;
                auto result = align::computeAlignment (slots, kFs, 0);
                expect (result.slots[0].phaseInvert && result.slots[1].phaseInvert && ! result.slots[2].phaseInvert);
                expect (result.slots[1].polarityChanged && ! result.slots[2].polarityChanged);
                slots[0].phaseInvert = false;

                // Empty slots are left out
                slots[1].ir = {};
                result = align::computeAlignment (slots, kFs, 0);
                expect (result.ok);
                expect (! result.slots[1].hasIR);
                expectWithinAbsoluteError (result.slots[0].delayMs, 0.0, 0.002);       // now slot 1 is the latest
                expectWithinAbsoluteError (result.slots[2].delayMs, 14.0 / 48.0, 0.002);

                // Nothing to do, or impossible: ok is false and the message says why
                slots[2].ir = {};
                result = align::computeAlignment (slots, kFs, 0);
                expect (! result.ok && result.message.containsIgnoreCase ("at least two"));

                result = align::computeAlignment (slots, kFs, 1);
                expect (! result.ok && result.message.containsIgnoreCase ("empty"));

                slots[1].ir = makeMicIR (30.0 + 0.0125 * kFs, kFs, 1, 1.0f, 8192); // 12.5 ms later: more than the Delay knob has
                result = align::computeAlignment (slots, kFs, 0);
                expect (! result.ok && result.message.contains ("12.5 ms"), result.message);

                // An unrelated IR (another cabinet altogether) gives no trustworthy offset:
                // it is left alone, and the rest is still aligned.
                slots[1].ir = makeMicIR (30.0 + 25.0);
                slots[2].ir = makeTestIR (1, 4096, 1234, 100); // noise: nothing in common with the others
                slots[2].phaseInvert = true;
                result = align::computeAlignment (slots, kFs, 0);
                expect (result.ok, result.message);
                expect (result.slots[0].aligned && result.slots[1].aligned && ! result.slots[2].aligned);
                expect (result.slots[2].match < align::kMinMatch);
                expect (! result.slots[2].polarityChanged && result.slots[2].phaseInvert); // keeps what it had
                expectWithinAbsoluteError (result.slots[0].delayMs, 25.0 / 48.0, 0.002);
                expect (result.message.contains ("not changed"), result.message);

                // If nothing resembles the reference, nothing is changed at all
                slots[1].ir = makeTestIR (1, 4096, 99, 20);
                result = align::computeAlignment (slots, kFs, 0);
                expect (! result.ok && result.message.containsIgnoreCase ("too little in common"), result.message);

                for (const auto& slot : result.slots)
                    expect (! slot.aligned);

                slots[2].ir = {};
                slots[2].phaseInvert = false;

                slots[1].ir = makeMicIR (30.0 + 0.0095 * kFs, kFs, 1, 1.0f, 8192); // 9.5 ms: still possible
                result = align::computeAlignment (slots, kFs, 0);
                expect (result.ok);
                expectWithinAbsoluteError (result.slots[0].delayMs, 9.5, 0.002);
            }

            beginTest ("In the plugin: three misaligned mics add up fully after Align");
            {
                // Three copies of the same IR at different distances, one with flipped
                // polarity. Perfectly aligned they must sum to exactly three times one of them.
                IReneProcessor processor;
                setParam (processor, "autogain", 0.0f); // this test measures how the mics add up
                processor.setNonRealtime (true);
                processor.prepareToPlay (kFs, 256);

                processor.getSlots().setSource (0, makeSource (makeMicIR (4.0), kFs, "close.wav"), false, false);
                const auto singleLevel = outputLevelDb (processor);

                processor.getSlots().setSource (1, makeSource (makeMicIR (4.0 + 40.0, kFs, 1, -1.0f), kFs, "far flipped.wav"), false, false);
                processor.getSlots().setSource (2, makeSource (makeMicIR (4.0 + 13.0), kFs, "mid.wav"), false, false);
                setParam (processor, "slot3_delay", 3.3f); // a stale manual setting that must be replaced

                const auto before = outputLevelDb (processor);

                const auto result = processor.alignSlots (0);
                expect (result.ok, result.message);

                expectWithinAbsoluteError (getParam (processor, "slot1_delay"), 40.0f / 48.0f, 0.002f);
                expectWithinAbsoluteError (getParam (processor, "slot2_delay"), 0.0f, 0.002f);
                expectWithinAbsoluteError (getParam (processor, "slot3_delay"), 27.0f / 48.0f, 0.002f);
                expectEquals (getParam (processor, "slot1_phase"), 0.0f);
                expectEquals (getParam (processor, "slot2_phase"), 1.0f); // the flipped mic is corrected
                expectEquals (getParam (processor, "slot3_phase"), 0.0f);

                outputLevelDb (processor); // let the delay glide and gain ramps finish
                const auto after = outputLevelDb (processor);

                // 3 identical signals in phase: +9.54 dB over one of them
                expectWithinAbsoluteError (after - singleLevel, 9.54, 0.1, "aligned sum is " + juce::String (after - singleLevel, 2) + " dB above one mic");
                expect (after > before + 3.0, "alignment did not improve the sum");

                // The result is not just the right level but the right signal:
                // exactly three times the reference, delayed.
                const auto input = makeNoise (2, 12000, 99);
                const auto output = renderProcessor (processor, input);
                const auto delayedIR = makeMicIR (4.0 + 40.0);
                auto expected = convolve (input.getReadPointer (0), 12000, delayedIR.getReadPointer (0), delayedIR.getNumSamples());

                for (auto& v : expected)
                    v *= 3.0;

                // (The Delay parameter has steps of 0.001 ms, about 1/20 of a sample at 48 kHz,
                // so the slots can be up to 1/40 of a sample off: a residual around -45 dB.)
                const auto residual = errorDb (output.getReadPointer (0) + 4000, expected.data() + 4000, 8000);
                logMessage ("  aligned sum vs. ideal: " + juce::String (residual, 1) + " dB");
                expect (residual < -40.0, "Aligned sum differs from the ideal by " + juce::String (residual, 1) + " dB");

                // Aligning again (to any slot) changes nothing: it is already aligned
                const auto again = processor.alignSlots (2);
                expect (again.ok);
                expectWithinAbsoluteError (getParam (processor, "slot1_delay"), 40.0f / 48.0f, 0.002f);
                expectWithinAbsoluteError (getParam (processor, "slot3_delay"), 27.0f / 48.0f, 0.002f);
                expectEquals (getParam (processor, "slot2_phase"), 1.0f);
            }

            beginTest ("In the plugin: other session rates, resampled and stereo IRs, failures change nothing");
            {
                // IRs recorded at 48 kHz and 96 kHz in a 44.1 kHz session
                IReneProcessor processor;
                setParam (processor, "autogain", 0.0f); // this test measures how the mics add up
                processor.setNonRealtime (true);
                processor.prepareToPlay (44100.0, 256);

                processor.getSlots().setSource (0, makeSource (makeMicIR (300.0, 48000.0, 1, 1.0f, 6000), 48000.0), false, false);
                const auto singleLevel = outputLevelDb (processor);

                // 1.25 ms later, stereo, at 96 kHz. (Half the sample values: an IR with twice
                // as many samples per second is otherwise twice as loud as a filter.)
                processor.getSlots().setSource (1, makeSource (makeMicIR (600.0 + 120.0, 96000.0, 2, 0.5f, 12000), 96000.0), false, false);

                auto result = processor.alignSlots (1);
                expect (result.ok, result.message);
                expectWithinAbsoluteError (getParam (processor, "slot1_delay"), 1.25f, 0.003f);
                expectWithinAbsoluteError (getParam (processor, "slot2_delay"), 0.0f, 0.003f);

                outputLevelDb (processor);
                expectWithinAbsoluteError (outputLevelDb (processor) - singleLevel, 6.02, 0.1);

                // Too far apart: reported, and no parameter is touched
                processor.getSlots().setSource (2, makeSource (makeMicIR (300.0 + 0.015 * 48000.0, 48000.0, 1, -1.0f, 8192), 48000.0), false, false);
                result = processor.alignSlots (0);
                expect (! result.ok);
                expect (result.message.containsIgnoreCase ("can't be aligned"), result.message);
                expectWithinAbsoluteError (getParam (processor, "slot1_delay"), 1.25f, 0.003f);
                expectEquals (getParam (processor, "slot3_phase"), 0.0f);
                expectEquals (getParam (processor, "slot3_delay"), 0.0f);

                // An unrelated IR in a slot is not touched: its delay and polarity stay as set
                processor.getSlots().setSource (2, makeSource (makeTestIR (1, 6000, 777, 50), 48000.0), false, false);
                setParam (processor, "slot3_delay", 2.5f);
                setParam (processor, "slot3_phase", 1.0f);
                result = processor.alignSlots (0);
                expect (result.ok, result.message);
                expect (! result.slots[2].aligned);
                expectWithinAbsoluteError (getParam (processor, "slot3_delay"), 2.5f, 0.001f);
                expectEquals (getParam (processor, "slot3_phase"), 1.0f);
                expectWithinAbsoluteError (getParam (processor, "slot1_delay"), 1.25f, 0.003f);
                setParam (processor, "slot3_phase", 0.0f);
                setParam (processor, "slot3_delay", 0.0f);

                // Reference slot empty / only one IR
                processor.getSlots().clear (2);
                processor.getSlots().clear (1);
                expect (! processor.alignSlots (1).ok);
                expect (! processor.alignSlots (0).ok);

                // Before the engine runs there is nothing to analyse
                IReneProcessor notPrepared;
                notPrepared.getSlots().setSource (0, makeSource (makeMicIR (4.0), kFs), false, false);
                notPrepared.getSlots().setSource (1, makeSource (makeMicIR (9.0), kFs), false, false);
                result = notPrepared.alignSlots (0);
                expect (! result.ok && result.message.isNotEmpty());
            }

            beginTest ("Slot options are respected: auto-trim and length change what is aligned");
            {
                // With auto-trim on, a slot's leading silence is already removed, so
                // the alignment must be based on the IR as it is actually played.
                IReneProcessor processor;
                setParam (processor, "autogain", 0.0f); // these tests measure how the mics add up
                processor.setNonRealtime (true);
                processor.prepareToPlay (kFs, 256);

                processor.getSlots().setSource (0, makeSource (makeMicIR (2.0), kFs), false, false);
                const auto singleLevel = outputLevelDb (processor);

                processor.getSlots().setSource (1, makeSource (makeMicIR (2.0 + 200.0), kFs), false, true); // auto-trimmed
                const auto result = processor.alignSlots (0);
                expect (result.ok, result.message);

                // both delays are small: the 200 samples were trimmed away, not delayed for
                expect (getParam (processor, "slot1_delay") < 0.3f && getParam (processor, "slot2_delay") < 0.3f);

                outputLevelDb (processor);
                expectWithinAbsoluteError (outputLevelDb (processor) - singleLevel, 6.02, 0.15);
            }

            beginTest ("The Align button is on every slot strip");
            {
                GlobalSettings::Shared()->set ({});
                IReneProcessor processor;
                setParam (processor, "autogain", 0.0f); // these tests measure how the mics add up
                processor.prepareToPlay (kFs, 256);
                processor.getSlots().setSource (0, makeSource (makeMicIR (4.0), kFs), false, false);
                processor.getSlots().setSource (1, makeSource (makeMicIR (30.0), kFs), false, false);

                std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

                // find the buttons by their text and click the one of slot 2
                juce::Array<juce::Button*> alignButtons;
                std::function<void (juce::Component&)> collect = [&] (juce::Component& c)
                {
                    for (auto* child : c.getChildren())
                    {
                        if (auto* b = dynamic_cast<juce::Button*> (child); b != nullptr && b->getButtonText() == "Align")
                            alignButtons.add (b);

                        collect (*child);
                    }
                };

                collect (*editor);
                expectEquals (alignButtons.size(), kNumSlots);

                if (alignButtons.size() == kNumSlots)
                {
                    expect (alignButtons[0]->isEnabled() && alignButtons[1]->isEnabled());
                    expect (! alignButtons[kNumSlots - 1]->isEnabled()); // empty slot

                    alignButtons[1]->triggerClick();
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);

                    expectWithinAbsoluteError (getParam (processor, "slot1_delay"), 26.0f / 48.0f, 0.002f);
                    expectWithinAbsoluteError (getParam (processor, "slot2_delay"), 0.0f, 0.002f);
                    editor->createComponentSnapshot (editor->getLocalBounds()); // the result dialog draws
                }

                editor.reset();
            }
        }
    };

    TimeAlignTests timeAlignTests;
}
