#include "TestHelpers.h"

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "export/Exporter.h"

namespace
{
    using namespace irene;
    using namespace irene::test;

    void pump (int milliseconds = 50)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
    }

    void setParam (IReneProcessor& p, const juce::String& id, float realValue)
    {
        auto* param = p.getValueTreeState().getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }

    /** A realistic-ish cabinet IR: decaying, band-limited noise. */
    juce::AudioBuffer<float> makeCabIR (int numChannels, int numSamples, int seed, int preDelay)
    {
        auto ir = makeTestIR (numChannels, numSamples, seed, preDelay);

        // gentle low-pass so that most energy is where guitar cabinets have it
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto state = 0.0f;

            for (int i = 0; i < numSamples; ++i)
            {
                state += 0.35f * (ir.getSample (ch, i) - state);
                ir.setSample (ch, i, state);
            }
        }

        return ir;
    }

    /** Sets up a busy patch: three slots, everything in use. */
    void configure (IReneProcessor& p)
    {
        auto& slots = p.getSlots();
        slots.setSource (0, makeSource (makeCabIR (1, 2400, 1, 4), 48000.0, "a.wav"), true, false);
        slots.setSource (1, makeSource (makeCabIR (2, 4800, 2, 30), 96000.0, "b.wav"), true, true); // resampled, auto-trimmed
        slots.setSource (2, makeSource (makeCabIR (1, 1500, 3, 9), 44100.0, "c.wav"), false, false);

        setParam (p, "slot1_gain", -3.0f);
        setParam (p, "slot1_pan", -40.0f);
        setParam (p, "slot1_length", 70.0f);
        setParam (p, "slot1_hp_on", 1.0f);
        setParam (p, "slot1_hp_freq", 90.0f);

        setParam (p, "slot2_gain", 1.5f);
        setParam (p, "slot2_pan", 25.0f);
        setParam (p, "slot2_delay", 0.777f); // fractional samples
        setParam (p, "slot2_phase", 1.0f);
        setParam (p, "slot2_mid_on", 1.0f);
        setParam (p, "slot2_mid_freq", 1800.0f);
        setParam (p, "slot2_mid_gain", -5.0f);
        setParam (p, "slot2_mid_q", 2.0f);

        setParam (p, "slot3_gain", -6.0f);
        setParam (p, "slot3_delay", 2.0f);
        setParam (p, "slot3_lp_on", 1.0f);
        setParam (p, "slot3_lp_freq", 6000.0f);
        setParam (p, "slot3_lp_slope", 1.0f);

        setParam (p, "in_hp_on", 1.0f);
        setParam (p, "in_hp_freq", 60.0f);
        setParam (p, "in_hp_slope", 1.0f);
        setParam (p, "in_low_on", 1.0f);
        setParam (p, "in_low_gain", 2.5f);
        setParam (p, "in_low_type", 1.0f);
        setParam (p, "out_high_on", 1.0f);
        setParam (p, "out_high_freq", 4000.0f);
        setParam (p, "out_high_gain", 3.0f);
        setParam (p, "out_lp_on", 1.0f);
        setParam (p, "out_lp_freq", 11000.0f);
        setParam (p, "out_gain", -4.0f);
    }

    juce::AudioBuffer<float> renderPlugin (IReneProcessor& p, const juce::AudioBuffer<float>& input, double sampleRate,
                                           const juce::AudioChannelSet& layout)
    {
        p.setBusesLayout (makeLayout (layout, layout));
        p.setNonRealtime (true);
        p.prepareToPlay (sampleRate, 256);

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

    class ExportTests final : public juce::UnitTest
    {
    public:
        ExportTests() : juce::UnitTest ("Export as single IR", "IRene") {}

        /** The heart of it: noise through the plugin == noise convolved with the exported IR. */
        double equivalenceErrorDb (IReneProcessor& p, const ExportOptions& options, const juce::AudioChannelSet& layout,
                                   double sampleRate, float pluginScale = 1.0f)
        {
            const auto numChannels = layout.size();
            const auto numSamples = 12000;
            const auto input = makeNoise (numChannels, numSamples, 77);
            const auto pluginOut = renderPlugin (p, input, sampleRate, layout);

            const auto exported = renderExport (ExportSnapshot::capture (p), options);
            expect (exported.succeeded(), exported.error);

            if (! exported.succeeded())
                return 0.0;

            expectEquals (exported.audio.getNumChannels(), numChannels);
            expectEquals (exported.sampleRate, sampleRate);

            auto worst = -300.0;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto viaIR = convolve (input.getReadPointer (ch), numSamples, exported.audio.getReadPointer (ch), exported.audio.getNumSamples());

                for (auto& v : viaIR)
                    v /= pluginScale;

                // skip the very start, where the plugin's parameter smoothing could still move
                const auto skip = 2400;
                worst = std::max (worst, errorDb (pluginOut.getReadPointer (ch) + skip, viaIR.data() + skip, numSamples - skip));
            }

            return worst;
        }

        void runTest() override
        {
            const auto mono = juce::AudioChannelSet::mono();
            const auto stereo = juce::AudioChannelSet::stereo();

            beginTest ("Equivalence: plugin wet path == convolution with the exported IR");
            {
                IReneProcessor processor;
                configure (processor);

                // Export everything the plugin does (filters and output gain included), stereo, session rate
                ExportOptions options;
                options.stereo = true; // the default is mono
                options.includeOutputGain = true;

                auto error = equivalenceErrorDb (processor, options, stereo, 48000.0);
                logMessage ("  stereo, 48 kHz: error " + juce::String (error, 1) + " dB");
                expect (error < -60.0, "Stereo export differs from the plugin by " + juce::String (error, 1) + " dB");

                // Another session rate: every IR is resampled from its original
                error = equivalenceErrorDb (processor, options, stereo, 44100.0);
                logMessage ("  stereo, 44.1 kHz: error " + juce::String (error, 1) + " dB");
                expect (error < -60.0, "Export at 44.1 kHz differs by " + juce::String (error, 1) + " dB");

                // Mono: what the plugin does on a mono track
                options.stereo = false;
                error = equivalenceErrorDb (processor, options, mono, 48000.0);
                logMessage ("  mono, 48 kHz: error " + juce::String (error, 1) + " dB");
                expect (error < -60.0, "Mono export differs from the plugin by " + juce::String (error, 1) + " dB");

                // Mute and solo are exported as heard
                options.stereo = true;
                setParam (processor, "slot2_solo", 1.0f);
                setParam (processor, "slot3_solo", 1.0f);
                setParam (processor, "slot3_mute", 1.0f);
                error = equivalenceErrorDb (processor, options, stereo, 48000.0);
                expect (error < -60.0, "Export with mute/solo differs by " + juce::String (error, 1) + " dB");
                setParam (processor, "slot2_solo", 0.0f);
                setParam (processor, "slot3_solo", 0.0f);
                setParam (processor, "slot3_mute", 0.0f);

                // The dry/wet mix is ignored: the export is always 100 % wet
                setParam (processor, "mix", 35.0f);
                const auto withMix = renderExport (ExportSnapshot::capture (processor), options);
                setParam (processor, "mix", 100.0f);
                const auto fullyWet = renderExport (ExportSnapshot::capture (processor), options);
                expectEquals (withMix.audio.getNumSamples(), fullyWet.audio.getNumSamples());
                expect (errorDb (withMix.audio.getReadPointer (0), fullyWet.audio.getReadPointer (0), fullyWet.audio.getNumSamples()) < -150.0);
            }

            beginTest ("Options: output gain, filter blocks");
            {
                IReneProcessor processor;
                configure (processor);

                // Default: output gain NOT included -> the plugin (at -4 dB) is quieter by exactly that
                ExportOptions options;
                options.stereo = true; // the default is mono
                expect (! options.includeOutputGain && options.includeInputOutputFilters);
                const auto gain = juce::Decibels::decibelsToGain (-4.0f);
                auto error = equivalenceErrorDb (processor, options, stereo, 48000.0, 1.0f / gain);
                expect (error < -60.0, "Without output gain: " + juce::String (error, 1) + " dB");

                // Without input/output filters: equals the plugin with those blocks bypassed
                options.includeInputOutputFilters = false;
                options.includeOutputGain = true;
                setParam (processor, "in_filter_bypass", 1.0f);
                setParam (processor, "out_filter_bypass", 1.0f);
                error = equivalenceErrorDb (processor, options, stereo, 48000.0);
                expect (error < -60.0, "Without filter blocks: " + juce::String (error, 1) + " dB");

                // ...and the option really takes them out even when they are active in the plugin
                setParam (processor, "in_filter_bypass", 0.0f);
                setParam (processor, "out_filter_bypass", 0.0f);
                const auto without = renderExport (ExportSnapshot::capture (processor), options);
                options.includeInputOutputFilters = true;
                const auto with = renderExport (ExportSnapshot::capture (processor), options);
                const auto f = 30.0; // far below the 60 Hz input high-pass
                expect (toDb (std::abs (dtft (with.audio.getReadPointer (0), with.audio.getNumSamples(), f, 48000.0)))
                        < toDb (std::abs (dtft (without.audio.getReadPointer (0), without.audio.getNumSamples(), f, 48000.0))) - 10.0);
            }

            beginTest ("Sample rate, length, fade-out and peak normalisation");
            {
                IReneProcessor processor;
                configure (processor);
                processor.prepareToPlay (48000.0, 256);

                // Session rate by default
                ExportOptions options;
                options.stereo = true; // the default is mono; this test looks at both channels
                auto reference = renderExport (ExportSnapshot::capture (processor), options);
                expect (reference.succeeded());
                expectEquals (reference.sampleRate, 48000.0);

                // Auto length: ends when the tail is 90 dB down (plus a short fade), and ends at zero
                const auto n = reference.audio.getNumSamples();
                const auto peak = reference.audio.getMagnitude (0, n);
                expect (n > 2000 && n < 48000 * 4, "Auto length gave " + juce::String (n) + " samples");
                expect (reference.audio.getMagnitude (n - 300, 300) < peak * 1.0e-4f); // tail is far down
                expectWithinAbsoluteError (reference.audio.getSample (0, n - 1), 0.0f, 1.0e-9f);
                expectWithinAbsoluteError (reference.peakDb, juce::Decibels::gainToDecibels (peak), 0.01f);

                // Other rates: the same response. (Not to the last hundredth of a dB: the
                // filters and the fractional delay are computed for the new rate.)
                for (const auto rate : { 44100.0, 96000.0 })
                {
                    options.sampleRate = rate;
                    const auto other = renderExport (ExportSnapshot::capture (processor), options);
                    expect (other.succeeded());
                    expectEquals (other.sampleRate, rate);

                    for (const auto freq : { 150.0, 700.0, 2500.0, 6000.0 })
                        for (int ch = 0; ch < 2; ++ch)
                            expectWithinAbsoluteError (toDb (std::abs (dtft (other.audio.getReadPointer (ch), other.audio.getNumSamples(), freq, rate))),
                                                       toDb (std::abs (dtft (reference.audio.getReadPointer (ch), n, freq, 48000.0))),
                                                       0.4, "rate " + juce::String (rate) + ", " + juce::String (freq) + " Hz");
                }

                options.sampleRate = 0.0;

                // Fixed length, with fade-out
                options.autoLength = false;
                options.lengthMs = 20.0;
                const auto fixed = renderExport (ExportSnapshot::capture (processor), options);
                expectEquals (fixed.audio.getNumSamples(), 960);
                expectWithinAbsoluteError (fixed.audio.getSample (1, 959), 0.0f, 1.0e-9f);
                expectEquals (fixed.audio.getSample (0, 100), reference.audio.getSample (0, 100)); // start is untouched

                // Peak normalisation to -0.3 dBFS
                options.autoLength = true;
                options.normalisePeak = true;
                const auto normalised = renderExport (ExportSnapshot::capture (processor), options);
                expectWithinAbsoluteError (normalised.peakDb, -0.3f, 0.001f);
                expectWithinAbsoluteError (normalised.audio.getMagnitude (0, normalised.audio.getNumSamples()), 0.96605f, 1.0e-4f);
            }

            beginTest ("Nothing to export, silence, cancelling");
            {
                IReneProcessor processor;
                auto result = renderExport (ExportSnapshot::capture (processor), {});
                expect (! result.succeeded());
                expect (result.error.containsIgnoreCase ("nothing to export"));

                processor.getSlots().setSource (0, makeSource (makeCabIR (1, 1000, 5, 2), 48000.0), true, false);
                setParam (processor, "slot1_mute", 1.0f);
                result = renderExport (ExportSnapshot::capture (processor), {});
                expect (! result.succeeded());
                expect (result.error.containsIgnoreCase ("silent"));

                setParam (processor, "slot1_mute", 0.0f);
                result = renderExport (ExportSnapshot::capture (processor), {}, [] (float) { return false; });
                expect (! result.succeeded());
                expectEquals (result.error, juce::String ("Cancelled."));

                // progress goes from 0 to 1
                auto last = -1.0f;
                auto monotonic = true;
                result = renderExport (ExportSnapshot::capture (processor), {}, [&] (float p)
                {
                    monotonic = monotonic && p >= last;
                    last = p;
                    return true;
                });
                expect (result.succeeded() && monotonic);
                expectEquals (last, 1.0f);
            }

            beginTest ("Files: 24-bit and 32-bit float WAV; clipping is refused, not hidden");
            {
                TempFolder temp;
                IReneProcessor processor;
                processor.getSlots().setSource (0, makeSource (makeCabIR (2, 2000, 6, 3), 48000.0), true, false);
                const auto snapshot = ExportSnapshot::capture (processor);

                ExportOptions options;
                options.stereo = true; // the default is mono
                options.normalisePeak = true;
                const auto rendered = renderExport (snapshot, options);

                for (const auto bits : { 24, 32 })
                {
                    options.bitDepth = bits;
                    const auto file = temp.folder.getChildFile ("sub folder").getChildFile ("export" + juce::String (bits) + ".wav");
                    expectEquals (exportToFile (snapshot, options, file), juce::String());
                    expect (file.existsAsFile());

                    // The exported file is itself a valid IR that IRene can load again
                    const auto loaded = loadIRFile (file);
                    expect (loaded.source != nullptr, loaded.error);

                    if (loaded.source != nullptr)
                    {
                        expectEquals (loaded.source->sampleRate, 48000.0);
                        expectEquals (loaded.source->audio.getNumChannels(), 2);
                        expectEquals (loaded.source->audio.getNumSamples(), rendered.audio.getNumSamples());

                        const auto tolerance = bits == 32 ? 0.0f : 2.5e-7f;

                        for (int i = 0; i < rendered.audio.getNumSamples(); i += 13)
                            expectWithinAbsoluteError (loaded.source->audio.getSample (1, i), rendered.audio.getSample (1, i), tolerance);
                    }

                    const auto info = probeIRFile (file);
                    expect (info.valid);
                }

                // An IR that peaks above 0 dBFS can't go into a 24-bit file
                setParam (processor, "slot1_gain", 12.0f);
                setParam (processor, "out_gain", 24.0f);
                options.normalisePeak = false;
                options.includeOutputGain = true;
                options.bitDepth = 24;

                const auto loud = temp.folder.getChildFile ("loud.wav");
                const auto error = exportToFile (ExportSnapshot::capture (processor), options, loud);
                expect (error.containsIgnoreCase ("24-bit"), error);
                expect (! loud.existsAsFile());

                options.bitDepth = 32;
                expectEquals (exportToFile (ExportSnapshot::capture (processor), options, loud), juce::String());
                expect (loud.existsAsFile());
                expect (loadIRFile (loud).source->audio.getMagnitude (0, 100) > 1.0f); // float keeps the overs
            }

            beginTest ("Export panel: runs on a background thread and reports the result");
            {
                GlobalSettings::Shared()->set ({});
                TempFolder temp;

                IReneProcessor processor;
                processor.prepareToPlay (48000.0, 256);
                processor.getSlots().setSource (0, makeSource (makeCabIR (1, 2000, 8, 3), 48000.0), true, false);

                std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
                auto* irene = dynamic_cast<IReneEditor*> (editor.get());
                expect (irene != nullptr);

                if (irene == nullptr)
                    return;

                auto* panel = irene->getExportPanelForTests();
                panel->setVisible (true);
                pump();
                editor->createComponentSnapshot (editor->getLocalBounds());

                const auto options = panel->readOptions();
                expect (options.includeInputOutputFilters && ! options.includeOutputGain); // the specified defaults
                expect (! options.stereo && options.autoLength && ! options.normalisePeak); // mono by default
                expectEquals (options.bitDepth, 24);
                expectEquals (options.sampleRate, 0.0);

                const auto file = temp.folder.getChildFile ("from panel.wav");
                panel->startExport (file);
                expect (panel->isExporting());

                for (int i = 0; i < 400 && panel->isExporting(); ++i)
                    pump (25);

                expect (! panel->isExporting());
                expect (file.existsAsFile());
                expect (loadIRFile (file).source != nullptr);

                editor->createComponentSnapshot (editor->getLocalBounds());
                editor.reset();
            }
        }
    };

    ExportTests exportTests;
}
