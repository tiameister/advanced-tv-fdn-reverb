#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PresetManager.h"

/** Native JUCE fallback editor (WebUIEditor is the default). */
class ReverbPluginEditor : public juce::AudioProcessorEditor
{
public:
    explicit ReverbPluginEditor(ReverbPluginProcessor& proc);
    ~ReverbPluginEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    ReverbPluginProcessor& processor_;

    struct LabelledKnob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag,
                              juce::Slider::TextBoxBelow };
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

        void init(juce::AudioProcessorValueTreeState& apvts,
                  const char* paramId,
                  const juce::String& displayName,
                  juce::Component* parent);
    };

    LabelledKnob kTime_, kSize_, kDamping_, kPreDelay_, kMix_;
    PresetManager  presetManager_;
    juce::ComboBox presetBox_;

    static constexpr int kW = 720;
    static constexpr int kH = 200;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverbPluginEditor)
};
