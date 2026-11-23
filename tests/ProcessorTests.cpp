#include "TestHelpers.h"

#include "PluginProcessor.h"
#include "ir/IRProcessing.h"

namespace
{
    using namespace irene;
    using namespace irene::test;

    void setParam (IReneProcessor& p, const juce::String& id, float realValue)
    {
        auto* param = p.getValueTreeState().getParameter (id);
        jassert (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }

    float getParam (IReneProcessor& p, const juce::String& id)
    {
        return p.getValueTreeState().getRawParameterValue (id)->load();
    }

    /** True if all parameters are at their defaults (allowing for float rounding
        in the parameter ranges). */
    bool isDefault (const EngineSettings& s)
    {
        const auto close = [] (float a, float b) { return std::abs (a - b) <= 1.0e-3f * std::max (1.0f, std::abs (b)); };

        const auto filterIsDefault = [&] (const FilterBlockSettings& f)
        {
            const FilterBlockSettings d;
            auto ok = f.bypass == d.bypass && f.hpOn == d.hpOn && f.lpOn == d.lpOn && f.hpSteep == d.hpSteep && f.lpSteep == d.lpSteep
                      && close (f.hpFreq, d.hpFreq) && close (f.lpFreq, d.lpFreq);

            for (size_t b = 0; b < f.bands.size(); ++b)
                ok = ok && f.bands[b].on == d.bands[b].on && f.bands[b].shelf == d.bands[b].shelf
                     && close (f.bands[b].freq, d.bands[b].freq) && close (f.bands[b].q, d.bands[b].q)
                     && close (f.bands[b].gainDb, d.bands[b].gainDb);

            return ok;
        };

        const EngineSettings d;
        auto ok = close (s.mix, d.mix) && close (s.outGainDb, d.outGainDb) && filterIsDefault (s.inputFilter) && filterIsDefault (s.outputFilter);

        for (const auto& slot : s.slots)
            ok = ok && close (slot.gainDb, 0.0f) && close (slot.pan, 0.0f) && close (slot.delayMs, 0.0f) && close (slot.lengthPercent, 100.0f)
                 && ! slot.phaseInvert && ! slot.mute && ! slot.solo && filterIsDefault (slot.filter);

        return ok;
    }

    juce::AudioBuffer<float> renderProcessor (IReneProcessor& p, const juce::AudioBuffer<float>& input, int blockSize)
    {
        juce::AudioBuffer<float> output;
        output.makeCopyOf (input);
        juce::MidiBuffer midi;

        for (int pos = 0; pos < output.getNumSamples(); pos += blockSize)
        {
            const auto n = std::min (blockSize, output.getNumSamples() - pos);
            juce::AudioBuffer<float> block (output.getArrayOfWritePointers(), output.getNumChannels(), pos, n);
            p.processBlock (block, midi);
        }

        return output;
    }

    class ProcessorTests final : public juce::UnitTest
    {
    public:
        ProcessorTests() : juce::UnitTest ("Processor", "IRene") {}

        void runTest() override
        {
            const auto mono = juce::AudioChannelSet::mono();
            const auto stereo = juce::AudioChannelSet::stereo();

            beginTest ("Bus layouts");
            {
                IReneProcessor processor;
                expect (processor.checkBusesLayoutSupported (makeLayout (mono, mono)));
                expect (processor.checkBusesLayoutSupported (makeLayout (mono, stereo)));
                expect (processor.checkBusesLayoutSupported (makeLayout (stereo, stereo)));
                expect (! processor.checkBusesLayoutSupported (makeLayout (stereo, mono)));
                expect (! processor.checkBusesLayoutSupported (makeLayout (juce::AudioChannelSet::create5point1(), stereo)));
            }

            beginTest ("Parameter IDs are stable");
            {
                // DAW projects and automation depend on these IDs. This list is written out
                // independently of the plugin code on purpose: if this test fails, an ID was
                // renamed or removed, which breaks existing projects.
                juce::StringArray expected { "mix", "out_gain", "autogain" };

                const juce::StringArray filterSuffixes {
                    "filter_bypass", "hp_on", "hp_freq", "hp_slope",
                    "low_on", "low_freq", "low_gain", "low_q", "low_type",
                    "mid_on", "mid_freq", "mid_gain", "mid_q",
                    "high_on", "high_freq", "high_gain", "high_q", "high_type",
                    "lp_on", "lp_freq", "lp_slope"
                };

                const juce::StringArray slotSuffixes { "gain", "pan", "phase", "delay", "mute", "solo", "length" };

                juce::StringArray prefixes { "in", "out" };

                for (int i = 1; i <= kNumSlots; ++i)
                {
                    prefixes.add ("slot" + juce::String (i));

                    for (const auto& suffix : slotSuffixes)
                        expected.add ("slot" + juce::String (i) + "_" + suffix);
                }

                for (const auto& prefix : prefixes)
                    for (const auto& suffix : filterSuffixes)
                        expected.add (prefix + "_" + suffix);

                IReneProcessor processor;
                juce::StringArray actual;

                for (auto* param : processor.getParameters())
                    if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
                    {
                        actual.add (withId->paramID);

                        // 1 for everything up to 0.2 (slots 1-3), 2 for what 0.3 added
                        const auto slotNumber = withId->paramID.startsWith ("slot") ? withId->paramID.substring (4).getIntValue() : 0;
                        const auto addedIn03 = withId->paramID == "autogain" || slotNumber >= 4;
                        expectEquals (withId->getVersionHint(), addedIn03 ? 2 : 1, withId->paramID);
                    }

                expected.sort (false);
                actual.sort (false);
                expectEquals (actual.joinIntoString ("\n"), expected.joinIntoString ("\n"));
                expectEquals (actual.size(), 3 + kNumSlots * 7 + (kNumSlots + 2) * 21);

                // The parameters of 0.2 keep their positions; the new ones come after them
                const auto params = processor.getParameters();
                const auto idAt = [&params] (int index) { return dynamic_cast<juce::AudioProcessorParameterWithID*> (params[index])->paramID; };
                expectEquals (idAt (0), juce::String ("mix"));
                expectEquals (idAt (params.size() - 1), juce::String ("autogain"));

                // Defaults: fully wet, everything neutral
                expectEquals (getParam (processor, "mix"), 100.0f);
                expectEquals (getParam (processor, "slot1_gain"), 0.0f);
                expectEquals (getParam (processor, "out_gain"), 0.0f);    // exactly 0 dB on every CPU,
                expectEquals (getParam (processor, "in_low_gain"), 0.0f); // so that neutral means bit-transparent
                expectEquals (getParam (processor, "slot2_length"), 100.0f);
                expectEquals (getParam (processor, "in_hp_on"), 0.0f);
                expectEquals (getParam (processor, "out_mid_freq"), 1000.0f);
                expect (isDefault (processor.getParamRefs().read()));
            }

            beginTest ("Nothing loaded: dry signal, any block size, zero latency reported");
            {
                IReneProcessor processor;
                expect (processor.setBusesLayout (makeLayout (stereo, stereo)));
                processor.prepareToPlay (48000.0, 512);
                expectEquals (processor.getLatencySamples(), 0);

                const auto input = makeNoise (2, 3000, 1);
                const auto output = renderProcessor (processor, input, 173);

                for (int ch = 0; ch < 2; ++ch)
                    expect (errorDb (output.getReadPointer (ch), input.getReadPointer (ch), 3000) < -140.0);
            }

            beginTest ("Loading a file in the background");
            {
                TempFolder temp;
                const auto ir = makeTestIR (2, 1200, 2);
                const auto file = temp.folder.getChildFile ("cab.wav");
                expect (writeWav (file, ir, 48000.0));

                IReneProcessor processor;
                auto& slots = processor.getSlots();
                slots.setLoadDefaults (false, false);

                processor.prepareToPlay (48000.0, 256);
                expect (slots.getInfo (1).status == SlotInfo::Status::empty);

                slots.loadFile (1, file);
                slots.waitForPendingLoads();

                const auto info = slots.getInfo (1);
                expect (info.status == SlotInfo::Status::ready);
                expect (! info.loading);
                expectEquals (info.reference.name, juce::String ("cab.wav"));
                expectEquals (info.reference.hash, hashFile (file));
                expectEquals (info.sourceRate, 48000.0);
                expectEquals (info.engineRate, 48000.0);
                expectEquals (info.numChannels, 2);
                expect (! info.isResampled());
                expectWithinAbsoluteError (info.playedLengthSeconds, 0.025, 1.0e-9);
                expect (slots.getInfo (0).status == SlotInfo::Status::empty);

                // Offline rendering picks the new IR up immediately and deterministically
                processor.setNonRealtime (true);
                const auto input = makeNoise (2, 4000, 3);
                const auto output = renderProcessor (processor, input, 256);
                const auto expected = convolve (input.getReadPointer (1), 4000, ir.getReadPointer (1), 1200);
                expect (errorDb (output.getReadPointer (1), expected.data(), 4000) < -90.0);

                // A failed load reports an error and keeps the current IR
                slots.loadFile (1, temp.folder.getChildFile ("nope.wav"));
                slots.waitForPendingLoads();
                expect (slots.getInfo (1).status == SlotInfo::Status::ready);
                expect (slots.getInfo (1).error.isNotEmpty());
                expectEquals (slots.getInfo (1).reference.name, juce::String ("cab.wav"));

                // Rapid auditioning: only the last request counts
                juce::Array<juce::File> files;

                for (int i = 0; i < 6; ++i)
                {
                    files.add (temp.folder.getChildFile ("ir" + juce::String (i) + ".wav"));
                    expect (writeWav (files.getLast(), makeTestIR (1, 500 + i, 10 + i), 48000.0));
                }

                for (const auto& f : files)
                    slots.loadFile (0, f);

                slots.waitForPendingLoads();
                expectEquals (slots.getInfo (0).reference.name, juce::String ("ir5.wav"));
                expect (slots.getInfo (0).error.isEmpty());

                slots.clear (0);
                expect (slots.getInfo (0).status == SlotInfo::Status::empty);
                expect (slots.getInfo (0).reference.isEmpty());
            }

            beginTest ("Sample rate handling: always resampled from the original");
            {
                const auto ir = makeTestIR (1, 4800, 4, 300); // see the note on pre-ringing in IRProcessingTests
                const auto source = makeSource (ir, 48000.0);

                IReneProcessor processor;
                auto& slots = processor.getSlots();
                slots.setSource (0, source, false, false);

                // Session at 44.1 kHz: resampled
                processor.prepareToPlay (44100.0, 256);
                auto info = slots.getInfo (0);
                expect (info.isResampled());
                expectEquals (info.sourceRate, 48000.0);
                expectEquals (info.engineRate, 44100.0);
                expectEquals (slots.getPreparedIR (0).getNumSamples(), 4410);

                // The plugin's response at 44.1 kHz equals the IR's own response
                processor.setNonRealtime (true);
                auto impulse = makeDelta (2, 8192);
                auto output = renderProcessor (processor, impulse, 256);

                for (const auto freq : { 100.0, 1000.0, 5000.0, 12000.0 })
                    expectWithinAbsoluteError (toDb (std::abs (dtft (output.getReadPointer (0), 8192, freq, 44100.0))),
                                               toDb (std::abs (dtft (ir.getReadPointer (0), 4800, freq, 48000.0))), 0.02);

                // Then 96 kHz, then back to 48 kHz: because every change starts from the
                // original audio, the IR at 48 kHz is the untouched original, bit for bit.
                processor.prepareToPlay (96000.0, 256);
                expectEquals (slots.getPreparedIR (0).getNumSamples(), 9600);

                processor.prepareToPlay (48000.0, 256);
                info = slots.getInfo (0);
                expect (! info.isResampled());

                const auto prepared = slots.getPreparedIR (0);
                expectEquals (prepared.getNumSamples(), 4800);

                for (int i = 0; i < 4800; ++i)
                    expectEquals (prepared.getSample (0, i), ir.getSample (0, i));

                output = renderProcessor (processor, impulse, 256);

                for (int i = 0; i < 4800; i += 3)
                    expectWithinAbsoluteError (output.getSample (0, i), ir.getSample (0, i), 2.0e-6f);
            }

            beginTest ("Normalisation is separate from the slot gain; length parameter is followed");
            {
                const auto ir = makeDelta (1, 4800, 0, 0.25f);

                IReneProcessor processor;
                auto& slots = processor.getSlots();
                processor.prepareToPlay (48000.0, 256);
                processor.setNonRealtime (true);

                slots.setSource (0, makeSource (ir, 48000.0), true, false);
                expectWithinAbsoluteError (slots.getInfo (0).normalisationGainDb, 12.04f, 0.02f);
                expectEquals (getParam (processor, "slot1_gain"), 0.0f); // the user's gain is untouched

                auto impulse = makeDelta (2, 256);
                auto output = renderProcessor (processor, impulse, 256);
                expectWithinAbsoluteError (output.getSample (0, 0), 1.0f, 0.003f);

                slots.setSlotOptions (0, false, false);
                output = renderProcessor (processor, impulse, 256);
                expectWithinAbsoluteError (output.getSample (0, 0), 0.25f, 1.0e-5f);

                // The background thread notices a changed length parameter and rebuilds
                expectWithinAbsoluteError (slots.getInfo (0).playedLengthSeconds, 0.1, 1.0e-9);
                setParam (processor, "slot1_length", 50.0f);

                for (int tries = 0; tries < 400 && std::abs (slots.getInfo (0).playedLengthSeconds - 0.05) > 1.0e-6; ++tries)
                    juce::Thread::sleep (5);

                expectWithinAbsoluteError (slots.getInfo (0).playedLengthSeconds, 0.05, 1.0e-6);
            }

            beginTest ("State round trip: parameters and embedded IRs");
            {
                TempFolder temp;
                const auto file = temp.folder.getChildFile ("mic.wav");
                expect (writeWav (file, makeTestIR (2, 3000, 5), 96000.0));

                juce::MemoryBlock state;
                juce::AudioBuffer<float> outputBefore;
                const auto input = makeNoise (2, 6000, 6);
                IRReference reference;

                {
                    IReneProcessor processor;
                    auto& slots = processor.getSlots();
                    slots.setLoadDefaults (true, true);

                    slots.loadFile (0, file);
                    slots.waitForPendingLoads();
                    slots.setSource (2, makeSource (makeTestIR (1, 700, 7), 44100.0, "memory.wav"), false, false);
                    reference = slots.getInfo (0).reference;

                    setParam (processor, "slot1_gain", -4.5f);
                    setParam (processor, "slot1_pan", -30.0f);
                    setParam (processor, "slot1_delay", 1.234f);
                    setParam (processor, "slot1_length", 60.0f);
                    setParam (processor, "slot3_phase", 1.0f);
                    setParam (processor, "slot3_hp_on", 1.0f);
                    setParam (processor, "slot3_hp_freq", 150.0f);
                    setParam (processor, "slot3_hp_slope", 1.0f);
                    setParam (processor, "in_low_on", 1.0f);
                    setParam (processor, "in_low_gain", 3.0f);
                    setParam (processor, "in_low_type", 1.0f);
                    setParam (processor, "out_high_on", 1.0f);
                    setParam (processor, "out_high_gain", -2.0f);
                    setParam (processor, "out_high_q", 2.5f);
                    setParam (processor, "mix", 80.0f);
                    setParam (processor, "out_gain", -3.0f);

                    processor.getStateInformation (state);

                    processor.setNonRealtime (true);
                    processor.prepareToPlay (48000.0, 256);
                    outputBefore = renderProcessor (processor, input, 256);
                    expect (outputBefore.getMagnitude (0, 6000) > 0.01f);
                }

                // The original file is gone: the project must still open correctly
                expect (file.deleteFile());

                {
                    IReneProcessor processor;
                    processor.setStateInformation (state.getData(), static_cast<int> (state.getSize()));

                    expectWithinAbsoluteError (getParam (processor, "slot1_gain"), -4.5f, 1.0e-4f);
                    expectWithinAbsoluteError (getParam (processor, "slot1_delay"), 1.234f, 1.0e-4f);
                    expectEquals (getParam (processor, "slot3_hp_slope"), 1.0f);
                    expectEquals (getParam (processor, "in_low_type"), 1.0f);
                    expectWithinAbsoluteError (getParam (processor, "out_high_q"), 2.5f, 1.0e-4f);
                    expectWithinAbsoluteError (getParam (processor, "mix"), 80.0f, 1.0e-4f);

                    auto& slots = processor.getSlots();
                    const auto info0 = slots.getInfo (0);
                    expect (info0.status == SlotInfo::Status::ready);
                    expect (info0.reference == reference);
                    expectEquals (info0.sourceRate, 96000.0);
                    expectEquals (info0.numChannels, 2);
                    expect (info0.normalise && info0.autoTrim);

                    expect (slots.getInfo (1).status == SlotInfo::Status::empty);

                    const auto info2 = slots.getInfo (2);
                    expect (info2.status == SlotInfo::Status::ready);
                    expectEquals (info2.reference.name, juce::String ("memory.wav"));
                    expectEquals (info2.sourceRate, 44100.0);
                    expect (! info2.normalise && ! info2.autoTrim);

                    processor.setNonRealtime (true);
                    processor.prepareToPlay (48000.0, 256);
                    const auto outputAfter = renderProcessor (processor, input, 256);

                    for (int ch = 0; ch < 2; ++ch)
                        expect (errorDb (outputAfter.getReadPointer (ch), outputBefore.getReadPointer (ch), 6000) < -120.0,
                                "Restored instance sounds different");

                    // Saving the restored instance gives the same state again
                    juce::MemoryBlock stateAgain;
                    processor.getStateInformation (stateAgain);
                    expect (juce::ValueTree::readFromData (stateAgain.getData(), stateAgain.getSize())
                                .isEquivalentTo (juce::ValueTree::readFromData (state.getData(), state.getSize())));

                    // Restoring while already prepared (e.g. undo in the DAW) also works
                    processor.getSlots().clear (0);
                    processor.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
                    const auto outputAgain = renderProcessor (processor, input, 256);
                    expect (errorDb (outputAgain.getReadPointer (0) + 3000, outputBefore.getReadPointer (0) + 3000, 3000) < -60.0);
                }

                // Foreign or empty data is ignored
                {
                    IReneProcessor processor;
                    const char garbage[] = "this is not a valid state";
                    processor.setStateInformation (garbage, static_cast<int> (sizeof (garbage)));
                    processor.setStateInformation (nullptr, 0);
                    expect (isDefault (processor.getParamRefs().read()));
                }
            }

            beginTest ("State without embedded audio: file is found by path, otherwise marked missing");
            {
                TempFolder temp;
                const auto present = temp.folder.getChildFile ("present.wav");
                const auto gone = temp.folder.getChildFile ("gone.wav");
                expect (writeWav (present, makeTestIR (1, 800, 8), 48000.0));
                expect (writeWav (gone, makeTestIR (1, 900, 9), 48000.0));

                juce::ValueTree state;
                IRReference goneReference;

                {
                    IReneProcessor processor;
                    processor.getSlots().loadFile (0, present);
                    processor.getSlots().loadFile (1, gone);
                    processor.getSlots().waitForPendingLoads();
                    goneReference = processor.getSlots().getInfo (1).reference;
                    state = processor.saveState (false);
                }

                expect (gone.deleteFile());

                IReneProcessor processor;
                processor.restoreState (state);

                expect (processor.getSlots().getInfo (0).status == SlotInfo::Status::ready);
                expectEquals (processor.getSlots().getInfo (0).reference.name, juce::String ("present.wav"));

                const auto missing = processor.getSlots().getInfo (1);
                expect (missing.status == SlotInfo::Status::missing);
                expect (missing.reference == goneReference);

                // A missing slot is silent but keeps its reference when saved again
                const auto saved = processor.saveState (true);
                IReneProcessor another;
                another.restoreState (saved);
                expect (another.getSlots().getInfo (1).status == SlotInfo::Status::missing);
                expect (another.getSlots().getInfo (1).reference == goneReference);
                expect (another.getSlots().getInfo (0).status == SlotInfo::Status::ready);
            }

            beginTest ("Swap slots exchanges IR and all settings");
            {
                IReneProcessor processor;
                auto& slots = processor.getSlots();
                processor.prepareToPlay (48000.0, 256);
                processor.setNonRealtime (true);

                slots.setSource (0, makeSource (makeTestIR (1, 600, 20), 48000.0, "a.wav"), false, false);
                slots.setSource (2, makeSource (makeTestIR (2, 900, 21), 48000.0, "c.wav"), true, true);

                setParam (processor, "slot1_gain", -7.0f);
                setParam (processor, "slot1_mid_on", 1.0f);
                setParam (processor, "slot1_mid_gain", 6.0f);
                setParam (processor, "slot1_length", 70.0f);
                setParam (processor, "slot3_pan", 40.0f);
                setParam (processor, "slot3_delay", 2.5f);
                setParam (processor, "slot3_lp_on", 1.0f);
                setParam (processor, "slot3_solo", 0.0f);

                // the length change is picked up by the background thread
                for (int tries = 0; tries < 400 && std::abs (slots.getInfo (0).playedLengthSeconds - 0.00875) > 1.0e-6; ++tries)
                    juce::Thread::sleep (5);

                expectWithinAbsoluteError (slots.getInfo (0).playedLengthSeconds, 0.00875, 1.0e-6);

                const auto input = makeNoise (2, 5000, 22);
                renderProcessor (processor, input, 256); // let everything settle
                const auto before = renderProcessor (processor, input, 256);

                processor.swapSlots (0, 2);

                expectEquals (slots.getInfo (0).reference.name, juce::String ("c.wav"));
                expectEquals (slots.getInfo (2).reference.name, juce::String ("a.wav"));
                expect (slots.getInfo (0).normalise && slots.getInfo (0).autoTrim);
                expect (! slots.getInfo (2).normalise);

                expectWithinAbsoluteError (getParam (processor, "slot3_gain"), -7.0f, 1.0e-4f);
                expectEquals (getParam (processor, "slot1_gain"), 0.0f);
                expectEquals (getParam (processor, "slot3_mid_on"), 1.0f);
                expectWithinAbsoluteError (getParam (processor, "slot3_mid_gain"), 6.0f, 1.0e-4f);
                expectWithinAbsoluteError (getParam (processor, "slot3_length"), 70.0f, 1.0e-3f);
                expectWithinAbsoluteError (getParam (processor, "slot1_pan"), 40.0f, 1.0e-4f);
                expectWithinAbsoluteError (getParam (processor, "slot1_delay"), 2.5f, 1.0e-3f);
                expectEquals (getParam (processor, "slot1_lp_on"), 1.0f);
                expectEquals (getParam (processor, "slot2_gain"), 0.0f);

                // The sum of the slots is the same as before: nothing but the positions changed
                renderProcessor (processor, input, 256); // settle the smoothing
                const auto after = renderProcessor (processor, input, 256);

                for (int ch = 0; ch < 2; ++ch)
                    expect (errorDb (after.getReadPointer (ch) + 2000, before.getReadPointer (ch) + 2000, 3000) < -80.0,
                            "Swapping slots changed the sound");
            }

            beginTest ("Mono layouts through the processor, bypass");
            {
                IReneProcessor processor;
                expect (processor.setBusesLayout (makeLayout (mono, stereo)));
                processor.getSlots().setSource (0, makeSource (makeDelta (1, 4), 48000.0), false, false);
                processor.setNonRealtime (true);
                processor.prepareToPlay (48000.0, 128);

                juce::AudioBuffer<float> buffer (2, 128);
                auto noise = makeNoise (1, 128, 30);
                buffer.copyFrom (0, 0, noise, 0, 0, 128);

                for (int i = 0; i < 128; ++i)
                    buffer.setSample (1, i, 55.0f);

                juce::MidiBuffer midi;
                processor.processBlock (buffer, midi);

                for (int i = 0; i < 128; ++i)
                {
                    expectWithinAbsoluteError (buffer.getSample (0, i), noise.getSample (0, i), 1.0e-6f);
                    expectWithinAbsoluteError (buffer.getSample (1, i), noise.getSample (0, i), 1.0e-6f);
                }

                for (int i = 0; i < 128; ++i)
                    buffer.setSample (1, i, 55.0f);

                processor.processBlockBypassed (buffer, midi);

                for (int i = 0; i < 128; ++i)
                    expectEquals (buffer.getSample (1, i), buffer.getSample (0, i));

                IReneProcessor monoProcessor;
                expect (monoProcessor.setBusesLayout (makeLayout (mono, mono)));
                monoProcessor.getSlots().setSource (0, makeSource (makeDelta (2, 4), 48000.0), false, false);
                monoProcessor.setNonRealtime (true);
                monoProcessor.prepareToPlay (44100.0, 64);

                juce::AudioBuffer<float> monoBuffer (1, 64);
                monoBuffer.clear();
                monoBuffer.setSample (0, 0, 1.0f);
                monoProcessor.processBlock (monoBuffer, midi);
                expect (monoBuffer.getMagnitude (0, 64) > 0.5f);
            }
        }
    };

    ProcessorTests processorTests;
}
