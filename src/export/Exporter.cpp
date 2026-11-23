#include "Exporter.h"

#include <numbers>

#include "PluginProcessor.h"
#include "dsp/Engine.h"
#include "ir/IRProcessing.h"

namespace irene
{
    namespace
    {
        constexpr int kBlockSize = 512;

        // Filters (especially a 20 Hz high-pass or a narrow band) ring on after
        // the IRs have ended; render this much extra before looking for the end.
        constexpr double kExtraTailSeconds = 3.0;

        constexpr double kFadeSeconds = 0.005;

        void applyFadeOut (juce::AudioBuffer<float>& audio, double sampleRate)
        {
            const auto n = audio.getNumSamples();
            const auto fade = juce::jlimit (1, juce::jmax (1, n / 4), static_cast<int> (kFadeSeconds * sampleRate));

            for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            {
                auto* data = audio.getWritePointer (ch);

                for (int i = 0; i < fade; ++i)
                {
                    const auto phase = static_cast<double> (i + 1) / fade;
                    data[n - fade + i] *= static_cast<float> (0.5 * (1.0 + std::cos (std::numbers::pi * phase)));
                }
            }
        }
    }

    //==============================================================================
    ExportSnapshot ExportSnapshot::capture (IReneProcessor& processor)
    {
        ExportSnapshot snapshot;
        // The bare parameters (no Solo target, no A/B match gain), plus the
        // auto-gain correction, which is part of the sound.
        snapshot.settings = processor.getParamRefs().read();

        if (snapshot.settings.autoGainOn)
        {
            processor.updateLevelsNow();
            snapshot.settings.autoGainDb = processor.getLevels().getAutoGainDb();
        }

        snapshot.sessionRate = processor.getEngine().isPrepared() ? processor.getEngine().getSampleRate() : 48000.0;

        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto info = processor.getSlots().getInfo (i);
            auto& slot = snapshot.slots[static_cast<size_t> (i)];
            slot.source = processor.getSlots().getSource (i);
            slot.normalise = info.normalise;
            slot.autoTrim = info.autoTrim;
        }

        return snapshot;
    }

    bool ExportSnapshot::hasAnyIR() const noexcept
    {
        for (const auto& slot : slots)
            if (slot.source != nullptr)
                return true;

        return false;
    }

    //==============================================================================
    ExportResult renderExport (const ExportSnapshot& snapshot, const ExportOptions& options,
                               const std::function<bool (float)>& progress)
    {
        ExportResult result;

        const auto report = [&progress] (float value) { return progress == nullptr || progress (value); };
        const auto cancelled = [&result] { result.error = "Cancelled."; return result; };

        if (! snapshot.hasAnyIR())
        {
            result.error = "There is nothing to export: no slot has an IR loaded.";
            return result;
        }

        const auto rate = options.sampleRate > 0.0 ? options.sampleRate : snapshot.sessionRate;
        result.sampleRate = rate;

        // --- Settings as they are rendered -------------------------------------
        auto settings = snapshot.settings;
        settings.mix = 1.0f; // dry/wet is always ignored

        if (! options.includeInputOutputFilters)
        {
            settings.inputFilter = {};
            settings.outputFilter = {};
        }

        if (! options.includeOutputGain)
            settings.outGainDb = 0.0f;

        // --- A private engine with the IRs prepared for the export rate ----------
        const auto numChannels = options.stereo ? 2 : 1;
        Engine engine;
        auto longestIR = 0;

        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto index = static_cast<size_t> (i);
            const auto& slot = snapshot.slots[index];

            if (slot.source == nullptr)
                continue;

            // always from the original audio, straight to the export rate
            auto prepared = irproc::prepare (*slot.source, { rate, settings.slots[index].lengthPercent, slot.normalise, slot.autoTrim });
            longestIR = juce::jmax (longestIR, prepared.getNumSamples());
            engine.getSlot (i).setIR (std::move (prepared));

            if (! report (0.3f * static_cast<float> (i + 1) / kNumSlots))
                return cancelled();
        }

        engine.prepare (rate, kBlockSize, numChannels, numChannels); // installs the IRs synchronously

        // --- Render the impulse response -----------------------------------------
        const auto fixedLength = static_cast<int> (std::ceil (juce::jlimit (ExportOptions::kMinLengthMs, ExportOptions::kMaxLengthMs, options.lengthMs) * 0.001 * rate));
        const auto maxDelay = static_cast<int> (std::ceil (kMaxDelayMs * 0.001 * rate));
        const auto autoRenderLength = juce::jmin (static_cast<int> (ExportOptions::kMaxLengthMs * 0.001 * rate),
                                                  longestIR + maxDelay + static_cast<int> (kExtraTailSeconds * rate));
        const auto renderLength = juce::jmax (2, options.autoLength ? autoRenderLength : fixedLength);

        juce::AudioBuffer<float> audio (numChannels, renderLength);
        audio.clear();

        for (int ch = 0; ch < numChannels; ++ch)
            audio.setSample (ch, 0, 1.0f);

        for (int pos = 0; pos < renderLength; pos += kBlockSize)
        {
            const auto n = juce::jmin (kBlockSize, renderLength - pos);
            juce::AudioBuffer<float> block (audio.getArrayOfWritePointers(), numChannels, pos, n);
            engine.process (block, settings, true);

            if ((pos / kBlockSize) % 64 == 0 && ! report (0.3f + 0.65f * static_cast<float> (pos) / static_cast<float> (renderLength)))
                return cancelled();
        }

        // --- Length ---------------------------------------------------------------
        auto peak = audio.getMagnitude (0, renderLength);

        if (peak <= 0.0f)
        {
            result.error = "The result is silent (all slots are muted, off, or not soloed).";
            return result;
        }

        if (options.autoLength)
        {
            const auto threshold = peak * static_cast<float> (std::pow (10.0, ExportOptions::kAutoTailDb / 20.0));
            auto last = 0;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto* data = audio.getReadPointer (ch);

                for (int i = renderLength; --i > last;)
                {
                    if (std::abs (data[i]) >= threshold)
                    {
                        last = i;
                        break;
                    }
                }
            }

            // keep a little after the last audible sample for the fade-out
            const auto keep = juce::jlimit (juce::jmin (renderLength, 64), renderLength, last + 1 + static_cast<int> (kFadeSeconds * rate));
            audio.setSize (numChannels, keep, true);
        }

        applyFadeOut (audio, rate);

        // --- Level ------------------------------------------------------------------
        peak = audio.getMagnitude (0, audio.getNumSamples());

        if (options.normalisePeak && peak > 0.0f)
        {
            audio.applyGain (static_cast<float> (std::pow (10.0, ExportOptions::kNormalisedPeakDb / 20.0)) / peak);
            peak = audio.getMagnitude (0, audio.getNumSamples());
        }

        result.peakDb = juce::Decibels::gainToDecibels (peak, -100.0f);
        result.audio = std::move (audio);
        report (1.0f);
        return result;
    }

    juce::String writeExportFile (const juce::File& destination, const juce::AudioBuffer<float>& audio, double sampleRate, int bitDepth)
    {
        if (destination.getParentDirectory().createDirectory().failed())
            return "The folder could not be created:\n" + destination.getParentDirectory().getFullPathName();

        juce::TemporaryFile temp (destination);

        {
            std::unique_ptr<juce::OutputStream> stream = temp.getFile().createOutputStream();

            if (stream == nullptr)
                return "The file could not be written:\n" + destination.getFullPathName();

            auto writerOptions = juce::AudioFormatWriterOptions()
                                     .withSampleRate (sampleRate)
                                     .withNumChannels (audio.getNumChannels())
                                     .withBitsPerSample (bitDepth == 32 ? 32 : 24);

            if (bitDepth == 32)
                writerOptions = writerOptions.withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);

            juce::WavAudioFormat format;
            auto writer = format.createWriterFor (stream, writerOptions);

            if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()))
                return "The file could not be written:\n" + destination.getFullPathName();
        }

        if (! temp.overwriteTargetFileWithTemporary())
            return "The file could not be written:\n" + destination.getFullPathName();

        return {};
    }

    juce::String exportToFile (const ExportSnapshot& snapshot, const ExportOptions& options, const juce::File& destination,
                               const std::function<bool (float)>& progress)
    {
        const auto result = renderExport (snapshot, options, progress);

        if (! result.succeeded())
            return result.error;

        if (options.bitDepth != 32 && result.peakDb > 0.0f)
            return "The IR peaks at +" + juce::String (result.peakDb, 1) + " dBFS, which a 24-bit file can't hold without clipping.\n"
                   "Turn on peak normalisation or choose 32-bit float.";

        return writeExportFile (destination, result.audio, result.sampleRate, options.bitDepth);
    }
}
