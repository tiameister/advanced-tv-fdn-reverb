#pragma once
#include <JuceHeader.h>
#include <array>
#include <string_view>

/**
 * Factory preset registry for the five-parameter Dattorro engine.
 */
class PresetManager
{
public:
    explicit PresetManager(juce::AudioProcessorValueTreeState& apvts);

    int              getNumPresets()        const noexcept;
    juce::String     getPresetName(int idx) const noexcept;
    int              getPresetIndexByName(const juce::String& name) const noexcept;
    void             loadPreset(int idx);
    void             loadPresetByName(const juce::String& name);

private:
    struct PresetData
    {
        const char* name;
        float time;
        float size;
        float damping;
        float preDelay;
        float mix;
    };

    static const std::array<PresetData, 4> kFactoryPresets;

    juce::ValueTree createPresetState(const PresetData& p) const;

    juce::AudioProcessorValueTreeState& apvts_;
};
