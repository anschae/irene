#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

namespace irene
{
    /** How a slot refers to an IR without holding its audio: used for presets,
        the browser, favourites and "missing" slots. */
    struct IRReference
    {
        juce::String path; // last known absolute path
        juce::String name; // file name including extension
        juce::String hash; // content hash of the file

        bool isEmpty() const noexcept { return path.isEmpty() && name.isEmpty() && hash.isEmpty(); }
        bool operator== (const IRReference&) const = default;
    };

    /** An impulse response exactly as loaded: original audio at its original
        sample rate. Immutable and shared; everything a slot plays is derived
        from this (never from an already-resampled copy). */
    class IRSource final
    {
    public:
        using Ptr = std::shared_ptr<const IRSource>;

        static constexpr double kMaxLengthSeconds = 10.0;
        static constexpr int kMaxChannels = 2;

        /** Builds a source from decoded audio. Truncates to 2 channels / 10 s
            and analyses the audio (loudness, leading silence). */
        static Ptr create (const juce::AudioBuffer<float>& audio, double sampleRate, IRReference reference);

        juce::AudioBuffer<float> audio; // 1 or 2 channels
        double sampleRate = 48000.0;
        IRReference reference;

        int fileNumChannels = 1;       // channel count of the file before reduction to 2
        bool wasTruncated = false;     // longer than kMaxLengthSeconds
        float normalisationGainDb = 0; // gain that brings the IR to the reference loudness
        int leadingSilenceSamples = 0; // samples that auto-trim would remove

        bool isStereo() const noexcept { return audio.getNumChannels() > 1; }
        double lengthSeconds() const noexcept { return audio.getNumSamples() / sampleRate; }

        /** Notices for the user (extra channels dropped, truncated), or empty. */
        juce::String getNotice() const;

    private:
        IRSource() = default;
    };

    //==============================================================================
    struct IRLoadResult
    {
        IRSource::Ptr source; // null on failure
        juce::String error;
    };

    /** Reads a WAV or AIFF file. Blocking: call from a background thread. */
    IRLoadResult loadIRFile (const juce::File& file);

    /** Content hash used to recognise an IR file even after it was moved or renamed. */
    juce::String hashData (const void* data, size_t size);
    juce::String hashFile (const juce::File& file);

    bool isSupportedIRFile (const juce::File& file);

    /** Basic facts about an audio file, read from its header only (fast). */
    struct IRFileInfo
    {
        bool valid = false;
        double sampleRate = 0.0;
        int numChannels = 0;
        juce::int64 lengthInSamples = 0;

        double lengthSeconds() const noexcept { return sampleRate > 0.0 ? static_cast<double> (lengthInSamples) / sampleRate : 0.0; }
    };

    IRFileInfo probeIRFile (const juce::File& file);
}
