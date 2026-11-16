#include "IRSource.h"

#include "IRProcessing.h"

namespace irene
{
    namespace
    {
        constexpr juce::int64 kMaxFileBytes = 1024ll * 1024ll * 1024ll;

        IRLoadResult fail (const juce::String& message)
        {
            return { nullptr, message };
        }
    }

    //==============================================================================
    IRSource::Ptr IRSource::create (const juce::AudioBuffer<float>& input, double rate, IRReference ref)
    {
        if (input.getNumChannels() < 1 || input.getNumSamples() < 1 || rate <= 0.0)
            return nullptr;

        const auto maxSamples = static_cast<int> (kMaxLengthSeconds * rate);
        const auto numChannels = juce::jmin (kMaxChannels, input.getNumChannels());
        const auto numSamples = juce::jmin (maxSamples, input.getNumSamples());

        std::shared_ptr<IRSource> source (new IRSource());

        source->audio.setSize (numChannels, numSamples);

        for (int ch = 0; ch < numChannels; ++ch)
            source->audio.copyFrom (ch, 0, input, ch, 0, numSamples);

        source->sampleRate = rate;
        source->reference = std::move (ref);
        source->fileNumChannels = input.getNumChannels();
        source->wasTruncated = input.getNumSamples() > maxSamples;
        source->normalisationGainDb = irproc::computeNormalisationGainDb (source->audio, rate);
        source->leadingSilenceSamples = irproc::findLeadingSilence (source->audio);
        return source;
    }

    juce::String IRSource::getNotice() const
    {
        juce::StringArray notices;

        if (fileNumChannels > kMaxChannels)
            notices.add (juce::String (fileNumChannels) + " channels: using the first two");

        if (wasTruncated)
            notices.add ("longer than " + juce::String (juce::roundToInt (kMaxLengthSeconds)) + " s: truncated");

        return notices.joinIntoString ("; ");
    }

    //==============================================================================
    juce::String hashData (const void* data, size_t size)
    {
        // 64-bit FNV-1a. Not cryptographic; it only has to tell IR files apart.
        // The size is appended to make accidental collisions even less likely.
        const auto* bytes = static_cast<const juce::uint8*> (data);
        juce::uint64 h = 0xcbf29ce484222325ull;

        for (size_t i = 0; i < size; ++i)
        {
            h ^= bytes[i];
            h *= 0x100000001b3ull;
        }

        return juce::String::toHexString (static_cast<juce::int64> (h)).paddedLeft ('0', 16)
               + "-" + juce::String::toHexString (static_cast<juce::int64> (size));
    }

    juce::String hashFile (const juce::File& file)
    {
        juce::MemoryBlock data;

        if (file.getSize() > kMaxFileBytes || ! file.loadFileAsData (data))
            return {};

        return hashData (data.getData(), data.getSize());
    }

    bool isSupportedIRFile (const juce::File& file)
    {
        return file.hasFileExtension ("wav;wave;aif;aiff");
    }

    IRLoadResult loadIRFile (const juce::File& file)
    {
        if (! file.existsAsFile())
            return fail ("File not found: " + file.getFullPathName());

        if (! isSupportedIRFile (file))
            return fail ("Unsupported file type (use WAV or AIFF): " + file.getFileName());

        if (file.getSize() > kMaxFileBytes)
            return fail ("File is too large: " + file.getFileName());

        juce::MemoryBlock data;

        if (! file.loadFileAsData (data) || data.getSize() == 0)
            return fail ("Could not read " + file.getFileName());

        std::unique_ptr<juce::AudioFormatReader> reader;

        {
            juce::WavAudioFormat wav;
            juce::AiffAudioFormat aiff;

            // Try the format matching the extension first, then the other one.
            std::array<juce::AudioFormat*, 2> formats { &wav, &aiff };

            if (file.hasFileExtension ("aif;aiff"))
                std::swap (formats[0], formats[1]);

            for (auto* format : formats)
            {
                reader.reset (format->createReaderFor (new juce::MemoryInputStream (data, false), true));

                if (reader != nullptr)
                    break;
            }
        }

        if (reader == nullptr || reader->lengthInSamples <= 0 || reader->numChannels == 0 || reader->sampleRate <= 0.0)
            return fail ("Not a valid WAV or AIFF file: " + file.getFileName());

        // Read one sample more than the limit so that create() can tell that the
        // file was longer, without decoding the whole of a very long file.
        const auto maxSamples = static_cast<juce::int64> (IRSource::kMaxLengthSeconds * reader->sampleRate) + 1;
        const auto numSamples = static_cast<int> (juce::jmin (maxSamples, reader->lengthInSamples));
        const auto numChannels = static_cast<int> (reader->numChannels);

        juce::AudioBuffer<float> audio (numChannels, numSamples);

        if (! reader->read (&audio, 0, numSamples, 0, true, true))
            return fail ("Could not decode " + file.getFileName());

        IRReference ref { file.getFullPathName(), file.getFileName(), hashData (data.getData(), data.getSize()) };

        if (auto source = IRSource::create (audio, reader->sampleRate, std::move (ref)))
            return { source, {} };

        return fail ("Empty audio file: " + file.getFileName());
    }
}
