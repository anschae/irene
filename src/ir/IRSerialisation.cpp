#include "IRSerialisation.h"

#include <juce_core/juce_core.h>

#if ! JUCE_LITTLE_ENDIAN
 #error "IR serialisation assumes a little-endian platform"
#endif

namespace irene
{
    namespace
    {
        constexpr int kMaxChannels = 8;
        constexpr int kMaxSamples = 16 * 1024 * 1024;
    }

    juce::MemoryBlock encodeAudio (const juce::AudioBuffer<float>& audio)
    {
        const auto numChannels = audio.getNumChannels();
        const auto numSamples = audio.getNumSamples();
        const auto numValues = static_cast<size_t> (numChannels) * static_cast<size_t> (numSamples);

        juce::MemoryBlock planes (numValues * sizeof (float));
        auto* dest = static_cast<juce::uint8*> (planes.getData());

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto* src = reinterpret_cast<const juce::uint8*> (audio.getReadPointer (ch));
            const auto offset = static_cast<size_t> (ch) * static_cast<size_t> (numSamples);

            for (size_t i = 0; i < static_cast<size_t> (numSamples); ++i)
                for (size_t b = 0; b < sizeof (float); ++b)
                    dest[b * numValues + offset + i] = src[i * sizeof (float) + b];
        }

        juce::MemoryBlock result;

        {
            juce::MemoryOutputStream out (result, false);
            juce::GZIPCompressorOutputStream zip (out, 6);
            zip.write (planes.getData(), planes.getSize());
            zip.flush();
        }

        return result;
    }

    bool decodeAudio (const juce::MemoryBlock& data, int numChannels, int numSamples, juce::AudioBuffer<float>& result)
    {
        if (numChannels < 1 || numChannels > kMaxChannels || numSamples < 1 || numSamples > kMaxSamples)
            return false;

        const auto numValues = static_cast<size_t> (numChannels) * static_cast<size_t> (numSamples);
        juce::MemoryBlock planes (numValues * sizeof (float));

        {
            juce::MemoryInputStream in (data, false);
            juce::GZIPDecompressorInputStream unzip (in);

            if (unzip.read (planes.getData(), static_cast<int> (planes.getSize())) != static_cast<int> (planes.getSize()))
                return false;

            char extra = 0;

            if (unzip.read (&extra, 1) != 0) // more data than announced
                return false;
        }

        result.setSize (numChannels, numSamples);
        const auto* src = static_cast<const juce::uint8*> (planes.getData());

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* dest = reinterpret_cast<juce::uint8*> (result.getWritePointer (ch));
            const auto offset = static_cast<size_t> (ch) * static_cast<size_t> (numSamples);

            for (size_t i = 0; i < static_cast<size_t> (numSamples); ++i)
                for (size_t b = 0; b < sizeof (float); ++b)
                    dest[i * sizeof (float) + b] = src[b * numValues + offset + i];
        }

        return true;
    }
}
