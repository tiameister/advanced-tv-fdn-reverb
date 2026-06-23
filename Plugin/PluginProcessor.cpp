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

    // Helper: logarithmic range with sensible skew
    auto logRange = [](float lo, float hi, float centre) -> Range
    {
        Range r { lo, hi };
        r.setSkewForCentre(centre);
        return r;
    };

    // ── Global controls ───────────────────────────────────────────────────────
    params.push_back(std::make_unique<Param>(
        kParamPreDelay, "Pre-Delay (ms)",
        Range{ 0.0f, 500.0f, 0.1f }, 0.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("ms")));

    params.push_back(std::make_unique<Param>(
        kParamDistance, "Distance",
        Range{ 0.0f, 1.0f, 0.001f }, 0.5f));

    params.push_back(std::make_unique<Param>(
        kParamMasterWet, "Wet Mix",
        Range{ 0.0f, 1.0f, 0.001f }, 1.0f));

    // ── FDN controls ──────────────────────────────────────────────────────────
    params.push_back(std::make_unique<Param>(
        kParamFeedback, "FDN Feedback",
        Range{ 0.0f, 0.99f, 0.001f }, 0.85f));

    params.push_back(std::make_unique<Param>(
        kParamModDepth, "Mod Depth (smp)",
        Range{ 0.0f, 2.0f, 0.01f }, 0.75f,
        juce::AudioParameterFloatAttributes{}.withLabel("smp")));

    // ── Decay EQ ──────────────────────────────────────────────────────────────
    params.push_back(std::make_unique<Param>(
        kParamLowFreq, "Low Shelf Freq",
        logRange(20.0f, 2000.0f, 200.0f), 250.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("Hz")));

    params.push_back(std::make_unique<Param>(
        kParamLowT60, "Low T60",
        logRange(0.1f, 20.0f, 3.0f), 3.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("s")));

    params.push_back(std::make_unique<Param>(
        kParamMidFreq, "Mid Freq",
        logRange(200.0f, 10000.0f, 1500.0f), 1500.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("Hz")));

    params.push_back(std::make_unique<Param>(
        kParamMidT60, "Mid T60",
        logRange(0.1f, 20.0f, 2.0f), 2.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("s")));

    params.push_back(std::make_unique<Param>(
        kParamHighFreq, "High Shelf Freq",
        logRange(1000.0f, 20000.0f, 5000.0f), 5000.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("Hz")));

    params.push_back(std::make_unique<Param>(
        kParamHighT60, "High T60",
        logRange(0.1f, 10.0f, 0.8f), 0.8f,
        juce::AudioParameterFloatAttributes{}.withLabel("s")));

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
                          80.0f,   // erLengthMs
                          0.0f,    // initialPreDelayMs
                          3000.0f, // erDensityHz
                          1.0f);   // erMinSpacingMs

    // Cache atomic pointers — getRawParameterValue is not lock-free on all
    // platforms, so we call it once here (non-audio thread) and cache.
    pPreDelay_  = apvts.getRawParameterValue(kParamPreDelay);
    pDistance_  = apvts.getRawParameterValue(kParamDistance);
    pMasterWet_ = apvts.getRawParameterValue(kParamMasterWet);
    pFeedback_  = apvts.getRawParameterValue(kParamFeedback);
    pModDepth_  = apvts.getRawParameterValue(kParamModDepth);
    pLowFreq_   = apvts.getRawParameterValue(kParamLowFreq);
    pLowT60_    = apvts.getRawParameterValue(kParamLowT60);
    pMidFreq_   = apvts.getRawParameterValue(kParamMidFreq);
    pMidT60_    = apvts.getRawParameterValue(kParamMidT60);
    pHighFreq_  = apvts.getRawParameterValue(kParamHighFreq);
    pHighT60_   = apvts.getRawParameterValue(kParamHighT60);

    // Invalidate shadows so setDecayEQ fires on the first processBlock
    prevLowFreq_ = prevLowT60_ = -1.0f;
    prevMidFreq_ = prevMidT60_ = -1.0f;
    prevHighFreq_ = prevHighT60_ = -1.0f;
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
    const float preDelay  = pPreDelay_  ? pPreDelay_ ->load(std::memory_order_relaxed) : 0.0f;
    const float distance  = pDistance_  ? pDistance_ ->load(std::memory_order_relaxed) : 0.5f;
    const float masterWet = pMasterWet_ ? pMasterWet_->load(std::memory_order_relaxed) : 1.0f;
    const float feedback  = pFeedback_  ? pFeedback_ ->load(std::memory_order_relaxed) : 0.85f;
    const float modDepth  = pModDepth_  ? pModDepth_ ->load(std::memory_order_relaxed) : 0.75f;

    const float lf = pLowFreq_  ? pLowFreq_ ->load(std::memory_order_relaxed) : 250.0f;
    const float lt = pLowT60_   ? pLowT60_  ->load(std::memory_order_relaxed) : 3.0f;
    const float mf = pMidFreq_  ? pMidFreq_ ->load(std::memory_order_relaxed) : 1500.0f;
    const float mt = pMidT60_   ? pMidT60_  ->load(std::memory_order_relaxed) : 2.0f;
    const float hf = pHighFreq_ ? pHighFreq_->load(std::memory_order_relaxed) : 5000.0f;
    const float ht = pHighT60_  ? pHighT60_ ->load(std::memory_order_relaxed) : 0.8f;

    // ── 2. Forward smooth-able params (internal one-pole smoothers absorb jumps)
    reverbEngine_.setPreDelayMs(preDelay);
    reverbEngine_.setDistance(distance);
    reverbEngine_.setMasterWet(masterWet);
    reverbEngine_.setFdnFeedback(feedback);
    reverbEngine_.setFdnModDepth(modDepth);

    // ── 3. Decay EQ — only recalculate when a parameter has changed ───────────
    // updateFilterCoefficients() uses pow/sin/cos × 16 channels; skip if idle.
    if (lf != prevLowFreq_ || lt != prevLowT60_ ||
        mf != prevMidFreq_ || mt != prevMidT60_ ||
        hf != prevHighFreq_ || ht != prevHighT60_)
    {
        reverbEngine_.setDecayEQ(lf, lt, mf, mt, hf, ht);
        prevLowFreq_  = lf;  prevLowT60_  = lt;
        prevMidFreq_  = mf;  prevMidT60_  = mt;
        prevHighFreq_ = hf;  prevHighT60_ = ht;
    }

    // ── 4. In-place stereo processing ────────────────────────────────────────
    if (numChannels >= 2)
    {
        float* L = buffer.getWritePointer(0);
        float* R = buffer.getWritePointer(1);
        reverbEngine_.processBlock(L, R, numSamples);
    }
    else
    {
        // Mono fallback: duplicate channel to fake stereo, then sum back
        float* mono = buffer.getWritePointer(0);
        std::vector<float> tmpR(static_cast<std::size_t>(numSamples));
        std::copy(mono, mono + numSamples, tmpR.begin());
        reverbEngine_.processBlock(mono, tmpR.data(), numSamples);
        for (int i = 0; i < numSamples; ++i)
            mono[i] = 0.5f * (mono[i] + tmpR[static_cast<std::size_t>(i)]);
    }
}

// ── State persistence ─────────────────────────────────────────────────────────

void ReverbPluginProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, dest);
}

void ReverbPluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
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
