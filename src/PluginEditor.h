#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

namespace irene
{
    /** Milestone 1: placeholder editor showing the plugin name and version. */
    class IReneEditor final : public juce::AudioProcessorEditor
    {
    public:
        explicit IReneEditor (IReneProcessor&);
        ~IReneEditor() override = default;

        void paint (juce::Graphics&) override;
        void resized() override {}

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IReneEditor)
    };
}
