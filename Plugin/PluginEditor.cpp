#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

// ── Colour palette ────────────────────────────────────────────────────────────
namespace Col
{
    static constexpr juce::uint32 background = 0xFF14161B;
    static constexpr juce::uint32 panel      = 0xFF1F2229;
    static constexpr juce::uint32 panelChar  = 0xFF1A1E28; // slightly cooler for Character
    static constexpr juce::uint32 accent     = 0xFF5B8DFE; // cool blue  — Main / Character
    static constexpr juce::uint32 accentWarm = 0xFFD07B4A; // warm amber — Decay graph section
    static constexpr juce::uint32 textMain   = 0xFFE8E8EE;
    static constexpr juce::uint32 textDim    = 0xFF6A6A80;
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
        setColour(juce::Slider::rotarySliderFillColourId,    juce::Colour(Col::knobThumb));
        setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(Col::knobTrack));
        setColour(juce::Slider::thumbColourId,               juce::Colour(Col::accent));
        setColour(juce::Slider::textBoxTextColourId,         juce::Colour(Col::textDim));
        setColour(juce::Slider::textBoxOutlineColourId,      juce::Colour(0x00000000));
        setColour(juce::Slider::textBoxBackgroundColourId,   juce::Colour(0x00000000));
        setColour(juce::Label::textColourId,                 juce::Colour(Col::textMain));

        // TextButton colours for the smart-view toggle
        setColour(juce::TextButton::buttonColourId,   juce::Colour(0xFF252835));
        setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF2D3550));
        setColour(juce::TextButton::textColourOffId,  juce::Colour(Col::accent));
        setColour(juce::TextButton::textColourOnId,   juce::Colour(Col::accent));
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

        const juce::Colour fillCol(Col::knobThumb);
        const juce::Colour dotCol(Col::accent);

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
        value.addCentredArc(cx, cy, radius, radius, 0.0f, rotaryStartAngle, angle, true);
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

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool, bool isButtonDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        auto col    = isButtonDown ? backgroundColour.brighter(0.1f) : backgroundColour;
        g.setColour(col);
        g.fillRoundedRectangle(bounds, 4.0f);
        g.setColour(juce::Colour(Col::accent).withAlpha(0.4f));
        g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
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
    label.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    label.setColour(juce::Label::textColourId, juce::Colour(Col::textDim));
    parent->addAndMakeVisible(label);

    attachment = std::make_unique<APVTS::SliderAttachment>(apvts, paramId, slider);
}

// ── Destructor ────────────────────────────────────────────────────────────────
ReverbPluginEditor::~ReverbPluginEditor()
{
    for (LabelledKnob* k : { &kReverbTime_, &kSize_, &kPreDelay_, &kProximity_, &kMasterWet_,
                              &kModDepth_,  &kSpace_, &kErLength_, &kErDensity_ })
        k->slider.setLookAndFeel(nullptr);

    presetBox_  .setLookAndFeel(nullptr);
    smartViewBtn_.setLookAndFeel(nullptr);
}

// ── Constructor ───────────────────────────────────────────────────────────────
ReverbPluginEditor::ReverbPluginEditor(ReverbPluginProcessor& proc)
    : AudioProcessorEditor(proc), processor_(proc),
      presetManager_(proc.apvts)
{
    auto& apvts = proc.apvts;
    using P = ReverbPluginProcessor;

    // ── Row A: Main ───────────────────────────────────────────────────────────
    // "Proximity" replaces the technical "Distance" label; APVTS ID unchanged.
    // "Wet Mix" is self-explanatory.
    kReverbTime_.init(apvts, P::kParamReverbTime, "Reverb Time", this);
    kSize_      .init(apvts, P::kParamSize,        "Room Size",   this);
    kPreDelay_  .init(apvts, P::kParamPreDelay,    "Pre-Delay",   this);
    kProximity_ .init(apvts, P::kParamDistance,    "Proximity",   this); // renamed
    kMasterWet_ .init(apvts, P::kParamMasterWet,   "Wet Mix",     this);

    // ── Row B: Character (advanced) ───────────────────────────────────────────
    // "Space" replaces "Stereo Width"; APVTS ID unchanged.
    kModDepth_  .init(apvts, P::kParamModDepth,    "Mod Depth",   this);
    kSpace_     .init(apvts, P::kParamStereoWidth,  "Space",       this); // renamed
    kErLength_  .init(apvts, P::kParamErLength,     "ER Length",   this);
    kErDensity_ .init(apvts, P::kParamErDensity,    "ER Density",  this);

    // Row B starts hidden (basic view)
    for (auto* k : { &kModDepth_, &kSpace_, &kErLength_, &kErDensity_ })
    {
        k->slider.setVisible(false);
        k->label .setVisible(false);
    }

    // ── Decay Graph (replaces 3-knob Row C) ───────────────────────────────────
    decayGraph_.setApvts(apvts);
    addAndMakeVisible(decayGraph_);

    // ── Section headers ───────────────────────────────────────────────────────
    auto initHdr = [this](juce::Label& lbl, const juce::String& text, juce::uint32 col)
    {
        lbl.setText(text, juce::dontSendNotification);
        lbl.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)).boldened());
        lbl.setColour(juce::Label::textColourId, juce::Colour(col));
        lbl.setJustificationType(juce::Justification::left);
        addAndMakeVisible(lbl);
    };
    initHdr(labelMain_,  "MAIN",       Col::accent);
    initHdr(labelChar_,  "CHARACTER",  Col::accent);
    initHdr(labelDecay_, "DECAY CURVE", Col::accentWarm);

    labelChar_.setVisible(false); // shown only in advanced mode

    // ── Preset picker ─────────────────────────────────────────────────────────
    presetBox_.addItem("-- Preset --", 1); // placeholder
    for (int i = 0; i < presetManager_.getNumPresets(); ++i)
        presetBox_.addItem(presetManager_.getPresetName(i), i + 2);
    presetBox_.setSelectedId(1, juce::dontSendNotification);
    presetBox_.setLookAndFeel(&gLnF);
    presetBox_.onChange = [this]
    {
        const int id = presetBox_.getSelectedId();
        if (id >= 2)
            presetManager_.loadPreset(id - 2);
    };
    addAndMakeVisible(presetBox_);

    // ── Smart-view toggle button ──────────────────────────────────────────────
    smartViewBtn_.setButtonText("CHARACTER  \xe2\x96\xbe"); // ▾ suffix
    smartViewBtn_.setLookAndFeel(&gLnF);
    smartViewBtn_.onClick = [this] { toggleAdvancedView(); };
    addAndMakeVisible(smartViewBtn_);

    setSize(kW, kHBasic);
}

// ── Smart-view toggle ─────────────────────────────────────────────────────────
void ReverbPluginEditor::toggleAdvancedView()
{
    advancedView_ = !advancedView_;

    // Show / hide Row B controls and label
    for (auto* k : { &kModDepth_, &kSpace_, &kErLength_, &kErDensity_ })
    {
        k->slider.setVisible(advancedView_);
        k->label .setVisible(advancedView_);
    }
    labelChar_.setVisible(advancedView_);

    // Update button label
    const juce::String arrow = advancedView_ ? " \xe2\x96\xb4" : " \xe2\x96\xbe"; // ▴ / ▾
    smartViewBtn_.setButtonText("CHARACTER" + arrow);

    // Resize the window
    setSize(kW, advancedView_ ? kHAdvanced : kHBasic);

    resized();
    repaint();
}

// ── paint ─────────────────────────────────────────────────────────────────────
void ReverbPluginEditor::paint(juce::Graphics& g)
{
    const int W = getWidth();
    const int H = getHeight();

    g.fillAll(juce::Colour(Col::background));

    // Header strip
    g.setColour(juce::Colour(Col::panel));
    g.fillRect(0, 0, W, kHeaderH);
    g.setColour(juce::Colour(Col::accent));
    g.fillRect(0, 0, 4, kHeaderH); // left accent bar

    g.setFont(juce::Font(juce::FontOptions{}.withHeight(20.0f)).boldened());
    g.setColour(juce::Colour(Col::textMain));
    g.drawText("TiaVerb", 14, 0, 220, kHeaderH, juce::Justification::centredLeft);

    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    g.setColour(juce::Colour(Col::textDim));
    g.drawText(JucePlugin_VersionString, W - 60, 0, 50, kHeaderH, juce::Justification::centredRight);

    // Row A panel
    const int rowAY = kHeaderH + kGap;
    g.setColour(juce::Colour(Col::panel));
    g.fillRoundedRectangle(10.0f, float(rowAY), float(W - 20), float(kRowH), 6.0f);
    g.setColour(juce::Colour(Col::accent).withAlpha(0.5f));
    g.fillRoundedRectangle(10.0f, float(rowAY), 4.0f, float(kRowH), 2.0f);

    // Row B panel (advanced mode only)
    if (advancedView_)
    {
        const int rowBY = rowAY + kRowH + kGap;
        g.setColour(juce::Colour(Col::panelChar));
        g.fillRoundedRectangle(10.0f, float(rowBY), float(W - 20), float(kRowH), 6.0f);
        g.setColour(juce::Colour(Col::accent).withAlpha(0.3f));
        g.fillRoundedRectangle(10.0f, float(rowBY), 4.0f, float(kRowH), 2.0f);
    }

    // Decay graph panel
    const int graphY = advancedView_ ? (kHeaderH + kGap + kRowH + kGap + kRowH + kGap)
                                     : (kHeaderH + kGap + kRowH + kGap);
    g.setColour(juce::Colour(Col::panel));
    g.fillRoundedRectangle(10.0f, float(graphY), float(W - 20), float(kGraphH), 6.0f);
    g.setColour(juce::Colour(Col::accentWarm).withAlpha(0.55f));
    g.fillRoundedRectangle(10.0f, float(graphY), 4.0f, float(kGraphH), 2.0f);

    (void)H; // used implicitly through setSize
}

// ── resized ───────────────────────────────────────────────────────────────────
void ReverbPluginEditor::resized()
{
    const int W = getWidth();
    layoutRows();

    // Preset picker — centred in header
    presetBox_.setBounds(W / 2 - 90, 9, 180, 26);

    // Smart-view button — top right of header
    smartViewBtn_.setBounds(W - 152, 8, 144, 28);
}

void ReverbPluginEditor::layoutRows()
{
    const int W = getWidth();
    constexpr int kKnobW  = 84;
    constexpr int kKnobH  = 80;
    constexpr int kLabelH = 18;
    constexpr int kHdrH   = 14;

    // ── Row A: 5 knobs ────────────────────────────────────────────────────────
    {
        const int sectionY = kHeaderH + kGap;
        const int knobY    = sectionY + kHdrH + 8;
        labelMain_.setBounds(18, sectionY + 5, 60, kHdrH);

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
        place(kProximity_,  3);
        place(kMasterWet_,  4);
    }

    // ── Row B: 4 knobs (advanced mode) ───────────────────────────────────────
    if (advancedView_)
    {
        const int sectionY = kHeaderH + kGap + kRowH + kGap;
        const int knobY    = sectionY + kHdrH + 8;
        labelChar_.setBounds(18, sectionY + 5, 120, kHdrH);

        const int spacing = W / 4;
        auto place = [&](LabelledKnob& k, int col)
        {
            const int cx = col * spacing + spacing / 2;
            k.slider.setBounds(cx - kKnobW / 2, knobY,          kKnobW, kKnobH);
            k.label .setBounds(cx - kKnobW / 2, knobY + kKnobH, kKnobW, kLabelH);
        };
        place(kModDepth_,  0);
        place(kSpace_,     1);
        place(kErLength_,  2);
        place(kErDensity_, 3);
    }

    // ── Decay graph ───────────────────────────────────────────────────────────
    {
        const int graphY = advancedView_
            ? (kHeaderH + kGap + kRowH + kGap + kRowH + kGap)
            : (kHeaderH + kGap + kRowH + kGap);

        // Header label above graph
        labelDecay_.setBounds(18, graphY + 5, 140, kHdrH);

        // Graph fills the rest of the panel (with insets for the label and padding)
        decayGraph_.setBounds(16, graphY + kHdrH + 10,
                              W - 32, kGraphH - kHdrH - 18);
    }
}
