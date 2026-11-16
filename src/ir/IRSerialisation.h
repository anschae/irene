#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace irene
{
    /** Lossless, compressed storage of IR audio inside plugin state and
        portable presets.

        Format: the raw 32-bit float samples (channel after channel), stored as
        four byte planes (all first bytes, then all second bytes, ...) and
        zlib-compressed. Splitting floats into byte planes makes them compress
        noticeably better and is exactly reversible. */
    juce::MemoryBlock encodeAudio (const juce::AudioBuffer<float>& audio);

    /** Returns false if the data doesn't decode to exactly the given size. */
    bool decodeAudio (const juce::MemoryBlock& data, int numChannels, int numSamples, juce::AudioBuffer<float>& result);
}
