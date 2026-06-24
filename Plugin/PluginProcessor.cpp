#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "WebUIEditor.h"

#include <algorithm>

juce::AudioProcessorValueTreeState::ParameterLayout
ReverbPluginProcessor::createParameterLayout()
{
    using Param = juce::AudioParameterFloat;
    using Range = juce::NormalisableRange<float>;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto logRange = [](float lo, float hi, float centre) -> Range
    {
        Range r { lo, hi };
        r.setSkewForCentre(centre);
        return r;
    };

    params.push_back(std::make_unique<Param>(
        kParamTime, "Time",
        logRange(0.1f, 20.0f, 2.5f), 2.5f,
        juce::AudioParameterFloatAttributes{}.withLabel("s")));

    params.push_back(std::make_unique<Param>(
        kParamSize, "Size",
        Range{ 0.0f, 1.0f, 0.001f }, 0.33f));

    params.push_back(std::make_unique<Param>(
        kParamDamping, "Damping",
        logRange(500.0f, 20000.0f, 8000.0f), 8000.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("Hz")));

    params.push_back(std::make_unique<Param>(
        kParamPreDelay, "Pre-Delay",
        Range{ 0.0f, 200.0f, 0.1f }, 0.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("ms")));

    params.push_back(std::make_unique<Param>(
        kParamMix, "Mix",
        Range{ 0.0f, 1.0f, 0.001f }, 0.35f));

    return { params.begin(), params.end() };
}

ReverbPluginProcessor::ReverbPluginProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
}

void ReverbPluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    reverbEngine_.prepare(sampleRate, std::max(1, samplesPerBlock));

    pTime_     = apvts.getRawParameterValue(kParamTime);
    pSize_     = apvts.getRawParameterValue(kParamSize);
    pDamping_  = apvts.getRawParameterValue(kParamDamping);
    pPreDelay_ = apvts.getRawParameterValue(kParamPreDelay);
    pMix_      = apvts.getRawParameterValue(kParamMix);

    monoScratchR_.resize(static_cast<std::size_t>(std::max(1, samplesPerBlock)));

    auto load = [](std::atomic<float>* p, float fallback)
    { return p ? p->load(std::memory_order_relaxed) : fallback; };

    reverbEngine_.setTime      (load(pTime_,     2.5f));
    reverbEngine_.setSize      (load(pSize_,     0.33f));
    reverbEngine_.setDamping   (load(pDamping_,  8000.0f));
    reverbEngine_.setPreDelayMs(load(pPreDelay_, 0.0f));
    reverbEngine_.setMix       (load(pMix_,      0.35f));
}

void ReverbPluginProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& /*midi*/)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels == 0)
        return;

    auto load = [](std::atomic<float>* p, float fallback)
    { return p ? p->load(std::memory_order_relaxed) : fallback; };

    reverbEngine_.setTime      (load(pTime_,     2.5f));
    reverbEngine_.setSize      (load(pSize_,     0.33f));
    reverbEngine_.setDamping   (load(pDamping_,  8000.0f));
    reverbEngine_.setPreDelayMs(load(pPreDelay_, 0.0f));
    reverbEngine_.setMix       (load(pMix_,      0.35f));

    if (numChannels >= 2)
    {
        float* L = buffer.getWritePointer(0);
        float* R = buffer.getWritePointer(1);
        reverbEngine_.processBlock(L, R, numSamples);
    }
    else
    {
        jassert(numSamples <= static_cast<int>(monoScratchR_.size()));
        float* mono = buffer.getWritePointer(0);
        std::copy(mono, mono + numSamples, monoScratchR_.begin());
        reverbEngine_.processBlock(mono, monoScratchR_.data(), numSamples);
        for (int i = 0; i < numSamples; ++i)
            mono[i] = 0.5f * (mono[i] + monoScratchR_[static_cast<std::size_t>(i)]);
    }
}

void ReverbPluginProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty("version", 3, nullptr);
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, dest);
}

void ReverbPluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));

    if (pTime_)
    {
        auto load = [](std::atomic<float>* p, float f)
        { return p ? p->load(std::memory_order_relaxed) : f; };

        reverbEngine_.setTime      (load(pTime_,     2.5f));
        reverbEngine_.setSize      (load(pSize_,     0.33f));
        reverbEngine_.setDamping   (load(pDamping_,  8000.0f));
        reverbEngine_.setPreDelayMs(load(pPreDelay_, 0.0f));
        reverbEngine_.setMix       (load(pMix_,      0.35f));
    }
}

juce::AudioProcessorEditor* ReverbPluginProcessor::createEditor()
{
    return new WebUIEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ReverbPluginProcessor();
}
