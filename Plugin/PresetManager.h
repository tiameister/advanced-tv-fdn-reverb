#pragma once
#include <JuceHeader.h>
#include <array>
#include <string_view>

/**
 * @file  PresetManager.h
 *
 * Lightweight factory-preset registry.
 *
 * Each preset is stored as a JUCE ValueTree snapshot that matches the APVTS
 * state schema.  Loading a preset replaces the current APVTS state, which
 * automatically pushes all parameter values to the audio thread through the
 * normal APVTS / PluginProcessor::setStateInformation pathway.
 *
 * Usage (from the editor):
 *
 *   PresetManager pm(proc.apvts);
 *   pm.loadPreset(2);           // loads "Cathedral"
 *   int n = pm.getNumPresets(); // 5
 *   pm.getPresetName(0);        // "Small Room"
 */
class PresetManager
{
public:
    explicit PresetManager(juce::AudioProcessorValueTreeState& apvts);

    int              getNumPresets()            const noexcept;
    juce::String     getPresetName(int idx)     const noexcept;
    void             loadPreset(int idx);

private:
    struct PresetData
    {
        const char*  name;
        // APVTS parameter values — order must match createPresetState()
        float reverbTime;
        float size;
        float preDelay;
        float distance;
        float masterWet;
        float modDepth;
        float stereoWidth;
        float erLength;
        float erDensity;
        float bassDecay;
        float midDecay;
        float hfDecay;
    };

    static const std::array<PresetData, 5> kFactoryPresets;

    juce::ValueTree createPresetState(const PresetData& p) const;

    juce::AudioProcessorValueTreeState& apvts_;
};
