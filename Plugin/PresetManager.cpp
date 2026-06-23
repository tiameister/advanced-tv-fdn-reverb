#include "PresetManager.h"
#include "PluginProcessor.h"

// ── Factory preset table ──────────────────────────────────────────────────────
//
// Column order:  name | RT | size | preDelay | distance | wet |
//                modDepth | width | erLen | erDens | bass | mid | hf
//
// Design notes:
//   • "Small Room"  — tight space, bright, lively.  HF decay is intentionally
//     longer than mid (small rooms have hard surfaces).
//   • "Studio Plate" — classic EMT 140 character: short, dense, smooth.
//   • "Drum Room"   — punchy, snappy transient field with strong ER.
//   • "Large Hall"  — concert hall with pronounced early reflections.
//   • "Cathedral"   — maximum size, extremely long RT, rolling treble cut.
//
// ─────────────────────────────────────────────────────────────────────────────
const std::array<PresetManager::PresetData, 5> PresetManager::kFactoryPresets =
{{
    //             name            RT    size  pre   dist  wet   mod   wid   erL   erD   bass  mid   hf
    { "Small Room",               0.6f, 0.18f, 5.f, 0.3f, 0.35f, 0.3f, 1.0f, 60.f, 14.f, 1.1f, 1.0f, 0.9f },
    { "Studio Plate",             1.2f, 0.28f,10.f, 0.5f, 0.40f, 0.5f, 1.2f, 40.f, 20.f, 1.3f, 1.0f, 0.5f },
    { "Drum Room",                0.9f, 0.35f, 8.f, 0.4f, 0.38f, 0.4f, 1.3f, 80.f, 18.f, 1.6f, 1.0f, 0.4f },
    { "Large Hall",               2.8f, 0.65f,22.f, 0.6f, 0.45f, 0.7f, 1.5f,100.f, 16.f, 2.0f, 1.0f, 0.25f},
    { "Cathedral",                6.0f, 0.92f,40.f, 0.7f, 0.50f, 0.9f, 1.8f,120.f, 12.f, 2.8f, 1.0f, 0.10f},
}};

// ── Constructor ───────────────────────────────────────────────────────────────
PresetManager::PresetManager(juce::AudioProcessorValueTreeState& apvts)
    : apvts_(apvts)
{}

// ── Accessors ─────────────────────────────────────────────────────────────────
int PresetManager::getNumPresets() const noexcept
{
    return static_cast<int>(kFactoryPresets.size());
}

juce::String PresetManager::getPresetName(int idx) const noexcept
{
    if (idx < 0 || idx >= getNumPresets()) return {};
    return kFactoryPresets[static_cast<std::size_t>(idx)].name;
}

// ── Load ──────────────────────────────────────────────────────────────────────
void PresetManager::loadPreset(int idx)
{
    if (idx < 0 || idx >= getNumPresets()) return;

    const auto state = createPresetState(kFactoryPresets[static_cast<std::size_t>(idx)]);

    // replaceState() pushes every APVTS parameter from the ValueTree, which
    // triggers PluginProcessor::parameterChanged() (or the block-level reads)
    // and ultimately calls the ReverbEngine setters on the audio thread.
    apvts_.replaceState(state);
}

// ── Helper: build APVTS ValueTree from preset data ───────────────────────────
juce::ValueTree PresetManager::createPresetState(const PresetData& p) const
{
    using P = ReverbPluginProcessor;

    // Build a ValueTree with the same identifier JUCE APVTS uses internally.
    // The APVTS root ID is whatever was passed to the APVTS constructor as
    // "valueTreeType"; PluginProcessor uses "ReverbParams".
    juce::ValueTree state("ReverbParams");

    auto addParam = [&](const char* id, float value)
    {
        juce::ValueTree param("PARAM");
        param.setProperty("id",    id,    nullptr);
        param.setProperty("value", value, nullptr);
        state.addChild(param, -1, nullptr);
    };

    addParam(P::kParamReverbTime,  p.reverbTime);
    addParam(P::kParamSize,        p.size);
    addParam(P::kParamPreDelay,    p.preDelay);
    addParam(P::kParamDistance,    p.distance);
    addParam(P::kParamMasterWet,   p.masterWet);
    addParam(P::kParamModDepth,    p.modDepth);
    addParam(P::kParamStereoWidth, p.stereoWidth);
    addParam(P::kParamErLength,    p.erLength);
    addParam(P::kParamErDensity,   p.erDensity);
    addParam(P::kParamBassDecay,   p.bassDecay);
    addParam(P::kParamMidDecay,    p.midDecay);
    addParam(P::kParamHfDecay,     p.hfDecay);

    return state;
}
