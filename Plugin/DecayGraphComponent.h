#pragma once
#include <JuceHeader.h>
#include <array>

/**
 * @file  DecayGraphComponent.h
 *
 * Interactive frequency–dependent-decay editor.
 *
 * Replaces the three Bass/Mid/HF decay knobs with a visual curve drawn on a
 * log-frequency canvas.  Three draggable control nodes live at the fixed
 * frequencies used by AbsorptionBank (250 Hz, 1.5 kHz, 5 kHz).  Dragging a
 * node up increases the T60 multiplier for that band; dragging down shortens it.
 *
 * The component writes directly to the APVTS parameters ("bassDecay",
 * "midDecay", "hfDecay") via setValueNotifyingHost() and listens to them so
 * that it stays in sync with DAW automation and preset recall.
 */
class DecayGraphComponent : public juce::Component,
                            private juce::AudioProcessorValueTreeState::Listener
{
public:
    DecayGraphComponent();
    ~DecayGraphComponent() override;

    /** Must be called once before addAndMakeVisible().  Registers APVTS listeners. */
    void setApvts(juce::AudioProcessorValueTreeState& apvts);

    // ── Component overrides ───────────────────────────────────────────────────
    void paint(juce::Graphics& g) override;
    void resized() override {}

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

private:
    // ── APVTS listener ────────────────────────────────────────────────────────
    void parameterChanged(const juce::String& paramId, float newValue) override;

    // ── Coordinate mapping ────────────────────────────────────────────────────
    /** Convert frequency [20, 20000] Hz to component X in pixels. */
    float freqToX(float freqHz) const;
    /** Convert T60 multiplier to component Y in pixels (higher mult = top). */
    float multToY(float mult) const;
    /** Inverse of multToY. */
    float yToMult(float y) const;

    /** Piecewise-linear interpolation of T60 mult across the 3 nodes (log-freq). */
    float getMultAt(float freqHz) const;

    /** Screen-space centre of node idx. */
    juce::Point<float> getNodeCentre(int idx) const;

    /** Returns the node index if pos is within kHitRadius, else -1. */
    int hitTest(juce::Point<float> pos) const;

    // ── Drawing helpers ───────────────────────────────────────────────────────
    void drawGrid(juce::Graphics& g) const;
    void drawCurve(juce::Graphics& g) const;
    void drawNodes(juce::Graphics& g) const;
    void drawLabels(juce::Graphics& g) const;

    // ── State ─────────────────────────────────────────────────────────────────
    juce::AudioProcessorValueTreeState* apvts_ = nullptr;

    // Three control-point multipliers.  Index 0 = bass (250 Hz),
    // 1 = mid (1.5 kHz), 2 = high (5 kHz).
    std::array<float, 3> mults_ = { 1.4f, 1.0f, 0.2f };

    int draggingIdx_ = -1;
    int hoverIdx_    = -1;

    // ── Constants ─────────────────────────────────────────────────────────────
    static constexpr float  kFreqMin  = 20.0f;
    static constexpr float  kFreqMax  = 20000.0f;
    static constexpr float  kMultMin  = 0.05f;
    static constexpr float  kMultMax  = 3.0f;
    static constexpr float  kHitRadius = 10.0f; // px — mouse hit area

    // Fixed frequencies matching AbsorptionBank band centres
    static constexpr std::array<float, 3> kFreqs = { 250.0f, 1500.0f, 5000.0f };

    // APVTS parameter IDs for the three nodes
    static constexpr std::array<const char*, 3> kParamIds =
        { "bassDecay", "midDecay", "hfDecay" };

    // Visual node colours (warm→neutral→cool)
    static const std::array<juce::Colour, 3> kNodeColours;

    // Padding inside the component for axes / labels
    static constexpr float kPadLeft   = 30.0f;
    static constexpr float kPadRight  = 10.0f;
    static constexpr float kPadTop    = 10.0f;
    static constexpr float kPadBottom = 22.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecayGraphComponent)
};
