#include "DecayGraphComponent.h"
#include <cmath>

// ── Static member definitions ─────────────────────────────────────────────────
const std::array<juce::Colour, 3> DecayGraphComponent::kNodeColours =
{
    juce::Colour (0xFFE8A44A), // bass  — amber / warm
    juce::Colour (0xFF60A8D4), // mid   — neutral blue
    juce::Colour (0xFF4AD4C0), // high  — cool cyan
};

// ── Constructor / Destructor ──────────────────────────────────────────────────
DecayGraphComponent::DecayGraphComponent()
{
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

DecayGraphComponent::~DecayGraphComponent()
{
    if (apvts_)
    {
        for (const auto* id : kParamIds)
            apvts_->removeParameterListener(id, this);
    }
}

void DecayGraphComponent::setApvts(juce::AudioProcessorValueTreeState& apvts)
{
    apvts_ = &apvts;
    for (const auto* id : kParamIds)
        apvts_->addParameterListener(id, this);

    // Sync initial values from APVTS
    const char* ids[] = { "bassDecay", "midDecay", "hfDecay" };
    for (int i = 0; i < 3; ++i)
    {
        if (auto* param = apvts_->getRawParameterValue(ids[i]))
            mults_[static_cast<std::size_t>(i)] = param->load();
    }
}

// ── APVTS listener ────────────────────────────────────────────────────────────
void DecayGraphComponent::parameterChanged(const juce::String& paramId, float newValue)
{
    if (paramId == "bassDecay") mults_[0] = newValue;
    else if (paramId == "midDecay")  mults_[1] = newValue;
    else if (paramId == "hfDecay")   mults_[2] = newValue;

    // Repaint on the message thread — parameterChanged is dispatched there by JUCE
    repaint();
}

// ── Coordinate mapping ────────────────────────────────────────────────────────
float DecayGraphComponent::freqToX(float freqHz) const
{
    const float logMin = std::log10(kFreqMin);
    const float logMax = std::log10(kFreqMax);
    const float t      = (std::log10(freqHz) - logMin) / (logMax - logMin);
    return kPadLeft + t * (float(getWidth()) - kPadLeft - kPadRight);
}

float DecayGraphComponent::multToY(float mult) const
{
    // Clamp to avoid log(0)
    mult = std::clamp(mult, kMultMin, kMultMax);
    const float t = (mult - kMultMin) / (kMultMax - kMultMin);
    // t=1 (max mult) → top of canvas; t=0 (min) → bottom
    return kPadTop + (1.0f - t) * (float(getHeight()) - kPadTop - kPadBottom);
}

float DecayGraphComponent::yToMult(float y) const
{
    const float usable = float(getHeight()) - kPadTop - kPadBottom;
    const float t      = 1.0f - (y - kPadTop) / usable;
    return std::clamp(kMultMin + t * (kMultMax - kMultMin), kMultMin, kMultMax);
}

float DecayGraphComponent::getMultAt(float freqHz) const
{
    const float logF  = std::log10(freqHz);
    const float logF0 = std::log10(kFreqs[0]);
    const float logF1 = std::log10(kFreqs[1]);
    const float logF2 = std::log10(kFreqs[2]);

    if (logF <= logF0) return mults_[0];
    if (logF >= logF2) return mults_[2];

    if (logF <= logF1)
    {
        const float t = (logF - logF0) / (logF1 - logF0);
        return mults_[0] + t * (mults_[1] - mults_[0]);
    }
    const float t = (logF - logF1) / (logF2 - logF1);
    return mults_[1] + t * (mults_[2] - mults_[1]);
}

juce::Point<float> DecayGraphComponent::getNodeCentre(int idx) const
{
    return { freqToX(kFreqs[static_cast<std::size_t>(idx)]),
             multToY(mults_[static_cast<std::size_t>(idx)]) };
}

int DecayGraphComponent::hitTest(juce::Point<float> pos) const
{
    for (int i = 0; i < 3; ++i)
    {
        if (pos.getDistanceFrom(getNodeCentre(i)) <= kHitRadius)
            return i;
    }
    return -1;
}

// ── Drawing ───────────────────────────────────────────────────────────────────
void DecayGraphComponent::paint(juce::Graphics& g)
{
    // Background
    g.fillAll(juce::Colour(0xFF1A1C22));

    drawGrid(g);
    drawCurve(g);
    drawLabels(g);
    drawNodes(g);
}

void DecayGraphComponent::drawGrid(juce::Graphics& g) const
{
    const auto bounds = getLocalBounds().toFloat();
    const float top    = kPadTop;
    const float bottom = bounds.getHeight() - kPadBottom;

    g.setColour(juce::Colour(0xFF2A2D38));

    // Vertical frequency grid lines
    constexpr float kGridFreqs[] =
        { 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f };
    for (float f : kGridFreqs)
    {
        const float x = freqToX(f);
        g.drawLine(x, top, x, bottom, 1.0f);
    }

    // Horizontal multiplier reference lines
    constexpr float kGridMults[] = { 0.5f, 1.0f, 1.5f, 2.0f, 2.5f };
    for (float m : kGridMults)
    {
        const float y = multToY(m);
        g.setColour(m == 1.0f ? juce::Colour(0xFF3A3E50)   // RT reference = brighter
                               : juce::Colour(0xFF262932));
        g.drawLine(kPadLeft, y, bounds.getWidth() - kPadRight, y, 1.0f);
    }

    // "1× RT" label on the reference line
    g.setColour(juce::Colour(0xFF505570));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f)));
    g.drawText("1\xd7 RT",
               juce::Rectangle<float>(kPadLeft, multToY(1.0f) - 10.f, 28.f, 10.f),
               juce::Justification::centredRight, false);
}

void DecayGraphComponent::drawCurve(juce::Graphics& g) const
{
    const float W      = float(getWidth());
    const float bottom = float(getHeight()) - kPadBottom;

    juce::Path fill, stroke;

    // Sample the curve at every 2 pixels across the usable width
    const int steps = std::max(2, int((W - kPadLeft - kPadRight) / 2.0f));
    const float freqMin  = kFreqMin;
    const float freqMax  = kFreqMax;
    const float logMin   = std::log10(freqMin);
    const float logRange = std::log10(freqMax) - logMin;

    for (int s = 0; s <= steps; ++s)
    {
        const float t    = float(s) / float(steps);
        const float freq = std::pow(10.0f, logMin + t * logRange);
        const float x    = freqToX(freq);
        const float y    = multToY(getMultAt(freq));

        if (s == 0)
        {
            fill.startNewSubPath(x, bottom);
            fill.lineTo(x, y);
            stroke.startNewSubPath(x, y);
        }
        else
        {
            fill.lineTo(x, y);
            stroke.lineTo(x, y);
        }
    }
    fill.lineTo(W - kPadRight, bottom);
    fill.closeSubPath();

    // Shaded fill — gradient warm→cool matching node colours
    juce::ColourGradient grad(juce::Colour(0x30E8A44A), kPadLeft,     0.f,
                               juce::Colour(0x304AD4C0), W - kPadRight, 0.f,
                               false);
    g.setGradientFill(grad);
    g.fillPath(fill);

    // Stroke line
    juce::ColourGradient strokeGrad(juce::Colour(0xCCE8A44A), kPadLeft,     0.f,
                                     juce::Colour(0xCC4AD4C0), W - kPadRight, 0.f,
                                     false);
    g.setGradientFill(strokeGrad);
    g.strokePath(stroke, juce::PathStrokeType(1.8f));
}

void DecayGraphComponent::drawNodes(juce::Graphics& g) const
{
    static const char* kLabels[] = { "BASS", "MID", "HIGH" };

    for (int i = 0; i < 3; ++i)
    {
        const auto  centre = getNodeCentre(i);
        const auto  col    = kNodeColours[static_cast<std::size_t>(i)];
        const bool  active = (draggingIdx_ == i) || (hoverIdx_ == i);
        const float radius = active ? kHitRadius + 2.0f : kHitRadius;

        // Outer glow on hover / drag
        if (active)
        {
            g.setColour(col.withAlpha(0.25f));
            g.fillEllipse(centre.x - radius - 4.f, centre.y - radius - 4.f,
                          (radius + 4.f) * 2.f, (radius + 4.f) * 2.f);
        }

        // Fill
        g.setColour(active ? col : col.darker(0.3f));
        g.fillEllipse(centre.x - radius, centre.y - radius,
                      radius * 2.f, radius * 2.f);

        // Outline
        g.setColour(juce::Colours::white.withAlpha(0.6f));
        g.drawEllipse(centre.x - radius, centre.y - radius,
                      radius * 2.f, radius * 2.f, 1.2f);

        // Value label
        juce::String valStr = juce::String(mults_[static_cast<std::size_t>(i)], 2) + "\xd7";
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f)));
        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.drawText(valStr,
                   juce::Rectangle<float>(centre.x - 18.f, centre.y - radius - 14.f, 36.f, 12.f),
                   juce::Justification::centred, false);

        // Band label below node
        g.setColour(col.withAlpha(0.7f));
        g.drawText(kLabels[i],
                   juce::Rectangle<float>(centre.x - 18.f, centre.y + radius + 2.f, 36.f, 10.f),
                   juce::Justification::centred, false);
    }
}

void DecayGraphComponent::drawLabels(juce::Graphics& g) const
{
    // Frequency axis labels at bottom
    struct FreqLabel { float freq; const char* text; };
    constexpr FreqLabel kLabels[] =
    {
        {  100.f,  "100" },
        {  500.f,  "500" },
        { 1000.f,  "1k"  },
        { 5000.f,  "5k"  },
        {10000.f,  "10k" },
    };

    g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f)));
    g.setColour(juce::Colour(0xFF505570));

    const float bottom = float(getHeight()) - kPadBottom + 4.f;
    for (const auto& l : kLabels)
    {
        const float x = freqToX(l.freq);
        g.drawText(l.text, juce::Rectangle<float>(x - 12.f, bottom, 24.f, 12.f),
                   juce::Justification::centred, false);
    }

    // Y-axis multiplier labels
    constexpr float kMLabels[] = { 0.5f, 1.0f, 2.0f, 3.0f };
    for (float m : kMLabels)
    {
        const float y   = multToY(m);
        const auto  str = juce::String(m, 1) + "\xd7";
        g.drawText(str, juce::Rectangle<float>(0.f, y - 6.f, kPadLeft - 4.f, 12.f),
                   juce::Justification::centredRight, false);
    }
}

// ── Mouse events ──────────────────────────────────────────────────────────────
void DecayGraphComponent::mouseDown(const juce::MouseEvent& e)
{
    draggingIdx_ = hitTest(e.position);
}

void DecayGraphComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (draggingIdx_ < 0 || !apvts_) return;

    const float newMult = std::clamp(yToMult(e.position.y), kMultMin, kMultMax);
    mults_[static_cast<std::size_t>(draggingIdx_)] = newMult;

    // Push value to APVTS so the audio thread picks it up
    const auto* id = kParamIds[static_cast<std::size_t>(draggingIdx_)];
    if (auto* param = dynamic_cast<juce::RangedAudioParameter*>(apvts_->getParameter(id)))
        param->setValueNotifyingHost(param->convertTo0to1(newMult));

    repaint();
}

void DecayGraphComponent::mouseUp(const juce::MouseEvent&)
{
    draggingIdx_ = -1;
    repaint();
}

void DecayGraphComponent::mouseMove(const juce::MouseEvent& e)
{
    const int newHover = hitTest(e.position);
    if (newHover != hoverIdx_)
    {
        hoverIdx_ = newHover;
        setMouseCursor(hoverIdx_ >= 0 ? juce::MouseCursor::UpDownResizeCursor
                                       : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void DecayGraphComponent::mouseExit(const juce::MouseEvent&)
{
    hoverIdx_ = -1;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();
}
