#include "TestHelpers.h"

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "analysis/Response.h"

namespace
{
    using namespace irene;
    using namespace irene::test;

    constexpr double kFs = 48000.0;

    void pump (int milliseconds = 50)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
    }

    juce::AudioBuffer<float> copyOf (const juce::AudioBuffer<float>& b)
    {
        juce::AudioBuffer<float> c;
        c.makeCopyOf (b);
        return c;
    }

    /** A smooth, band-limited test IR (so that its response has no razor-thin
        notches that would make dB comparisons meaningless). */
    juce::AudioBuffer<float> makeSmoothIR (int numChannels, int seed, int preDelay)
    {
        juce::Random random (seed);
        juce::AudioBuffer<float> ir (numChannels, 512);
        ir.clear();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            ir.setSample (ch, preDelay, 0.6f + 0.2f * random.nextFloat());

            for (int i = preDelay + 1; i < preDelay + 40; ++i)
                ir.setSample (ch, i, 0.25f * std::exp (-0.15f * static_cast<float> (i - preDelay)) * (random.nextFloat() * 2.0f - 1.0f));
        }

        return ir;
    }

    class ResponseTests final : public juce::UnitTest
    {
    public:
        ResponseTests() : juce::UnitTest ("Frequency response display", "IRene") {}

        void runTest() override
        {
            const auto frequencies = analysis::makeDisplayFrequencies();

            beginTest ("Display frequencies: 20 Hz to 20 kHz, log spaced");
            {
                expectWithinAbsoluteError (frequencies.front(), 20.0, 1.0e-9);
                expectWithinAbsoluteError (frequencies.back(), 20000.0, 1.0e-6);
                expect (frequencies.size() > 400);
                expectWithinAbsoluteError (frequencies[48] / frequencies[0], 2.0, 1.0e-9); // 48 points per octave
            }

            beginTest ("IR spectrum: exact for short IRs, FFT path agrees for long ones");
            {
                // delta at sample 10, gain 0.5: magnitude -6.02 dB everywhere, linear phase
                const auto spectrum = analysis::computeSpectrum (makeDelta (1, 64, 10, 0.5f), kFs, frequencies);
                expect (spectrum.isValid() && ! spectrum.stereo);

                for (size_t i = 0; i < frequencies.size(); i += 37)
                {
                    const auto expected = std::polar (0.5, -2.0 * std::numbers::pi * frequencies[i] / kFs * 10.0);
                    expect (std::abs (std::complex<double> (spectrum.left[i]) - expected) < 1.0e-5);
                    expect (spectrum.left[i] == spectrum.right[i]);
                }

                // A long IR goes through the FFT path. Put its energy early (as in real IRs)
                // and compare with the exact evaluation of the short version.
                const auto shortIR = makeSmoothIR (2, 5, 20);
                juce::AudioBuffer<float> longIR (2, 200000);
                longIR.clear();
                longIR.copyFrom (0, 0, shortIR, 0, 0, 512);
                longIR.copyFrom (1, 0, shortIR, 1, 0, 512);

                const auto exact = analysis::computeSpectrum (shortIR, kFs, frequencies);
                const auto viaFft = analysis::computeSpectrum (longIR, kFs, frequencies);
                expect (viaFft.stereo);

                for (size_t i = 0; i < frequencies.size(); i += 11)
                    expectWithinAbsoluteError (toDb (std::abs (viaFft.right[i])), toDb (std::abs (exact.right[i])), 0.05);

                expect (! analysis::computeSpectrum (juce::AudioBuffer<float>(), kFs, frequencies).isValid());
            }

            beginTest ("The displayed response equals what the engine actually does");
            {
                // Three slots with different IRs, delays, filters, pan, phase and gain;
                // input and output filters on. Measure the engine with an impulse and
                // compare with the analytic curve on both channels and the mid signal.
                const std::array<juce::AudioBuffer<float>, 3> irs { makeSmoothIR (1, 1, 3), makeSmoothIR (2, 2, 9), makeSmoothIR (1, 3, 5) };

                EngineSettings s;
                s.inputFilter.hpOn = true;
                s.inputFilter.hpFreq = 70.0f;
                s.inputFilter.hpSteep = true;
                s.outputFilter.bands[2] = { true, 6000.0f, -4.0f, 0.8f, true };
                s.outputFilter.lpOn = true;
                s.outputFilter.lpFreq = 12000.0f;

                s.slots[0].gainDb = -3.0f;
                s.slots[0].pan = -0.6f;
                s.slots[0].filter.bands[1] = { true, 900.0f, 6.0f, 2.0f, false };

                s.slots[1].gainDb = 2.0f;
                s.slots[1].pan = 0.4f;
                s.slots[1].delayMs = 0.73f; // 35.04 samples: fractional
                s.slots[1].phaseInvert = true;
                s.slots[1].filter.lpOn = true;
                s.slots[1].filter.lpFreq = 5000.0f;

                s.slots[2].delayMs = 0.25f; // 12 samples: comb filtering against slot 1
                s.slots[2].filter.hpOn = true;
                s.slots[2].filter.hpFreq = 300.0f;

                s.outGainDb = -9.0f; // must NOT appear in the display
                s.mix = 0.5f;        // neither must the mix: the display shows the wet path

                const auto measure = [&] (const EngineSettings& settings, bool stereoOut)
                {
                    Engine engine;

                    for (int i = 0; i < 3; ++i)
                        engine.getSlot (i).setIR (copyOf (irs[static_cast<size_t> (i)]));

                    engine.prepare (kFs, 256, stereoOut ? 2 : 1, stereoOut ? 2 : 1);

                    auto wetOnly = settings;
                    wetOnly.mix = 1.0f;
                    wetOnly.outGainDb = 0.0f;

                    auto impulse = makeDelta (stereoOut ? 2 : 1, 1 << 14);
                    return render (engine, impulse, wetOnly, { 256 });
                };

                analysis::ResponseInput input;
                input.settings = s;
                input.sampleRate = kFs;

                for (size_t i = 0; i < 3; ++i)
                    input.slots[i] = std::make_shared<const analysis::SlotSpectrum> (analysis::computeSpectrum (irs[i], kFs, frequencies));

                const auto compare = [&] (const analysis::ResponseCurves& curves, const std::vector<float>& measuredDb, const juce::String& what)
                {
                    auto worst = 0.0f;

                    for (size_t i = 0; i < frequencies.size(); ++i)
                        if (measuredDb[i] > -70.0f) // below that, float noise in the measurement dominates
                            worst = std::max (worst, std::abs (curves.combinedDb[i] - measuredDb[i]));

                    expect (worst < 0.05f, what + ": display and engine differ by up to " + juce::String (worst, 3) + " dB");
                };

                const auto measuredCurve = [&] (const juce::AudioBuffer<float>& out, analysis::Channel channel)
                {
                    std::vector<float> db (frequencies.size());

                    for (size_t i = 0; i < frequencies.size(); ++i)
                    {
                        const auto left = dtft (out.getReadPointer (0), out.getNumSamples(), frequencies[i], kFs);
                        const auto right = out.getNumChannels() > 1 ? dtft (out.getReadPointer (1), out.getNumSamples(), frequencies[i], kFs) : left;
                        const auto value = channel == analysis::Channel::left ? left : (channel == analysis::Channel::right ? right : 0.5 * (left + right));
                        db[i] = static_cast<float> (toDb (std::abs (value)));
                    }

                    return db;
                };

                // stereo output
                auto out = measure (s, true);

                for (const auto channel : { analysis::Channel::left, analysis::Channel::right, analysis::Channel::mid })
                    compare (analysis::computeResponse (input, frequencies, channel, analysis::Smoothing::none),
                             measuredCurve (out, channel), "stereo, channel " + juce::String (static_cast<int> (channel)));

                // The comb filtering between the slots is really in there: the combined
                // curve is not simply the louder of the slot curves.
                {
                    const auto curves = analysis::computeResponse (input, frequencies, analysis::Channel::mid, analysis::Smoothing::none);
                    auto deepestDip = 0.0f;

                    for (size_t i = 0; i < frequencies.size(); ++i)
                    {
                        const auto loudestSlot = std::max ({ curves.slotDb[0][i], curves.slotDb[1][i], curves.slotDb[2][i] });
                        deepestDip = std::min (deepestDip, curves.combinedDb[i] - loudestSlot);
                    }

                    expect (deepestDip < -6.0f, "No cancellation between slots visible");
                }

                // mute / solo follow what is heard
                auto soloed = s;
                soloed.slots[2].solo = true;
                input.settings = soloed;
                out = measure (soloed, true);
                compare (analysis::computeResponse (input, frequencies, analysis::Channel::left, analysis::Smoothing::none),
                         measuredCurve (out, analysis::Channel::left), "solo");

                // mono output: pan is ignored, stereo IRs are summed
                input.settings = s;
                input.stereoOutput = false;
                out = measure (s, false);
                compare (analysis::computeResponse (input, frequencies, analysis::Channel::mid, analysis::Smoothing::none),
                         measuredCurve (out, analysis::Channel::mid), "mono output");
            }

            beginTest ("Per-slot curves, empty slots, smoothing, filter curve");
            {
                analysis::ResponseInput input;
                input.sampleRate = kFs;

                // nothing loaded
                auto curves = analysis::computeResponse (input, frequencies, analysis::Channel::mid, analysis::Smoothing::none);
                expect (! curves.anything);

                // one slot with a delta at -6 dB: flat line at -6.02 dB, others have no curve
                input.slots[1] = std::make_shared<const analysis::SlotSpectrum> (analysis::computeSpectrum (makeDelta (1, 8, 0, 0.5f), kFs, frequencies));
                curves = analysis::computeResponse (input, frequencies, analysis::Channel::mid, analysis::Smoothing::none);
                expect (curves.anything);
                expect (curves.slotDb[0].empty() && curves.slotDb[2].empty());
                expectEquals (curves.slotDb[1].size(), frequencies.size());

                for (size_t i = 0; i < frequencies.size(); i += 50)
                {
                    expectWithinAbsoluteError (curves.combinedDb[i], -6.02f, 0.01f);
                    expectWithinAbsoluteError (curves.slotDb[1][i], -6.02f, 0.01f);
                }

                // hard-panned mono IR: +3 dB left, nothing right
                input.settings.slots[1].pan = -1.0f;
                curves = analysis::computeResponse (input, frequencies, analysis::Channel::left, analysis::Smoothing::none);
                expectWithinAbsoluteError (curves.combinedDb[100], -3.01f, 0.02f);
                curves = analysis::computeResponse (input, frequencies, analysis::Channel::right, analysis::Smoothing::none);
                expect (curves.combinedDb[100] < -100.0f);
                input.settings.slots[1].pan = 0.0f;

                // Smoothing fills in a narrow notch but keeps the overall level
                input.settings.outputFilter.bands[1] = { true, 1000.0f, -18.0f, 10.0f, false };
                const auto raw = analysis::computeResponse (input, frequencies, analysis::Channel::mid, analysis::Smoothing::none);
                const auto sixth = analysis::computeResponse (input, frequencies, analysis::Channel::mid, analysis::Smoothing::sixthOctave);
                const auto third = analysis::computeResponse (input, frequencies, analysis::Channel::mid, analysis::Smoothing::thirdOctave);

                const auto minOf = [] (const std::vector<float>& v) { return *std::min_element (v.begin(), v.end()); };
                expectWithinAbsoluteError (minOf (raw.combinedDb), -24.02f, 0.2f);
                expect (minOf (sixth.combinedDb) > minOf (raw.combinedDb) + 2.0f);
                expect (minOf (third.combinedDb) > minOf (sixth.combinedDb) + 1.0f);
                expectWithinAbsoluteError (third.combinedDb[10], -6.02f, 0.05f); // far from the notch nothing changes

                // The highlighted filter block's own curve
                const auto filterCurve = analysis::computeFilterCurve (input.settings.outputFilter, kFs, frequencies);
                expectWithinAbsoluteError (minOf (filterCurve), -18.0f, 0.2f);
                expectWithinAbsoluteError (filterCurve[10], 0.0f, 0.05f);
            }

            beginTest ("In the plugin: follows IR changes, parameters and the selected filter tab");
            {
                GlobalSettings::Shared()->set ({});

                IReneProcessor processor;
                processor.prepareToPlay (kFs, 256);

                std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
                auto* irene = dynamic_cast<IReneEditor*> (editor.get());
                expect (irene != nullptr);

                if (irene == nullptr)
                    return;

                auto* display = irene->getDisplayForTests();
                display->getWorkerForTests().waitUntilUpToDate();
                expect (! display->updateNowForTests().anything);

                // Load an IR (delta at -12 dB, not normalised): flat curve at -12 dB
                processor.getSlots().setSource (0, makeSource (makeDelta (1, 16, 0, 0.25f), kFs), false, false);
                display->getWorkerForTests().waitUntilUpToDate();
                pump();

                auto curves = display->updateNowForTests();
                expect (curves.anything);
                expectWithinAbsoluteError (curves.combinedDb[200], -12.04f, 0.02f);

                // Slot gain moves the curve
                processor.getValueTreeState().getParameter ("slot1_gain")->setValueNotifyingHost (
                    processor.getValueTreeState().getParameter ("slot1_gain")->convertTo0to1 (6.0f));
                curves = display->updateNowForTests();
                expectWithinAbsoluteError (curves.combinedDb[200], -6.04f, 0.03f);

                // Normalisation changes the IR itself: the display follows via the background thread
                processor.getSlots().setSlotOptions (0, true, false);
                display->getWorkerForTests().waitUntilUpToDate();
                curves = display->updateNowForTests();
                expectWithinAbsoluteError (curves.combinedDb[200], 6.0f, 0.05f);

                // Display options are part of the saved editor state
                processor.uiState.responseChannel = 2;
                processor.uiState.responseSmoothing = 1;
                processor.uiState.showSlotCurves = false;
                juce::MemoryBlock state;
                processor.getStateInformation (state);

                IReneProcessor restored;
                restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
                expectEquals (restored.uiState.responseChannel.load(), 2);
                expectEquals (restored.uiState.responseSmoothing.load(), 1);
                expect (! restored.uiState.showSlotCurves.load());

                // Painting with everything on
                processor.uiState.showSlotCurves = true;
                processor.getValueTreeState().getParameter ("in_mid_on")->setValueNotifyingHost (1.0f);
                processor.getValueTreeState().getParameter ("in_mid_gain")->setValueNotifyingHost (0.9f);
                pump (120);
                editor->createComponentSnapshot (editor->getLocalBounds());
                editor.reset();
            }
        }
    };

    ResponseTests responseTests;
}
