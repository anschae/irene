#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "IRSource.h"

// Offline IR processing. Everything here allocates and may take a while:
// never call it from the audio thread.

namespace irene::irproc
{
    /** High-quality (windowed-sinc) sample-rate conversion.

        The result is scaled so that the IR keeps its frequency response as a
        filter: a 96 kHz IR played in a 48 kHz session sounds the same. */
    juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& source, double sourceRate, double targetRate);

    /** Loudness-normalisation gain in dB.

        Brings the pink-noise-weighted (equal weight per octave) mean energy of
        the IR's frequency response between 80 Hz and 8 kHz to 0 dB, i.e. the
        normalised IR passes band-limited pink noise at unity level. */
    float computeNormalisationGainDb (const juce::AudioBuffer<float>& audio, double sampleRate);

    /** Number of leading samples that are silent (below -60 dB relative to the
        peak), leaving a short pre-roll. */
    int findLeadingSilence (const juce::AudioBuffer<float>& audio);

    /** Number of samples kept for a given length setting. */
    int trimmedLength (int numSamples, float lengthPercent, double sampleRate);

    /** Shortens the IR to trimmedLength() and fades out the end. A setting of
        100 % leaves the buffer untouched. */
    void applyLength (juce::AudioBuffer<float>& audio, float lengthPercent, double sampleRate);

    //==============================================================================
    struct PrepareOptions
    {
        double targetRate = 48000.0;
        float lengthPercent = 100.0f;
        bool normalise = true;
        bool autoTrim = false;

        bool operator== (const PrepareOptions&) const = default;
    };

    /** Step 1 (the expensive one): optional leading-silence trim, then
        resampling to the target rate. Depends only on targetRate and autoTrim. */
    juce::AudioBuffer<float> resampleSource (const IRSource& source, double targetRate, bool autoTrim);

    /** Step 2 (cheap): length trim with fade-out and normalisation gain,
        applied to the output of resampleSource(). Returns the IR as it is
        handed to the convolution engine (at least 2 samples long). */
    juce::AudioBuffer<float> finishPrepared (const juce::AudioBuffer<float>& resampled, const IRSource& source, const PrepareOptions&);

    /** Both steps. */
    juce::AudioBuffer<float> prepare (const IRSource& source, const PrepareOptions&);
}
