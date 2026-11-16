#include "TestHelpers.h"

#include "ir/IRProcessing.h"
#include "ir/IRSerialisation.h"

namespace
{
    using namespace irene;
    using namespace irene::test;

    /** Response of an IR *as a filter* at one frequency, in dB. */
    double responseDb (const juce::AudioBuffer<float>& ir, double freq, double sampleRate, int channel = 0)
    {
        return toDb (std::abs (dtft (ir.getReadPointer (channel), ir.getNumSamples(), freq, sampleRate)));
    }

    class IRProcessingTests final : public juce::UnitTest
    {
    public:
        IRProcessingTests() : juce::UnitTest ("IR processing", "IRene") {}

        void runTest() override
        {
            beginTest ("Normalisation: known gains");
            {
                // A unit impulse passes everything at 0 dB: nothing to correct
                expectWithinAbsoluteError (irproc::computeNormalisationGainDb (makeDelta (1, 64), 48000.0), 0.0f, 0.01f);

                // Half the amplitude needs +6.02 dB, at any sample rate and position
                for (const auto fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
                    expectWithinAbsoluteError (irproc::computeNormalisationGainDb (makeDelta (1, 500, 37, 0.5f), fs), 6.02f, 0.02f);

                // Silence is left alone
                juce::AudioBuffer<float> silence (1, 100);
                silence.clear();
                expectEquals (irproc::computeNormalisationGainDb (silence, 48000.0), 0.0f);
            }

            beginTest ("Normalisation: differently scaled IRs end up equally loud");
            {
                const auto fs = 48000.0;
                const auto ir = makeTestIR (2, 9600, 11);

                juce::AudioBuffer<float> quiet, loud;
                quiet.makeCopyOf (ir);
                quiet.applyGain (0.05f);
                loud.makeCopyOf (ir);
                loud.applyGain (7.0f);

                const auto gainRef = irproc::computeNormalisationGainDb (ir, fs);
                const auto gainQuiet = irproc::computeNormalisationGainDb (quiet, fs);
                const auto gainLoud = irproc::computeNormalisationGainDb (loud, fs);

                expectWithinAbsoluteError (gainQuiet - gainRef, 26.02f, 0.01f);
                expectWithinAbsoluteError (gainLoud - gainRef, -16.90f, 0.01f);

                // After normalisation, band-limited pink noise passes at unity:
                // check via the definition, the pink-weighted mean energy in 80 Hz - 8 kHz.
                juce::AudioBuffer<float> normalised;
                normalised.makeCopyOf (ir);
                normalised.applyGain (juce::Decibels::decibelsToGain (gainRef));
                expectWithinAbsoluteError (irproc::computeNormalisationGainDb (normalised, fs), 0.0f, 0.01f);
            }

            beginTest ("Normalisation: only the 80 Hz - 8 kHz band counts");
            {
                const auto fs = 48000.0;
                const auto n = 4096;

                // IR = delta + a strong 15 kHz burst: the burst is outside the band
                auto ir = makeDelta (1, n);

                for (int i = 0; i < n; ++i)
                    ir.addSample (0, i, 0.05f * static_cast<float> (std::sin (2.0 * std::numbers::pi * 15000.0 * i / fs)
                                                                 * std::exp (-static_cast<double> (i) / 800.0)));

                expectWithinAbsoluteError (irproc::computeNormalisationGainDb (ir, fs), 0.0f, 0.3f);

                // ...while a 1 kHz resonance inside the band clearly lowers the gain
                auto resonant = makeDelta (1, n);

                for (int i = 0; i < n; ++i)
                    resonant.addSample (0, i, 0.05f * static_cast<float> (std::sin (2.0 * std::numbers::pi * 1000.0 * i / fs)
                                                                       * std::exp (-static_cast<double> (i) / 800.0)));

                expect (irproc::computeNormalisationGainDb (resonant, fs) < -1.0f);
            }

            beginTest ("Resampling keeps the frequency response");
            {
                // The IR starts after 300 samples of silence. Band-limited resampling rings
                // slightly *before* a sharp onset; an IR that starts at sample 0 has no room
                // for that pre-ringing, so it is cut off (as in every resampler). Real cabinet
                // IRs have little energy near Nyquist, so this is inaudible, but this test
                // uses white noise and wants to measure the resampler itself.
                const auto ir = makeTestIR (2, 4800, 21, 300);

                struct Case { double from, to; };

                for (const auto c : { Case { 48000.0, 44100.0 }, Case { 44100.0, 48000.0 }, Case { 96000.0, 44100.0 },
                                      Case { 44100.0, 96000.0 }, Case { 48000.0, 192000.0 }, Case { 192000.0, 48000.0 } })
                {
                    const auto resampled = irproc::resample (ir, c.from, c.to);

                    expectEquals (resampled.getNumChannels(), 2);
                    expectWithinAbsoluteError (resampled.getNumSamples() / c.to, ir.getNumSamples() / c.from, 2.0 / c.to);

                    // Compare well inside the pass band of both rates
                    const auto limit = 0.4 * std::min (c.from, c.to);

                    for (const auto freq : { 60.0, 250.0, 1000.0, 3000.0, 7000.0, 12000.0, 17000.0 })
                    {
                        if (freq > limit)
                            continue;

                        for (int ch = 0; ch < 2; ++ch)
                        {
                            const auto before = dtft (ir.getReadPointer (ch), ir.getNumSamples(), freq, c.from);
                            const auto after = dtft (resampled.getReadPointer (ch), resampled.getNumSamples(), freq, c.to);

                            // complex comparison: magnitude and phase (i.e. timing) must both survive
                            expect (std::abs (after - before) < 0.003 * std::max (0.05, std::abs (before)),
                                    "Resampling " + juce::String (c.from) + " -> " + juce::String (c.to)
                                        + " changed the response at " + juce::String (freq) + " Hz: "
                                        + juce::String (toDb (std::abs (before)), 3) + " dB -> " + juce::String (toDb (std::abs (after)), 3) + " dB");
                        }
                    }
                }
            }

            beginTest ("Resampling: same rate is a bit-exact copy, downsampling removes aliasing");
            {
                const auto ir = makeTestIR (1, 1000, 3);
                const auto same = irproc::resample (ir, 48000.0, 48000.0);
                expectEquals (same.getNumSamples(), 1000);

                for (int i = 0; i < 1000; ++i)
                    expectEquals (same.getSample (0, i), ir.getSample (0, i));

                // A 30 kHz tone at 96 kHz must not fold back into a 44.1 kHz IR
                juce::AudioBuffer<float> tone (1, 9600);

                for (int i = 0; i < 9600; ++i)
                    tone.setSample (0, i, static_cast<float> (std::sin (2.0 * std::numbers::pi * 30000.0 * i / 96000.0)));

                const auto down = irproc::resample (tone, 96000.0, 44100.0);
                // The filter-preserving scale factor is 96/44.1 (+6.8 dB); the alias must be far below that
                expect (juce::Decibels::gainToDecibels (down.getMagnitude (200, down.getNumSamples() - 400)) < -80.0f);
            }

            beginTest ("Length trim and fade-out");
            {
                const auto fs = 48000.0;
                auto ir = makeTestIR (2, 9600, 4);

                juce::AudioBuffer<float> full;
                full.makeCopyOf (ir);
                irproc::applyLength (full, 100.0f, fs);
                expectEquals (full.getNumSamples(), 9600);
                expectEquals (full.getSample (0, 9599), ir.getSample (0, 9599)); // untouched

                juce::AudioBuffer<float> half;
                half.makeCopyOf (ir);
                irproc::applyLength (half, 50.0f, fs);
                expectEquals (half.getNumSamples(), 4800);
                expectEquals (half.getSample (0, 100), ir.getSample (0, 100)); // start untouched
                expectWithinAbsoluteError (half.getSample (0, 4799), 0.0f, 1.0e-6f); // faded to zero
                expectEquals (half.getSample (1, 4800 - 241), ir.getSample (1, 4800 - 241)); // fade is 5 ms = 240 samples

                juce::AudioBuffer<float> tiny;
                tiny.makeCopyOf (ir);
                irproc::applyLength (tiny, 0.0f, fs);
                expectEquals (tiny.getNumSamples(), 48); // never shorter than 1 ms
            }

            beginTest ("Leading silence detection");
            {
                auto ir = makeTestIR (2, 2000, 8, 300);
                expectEquals (irproc::findLeadingSilence (ir), 298); // 2 samples of pre-roll are kept

                expectEquals (irproc::findLeadingSilence (makeDelta (1, 10)), 0);

                juce::AudioBuffer<float> silence (1, 10);
                silence.clear();
                expectEquals (irproc::findLeadingSilence (silence), 0);
            }

            beginTest ("IRSource: channel and length limits, analysis");
            {
                // 4 channels, 11 seconds at 8 kHz
                auto big = makeNoise (4, 88000, 9, 0.1f);
                const auto source = IRSource::create (big, 8000.0, { "/x/big.wav", "big.wav", "abc" });

                expect (source != nullptr);
                expectEquals (source->audio.getNumChannels(), 2);
                expectEquals (source->audio.getNumSamples(), 80000);
                expect (source->wasTruncated);
                expectEquals (source->fileNumChannels, 4);
                expect (source->getNotice().contains ("4 channels"));
                expect (source->getNotice().contains ("truncated"));
                expectEquals (source->audio.getSample (1, 500), big.getSample (1, 500));

                const auto small = makeSource (makeDelta (1, 16, 0, 0.25f), 44100.0);
                expect (small->getNotice().isEmpty());
                expectWithinAbsoluteError (small->normalisationGainDb, 12.04f, 0.02f);

                expect (IRSource::create (juce::AudioBuffer<float>(), 48000.0, {}) == nullptr);
            }

            beginTest ("Prepare: resampled from the original, options applied");
            {
                const auto source = makeSource (makeTestIR (2, 4800, 31, 100), 48000.0);

                // Same rate, no options: the engine gets the original samples
                auto prepared = irproc::prepare (*source, { 48000.0, 100.0f, false, false });
                expectEquals (prepared.getNumSamples(), 4800);

                for (int i = 0; i < 4800; i += 7)
                    expectEquals (prepared.getSample (1, i), source->audio.getSample (1, i));

                // Normalisation only scales
                auto normalised = irproc::prepare (*source, { 48000.0, 100.0f, true, false });
                const auto gain = juce::Decibels::decibelsToGain (source->normalisationGainDb);
                expectWithinAbsoluteError (normalised.getSample (0, 150), source->audio.getSample (0, 150) * gain, 1.0e-6f);

                // Auto-trim removes the 100 samples of pre-delay (minus the pre-roll)
                auto trimmed = irproc::prepare (*source, { 48000.0, 100.0f, false, true });
                expectEquals (trimmed.getNumSamples(), 4800 - 98);
                expectEquals (trimmed.getSample (0, 2), source->audio.getSample (0, 100));

                // ...and is off by default
                expect (! irproc::PrepareOptions().autoTrim);
                expect (irproc::PrepareOptions().normalise);

                // Length
                auto shortened = irproc::prepare (*source, { 48000.0, 25.0f, false, false });
                expectEquals (shortened.getNumSamples(), 1200);

                // A different rate changes the length accordingly
                auto at441 = irproc::prepare (*source, { 44100.0, 100.0f, false, false });
                expectEquals (at441.getNumSamples(), 4410);
                expectWithinAbsoluteError (responseDb (at441, 2000.0, 44100.0), responseDb (source->audio, 2000.0, 48000.0), 0.02);

                // The engine never gets less than 2 samples
                const auto delta = makeSource (makeDelta (1, 1), 48000.0);
                expectEquals (irproc::prepare (*delta, { 48000.0, 100.0f, false, false }).getNumSamples(), 2);
            }

            beginTest ("Audio serialisation is lossless");
            {
                auto audio = makeTestIR (2, 12345, 77);
                audio.setSample (0, 5, 1.0e-30f);
                audio.setSample (1, 6, -0.0f);
                audio.setSample (0, 7, 3.4e38f);

                const auto encoded = encodeAudio (audio);
                juce::AudioBuffer<float> decoded;
                expect (decodeAudio (encoded, 2, 12345, decoded));

                for (int ch = 0; ch < 2; ++ch)
                    expect (std::memcmp (audio.getReadPointer (ch), decoded.getReadPointer (ch), sizeof (float) * 12345) == 0);

                // Wrong sizes and corrupt data are rejected
                expect (! decodeAudio (encoded, 2, 12344, decoded));
                expect (! decodeAudio (encoded, 1, 12345 * 2 + 1, decoded));
                expect (! decodeAudio (juce::MemoryBlock ("garbage", 7), 1, 10, decoded));
                expect (! decodeAudio (encoded, 0, 0, decoded));
            }

            beginTest ("Content hash");
            {
                const char a[] = "impulse response A";
                const char b[] = "impulse response B";

                expectEquals (hashData (a, sizeof (a)), hashData (a, sizeof (a)));
                expect (hashData (a, sizeof (a)) != hashData (b, sizeof (b)));
                expect (hashData (a, sizeof (a)) != hashData (a, sizeof (a) - 1));

                // The hash follows the content, not the file name or location
                TempFolder temp;
                const auto first = temp.folder.getChildFile ("one.wav");
                const auto second = temp.folder.getChildFile ("sub").getChildFile ("renamed.wav");
                expect (writeWav (first, makeTestIR (1, 500, 1), 48000.0));
                temp.folder.getChildFile ("sub").createDirectory();
                expect (first.copyFileTo (second));

                expect (hashFile (first).isNotEmpty());
                expectEquals (hashFile (first), hashFile (second));
                expect (hashFile (temp.folder.getChildFile ("does-not-exist.wav")).isEmpty());
            }

            beginTest ("File loading: WAV and AIFF, bit depths, errors");
            {
                TempFolder temp;
                const auto ir = makeTestIR (2, 2400, 5);

                for (const auto bits : { 16, 24, 32 })
                {
                    const auto file = temp.folder.getChildFile ("ir" + juce::String (bits) + ".wav");
                    expect (writeWav (file, ir, 96000.0, bits));

                    const auto result = loadIRFile (file);
                    expect (result.source != nullptr, result.error);

                    if (result.source != nullptr)
                    {
                        expectEquals (result.source->sampleRate, 96000.0);
                        expectEquals (result.source->audio.getNumChannels(), 2);
                        expectEquals (result.source->audio.getNumSamples(), 2400);
                        expectEquals (result.source->reference.name, file.getFileName());
                        expectEquals (result.source->reference.path, file.getFullPathName());
                        expectEquals (result.source->reference.hash, hashFile (file));

                        const auto tolerance = bits == 32 ? 0.0f : (bits == 24 ? 1.0e-6f : 1.0e-4f);
                        expectWithinAbsoluteError (result.source->audio.getSample (1, 77), ir.getSample (1, 77), tolerance);
                    }
                }

                // AIFF
                {
                    const auto file = temp.folder.getChildFile ("ir.aiff");
                    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
                    juce::AiffAudioFormat aiff;
                    auto writer = aiff.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (44100.0).withNumChannels (1).withBitsPerSample (24));
                    expect (writer != nullptr);

                    if (writer != nullptr)
                    {
                        const auto monoIR = makeTestIR (1, 2400, 6);
                        writer->writeFromAudioSampleBuffer (monoIR, 0, monoIR.getNumSamples());
                        writer.reset();

                        const auto result = loadIRFile (file);
                        expect (result.source != nullptr, result.error);

                        if (result.source != nullptr)
                        {
                            expectEquals (result.source->sampleRate, 44100.0);
                            expectEquals (result.source->audio.getNumChannels(), 1);
                        }
                    }
                }

                // Errors
                expect (loadIRFile (temp.folder.getChildFile ("missing.wav")).source == nullptr);

                const auto fake = temp.folder.getChildFile ("fake.wav");
                fake.replaceWithText ("this is not audio");
                const auto fakeResult = loadIRFile (fake);
                expect (fakeResult.source == nullptr);
                expect (fakeResult.error.isNotEmpty());

                const auto text = temp.folder.getChildFile ("notes.txt");
                text.replaceWithText ("hello");
                expect (loadIRFile (text).source == nullptr);
            }
        }
    };

    IRProcessingTests irProcessingTests;
}
