#pragma once

#include <juce_dsp/juce_dsp.h>

#include "FilterBlock.h"
#include "SlotDsp.h"
#include "params/Settings.h"

namespace irene
{
    /** The complete real-time signal path:

        Input -> Input Filter Block -> slots (summed = wet) -> Dry/Wet Mix
              -> Output Filter Block -> Output Gain -> Output

        Latency is zero (the convolution runs in its zero-latency mode), so the
        dry signal needs no delay compensation to stay aligned with the wet path.

        Independent of juce::AudioProcessor so it can be tested directly. */
    class Engine
    {
    public:
        /** Internal processing block size. Host blocks of any size are split up. */
        static constexpr int kMaxChunkSize = 512;

        Engine();

        void prepare (double sampleRate, int hostBlockSize, int numInputChannels, int numOutputChannels);
        bool isPrepared() const noexcept { return prepared; }
        double getSampleRate() const noexcept { return sampleRate; }

        /** Always 0; see class comment. */
        int getLatencySamples() const noexcept { return 0; }

        /** Processes `buffer` in place. Real-time safe. */
        void process (juce::AudioBuffer<float>& buffer, const EngineSettings& settings, bool nonRealtime) noexcept;

        SlotDsp& getSlot (int index) noexcept { return *slots[static_cast<size_t> (index)]; }

        //==============================================================================
        /** Peak level since the last call (0 if the channel doesn't exist). Any thread. */
        float readInputPeak (int channel) noexcept { return inputPeak[static_cast<size_t> (channel)].exchange (0.0f); }
        float readOutputPeak (int channel) noexcept { return outputPeak[static_cast<size_t> (channel)].exchange (0.0f); }

    private:
        void processChunk (float* const* host, int numSamples, const EngineSettings&, bool nonRealtime) noexcept;

        juce::dsp::ConvolutionMessageQueue messageQueue;
        std::array<std::unique_ptr<SlotDsp>, kNumSlots> slots;

        FilterBlock inputFilter, outputFilter;
        juce::AudioBuffer<float> work, wet;
        juce::SmoothedValue<float> mix, outGain;
        bool snapSmoothers = true;

        double sampleRate = 44100.0;
        int numIn = 2, numOut = 2, chunkSize = kMaxChunkSize;
        bool prepared = false;

        std::array<std::atomic<float>, 2> inputPeak {}, outputPeak {};

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Engine)
    };
}
