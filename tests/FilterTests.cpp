#include "TestHelpers.h"

#include "dsp/FilterBlock.h"
#include "dsp/FractionalDelay.h"

namespace
{
    using namespace irene;
    using namespace irene::test;

    constexpr int kImpulseLength = 1 << 16;

    /** Impulse response of a filter block with settled settings. */
    juce::AudioBuffer<float> impulseResponse (const FilterBlockSettings& settings, double sampleRate, int blockSize = 512)
    {
        FilterBlock filter;
        filter.prepare (sampleRate);

        juce::AudioBuffer<float> buffer (2, kImpulseLength);
        buffer.clear();
        buffer.setSample (0, 0, 1.0f);
        buffer.setSample (1, 0, 1.0f);

        for (int pos = 0; pos < kImpulseLength; pos += blockSize)
        {
            const auto n = std::min (blockSize, kImpulseLength - pos);
            float* channels[2] { buffer.getWritePointer (0, pos), buffer.getWritePointer (1, pos) };
            filter.setSettings (settings);
            filter.process (channels, 2, n);
        }

        return buffer;
    }

    class FilterTests final : public juce::UnitTest
    {
    public:
        FilterTests() : juce::UnitTest ("Filter block", "IRene") {}

        /** Compares the measured response of the DSP with the analytic one. */
        void expectMatchesAnalytic (const FilterBlockSettings& settings, double sampleRate, double toleranceDb = 0.02)
        {
            const auto ir = impulseResponse (settings, sampleRate);

            for (const auto freq : { 30.0, 55.0, 100.0, 180.0, 440.0, 1000.0, 2500.0, 5000.0, 9000.0, 15000.0, 19000.0 })
            {
                const auto measured = dtft (ir.getReadPointer (0), kImpulseLength, freq, sampleRate);
                const auto analytic = FilterBlock::response (settings, freq, sampleRate);

                // Compare as complex values (magnitude and phase), relative to the analytic magnitude,
                // wherever the response isn't deep in a stop band.
                if (std::abs (analytic) > 1.0e-3)
                {
                    const auto relativeError = std::abs (measured - analytic) / std::abs (analytic);
                    expect (relativeError < std::pow (10.0, toleranceDb / 20.0) - 1.0,
                            "Response mismatch at " + juce::String (freq) + " Hz, fs " + juce::String (sampleRate)
                                + ": measured " + juce::String (toDb (std::abs (measured)), 3)
                                + " dB, analytic " + juce::String (toDb (std::abs (analytic)), 3) + " dB");
                }
            }
        }

        void runTest() override
        {
            beginTest ("Default settings are transparent and bit-exact");
            {
                FilterBlock filter;
                filter.prepare (48000.0);

                auto buffer = makeNoise (2, 1000, 1);
                juce::AudioBuffer<float> reference;
                reference.makeCopyOf (buffer);

                float* channels[2] { buffer.getWritePointer (0), buffer.getWritePointer (1) };
                filter.setSettings ({});
                filter.process (channels, 2, 1000);

                expect (filter.isIdle());

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 1000; ++i)
                        expectEquals (buffer.getSample (ch, i), reference.getSample (ch, i));

                expectWithinAbsoluteError (std::abs (FilterBlock::response ({}, 1000.0, 48000.0)), 1.0, 1.0e-12);
            }

            beginTest ("High-pass: -3 dB at the cutoff, correct slopes");
            {
                for (const auto fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
                {
                    FilterBlockSettings s;
                    s.hpOn = true;
                    s.hpFreq = 200.0f;

                    auto ir = impulseResponse (s, fs);
                    expectWithinAbsoluteError (toDb (std::abs (dtft (ir.getReadPointer (0), kImpulseLength, 200.0, fs))), -3.01, 0.05);
                    // 12 dB/oct: two octaves below the cutoff is about -24 dB
                    expectWithinAbsoluteError (toDb (std::abs (dtft (ir.getReadPointer (0), kImpulseLength, 50.0, fs))), -24.1, 0.3);

                    s.hpSteep = true;
                    ir = impulseResponse (s, fs);
                    expectWithinAbsoluteError (toDb (std::abs (dtft (ir.getReadPointer (0), kImpulseLength, 200.0, fs))), -3.01, 0.05);
                    // 24 dB/oct: about -48 dB
                    expectWithinAbsoluteError (toDb (std::abs (dtft (ir.getReadPointer (0), kImpulseLength, 50.0, fs))), -48.2, 0.5);
                    // pass band is flat
                    expectWithinAbsoluteError (toDb (std::abs (dtft (ir.getReadPointer (0), kImpulseLength, 5000.0, fs))), 0.0, 0.01);
                }
            }

            beginTest ("Low-pass: -3 dB at the cutoff, correct slopes");
            {
                for (const auto fs : { 44100.0, 96000.0 })
                {
                    FilterBlockSettings s;
                    s.lpOn = true;
                    s.lpFreq = 2000.0f;

                    auto ir = impulseResponse (s, fs);
                    expectWithinAbsoluteError (toDb (std::abs (dtft (ir.getReadPointer (0), kImpulseLength, 2000.0, fs))), -3.01, 0.05);
                    expectWithinAbsoluteError (toDb (std::abs (dtft (ir.getReadPointer (0), kImpulseLength, 100.0, fs))), 0.0, 0.01);

                    s.lpSteep = true;
                    ir = impulseResponse (s, fs);
                    expectWithinAbsoluteError (toDb (std::abs (dtft (ir.getReadPointer (0), kImpulseLength, 2000.0, fs))), -3.01, 0.05);

                    // The slope is steeper than the analogue 24 dB/oct near Nyquist, so only
                    // require "at least".
                    expect (toDb (std::abs (dtft (ir.getReadPointer (0), kImpulseLength, 8000.0, fs))) < -47.0);
                }
            }

            beginTest ("EQ bands: gain at the centre, shelves reach their plateau");
            {
                const auto fs = 48000.0;

                for (const auto gainDb : { -18.0f, -6.0f, 4.5f, 18.0f })
                {
                    FilterBlockSettings s;
                    s.bands[1] = { true, 1000.0f, gainDb, 2.0f, false };
                    auto ir = impulseResponse (s, fs);
                    expectWithinAbsoluteError (toDb (std::abs (dtft (ir.getReadPointer (0), kImpulseLength, 1000.0, fs))),
                                               static_cast<double> (gainDb), 0.02);
                    expectWithinAbsoluteError (toDb (std::abs (dtft (ir.getReadPointer (0), kImpulseLength, 30.0, fs))), 0.0, 0.05);

                    FilterBlockSettings low;
                    low.bands[0] = { true, 200.0f, gainDb, kDefaultQ, true };
                    ir = impulseResponse (low, fs);
                    expectWithinAbsoluteError (toDb (std::abs (dtft (ir.getReadPointer (0), kImpulseLength, 10.0, fs))),
                                               static_cast<double> (gainDb), 0.1);
                    expectWithinAbsoluteError (toDb (std::abs (dtft (ir.getReadPointer (0), kImpulseLength, 200.0, fs))),
                                               static_cast<double> (gainDb) * 0.5, 0.05);
                    expectWithinAbsoluteError (toDb (std::abs (dtft (ir.getReadPointer (0), kImpulseLength, 10000.0, fs))), 0.0, 0.05);

                    FilterBlockSettings high;
                    high.bands[2] = { true, 3000.0f, gainDb, kDefaultQ, true };
                    ir = impulseResponse (high, fs);
                    expectWithinAbsoluteError (toDb (std::abs (dtft (ir.getReadPointer (0), kImpulseLength, 23000.0, fs))),
                                               static_cast<double> (gainDb), 0.1);
                    expectWithinAbsoluteError (toDb (std::abs (dtft (ir.getReadPointer (0), kImpulseLength, 30.0, fs))), 0.0, 0.05);
                }

                // A band that is off, or the Mid band set to "shelf", must not change anything
                FilterBlockSettings off;
                off.bands[1] = { false, 1000.0f, 12.0f, 1.0f, true };
                expectWithinAbsoluteError (std::abs (FilterBlock::response (off, 1000.0, fs)), 1.0, 1.0e-12);
            }

            beginTest ("DSP matches the analytic response (all stages, several sample rates)");
            {
                FilterBlockSettings s;
                s.hpOn = true;
                s.hpFreq = 70.0f;
                s.hpSteep = true;
                s.bands[0] = { true, 120.0f, 5.0f, 0.9f, true };
                s.bands[1] = { true, 800.0f, -7.5f, 3.0f, false };
                s.bands[2] = { true, 4200.0f, 6.0f, 1.4f, false };
                s.lpOn = true;
                s.lpFreq = 9000.0f;

                for (const auto fs : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
                    expectMatchesAnalytic (s, fs);

                s.hpSteep = false;
                s.lpSteep = true;
                s.bands[0].shelf = false;
                s.bands[2].shelf = true;
                expectMatchesAnalytic (s, 48000.0);

                // Extreme corner frequencies stay stable, also at 44.1 kHz
                s.lpFreq = 20000.0f;
                s.hpFreq = 20.0f;
                s.bands[2] = { true, 20000.0f, 18.0f, 10.0f, false };
                expectMatchesAnalytic (s, 44100.0, 0.05);
            }

            beginTest ("Block bypass is transparent");
            {
                FilterBlockSettings s;
                s.bypass = true;
                s.hpOn = true;
                s.hpFreq = 500.0f;
                s.bands[1] = { true, 1000.0f, 12.0f, 1.0f, false };

                const auto ir = impulseResponse (s, 48000.0);
                expectEquals (ir.getSample (0, 0), 1.0f);
                expectEquals (ir.getMagnitude (0, 1, kImpulseLength - 1), 0.0f);
                expectWithinAbsoluteError (std::abs (FilterBlock::response (s, 100.0, 48000.0)), 1.0, 1.0e-12);
            }

            beginTest ("Parameter changes are click-free");
            {
                // A low sine makes any discontinuity stand out: its own largest
                // sample-to-sample step is tiny.
                const auto fs = 48000.0;
                const auto numSamples = 48000;
                const auto sineStep = 0.5f * static_cast<float> (2.0 * std::numbers::pi * 100.0 / fs);

                const auto runScenario = [&] (const std::vector<FilterBlockSettings>& sequence, int blockSize)
                {
                    FilterBlock filter;
                    filter.prepare (fs);

                    juce::AudioBuffer<float> buffer (2, numSamples);

                    for (int i = 0; i < numSamples; ++i)
                    {
                        const auto v = 0.5f * static_cast<float> (std::sin (2.0 * std::numbers::pi * 100.0 * i / fs));
                        buffer.setSample (0, i, v);
                        buffer.setSample (1, i, v);
                    }

                    const auto segment = numSamples / static_cast<int> (sequence.size());

                    for (int pos = 0; pos < numSamples; pos += blockSize)
                    {
                        const auto n = std::min (blockSize, numSamples - pos);
                        const auto index = std::min (static_cast<int> (sequence.size()) - 1, pos / segment);
                        float* channels[2] { buffer.getWritePointer (0, pos), buffer.getWritePointer (1, pos) };
                        filter.setSettings (sequence[static_cast<size_t> (index)]);
                        filter.process (channels, 2, n);
                    }

                    return maxStep (buffer.getReadPointer (0), numSamples);
                };

                FilterBlockSettings neutral;

                FilterBlockSettings everythingOn;
                everythingOn.hpOn = true;
                everythingOn.hpFreq = 150.0f;
                everythingOn.bands[0] = { true, 100.0f, 12.0f, 1.0f, false };
                everythingOn.bands[1] = { true, 300.0f, -12.0f, 2.0f, false };
                everythingOn.lpOn = true;
                everythingOn.lpFreq = 400.0f;

                auto steepAndShelf = everythingOn;
                steepAndShelf.hpSteep = true;
                steepAndShelf.lpSteep = true;
                steepAndShelf.bands[0].shelf = true;
                steepAndShelf.hpFreq = 60.0f;
                steepAndShelf.bands[1].freq = 90.0f;

                auto bypassed = steepAndShelf;
                bypassed.bypass = true;

                for (const auto blockSize : { 1, 17, 64, 480 })
                {
                    const auto worst = runScenario ({ neutral, everythingOn, steepAndShelf, bypassed, everythingOn, neutral }, blockSize);

                    // Allow a few times the sine's own step (filters with boost steepen it),
                    // but nothing like the size of a real click (which would be ~0.1 or more).
                    expect (worst < 8.0f * sineStep,
                            "Discontinuity of " + juce::String (worst, 5) + " with block size " + juce::String (blockSize));
                }
            }

            beginTest ("Fractional delay: exact for whole samples, accurate between");
            {
                const auto fs = 48000.0;

                // Whole-sample delays are bit-exact
                for (const auto delaySamples : { 0, 1, 2, 7, 48, 480 })
                {
                    FractionalDelay delay;
                    delay.prepare (fs, 0.010);
                    delay.setDelaySamples (delaySamples);

                    auto buffer = makeNoise (2, 2000, 5);
                    juce::AudioBuffer<float> reference;
                    reference.makeCopyOf (buffer);

                    float* channels[2] { buffer.getWritePointer (0), buffer.getWritePointer (1) };
                    delay.process (channels, 2, 2000);

                    for (int i = 0; i < 2000; ++i)
                        expectEquals (buffer.getSample (0, i), i >= delaySamples ? reference.getSample (0, i - delaySamples) : 0.0f);
                }

                // Fractional delays: the measured response equals the analytic one, and the
                // delay is right in the range that matters for mic alignment.
                for (const auto delaySamples : { 0.25, 0.5, 1.5, 3.75, 100.3, 479.9 })
                {
                    FractionalDelay delay;
                    delay.prepare (fs, 0.010);
                    delay.setDelaySamples (delaySamples);

                    juce::AudioBuffer<float> buffer (2, 1024);
                    buffer.clear();
                    buffer.setSample (0, 0, 1.0f);
                    float* channels[2] { buffer.getWritePointer (0), buffer.getWritePointer (1) };
                    delay.process (channels, 2, 1024);

                    for (const auto freq : { 100.0, 1000.0, 5000.0 })
                    {
                        const auto omega = 2.0 * std::numbers::pi * freq / fs;
                        const auto measured = dtft (buffer.getReadPointer (0), 1024, freq, fs);
                        const auto analytic = FractionalDelay::response (delaySamples, omega);
                        const auto ideal = std::polar (1.0, -omega * delaySamples);

                        expect (std::abs (measured - analytic) < 1.0e-6);
                        expect (std::abs (measured - ideal) < 0.02, "Delay error at " + juce::String (freq) + " Hz");
                    }
                }

                // Changing the delay glides without clicks
                {
                    FractionalDelay delay;
                    delay.prepare (fs, 0.010);

                    juce::AudioBuffer<float> buffer (2, 24000);
                    const auto sineStep = 0.5f * static_cast<float> (2.0 * std::numbers::pi * 200.0 / fs);

                    for (int i = 0; i < 24000; ++i)
                        buffer.setSample (0, i, 0.5f * static_cast<float> (std::sin (2.0 * std::numbers::pi * 200.0 * i / fs)));

                    const std::array<double, 4> delays { 0.0, 200.5, 13.2, 480.0 };

                    for (int pos = 0; pos < 24000; pos += 100)
                    {
                        float* channels[2] { buffer.getWritePointer (0, pos), buffer.getWritePointer (1, pos) };
                        delay.setDelaySamples (delays[static_cast<size_t> (pos / 6000)]);
                        delay.process (channels, 2, 100);
                    }

                    expect (maxStep (buffer.getReadPointer (0), 24000) < 2.0f * sineStep);
                }
            }
        }
    };

    FilterTests filterTests;
}
