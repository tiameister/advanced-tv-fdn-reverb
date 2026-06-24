#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

void ReverbPluginEditor::LabelledKnob::init(APVTS& apvts,
                                            const char* paramId,
                                            const juce::String& displayName,
                                            juce::Component* parent)
{
    parent->addAndMakeVisible(slider);
    label.setText(displayName, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    parent->addAndMakeVisible(label);
    attachment = std::make_unique<APVTS::SliderAttachment>(apvts, paramId, slider);
}

ReverbPluginEditor::~ReverbPluginEditor() = default;

ReverbPluginEditor::ReverbPluginEditor(ReverbPluginProcessor& proc)
    : AudioProcessorEditor(proc), processor_(proc), presetManager_(proc.apvts)
{
    auto& apvts = proc.apvts;
    using P = ReverbPluginProcessor;

    kTime_.init(apvts, P::kParamTime,     "Time",      this);
    kSize_.init(apvts, P::kParamSize,     "Size",      this);
    kDamping_.init(apvts, P::kParamDamping, "Damping", this);
    kPreDelay_.init(apvts, P::kParamPreDelay, "Pre-Delay", this);
    kMix_.init(apvts, P::kParamMix,       "Mix",       this);

    presetBox_.addItem("-- Preset --", 1);
    for (int i = 0; i < presetManager_.getNumPresets(); ++i)
        presetBox_.addItem(presetManager_.getPresetName(i), i + 2);
    presetBox_.setSelectedId(1, juce::dontSendNotification);
    presetBox_.onChange = [this]
    {
        const int id = presetBox_.getSelectedId();
        if (id >= 2)
            presetManager_.loadPreset(id - 2);
    };
    addAndMakeVisible(presetBox_);

    setSize(kW, kH);
}

void ReverbPluginEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff14161b));
    g.setColour(juce::Colours::white);
    g.setFont(18.0f);
    g.drawText("TiaVerb", 12, 8, 120, 24, juce::Justification::left);
}

void ReverbPluginEditor::resized()
{
    presetBox_.setBounds(getWidth() / 2 - 90, 8, 180, 24);

    constexpr int knobW = 84;
    constexpr int knobH = 80;
    const int y = 40;
    const int spacing = getWidth() / 5;

    auto place = [&](LabelledKnob& k, int col)
    {
        const int cx = col * spacing + spacing / 2;
        k.slider.setBounds(cx - knobW / 2, y, knobW, knobH);
        k.label.setBounds(cx - knobW / 2, y + knobH, knobW, 18);
    };

    place(kTime_,     0);
    place(kSize_,     1);
    place(kDamping_,  2);
    place(kPreDelay_, 3);
    place(kMix_,      4);
}
