#include "TestHelpers.h"

#include "PluginEditor.h"
#include "PluginProcessor.h"

// Tests for step 2 (version 0.3): auto gain, the dry signal when nothing is
// loaded, five slots with a visible-slot count, A/B comparison with loudness
// matching, and mono processing.

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

    void pump (int milliseconds = 60)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
    }

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

    juce::AudioBuffer<float> renderProcessor (IReneProcessor& p, const juce::AudioBuffer<float>& input, int blockSize = 256)
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

    /** A processor with the given IRs (no normalisation), prepared offline at
        48 kHz in stereo, with the levels worked out. */
    void setUp (IReneProcessor& p, const std::vector<juce::AudioBuffer<float>>& irs)
    {
        p.setBusesLayout (makeLayout (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo()));
        p.setNonRealtime (true);
        p.prepareToPlay (kFs, 256);

        for (size_t i = 0; i < irs.size(); ++i)
            p.getSlots().setSource (static_cast<int> (i), makeSource (irs[i], kFs, "ir" + juce::String (static_cast<int> (i)) + ".wav"), false, false);

        p.updateLevelsNow();
    }

    /** Removes the parameter with the given ID from a saved state (to fake an older version). */
    void removeParameter (juce::ValueTree& state, const juce::String& id)
    {
        auto params = state.getChildWithName ("Parameters");

        for (int i = params.getNumChildren(); --i >= 0;)
            if (params.getChild (i).getProperty ("id").toString() == id)
                params.removeChild (i, nullptr);
    }

    class LevelsAndSlotsTests final : public juce::UnitTest
    {
    public:
        LevelsAndSlotsTests() : juce::UnitTest ("Levels, slots and A/B", "IRene") {}

        void runTest() override
        {
            const auto irA = makeTestIR (1, 600, 101, 20);
            const auto irB = makeTestIR (1, 600, 102, 20);

            beginTest ("Auto gain: adding slots doesn't make the blend louder, soloing doesn't make it quieter");
            {
                const auto input = makeNoise (2, 8000, 103);
                const auto single = convolve (input.getReadPointer (0), 8000, irA.getReadPointer (0), 600);

                // Two identical slots add up to +6 dB. Auto gain takes exactly that away.
                IReneProcessor processor;
                expectEquals (getParam (processor, "autogain"), 1.0f); // on in a new instance
                setUp (processor, { irA, irA });
                expectWithinAbsoluteError (processor.getLevels().getAutoGainDb(), -6.0206f, 0.01f);

                auto output = renderProcessor (processor, input);
                expect (errorDb (output.getReadPointer (0), single.data(), 8000) < -60.0);

                // Soloing one of them: no correction, the same loudness
                setParam (processor, "slot2_solo", 1.0f);
                processor.updateLevelsNow();
                expectWithinAbsoluteError (processor.getLevels().getAutoGainDb(), 0.0f, 1.0e-4f);
                setParam (processor, "slot2_solo", 0.0f);

                // A quieter second slot: only the extra loudness it adds is taken away
                setParam (processor, "slot2_gain", -10.0f);
                processor.updateLevelsNow();
                const auto expected = static_cast<float> (-20.0 * std::log10 (1.0 + std::pow (10.0, -10.0 / 20.0)));
                expectWithinAbsoluteError (processor.getLevels().getAutoGainDb(), expected, 0.02f);

                // Out of phase: the blend is quieter, and auto gain never boosts
                setParam (processor, "slot2_gain", 0.0f);
                setParam (processor, "slot2_phase", 1.0f);
                processor.updateLevelsNow();
                expectEquals (processor.getLevels().getAutoGainDb(), 0.0f);
                setParam (processor, "slot2_phase", 0.0f);

                // Off: the plain sum
                setParam (processor, "autogain", 0.0f);
                processor.updateLevelsNow();
                expectEquals (processor.readSettings().autoGainDb, 0.0f);
                processor.prepareToPlay (kFs, 256);
                output = renderProcessor (processor, input);
                std::vector<float> singleFloat (single.begin(), single.end());
                expectWithinAbsoluteError (rmsDb (output.getReadPointer (0) + 2000, 6000) - rmsDb (singleFloat.data() + 2000, 6000), 6.02, 0.01);

                // The display shows the corrected blend (auto gain is part of the wet path)
                setParam (processor, "autogain", 1.0f);
                processor.updateLevelsNow();
                analysis::ResponseInput in;
                in.settings = processor.readSettings();
                in.sampleRate = kFs;

                for (int i = 0; i < kNumSlots; ++i)
                    in.slots[static_cast<size_t> (i)] = processor.getSpectra().getSpectrum (i);

                const auto curves = analysis::computeResponse (in, processor.getSpectra().getFrequencies(), analysis::Channel::left, analysis::Smoothing::none);
                // each slot's curve is its share of what you hear: half of the (corrected) blend
                expectWithinAbsoluteError (curves.combinedDb[200] - curves.slotDb[0][200], 6.02f, 0.01f);
            }

            beginTest ("Auto gain is off in projects and presets from before 0.3");
            {
                IReneProcessor fresh;
                auto state = fresh.saveState (false);
                removeParameter (state, "autogain");
                state.setProperty ("stateVersion", 1, nullptr);

                IReneProcessor restored;
                restored.restoreState (state);
                expectEquals (getParam (restored, "autogain"), 0.0f);

                IReneProcessor preset;
                preset.applyPresetState (state);
                expectEquals (getParam (preset, "autogain"), 0.0f);

                // ...while a 0.3 state keeps whatever it says
                IReneProcessor current;
                current.restoreState (fresh.saveState (false));
                expectEquals (getParam (current, "autogain"), 1.0f);
            }

            beginTest ("Five slots: 3 shown by default, + and x");

            if (kNumSlots != 5)
            {
                logMessage ("  (skipped: written for kNumSlots = 5)");
            }
            else
            {
                IReneProcessor processor;
                expectEquals (processor.getVisibleSlots(), 3);
                expect (! processor.canHideLastSlot());

                expect (processor.addVisibleSlot());
                expect (processor.addVisibleSlot());
                expect (! processor.addVisibleSlot());
                expectEquals (processor.getVisibleSlots(), 5);

                // An empty last slot can be hidden; its settings go back to the defaults
                setParam (processor, "slot5_solo", 1.0f);
                setParam (processor, "slot5_gain", -7.0f);
                processor.uiState.targetSlot = 4;
                expect (processor.canHideLastSlot());
                expect (processor.hideLastSlot());
                expectEquals (processor.getVisibleSlots(), 4);
                expectEquals (getParam (processor, "slot5_solo"), 0.0f);
                expectEquals (getParam (processor, "slot5_gain"), 0.0f);
                expectEquals (processor.uiState.targetSlot.load(), 3); // the target moved to a visible slot

                // A slot with an IR can't be hidden
                processor.getSlots().setSource (3, makeSource (irA, kFs), false, false);
                expect (! processor.canHideLastSlot());
                expect (! processor.hideLastSlot());

                // A slot that has an IR is always shown
                processor.getSlots().clear (3);
                expect (processor.hideLastSlot());
                expectEquals (processor.getVisibleSlots(), 3);
                processor.getSlots().setSource (4, makeSource (irA, kFs), false, false);
                expectEquals (processor.getVisibleSlots(), 5);

                // ...only while it has one: the count that was chosen (three) applies again
                processor.getSlots().clear (4);
                expectEquals (processor.getVisibleSlots(), 3);

                // Never fewer than three
                expect (! processor.hideLastSlot());
                expectEquals (processor.getVisibleSlots(), 3);
            }

            beginTest ("Visible slots are saved with projects and presets; older projects load");
            {
                IReneProcessor processor;
                processor.addVisibleSlot();
                processor.getSlots().setSource (1, makeSource (irA, kFs, "two.wav"), false, false);

                IReneProcessor restored;
                restored.restoreState (processor.saveState (true));
                expectEquals (restored.getVisibleSlots(), 4);
                expectEquals (restored.getSlots().getInfo (1).reference.name, juce::String ("two.wav"));

                IReneProcessor viaPreset;
                viaPreset.applyPresetState (processor.savePresetState (true));
                expectEquals (viaPreset.getVisibleSlots(), 4);

                // "Init" goes back to three
                viaPreset.resetToDefaults();
                expectEquals (viaPreset.getVisibleSlots(), 3);

                // A project from 0.2: three slot entries, no visible count, Output tab = 4
                auto old = processor.saveState (true);
                old.setProperty ("stateVersion", 1, nullptr);
                old.removeProperty ("visibleSlots", nullptr);
                auto slots = old.getChildWithName (SlotManager::treeType);

                for (int i = slots.getNumChildren(); --i >= 0;)
                    if (static_cast<int> (slots.getChild (i).getProperty ("index")) >= 3)
                        slots.removeChild (i, nullptr);

                old.getChildWithName ("UI").setProperty ("filterTab", 4, nullptr);
                old.removeChild (old.getChildWithName ("AB"), nullptr);

                IReneProcessor fromOld;
                fromOld.restoreState (old);
                expectEquals (fromOld.getVisibleSlots(), 3);
                expectEquals (fromOld.uiState.filterTab.load(), kNumSlots + 1); // still the Output tab
                expectEquals (fromOld.getSlots().getInfo (1).reference.name, juce::String ("two.wav"));
                expect (fromOld.getSlots().getInfo (3).status == SlotInfo::Status::empty);
                expect (fromOld.getSlots().getInfo (4).status == SlotInfo::Status::empty);

                // In a 0.3 project, tab 4 means Slot 4
                auto current = processor.saveState (true);
                current.getChildWithName ("UI").setProperty ("filterTab", 4, nullptr);
                IReneProcessor fromCurrent;
                fromCurrent.restoreState (current);
                expectEquals (fromCurrent.uiState.filterTab.load(), 4);
            }

            beginTest ("A/B: two complete setups");
            {
                IReneProcessor processor;
                setUp (processor, { irA });
                expectEquals (processor.getActiveSetup(), 0);
                expect (! processor.hasOtherSetup());

                // The first switch: B starts as a copy of A
                processor.switchToSetup (1);
                processor.getSlots().waitForPendingLoads();
                expectEquals (processor.getActiveSetup(), 1);
                expectEquals (processor.getSlots().getInfo (0).reference.name, juce::String ("ir0.wav"));

                // Change B: other gain, other IR, one more slot
                setParam (processor, "slot1_gain", -5.0f);
                processor.addVisibleSlot();
                processor.getSlots().setSource (0, makeSource (irB, kFs, "b.wav"), false, false);

                processor.switchToSetup (0);
                processor.getSlots().waitForPendingLoads();
                expectEquals (getParam (processor, "slot1_gain"), 0.0f);
                expectEquals (processor.getSlots().getInfo (0).reference.name, juce::String ("ir0.wav"));
                expectEquals (processor.getVisibleSlots(), 3);

                processor.switchToSetup (1);
                processor.getSlots().waitForPendingLoads();
                expectEquals (getParam (processor, "slot1_gain"), -5.0f);
                expectEquals (processor.getSlots().getInfo (0).reference.name, juce::String ("b.wav"));
                expectEquals (processor.getVisibleSlots(), 4);

                // Saved with the project, including the setup that isn't playing (with its audio)
                IReneProcessor restored;
                restored.restoreState (processor.saveState (true));
                restored.getSlots().waitForPendingLoads();
                expectEquals (restored.getActiveSetup(), 1);
                expectEquals (getParam (restored, "slot1_gain"), -5.0f);
                restored.switchToSetup (0);
                restored.getSlots().waitForPendingLoads();
                expectEquals (getParam (restored, "slot1_gain"), 0.0f);
                expectEquals (restored.getSlots().getInfo (0).reference.name, juce::String ("ir0.wav"));
                expect (restored.getSlots().getSource (0) != nullptr);

                // Copy B over A
                processor.copyActiveSetupToOther();
                processor.switchToSetup (0);
                processor.getSlots().waitForPendingLoads();
                expectEquals (getParam (processor, "slot1_gain"), -5.0f);
                expectEquals (processor.getSlots().getInfo (0).reference.name, juce::String ("b.wav"));

                // Presets only touch the setup that is playing
                expect (! processor.savePresetState (false).getChildWithName ("AB").isValid());
            }

            beginTest ("A/B loudness match: B is played at A's loudness");
            {
                const auto input = makeNoise (2, 8000, 104);
                const auto single = convolve (input.getReadPointer (0), 8000, irA.getReadPointer (0), 600);

                IReneProcessor processor;
                setUp (processor, { irA });
                processor.setLoudnessMatch (true);
                expectEquals (processor.getMatchGainDb(), 0.0f); // A plays as it is

                processor.switchToSetup (1);
                setParam (processor, "slot1_gain", 7.0f);
                setParam (processor, "out_gain", -2.0f);
                processor.updateLevelsNow();
                expectWithinAbsoluteError (processor.getMatchGainDb(), -5.0f, 0.02f);
                expectWithinAbsoluteError (processor.readSettings().matchGainDb, -5.0f, 0.02f);

                processor.prepareToPlay (kFs, 256);
                const auto output = renderProcessor (processor, input);
                expect (errorDb (output.getReadPointer (0), single.data(), 8000) < -60.0, "B should sound exactly as loud as A");

                // Export never includes the match gain
                expectEquals (processor.getParamRefs().read().matchGainDb, 0.0f);

                processor.setLoudnessMatch (false);
                expectEquals (processor.getMatchGainDb(), 0.0f);
            }

            beginTest ("Mono input with mono IRs: one channel is processed, same result");
            {
                const auto numSamples = 6000;
                const auto monoInput = makeNoise (1, numSamples, 105);
                const auto expected = convolve (monoInput.getReadPointer (0), numSamples, irA.getReadPointer (0), 600);

                for (const auto numOut : { 1, 2 })
                {
                    Engine engine;
                    engine.getSlot (0).setIR (copyOf (irA));
                    engine.prepare (kFs, 256, 1, numOut);

                    juce::AudioBuffer<float> io (numOut, numSamples);
                    io.copyFrom (0, 0, monoInput, 0, 0, numSamples);

                    const auto output = render (engine, io, {}, { 256 });
                    expect (engine.getSlot (0).wasProcessedInMono());

                    for (int ch = 0; ch < numOut; ++ch)
                        expect (errorDb (output.getReadPointer (ch), expected.data(), numSamples) < -90.0);
                }

                // Stereo input, or a stereo IR: both channels
                {
                    Engine engine;
                    engine.getSlot (0).setIR (copyOf (irA));
                    engine.prepare (kFs, 256, 2, 2);
                    render (engine, makeNoise (2, 1000, 106), {}, { 256 });
                    expect (! engine.getSlot (0).wasProcessedInMono());
                }

                // Switching to a stereo IR mid-stream: the right channel continues
                // exactly where the left one is (delay and filter memory).
                {
                    Engine engine;
                    engine.getSlot (0).setIR (copyOf (irA));
                    engine.prepare (kFs, 256, 1, 2);

                    EngineSettings s;
                    s.slots[0].delayMs = 1.37f;
                    s.slots[0].filter.bands[1] = { true, 900.0f, 6.0f, 1.2f, false };

                    juce::AudioBuffer<float> io (2, numSamples);
                    io.copyFrom (0, 0, monoInput, 0, 0, numSamples);
                    io.clear (1, 0, numSamples);

                    juce::AudioBuffer<float> first (io.getArrayOfWritePointers(), 2, 0, 3000);
                    juce::AudioBuffer<float> second (io.getArrayOfWritePointers(), 2, 3000, 3000);

                    for (int pos = 0; pos < 3000; pos += 250)
                    {
                        juce::AudioBuffer<float> block (first.getArrayOfWritePointers(), 2, pos, 250);
                        engine.process (block, s, true);
                    }

                    expect (engine.getSlot (0).wasProcessedInMono());

                    // a "stereo" IR with the same response on both sides
                    juce::AudioBuffer<float> twin (2, 600);
                    twin.copyFrom (0, 0, irA, 0, 0, 600);
                    twin.copyFrom (1, 0, irA, 0, 0, 600);
                    engine.getSlot (0).setIR (std::move (twin));

                    for (int pos = 0; pos < 3000; pos += 250)
                    {
                        juce::AudioBuffer<float> block (second.getArrayOfWritePointers(), 2, pos, 250);
                        engine.process (block, s, true);
                    }

                    expect (! engine.getSlot (0).wasProcessedInMono());
                    expect (errorDb (io.getReadPointer (1) + 3000, io.getReadPointer (0) + 3000, 3000) < -120.0,
                            "left and right must be identical straight after the switch");
                }
            }

            beginTest ("Editor: + and x, window width, filter tabs, A/B buttons, auto gain readout");

            if (kNumSlots != 5)
            {
                logMessage ("  (skipped: written for kNumSlots = 5)");
            }
            else
            {
                GlobalSettings::Shared settings;
                settings->set ({});

                IReneProcessor processor;
                setUp (processor, { irA, irA });

                std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
                auto* irene = dynamic_cast<IReneEditor*> (editor.get());
                expect (irene != nullptr);

                if (irene == nullptr)
                    return;

                pump();
                const auto browser = GlobalSettings::kDefaultBrowserWidth + IReneEditor::kGap;
                expectEquals (editor->getWidth(), IReneEditor::baseWidthFor (3) + browser);
                expect (irene->getAddSlotButtonForTests().isVisible());

                // "+" twice: five slots, a wider window, no more "+"
                irene->getAddSlotButtonForTests().triggerClick();
                pump();
                expectEquals (irene->getVisibleSlots(), 4);
                expectEquals (editor->getWidth(), IReneEditor::baseWidthFor (4) + browser);
                expect (irene->getStripForTests (3)->isVisible());
                editor->createComponentSnapshot (editor->getLocalBounds());

                irene->getAddSlotButtonForTests().triggerClick();
                pump();
                expectEquals (irene->getVisibleSlots(), 5);
                expect (! irene->getAddSlotButtonForTests().isVisible());
                expectEquals (editor->getWidth(), IReneEditor::baseWidthFor (5) + browser);

                // the Slot 5 filter tab can be selected; hiding the slot falls back to Input
                irene->getFilterEditorForTests()->selectTab (5);
                processor.hideLastSlot();
                pump();
                expectEquals (irene->getVisibleSlots(), 4);
                expectEquals (irene->getFilterEditorForTests()->getSelectedTab(), 0);

                // A project that uses slot 5 opens with five slots
                processor.getSlots().setSource (4, makeSource (irB, kFs), false, false);
                pump();
                expectEquals (irene->getVisibleSlots(), 5);
                processor.getSlots().clear (4);
                processor.hideLastSlot();
                processor.hideLastSlot();
                pump();
                expectEquals (irene->getVisibleSlots(), 3);
                expectEquals (editor->getWidth(), IReneEditor::baseWidthFor (3) + browser);

                // Auto gain readout (the levels are worked out in the background: wait for them)
                processor.updateLevelsNow();
                pump (150);
                expectEquals (irene->getGlobalSectionForTests()->getAutoGainTextForTests(), juce::String ("-6.0 dB"));

                // A/B from the outside (e.g. a project being loaded) updates the buttons
                processor.switchToSetup (1);
                pump();
                editor->createComponentSnapshot (editor->getLocalBounds());
                processor.switchToSetup (0);

                editor.reset();
                settings->set ({});
                pump();
            }
        }
    };

    LevelsAndSlotsTests levelsAndSlotsTests;
}
