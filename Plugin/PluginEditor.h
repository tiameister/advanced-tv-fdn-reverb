#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

/**
 * ReverbPluginEditor
 * ───────────────────
 * Minimal functional UI — all 11 parameters as labelled rotary knobs,
 * organised into three logical groups.
 *
 * Layout (700 × 520 px)
 * ──────────────────────
 *   Header bar  : plugin name + version
 *   Row A       : Pre-Delay | Distance | Wet Mix        (3 knobs)
 *   Row B       : FDN Feedback | Mod Depth              (2 knobs)
 *   Row C       : Low Freq | Low T60 | Mid Freq | Mid T60 | Hi Freq | Hi T60 (6 knobs)
 *
 * Phase 5 (future): replace Row C with an interactive decay-curve display
 * showing T60 vs frequency (like FabFilter Pro-R).
 */
class ReverbPluginEditor : public juce::AudioProcessorEditor
{
public:
    explicit ReverbPluginEditor(ReverbPluginProcessor& proc);
    ~ReverbPluginEditor() override = default;

    void paint  (juce::Graphics& g) override;
    void resized() override;

private:
    ReverbPluginProcessor& processor_;

    // ── Knob helper ───────────────────────────────────────────────────────────
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

    // ── Knobs ─────────────────────────────────────────────────────────────────
    LabelledKnob kPreDelay_, kDistance_, kMasterWet_;
    LabelledKnob kFeedback_, kModDepth_;
    LabelledKnob kLowFreq_,  kLowT60_;
    LabelledKnob kMidFreq_,  kMidT60_;
    LabelledKnob kHighFreq_, kHighT60_;

    // ── Section header labels ─────────────────────────────────────────────────
    juce::Label labelGlobal_, labelFdn_, labelDecayEQ_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverbPluginEditor)
};
