#include "PresetManager.h"
#include "PluginProcessor.h"

const std::array<PresetManager::PresetData, 4> PresetManager::kFactoryPresets =
{{
    { "Small Room",  0.6f, 0.18f, 12000.0f,  5.0f, 0.30f },
    { "Lush Plate",  1.8f, 0.40f,  6000.0f, 12.0f, 0.40f },
    { "Large Hall",  3.5f, 0.70f,  3500.0f, 25.0f, 0.45f },
    { "Cathedral",   8.0f, 0.92f,  2000.0f, 40.0f, 0.50f },
}};

PresetManager::PresetManager(juce::AudioProcessorValueTreeState& apvts)
    : apvts_(apvts)
{}

int PresetManager::getNumPresets() const noexcept
{
    return static_cast<int>(kFactoryPresets.size());
}

juce::String PresetManager::getPresetName(int idx) const noexcept
{
    if (idx < 0 || idx >= getNumPresets()) return {};
    return kFactoryPresets[static_cast<std::size_t>(idx)].name;
}

int PresetManager::getPresetIndexByName(const juce::String& name) const noexcept
{
    for (int i = 0; i < getNumPresets(); ++i)
    {
        if (name.equalsIgnoreCase(getPresetName(i)))
            return i;
    }
    return -1;
}

void PresetManager::loadPreset(int idx)
{
    if (idx < 0 || idx >= getNumPresets()) return;
    apvts_.replaceState(createPresetState(kFactoryPresets[static_cast<std::size_t>(idx)]));
}

void PresetManager::loadPresetByName(const juce::String& name)
{
    loadPreset(getPresetIndexByName(name));
}

juce::ValueTree PresetManager::createPresetState(const PresetData& p) const
{
    using P = ReverbPluginProcessor;

    juce::ValueTree state("Parameters");

    auto addParam = [&](const char* id, float value)
    {
        juce::ValueTree param("PARAM");
        param.setProperty("id",    id,    nullptr);
        param.setProperty("value", value, nullptr);
        state.addChild(param, -1, nullptr);
    };

    addParam(P::kParamTime,     p.time);
    addParam(P::kParamSize,     p.size);
    addParam(P::kParamDamping,  p.damping);
    addParam(P::kParamPreDelay, p.preDelay);
    addParam(P::kParamMix,      p.mix);

    return state;
}
