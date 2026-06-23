#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

// ── Colours ───────────────────────────────────────────────────────────────────
namespace Col
{
    static constexpr juce::uint32 background = 0xFF16181D;
    static constexpr juce::uint32 panel      = 0xFF1F2229;
    static constexpr juce::uint32 accent     = 0xFF5B8DFE; // cool blue
    static constexpr juce::uint32 accentWarm = 0xFFD07B4A; // warm amber (Decay row)
    static constexpr juce::uint32 textMain   = 0xFFE8E8EE;
    static constexpr juce::uint32 textDim    = 0xFF808090;
    static constexpr juce::uint32 knobTrack  = 0xFF2E3140;
    static constexpr juce::uint32 knobThumb  = 0xFF5B8DFE;
    static constexpr juce::uint32 knobWarm   = 0xFFD07B4A;
}

// ── Custom LookAndFeel ────────────────────────────────────────────────────────
namespace
{

struct ReverbLookAndFeel : public juce::LookAndFeel_V4
{
    juce::uint32 thumbColour = Col::knobThumb;

    ReverbLookAndFeel()
    {
        setColour(juce::Slider::rotarySliderFillColourId,    juce::Colour(Col::knobThumb));
        setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(Col::knobTrack));
        setColour(juce::Slider::thumbColourId,               juce::Colour(Col::accent));
        setColour(juce::Slider::textBoxTextColourId,         juce::Colour(Col::textDim));
        setColour(juce::Slider::textBoxOutlineColourId,      juce::Colour(0x00000000));
        setColour(juce::Slider::textBoxBackgroundColourId,   juce::Colour(0x00000000));
        setColour(juce::Label::textColourId,                 juce::Colour(Col::textMain));
    }

    void drawRotarySlider(juce::Graphics& g,
                          int x, int y, int width, int height,
                          float sliderPosProportional,
                          float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<float>(float(x), float(y),
                                                   float(width), float(height))
                                .reduced(6.0f);
        const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();

        // Pick accent colour based on slider tag (warm for Decay row)
        const bool isWarm = (slider.getProperties().contains("warm"));
        const juce::Colour fillCol = isWarm ? juce::Colour(Col::knobWarm)
                                             : juce::Colour(Col::knobThumb);
        const juce::Colour dotCol  = isWarm ? juce::Colour(Col::accentWarm)
                                             : juce::Colour(Col::accent);

        // Track arc
        juce::Path track;
        track.addCentredArc(cx, cy, radius, radius, 0.0f,
                            rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(juce::Colour(Col::knobTrack));
        g.strokePath(track, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

        // Value arc
        const float angle = rotaryStartAngle + sliderPosProportional
                          * (rotaryEndAngle - rotaryStartAngle);
        juce::Path value;
        value.addCentredArc(cx, cy, radius, radius, 0.0f,
                            rotaryStartAngle, angle, true);
        g.setColour(fillCol);
        g.strokePath(value, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

        // Thumb dot
        const float thumbX = cx + (radius - 4.0f)
                           * std::cos(angle - juce::MathConstants<float>::halfPi);
        const float thumbY = cy + (radius - 4.0f)
                           * std::sin(angle - juce::MathConstants<float>::halfPi);
        g.setColour(dotCol);
        g.fillEllipse(thumbX - 4.0f, thumbY - 4.0f, 8.0f, 8.0f);

        // Centre cap
        g.setColour(juce::Colour(Col::panel));
        g.fillEllipse(cx - radius * 0.55f, cy - radius * 0.55f,
                      radius * 1.1f, radius * 1.1f);
    }
};

static ReverbLookAndFeel gLnF;

} // namespace

// ── LabelledKnob helpers ──────────────────────────────────────────────────────

void ReverbPluginEditor::LabelledKnob::init(APVTS& apvts,
                                            const char* paramId,
                                            const juce::String& displayName,
                                            juce::Component* parent)
{
    slider.setLookAndFeel(&gLnF);
    parent->addAndMakeVisible(slider);

    label.setText(displayName, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::Font(11.0f));
    label.setColour(juce::Label::textColourId, juce::Colour(Col::textDim));
    parent->addAndMakeVisible(label);

    attachment = std::make_unique<APVTS::SliderAttachment>(apvts, paramId, slider);
}

// ── Destructor ────────────────────────────────────────────────────────────────

ReverbPluginEditor::~ReverbPluginEditor()
{
    for (LabelledKnob* k : { &kReverbTime_, &kSize_, &kPreDelay_, &kDistance_, &kMasterWet_,
                              &kModDepth_,  &kStereoWidth_, &kErLength_, &kErDensity_,
                              &kBassDecay_, &kMidDecay_, &kHfDecay_ })
        k->slider.setLookAndFeel(nullptr);
}

// ── Constructor ───────────────────────────────────────────────────────────────

ReverbPluginEditor::ReverbPluginEditor(ReverbPluginProcessor& proc)
    : AudioProcessorEditor(proc), processor_(proc)
{
    auto& apvts = proc.apvts;
    using P = ReverbPluginProcessor;

    // ── Row A: Main ───────────────────────────────────────────────────────────
    kReverbTime_.init(apvts, P::kParamReverbTime,  "Reverb Time", this);
    kSize_      .init(apvts, P::kParamSize,        "Room Size",   this);
    kPreDelay_  .init(apvts, P::kParamPreDelay,    "Pre-Delay",   this);
    kDistance_  .init(apvts, P::kParamDistance,    "Distance",    this);
    kMasterWet_ .init(apvts, P::kParamMasterWet,   "Wet Mix",     this);

    // ── Row B: Character ──────────────────────────────────────────────────────
    kModDepth_    .init(apvts, P::kParamModDepth,    "Mod Depth",    this);
    kStereoWidth_ .init(apvts, P::kParamStereoWidth, "Width",        this);
    kErLength_    .init(apvts, P::kParamErLength,    "ER Length",    this);
    kErDensity_   .init(apvts, P::kParamErDensity,   "ER Density",   this);

    // ── Row C: Decay shape (warm colour to signal "shape" not length) ─────────
    auto initWarm = [&](LabelledKnob& k, const char* id, const juce::String& name)
    {
        k.init(apvts, id, name, this);
        k.slider.getProperties().set("warm", true);
    };
    initWarm(kBassDecay_, P::kParamBassDecay, "Bass");
    initWarm(kMidDecay_,  P::kParamMidDecay,  "Mid");
    initWarm(kHfDecay_,   P::kParamHfDecay,   "High");

    // ── Section headers ───────────────────────────────────────────────────────
    auto initHeader = [this](juce::Label& lbl, const juce::String& text, juce::uint32 col)
    {
        lbl.setText(text, juce::dontSendNotification);
        lbl.setFont(juce::Font(12.0f).boldened());
        lbl.setColour(juce::Label::textColourId, juce::Colour(col));
        lbl.setJustificationType(juce::Justification::left);
        addAndMakeVisible(lbl);
    };
    initHeader(labelMain_,  "MAIN",         Col::accent);
    initHeader(labelChar_,  "CHARACTER",    Col::accent);
    initHeader(labelDecay_, "DECAY SHAPE",  Col::accentWarm);

    setSize(720, 520);
}

// ── paint ─────────────────────────────────────────────────────────────────────

void ReverbPluginEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(Col::background));

    // Header strip
    g.setColour(juce::Colour(Col::panel));
    g.fillRect(0, 0, getWidth(), 44);
    g.setColour(juce::Colour(Col::accent));
    g.fillRect(0, 0, 4, 44);

    g.setFont(juce::Font(20.0f).boldened());
    g.setColour(juce::Colour(Col::textMain));
    g.drawText("PRO REVERB", 14, 0, 240, 44, juce::Justification::centredLeft);

    g.setFont(juce::Font(11.0f));
    g.setColour(juce::Colour(Col::textDim));
    g.drawText("Phase 5 - v0.5.0", getWidth() - 140, 0, 130, 44,
               juce::Justification::centredRight);

    // Panel backgrounds per section
    const auto panelCol = juce::Colour(Col::panel);
    g.setColour(panelCol);
    g.fillRoundedRectangle(10.0f,  54.0f, float(getWidth() - 20), 130.0f, 6.0f); // Main
    g.fillRoundedRectangle(10.0f, 194.0f, float(getWidth() - 20), 130.0f, 6.0f); // Character
    g.fillRoundedRectangle(10.0f, 334.0f, float(getWidth() - 20), 130.0f, 6.0f); // Decay

    // Warm accent stripe on Decay section
    g.setColour(juce::Colour(Col::accentWarm).withAlpha(0.6f));
    g.fillRoundedRectangle(10.0f, 334.0f, 4.0f, 130.0f, 2.0f);
}

// ── resized ───────────────────────────────────────────────────────────────────

void ReverbPluginEditor::resized()
{
    const int W = getWidth();
    constexpr int kKnobW  = 84;
    constexpr int kKnobH  = 80;
    constexpr int kLabelH = 18;
    constexpr int kHdrH   = 16;

    // ── Row A: 5 knobs ────────────────────────────────────────────────────────
    {
        const int sectionY = 54;
        const int knobY    = sectionY + kHdrH + 6;
        labelMain_.setBounds(18, sectionY + 4, 80, kHdrH);

        const int spacing = W / 5;
        auto place = [&](LabelledKnob& k, int col)
        {
            const int cx = col * spacing + spacing / 2;
            k.slider.setBounds(cx - kKnobW / 2, knobY,          kKnobW, kKnobH);
            k.label .setBounds(cx - kKnobW / 2, knobY + kKnobH, kKnobW, kLabelH);
        };
        place(kReverbTime_, 0);
        place(kSize_,       1);
        place(kPreDelay_,   2);
        place(kDistance_,   3);
        place(kMasterWet_,  4);
    }

    // ── Row B: 4 knobs ────────────────────────────────────────────────────────
    {
        const int sectionY = 194;
        const int knobY    = sectionY + kHdrH + 6;
        labelChar_.setBounds(18, sectionY + 4, 120, kHdrH);

        const int spacing = W / 4;
        auto place = [&](LabelledKnob& k, int col)
        {
            const int cx = col * spacing + spacing / 2;
            k.slider.setBounds(cx - kKnobW / 2, knobY,          kKnobW, kKnobH);
            k.label .setBounds(cx - kKnobW / 2, knobY + kKnobH, kKnobW, kLabelH);
        };
        place(kModDepth_,    0);
        place(kStereoWidth_, 1);
        place(kErLength_,    2);
        place(kErDensity_,   3);
    }

    // ── Row C: 3 decay-shape knobs ────────────────────────────────────────────
    {
        const int sectionY = 334;
        const int knobY    = sectionY + kHdrH + 6;
        labelDecay_.setBounds(18, sectionY + 4, 140, kHdrH);

        const int spacing = W / 3;
        auto place = [&](LabelledKnob& k, int col)
        {
            const int cx = col * spacing + spacing / 2;
            k.slider.setBounds(cx - kKnobW / 2, knobY,          kKnobW, kKnobH);
            k.label .setBounds(cx - kKnobW / 2, knobY + kKnobH, kKnobW, kLabelH);
        };
        place(kBassDecay_, 0);
        place(kMidDecay_,  1);
        place(kHfDecay_,   2);
    }
}
