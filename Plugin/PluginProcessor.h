#pragma once

#include <JuceHeader.h>
#include "DSP/ReverbEngine.h"

/**
 * ReverbPluginProcessor — JUCE shell around Dattorro ReverbEngine.
 *
 * Five APVTS parameters are read each processBlock via cached atomic pointers
 * and forwarded to ReverbEngine setters.
 */
class ReverbPluginProcessor : public juce::AudioProcessor
{
public:
    ReverbPluginProcessor();
    ~ReverbPluginProcessor() override = default;

    void prepareToPlay  (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock   (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool   acceptsMidi()  const override { return false; }
    bool   producesMidi() const override { return false; }
    bool   isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    int  getNumPrograms()                            override { return 1; }
    int  getCurrentProgram()                         override { return 0; }
    void setCurrentProgram(int)                      override {}
    const juce::String getProgramName(int)           override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    static constexpr const char* kParamTime      = "time";
    static constexpr const char* kParamSize       = "size";
    static constexpr const char* kParamDamping     = "damping";
    static constexpr const char* kParamPreDelay    = "preDelay";
    static constexpr const char* kParamMix         = "mix";

private:
    ReverbEngine reverbEngine_;
    std::vector<float> monoScratchR_;

    std::atomic<float>* pTime_      = nullptr;
    std::atomic<float>* pSize_      = nullptr;
    std::atomic<float>* pDamping_   = nullptr;
    std::atomic<float>* pPreDelay_  = nullptr;
    std::atomic<float>* pMix_       = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverbPluginProcessor)
};
