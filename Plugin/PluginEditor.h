#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

/**
 * ReverbPluginEditor
 * ───────────────────
 * Simplified FabFilter-style control surface — 12 parameters in three rows.
 *
 * Layout (720 × 520 px)
 * ──────────────────────
 *   Header bar    : plugin name + version
 *   Row A (Main)  : Reverb Time | Room Size | Pre-Delay | Distance | Wet Mix
 *   Row B (Char.) : Mod Depth | Stereo Width | ER Length | ER Density
 *   Row C (Decay) : Bass Decay | Mid Decay | HF Decay
 *
 * Row C encodes a simplified 3-point decay curve:
 *   bass/mid/hf multipliers × Reverb Time → per-band T60
 *   (Phase 5: replace Row C with an interactive draggable curve display)
 */
class ReverbPluginEditor : public juce::AudioProcessorEditor
{
public:
    explicit ReverbPluginEditor(ReverbPluginProcessor& proc);
    ~ReverbPluginEditor() override;

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

    // ── Row A: Main ───────────────────────────────────────────────────────────
    LabelledKnob kReverbTime_, kSize_, kPreDelay_, kDistance_, kMasterWet_;

    // ── Row B: Character ──────────────────────────────────────────────────────
    LabelledKnob kModDepth_, kStereoWidth_, kErLength_, kErDensity_;

    // ── Row C: Decay shape ────────────────────────────────────────────────────
    LabelledKnob kBassDecay_, kMidDecay_, kHfDecay_;

    // ── Section headers ───────────────────────────────────────────────────────
    juce::Label labelMain_, labelChar_, labelDecay_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverbPluginEditor)
};
