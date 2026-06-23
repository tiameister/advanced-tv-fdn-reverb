#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "DecayGraphComponent.h"
#include "PresetManager.h"

/**
 * ReverbPluginEditor
 * ───────────────────
 * FabFilter-style control surface with smart-view toggle and interactive
 * decay-curve editor.
 *
 * Layout — Basic mode  (720 × 410 px)
 * ─────────────────────────────────────
 *   Header bar   : name | version | "CHARACTER ▼" toggle
 *   Row A (Main) : Reverb Time | Room Size | Pre-Delay | Proximity | Wet Mix
 *   Decay Graph  : interactive 3-node log-frequency T60 curve
 *
 * Layout — Advanced mode  (720 × 560 px)
 * ────────────────────────────────────────
 *   + Row B (Character) : Mod Depth | Space | ER Length | ER Density
 *     (inserted between Row A and the graph; window expands vertically)
 *
 * The three Bass/Mid/HF decay parameters are wired directly to the
 * DecayGraphComponent via APVTS listeners — no separate knobs needed.
 */
class ReverbPluginEditor : public juce::AudioProcessorEditor
{
public:
    explicit ReverbPluginEditor(ReverbPluginProcessor& proc);
    ~ReverbPluginEditor() override;

    void paint  (juce::Graphics& g) override;
    void resized() override;

private:
    void toggleAdvancedView();
    void layoutRows();

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

    // ── Row A: Main (always visible) ──────────────────────────────────────────
    LabelledKnob kReverbTime_, kSize_, kPreDelay_, kProximity_, kMasterWet_;

    // ── Row B: Character (toggled by smart-view button) ───────────────────────
    LabelledKnob kModDepth_, kSpace_, kErLength_, kErDensity_;

    // ── Decay graph (replaces 3-knob Row C) ───────────────────────────────────
    DecayGraphComponent decayGraph_;

    // ── Section headers ───────────────────────────────────────────────────────
    juce::Label labelMain_, labelChar_, labelDecay_;

    // ── Preset selector ───────────────────────────────────────────────────────
    PresetManager    presetManager_;
    juce::ComboBox   presetBox_;

    // ── Smart-view toggle ─────────────────────────────────────────────────────
    juce::TextButton smartViewBtn_;
    bool             advancedView_ = false;

    // ── Layout constants ──────────────────────────────────────────────────────
    static constexpr int kW           = 720;
    static constexpr int kHeaderH     = 44;
    static constexpr int kGap         = 10;
    static constexpr int kRowH        = 140;
    static constexpr int kGraphH      = 190;
    static constexpr int kBottomPad   = 16;
    static constexpr int kHBasic      = kHeaderH + kGap + kRowH + kGap + kGraphH + kBottomPad;
    static constexpr int kHAdvanced   = kHeaderH + kGap + kRowH + kGap + kRowH + kGap + kGraphH + kBottomPad;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverbPluginEditor)
};
