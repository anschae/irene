#include "Parameters.h"

namespace irene
{
    namespace
    {
        using APVTS = juce::AudioProcessorValueTreeState;
        using Attributes = juce::AudioParameterFloatAttributes;

        juce::ParameterID pid (const juce::String& id) { return { id, kParamVersionHint }; }

        //==============================================================================
        juce::NormalisableRange<float> logRange (float lo, float hi)
        {
            return { lo, hi,
                     [] (float start, float end, float t) { return start * std::pow (end / start, t); },
                     [] (float start, float end, float v) { return std::log (v / start) / std::log (end / start); },
                     [] (float start, float end, float v) { return juce::jlimit (start, end, v); } };
        }

        juce::String freqToText (float hz, int)
        {
            return hz < 1000.0f ? juce::String (hz, hz < 100.0f ? 1 : 0) + " Hz"
                                : juce::String (hz / 1000.0f, 2) + " kHz";
        }

        float textToFreq (const juce::String& text)
        {
            const auto t = text.trim().toLowerCase();
            const auto value = t.getFloatValue();
            return t.contains ("k") ? value * 1000.0f : value;
        }

        std::unique_ptr<juce::AudioParameterFloat> makeFreq (const juce::String& id, const juce::String& name, float def)
        {
            return std::make_unique<juce::AudioParameterFloat> (
                pid (id), name, logRange (kMinFreq, kMaxFreq), def,
                Attributes().withStringFromValueFunction (freqToText).withValueFromStringFunction (textToFreq));
        }

        /** A linear dB range that snaps to 0.01 dB by rounding the value itself.

            (juce::NormalisableRange's own "interval" snaps relative to the range
            start, e.g. -60 + 0.01 * 6000, which is not exactly 0 in float
            arithmetic on every CPU. Rounding the value keeps 0 dB exactly 0 dB,
            so a neutral setting really is bit-transparent everywhere.) */
        juce::NormalisableRange<float> dbRange (float lo, float hi)
        {
            return { lo, hi,
                     [] (float start, float end, float t) { return start + (end - start) * t; },
                     [] (float start, float end, float v) { return (v - start) / (end - start); },
                     [] (float start, float end, float v) { return juce::jlimit (start, end, std::round (v * 100.0f) / 100.0f + 0.0f); } };
        }

        std::unique_ptr<juce::AudioParameterFloat> makeDb (const juce::String& id, const juce::String& name,
                                                           float lo, float hi, float def, bool lowestIsOff = false)
        {
            return std::make_unique<juce::AudioParameterFloat> (
                pid (id), name, dbRange (lo, hi), def,
                Attributes()
                    .withStringFromValueFunction ([lo, lowestIsOff] (float v, int)
                    {
                        if (lowestIsOff && v <= lo + 0.005f)
                            return juce::String ("Off");

                        if (std::abs (v) < 0.05f) // never "-0.0 dB"
                            return juce::String ("0.0 dB");

                        return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB";
                    })
                    .withValueFromStringFunction ([lo] (const juce::String& text)
                    {
                        const auto t = text.trim().toLowerCase();
                        return (t.startsWith ("off") || t.startsWith ("-inf")) ? lo : t.getFloatValue();
                    }));
        }

        std::unique_ptr<juce::AudioParameterFloat> makeQ (const juce::String& id, const juce::String& name)
        {
            return std::make_unique<juce::AudioParameterFloat> (
                pid (id), name, logRange (kMinQ, kMaxQ), kDefaultQ,
                Attributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2); }));
        }

        std::unique_ptr<juce::AudioParameterBool> makeBool (const juce::String& id, const juce::String& name, bool def = false)
        {
            return std::make_unique<juce::AudioParameterBool> (pid (id), name, def);
        }

        std::unique_ptr<juce::AudioParameterChoice> makeChoice (const juce::String& id, const juce::String& name,
                                                                const juce::StringArray& choices, int def = 0)
        {
            return std::make_unique<juce::AudioParameterChoice> (pid (id), name, choices, def);
        }

        //==============================================================================
        /** One (suffix, factory) table drives layout creation, the suffix lists and
            the swap-slot code, so nothing has to be kept in sync by hand. */
        struct ParamDef
        {
            juce::String suffix;
            std::function<std::unique_ptr<juce::RangedAudioParameter> (const juce::String& id, const juce::String& namePrefix)> make;
        };

        const std::vector<ParamDef>& filterBlockDefs()
        {
            static const auto defs = []
            {
                std::vector<ParamDef> d;
                const juce::StringArray slopes { "12 dB/oct", "24 dB/oct" };
                const juce::StringArray types { "Peak", "Shelf" };
                const std::array<const char*, kNumBands> bandLabels { "Low", "Mid", "High" };

                d.push_back ({ "filter_bypass", [] (auto& id, auto& n) { return makeBool (id, n + " Filter Bypass"); } });

                d.push_back ({ "hp_on", [] (auto& id, auto& n) { return makeBool (id, n + " HP On"); } });
                d.push_back ({ "hp_freq", [] (auto& id, auto& n) { return makeFreq (id, n + " HP Freq", kMinFreq); } });
                d.push_back ({ "hp_slope", [slopes] (auto& id, auto& n) { return makeChoice (id, n + " HP Slope", slopes); } });

                for (int b = 0; b < kNumBands; ++b)
                {
                    const juce::String band (ids::bandNames[static_cast<size_t> (b)]);
                    const juce::String label (bandLabels[static_cast<size_t> (b)]);
                    const auto defFreq = kDefaultBandFreqs[static_cast<size_t> (b)];

                    d.push_back ({ band + "_on", [label] (auto& id, auto& n) { return makeBool (id, n + " " + label + " On"); } });
                    d.push_back ({ band + "_freq", [label, defFreq] (auto& id, auto& n) { return makeFreq (id, n + " " + label + " Freq", defFreq); } });
                    d.push_back ({ band + "_gain", [label] (auto& id, auto& n) { return makeDb (id, n + " " + label + " Gain", -kMaxBandGainDb, kMaxBandGainDb, 0.0f); } });
                    d.push_back ({ band + "_q", [label] (auto& id, auto& n) { return makeQ (id, n + " " + label + " Q"); } });

                    if (b != 1) // Low and High can be switched between peak and shelf
                        d.push_back ({ band + "_type", [label, types] (auto& id, auto& n) { return makeChoice (id, n + " " + label + " Type", types); } });
                }

                d.push_back ({ "lp_on", [] (auto& id, auto& n) { return makeBool (id, n + " LP On"); } });
                d.push_back ({ "lp_freq", [] (auto& id, auto& n) { return makeFreq (id, n + " LP Freq", kMaxFreq); } });
                d.push_back ({ "lp_slope", [slopes] (auto& id, auto& n) { return makeChoice (id, n + " LP Slope", slopes); } });
                return d;
            }();

            return defs;
        }

        const std::vector<ParamDef>& slotDefs()
        {
            static const auto defs = []
            {
                std::vector<ParamDef> d;

                d.push_back ({ ids::gain, [] (auto& id, auto& n) { return makeDb (id, n + " Gain", kSlotGainOffDb, kSlotGainMaxDb, 0.0f, true); } });

                d.push_back ({ ids::pan, [] (auto& id, auto& n)
                {
                    return std::make_unique<juce::AudioParameterFloat> (
                        pid (id), n + " Pan", juce::NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f,
                        Attributes()
                            .withStringFromValueFunction ([] (float v, int)
                            {
                                const auto r = juce::roundToInt (v);
                                return r == 0 ? juce::String ("C") : (r < 0 ? "L" : "R") + juce::String (std::abs (r));
                            })
                            .withValueFromStringFunction ([] (const juce::String& text)
                            {
                                const auto t = text.trim().toUpperCase();

                                if (t.startsWith ("C"))
                                    return 0.0f;

                                if (t.startsWith ("L"))
                                    return -t.substring (1).getFloatValue();

                                if (t.startsWith ("R"))
                                    return t.substring (1).getFloatValue();

                                return t.getFloatValue();
                            }));
                } });

                d.push_back ({ ids::phase, [] (auto& id, auto& n) { return makeBool (id, n + " Phase Invert"); } });

                d.push_back ({ ids::delay, [] (auto& id, auto& n)
                {
                    return std::make_unique<juce::AudioParameterFloat> (
                        pid (id), n + " Delay", juce::NormalisableRange<float> (0.0f, kMaxDelayMs, 0.001f), 0.0f,
                        Attributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 3) + " ms"; }));
                } });

                d.push_back ({ ids::mute, [] (auto& id, auto& n) { return makeBool (id, n + " Mute"); } });
                d.push_back ({ ids::solo, [] (auto& id, auto& n) { return makeBool (id, n + " Solo"); } });

                d.push_back ({ ids::length, [] (auto& id, auto& n)
                {
                    return std::make_unique<juce::AudioParameterFloat> (
                        pid (id), n + " IR Length", juce::NormalisableRange<float> (kMinLengthPercent, 100.0f, 0.1f, 0.5f), 100.0f,
                        Attributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " %"; }));
                } });

                return d;
            }();

            return defs;
        }

        void addFilterBlock (juce::AudioProcessorParameterGroup& parent, const juce::String& prefix, const juce::String& displayName)
        {
            auto group = std::make_unique<juce::AudioProcessorParameterGroup> (prefix + "_filter", displayName + " Filter", " | ");

            for (const auto& def : filterBlockDefs())
                group->addChild (def.make (prefix + "_" + def.suffix, displayName));

            parent.addChild (std::move (group));
        }

        std::atomic<float>* raw (APVTS& apvts, const juce::String& id)
        {
            auto* p = apvts.getRawParameterValue (id);
            jassert (p != nullptr);
            return p;
        }

        bool asBool (const std::atomic<float>* p) noexcept
        {
            return p->load (std::memory_order_relaxed) >= 0.5f;
        }

        float asFloat (const std::atomic<float>* p) noexcept
        {
            return p->load (std::memory_order_relaxed);
        }
    }

    //==============================================================================
    juce::String ids::slotPrefix (int slotIndex)
    {
        return "slot" + juce::String (slotIndex + 1);
    }

    juce::String ids::slotParam (int slotIndex, const char* suffix)
    {
        return slotPrefix (slotIndex) + "_" + suffix;
    }

    juce::StringArray ids::filterBlockParamSuffixes()
    {
        juce::StringArray result;

        for (const auto& def : filterBlockDefs())
            result.add (def.suffix);

        return result;
    }

    juce::StringArray ids::slotParamSuffixes()
    {
        juce::StringArray result;

        for (const auto& def : slotDefs())
            result.add (def.suffix);

        result.addArray (filterBlockParamSuffixes());
        return result;
    }

    //==============================================================================
    APVTS::ParameterLayout createParameterLayout()
    {
        APVTS::ParameterLayout layout;

        auto global = std::make_unique<juce::AudioProcessorParameterGroup> ("global", "Global", " | ");

        global->addChild (std::make_unique<juce::AudioParameterFloat> (
            pid (ids::mix), "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
            Attributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " %"; })));

        global->addChild (makeDb (ids::outGain, "Output Gain", -kMaxOutGainDb, kMaxOutGainDb, 0.0f));
        addFilterBlock (*global, ids::inputPrefix, "In");
        addFilterBlock (*global, ids::outputPrefix, "Out");
        layout.add (std::move (global));

        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto prefix = ids::slotPrefix (i);
            const auto displayName = "Slot " + juce::String (i + 1);

            auto group = std::make_unique<juce::AudioProcessorParameterGroup> (prefix, displayName, " | ");

            for (const auto& def : slotDefs())
                group->addChild (def.make (prefix + "_" + def.suffix, displayName));

            addFilterBlock (*group, prefix, displayName);
            layout.add (std::move (group));
        }

        return layout;
    }

    //==============================================================================
    void FilterBlockParamRefs::attach (APVTS& apvts, const juce::String& prefix)
    {
        const auto p = prefix + "_";

        bypass = raw (apvts, p + "filter_bypass");
        hpOn = raw (apvts, p + "hp_on");
        hpFreq = raw (apvts, p + "hp_freq");
        hpSlope = raw (apvts, p + "hp_slope");

        for (size_t b = 0; b < bands.size(); ++b)
        {
            const auto bp = p + ids::bandNames[b] + "_";
            bands[b].on = raw (apvts, bp + "on");
            bands[b].freq = raw (apvts, bp + "freq");
            bands[b].gain = raw (apvts, bp + "gain");
            bands[b].q = raw (apvts, bp + "q");
            bands[b].type = b == 1 ? nullptr : raw (apvts, bp + "type");
        }

        lpOn = raw (apvts, p + "lp_on");
        lpFreq = raw (apvts, p + "lp_freq");
        lpSlope = raw (apvts, p + "lp_slope");
    }

    FilterBlockSettings FilterBlockParamRefs::read() const noexcept
    {
        FilterBlockSettings s;
        s.bypass = asBool (bypass);
        s.hpOn = asBool (hpOn);
        s.hpFreq = asFloat (hpFreq);
        s.hpSteep = asBool (hpSlope);

        for (size_t b = 0; b < bands.size(); ++b)
        {
            s.bands[b].on = asBool (bands[b].on);
            s.bands[b].freq = asFloat (bands[b].freq);
            s.bands[b].gainDb = asFloat (bands[b].gain);
            s.bands[b].q = asFloat (bands[b].q);
            s.bands[b].shelf = bands[b].type != nullptr && asBool (bands[b].type);
        }

        s.lpOn = asBool (lpOn);
        s.lpFreq = asFloat (lpFreq);
        s.lpSteep = asBool (lpSlope);
        return s;
    }

    void SlotParamRefs::attach (APVTS& apvts, int slotIndex)
    {
        gain = raw (apvts, ids::slotParam (slotIndex, ids::gain));
        pan = raw (apvts, ids::slotParam (slotIndex, ids::pan));
        phase = raw (apvts, ids::slotParam (slotIndex, ids::phase));
        delay = raw (apvts, ids::slotParam (slotIndex, ids::delay));
        mute = raw (apvts, ids::slotParam (slotIndex, ids::mute));
        solo = raw (apvts, ids::slotParam (slotIndex, ids::solo));
        length = raw (apvts, ids::slotParam (slotIndex, ids::length));
        filter.attach (apvts, ids::slotPrefix (slotIndex));
    }

    SlotSettings SlotParamRefs::read() const noexcept
    {
        SlotSettings s;
        s.gainDb = asFloat (gain);
        s.pan = asFloat (pan) / 100.0f;
        s.phaseInvert = asBool (phase);
        s.delayMs = asFloat (delay);
        s.mute = asBool (mute);
        s.solo = asBool (solo);
        s.lengthPercent = asFloat (length);
        s.filter = filter.read();
        return s;
    }

    void ParamRefs::attach (APVTS& apvts)
    {
        inputFilter.attach (apvts, ids::inputPrefix);
        outputFilter.attach (apvts, ids::outputPrefix);

        for (int i = 0; i < kNumSlots; ++i)
            slots[static_cast<size_t> (i)].attach (apvts, i);

        mix = raw (apvts, ids::mix);
        outGain = raw (apvts, ids::outGain);
    }

    EngineSettings ParamRefs::read() const noexcept
    {
        EngineSettings s;
        s.inputFilter = inputFilter.read();

        for (size_t i = 0; i < slots.size(); ++i)
            s.slots[i] = slots[i].read();

        s.mix = asFloat (mix) / 100.0f;
        s.outputFilter = outputFilter.read();
        s.outGainDb = asFloat (outGain);
        return s;
    }
}
