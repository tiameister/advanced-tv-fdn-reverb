#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

// ── Parameter layout ──────────────────────────────────────────────────────────

juce::AudioProcessorValueTreeState::ParameterLayout
ReverbPluginProcessor::createParameterLayout()
{
    using Param  = juce::AudioParameterFloat;
    using Range  = juce::NormalisableRange<float>;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto logRange = [](float lo, float hi, float centre) -> Range
    {
        Range r { lo, hi };
        r.setSkewForCentre(centre);
        return r;
    };

    // ── Main controls ─────────────────────────────────────────────────────────
    params.push_back(std::make_unique<Param>(
        kParamReverbTime, "Reverb Time",
        logRange(0.1f, 20.0f, 2.5f), 2.5f,
        juce::AudioParameterFloatAttributes{}.withLabel("s")));

    params.push_back(std::make_unique<Param>(
        kParamSize, "Room Size",
        Range{ 0.0f, 1.0f, 0.001f }, 0.33f));

    params.push_back(std::make_unique<Param>(
        kParamPreDelay, "Pre-Delay",
        Range{ 0.0f, 500.0f, 0.1f }, 0.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("ms")));

    params.push_back(std::make_unique<Param>(
        kParamDistance, "Distance",
        Range{ 0.0f, 1.0f, 0.001f }, 0.5f));

    params.push_back(std::make_unique<Param>(
        kParamMasterWet, "Wet Mix",
        Range{ 0.0f, 1.0f, 0.001f }, 1.0f));

    // ── Character controls ────────────────────────────────────────────────────
    params.push_back(std::make_unique<Param>(
        kParamModDepth, "Mod Depth",
        Range{ 0.0f, 8.0f, 0.01f }, 2.5f,
        juce::AudioParameterFloatAttributes{}.withLabel("ms")));

    params.push_back(std::make_unique<Param>(
        kParamStereoWidth, "Stereo Width",
        Range{ 0.0f, 2.0f, 0.01f }, 1.0f));

    params.push_back(std::make_unique<Param>(
        kParamErLength, "ER Length",
        logRange(20.0f, 200.0f, 80.0f), 80.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("ms")));

    params.push_back(std::make_unique<Param>(
        kParamErDensity, "ER Density",
        logRange(500.0f, 8000.0f, 3000.0f), 3000.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("Hz")));

    // ── Decay shape (spectral tilt multipliers relative to Reverb Time) ───────
    // These three knobs define a simple 3-point decay curve:
    //   Bass band T60 = reverbTime × bassDecay   (0.5–3.0, default 1.4 → warmth)
    //   Mid  band T60 = reverbTime × midDecay    (0.5–2.0, default 1.0 → flat)
    //   HF   band T60 = reverbTime × hfDecay     (0.05–1.0, default 0.2 → air)
    params.push_back(std::make_unique<Param>(
        kParamBassDecay, "Bass Decay",
        Range{ 0.5f, 3.0f, 0.01f }, 1.4f,
        juce::AudioParameterFloatAttributes{}.withLabel("×")));

    params.push_back(std::make_unique<Param>(
        kParamMidDecay, "Mid Decay",
        Range{ 0.5f, 2.0f, 0.01f }, 1.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("×")));

    params.push_back(std::make_unique<Param>(
        kParamHfDecay, "HF Decay",
        Range{ 0.05f, 1.0f, 0.001f }, 0.2f,
        juce::AudioParameterFloatAttributes{}.withLabel("×")));

    return { params.begin(), params.end() };
}

// ── Constructor ───────────────────────────────────────────────────────────────

ReverbPluginProcessor::ReverbPluginProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void ReverbPluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    reverbEngine_.prepare(sampleRate, std::max(1, samplesPerBlock),
                          80.0f,   // erLengthMs  (updated via setErLength below)
                          0.0f,    // initialPreDelayMs
                          3000.0f, // erDensityHz (updated via setErDensity below)
                          1.0f);   // erMinSpacingMs

    // Cache atomic pointers — getRawParameterValue is not lock-free everywhere
    pReverbTime_  = apvts.getRawParameterValue(kParamReverbTime);
    pSize_        = apvts.getRawParameterValue(kParamSize);
    pPreDelay_    = apvts.getRawParameterValue(kParamPreDelay);
    pDistance_    = apvts.getRawParameterValue(kParamDistance);
    pMasterWet_   = apvts.getRawParameterValue(kParamMasterWet);
    pModDepth_    = apvts.getRawParameterValue(kParamModDepth);
    pStereoWidth_ = apvts.getRawParameterValue(kParamStereoWidth);
    pErLength_    = apvts.getRawParameterValue(kParamErLength);
    pErDensity_   = apvts.getRawParameterValue(kParamErDensity);
    pBassDecay_   = apvts.getRawParameterValue(kParamBassDecay);
    pMidDecay_    = apvts.getRawParameterValue(kParamMidDecay);
    pHfDecay_     = apvts.getRawParameterValue(kParamHfDecay);

    // Pre-allocate mono scratch buffer
    monoScratchR_.resize(static_cast<std::size_t>(std::max(1, samplesPerBlock)));

    // ── Push all APVTS values to engine immediately ───────────────────────────
    // Without this the engine runs with constructor defaults for the first block.
    auto load = [](std::atomic<float>* p, float fallback)
    { return p ? p->load(std::memory_order_relaxed) : fallback; };

    reverbEngine_.setReverbTime   (load(pReverbTime_,  2.5f));
    reverbEngine_.setSize         (load(pSize_,        0.33f));
    reverbEngine_.setPreDelayMs   (load(pPreDelay_,    0.0f));
    reverbEngine_.setDistance     (load(pDistance_,    0.5f));
    reverbEngine_.setMasterWet    (load(pMasterWet_,   1.0f));
    reverbEngine_.setFdnModDepth  (load(pModDepth_,    2.5f));
    reverbEngine_.setFdnStereoWidth(load(pStereoWidth_, 1.0f));
    reverbEngine_.setErLength     (load(pErLength_,    80.0f));
    reverbEngine_.setErDensity    (load(pErDensity_,   3000.0f));
    reverbEngine_.setDecayShape   (load(pBassDecay_,   1.4f),
                                   load(pMidDecay_,    1.0f),
                                   load(pHfDecay_,     0.2f));
}

// ── processBlock ──────────────────────────────────────────────────────────────

void ReverbPluginProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& /*midi*/)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels == 0)
        return;

    // ── 1. Read APVTS atomics once (no allocation, no lock) ──────────────────
    auto load = [](std::atomic<float>* p, float fallback)
    { return p ? p->load(std::memory_order_relaxed) : fallback; };

    const float reverbTime  = load(pReverbTime_,  2.5f);
    const float size        = load(pSize_,        0.33f);
    const float preDelay    = load(pPreDelay_,    0.0f);
    const float distance    = load(pDistance_,    0.5f);
    const float masterWet   = load(pMasterWet_,   1.0f);
    const float modDepth    = load(pModDepth_,    2.5f);
    const float stereoWidth = load(pStereoWidth_, 1.0f);
    const float erLength    = load(pErLength_,    80.0f);
    const float erDensity   = load(pErDensity_,   3000.0f);
    const float bassDecay   = load(pBassDecay_,   1.4f);
    const float midDecay    = load(pMidDecay_,    1.0f);
    const float hfDecay     = load(pHfDecay_,     0.2f);

    // ── 2. Forward all parameters (internal smoothers absorb any steps) ───────
    reverbEngine_.setReverbTime    (reverbTime);
    reverbEngine_.setSize          (size);
    reverbEngine_.setPreDelayMs    (preDelay);
    reverbEngine_.setDistance      (distance);
    reverbEngine_.setMasterWet     (masterWet);
    reverbEngine_.setFdnModDepth   (modDepth);
    reverbEngine_.setFdnStereoWidth(stereoWidth);
    reverbEngine_.setErLength      (erLength);
    reverbEngine_.setErDensity     (erDensity);
    reverbEngine_.setDecayShape    (bassDecay, midDecay, hfDecay);

    // ── 3. In-place stereo processing ─────────────────────────────────────────
    if (numChannels >= 2)
    {
        float* L = buffer.getWritePointer(0);
        float* R = buffer.getWritePointer(1);
        reverbEngine_.processBlock(L, R, numSamples);
    }
    else
    {
        // Mono fallback: expand into scratch, process stereo, average back to mono.
        jassert(numSamples <= static_cast<int>(monoScratchR_.size()));
        float* mono = buffer.getWritePointer(0);
        std::copy(mono, mono + numSamples, monoScratchR_.begin());
        reverbEngine_.processBlock(mono, monoScratchR_.data(), numSamples);
        for (int i = 0; i < numSamples; ++i)
            mono[i] = 0.5f * (mono[i] + monoScratchR_[static_cast<std::size_t>(i)]);
    }
}

// ── State persistence ─────────────────────────────────────────────────────────

void ReverbPluginProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty("version", 2, nullptr);
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, dest);
}

void ReverbPluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
    {
        auto tree = juce::ValueTree::fromXml(*xml);
        // Future: inspect tree.getProperty("version") to migrate from v1 presets.
        apvts.replaceState(tree);
    }

    // Re-push all parameters after a state reload so the engine is immediately
    // in sync with the loaded values (setSize / setErLength / etc. may trigger
    // their respective update flags on the next processBlock).
    if (pReverbTime_)
    {
        auto load = [](std::atomic<float>* p, float f)
        { return p ? p->load(std::memory_order_relaxed) : f; };

        reverbEngine_.setReverbTime (load(pReverbTime_, 2.5f));
        reverbEngine_.setDecayShape (load(pBassDecay_,  1.4f),
                                     load(pMidDecay_,   1.0f),
                                     load(pHfDecay_,    0.2f));
        reverbEngine_.setSize       (load(pSize_,       0.33f));
        reverbEngine_.setErLength   (load(pErLength_,   80.0f));
        reverbEngine_.setErDensity  (load(pErDensity_,  3000.0f));
    }
}

// ── Editor factory ────────────────────────────────────────────────────────────

juce::AudioProcessorEditor* ReverbPluginProcessor::createEditor()
{
    return new ReverbPluginEditor(*this);
}

// ── JUCE plugin entry point ───────────────────────────────────────────────────

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ReverbPluginProcessor();
}
