#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

// ── Colours ───────────────────────────────────────────────────────────────────
namespace Col
{
    static constexpr juce::uint32 background = 0xFF16181D;
    static constexpr juce::uint32 panel      = 0xFF1F2229;
    static constexpr juce::uint32 accent     = 0xFF5B8DFE; // cool blue
    static constexpr juce::uint32 textMain   = 0xFFE8E8EE;
    static constexpr juce::uint32 textDim    = 0xFF808090;
    static constexpr juce::uint32 knobTrack  = 0xFF2E3140;
    static constexpr juce::uint32 knobThumb  = 0xFF5B8DFE;
}

// ── Custom LookAndFeel ────────────────────────────────────────────────────────
namespace
{
struct ReverbLookAndFeel : public juce::LookAndFeel_V4
{
    ReverbLookAndFeel()
    {
        setColour(juce::Slider::rotarySliderFillColourId,   juce::Colour(Col::knobThumb));
        setColour(juce::Slider::rotarySliderOutlineColourId,juce::Colour(Col::knobTrack));
        setColour(juce::Slider::thumbColourId,              juce::Colour(Col::accent));
        setColour(juce::Slider::textBoxTextColourId,        juce::Colour(Col::textDim));
        setColour(juce::Slider::textBoxOutlineColourId,     juce::Colour(0x00000000));
        setColour(juce::Slider::textBoxBackgroundColourId,  juce::Colour(0x00000000));
        setColour(juce::Label::textColourId,                juce::Colour(Col::textMain));
    }

    void drawRotarySlider(juce::Graphics& g,
                          int x, int y, int width, int height,
                          float sliderPosProportional,
                          float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& /*slider*/) override
    {
        const auto bounds = juce::Rectangle<float>((float)x, (float)y,
                                                   (float)width, (float)height)
                                .reduced(6.0f);
        const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();

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
        g.setColour(juce::Colour(Col::knobThumb));
        g.strokePath(value, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

        // Thumb dot
        const float thumbX = cx + (radius - 4.0f) * std::cos(angle - juce::MathConstants<float>::halfPi);
        const float thumbY = cy + (radius - 4.0f) * std::sin(angle - juce::MathConstants<float>::halfPi);
        g.setColour(juce::Colour(Col::accent));
        g.fillEllipse(thumbX - 4.0f, thumbY - 4.0f, 8.0f, 8.0f);

        // Centre cap
        g.setColour(juce::Colour(Col::panel));
        g.fillEllipse(cx - radius * 0.55f, cy - radius * 0.55f,
                      radius * 1.1f, radius * 1.1f);
    }
};

static ReverbLookAndFeel gLnF; // one shared instance for the editor
}

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
    label.setFont(juce::Font(11.0f));   // plain is the default — no flag needed
    label.setColour(juce::Label::textColourId, juce::Colour(Col::textDim));
    parent->addAndMakeVisible(label);

    attachment = std::make_unique<APVTS::SliderAttachment>(apvts, paramId, slider);
}

// ── Destructor ────────────────────────────────────────────────────────────────
// JUCE best practice: remove LookAndFeel pointer from every slider before the
// editor is torn down. gLnF is a file-scope static so it outlives editors, but
// clearing explicitly prevents JUCE assertion failures if the static order ever
// changes, and makes intentions clear to future readers.

ReverbPluginEditor::~ReverbPluginEditor()
{
    for (LabelledKnob* k : { &kPreDelay_, &kDistance_, &kMasterWet_,
                              &kFeedback_, &kModDepth_, &kStereoWidth_,
                              &kLowFreq_,  &kLowT60_,
                              &kMidFreq_,  &kMidT60_,
                              &kHighFreq_, &kHighT60_ })
        k->slider.setLookAndFeel(nullptr);
}

// ── Constructor ───────────────────────────────────────────────────────────────

ReverbPluginEditor::ReverbPluginEditor(ReverbPluginProcessor& proc)
    : AudioProcessorEditor(proc), processor_(proc)
{
    auto& apvts = proc.apvts;
    using P = ReverbPluginProcessor;

    kPreDelay_ .init(apvts, P::kParamPreDelay,  "Pre-Delay",   this);
    kDistance_ .init(apvts, P::kParamDistance,  "Distance",    this);
    kMasterWet_.init(apvts, P::kParamMasterWet, "Wet Mix",     this);
    kFeedback_    .init(apvts, P::kParamFeedback,    "Feedback",      this);
    kModDepth_    .init(apvts, P::kParamModDepth,    "Mod Depth",     this);
    kStereoWidth_ .init(apvts, P::kParamStereoWidth, "Stereo Width",  this);
    kLowFreq_  .init(apvts, P::kParamLowFreq,   "Low Freq",    this);
    kLowT60_   .init(apvts, P::kParamLowT60,    "Low T60",     this);
    kMidFreq_  .init(apvts, P::kParamMidFreq,   "Mid Freq",    this);
    kMidT60_   .init(apvts, P::kParamMidT60,    "Mid T60",     this);
    kHighFreq_ .init(apvts, P::kParamHighFreq,  "High Freq",   this);
    kHighT60_  .init(apvts, P::kParamHighT60,   "High T60",    this);

    // Section headers
    auto initHeader = [this](juce::Label& lbl, const juce::String& text)
    {
        lbl.setText(text, juce::dontSendNotification);
        lbl.setFont(juce::Font(12.0f).boldened());
        lbl.setColour(juce::Label::textColourId, juce::Colour(Col::accent));
        lbl.setJustificationType(juce::Justification::left);
        addAndMakeVisible(lbl);
    };
    initHeader(labelGlobal_,  "GLOBAL");
    initHeader(labelFdn_,     "FDN");
    initHeader(labelDecayEQ_, "DECAY EQ");

    setSize(700, 520);
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

    // Panel outlines for each section
    const auto panelColour = juce::Colour(Col::panel);
    g.setColour(panelColour);
    g.fillRoundedRectangle(10.0f,  54.0f,  getWidth() - 20.0f, 130.0f, 6.0f);
    g.fillRoundedRectangle(10.0f, 194.0f,  getWidth() - 20.0f, 130.0f, 6.0f);
    g.fillRoundedRectangle(10.0f, 334.0f,  getWidth() - 20.0f, 170.0f, 6.0f);
}

// ── resized ───────────────────────────────────────────────────────────────────

void ReverbPluginEditor::resized()
{
    const int W = getWidth();
    constexpr int kKnobW   = 90;
    constexpr int kKnobH   = 80;
    constexpr int kLabelH  = 18;
    constexpr int kHeaderH = 16;

    // ── Section A: Global (3 knobs) ───────────────────────────────────────────
    {
        const int sectionY  = 54;
        const int knobY     = sectionY + kHeaderH + 6;
        labelGlobal_.setBounds(18, sectionY + 4, 100, kHeaderH);

        const int spacing = W / 3;
        auto placeKnob = [&](LabelledKnob& k, int col)
        {
            const int cx = col * spacing + spacing / 2;
            k.slider.setBounds(cx - kKnobW / 2, knobY,         kKnobW, kKnobH);
            k.label .setBounds(cx - kKnobW / 2, knobY + kKnobH, kKnobW, kLabelH);
        };
        placeKnob(kPreDelay_,  0);
        placeKnob(kDistance_,  1);
        placeKnob(kMasterWet_, 2);
    }

    // ── Section B: FDN (3 knobs) ──────────────────────────────────────────────
    {
        const int sectionY = 194;
        const int knobY    = sectionY + kHeaderH + 6;
        labelFdn_.setBounds(18, sectionY + 4, 100, kHeaderH);

        const int spacing = W / 3;
        auto placeKnob = [&](LabelledKnob& k, int col)
        {
            const int cx = col * spacing + spacing / 2;
            k.slider.setBounds(cx - kKnobW / 2, knobY,          kKnobW, kKnobH);
            k.label .setBounds(cx - kKnobW / 2, knobY + kKnobH, kKnobW, kLabelH);
        };
        placeKnob(kFeedback_,    0);
        placeKnob(kModDepth_,    1);
        placeKnob(kStereoWidth_, 2);
    }

    // ── Section C: Decay EQ (6 knobs in 2 rows of 3) ─────────────────────────
    {
        const int sectionY = 334;
        const int row1Y    = sectionY + kHeaderH + 6;
        const int row2Y    = row1Y + kKnobH + kLabelH + 8;
        labelDecayEQ_.setBounds(18, sectionY + 4, 120, kHeaderH);

        const int spacing = W / 3;
        auto placeKnob = [&](LabelledKnob& k, int col, int rowY)
        {
            const int cx = col * spacing + spacing / 2;
            k.slider.setBounds(cx - kKnobW / 2, rowY,             kKnobW, kKnobH);
            k.label .setBounds(cx - kKnobW / 2, rowY + kKnobH,    kKnobW, kLabelH);
        };
        placeKnob(kLowFreq_,  0, row1Y);
        placeKnob(kMidFreq_,  1, row1Y);
        placeKnob(kHighFreq_, 2, row1Y);
        placeKnob(kLowT60_,   0, row2Y);
        placeKnob(kMidT60_,   1, row2Y);
        placeKnob(kHighT60_,  2, row2Y);
    }
}
