#include "WebUIEditor.h"
#include "WebUIContent.h"

using P = ReverbPluginProcessor;

namespace
{

const juce::Colour kWebViewBackground { 0xff080b12 };

static const char* const kAllParamIds[] =
{
    P::kParamTime, P::kParamSize, P::kParamDamping,
    P::kParamPreDelay, P::kParamMix,
};

juce::WebBrowserComponent::Options makeWebBrowserOptions(WebUIEditor& editor)
{
    auto userDataFolder =
        juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("TiaVerb_WebView2_Data");

    userDataFolder.createDirectory();

    return juce::WebBrowserComponent::Options{}
        .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
        .withWinWebView2Options(
            juce::WebBrowserComponent::Options::WinWebView2{}
                .withUserDataFolder(userDataFolder)
                .withBackgroundColour(kWebViewBackground))
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withNativeIntegrationEnabled(true)
        .withEventListener("setParameter",
            [&editor](const juce::var& payload)
            {
                editor.handleSetParameterFromWeb(payload);
            })
        .withEventListener("loadPreset",
            [&editor](const juce::var& payload)
            {
                editor.handleLoadPresetFromWeb(payload);
            });
}

void setApvtsParameter(ReverbPluginProcessor& proc,
                       const juce::String& paramId,
                       float denormalisedValue)
{
    if (auto* param = dynamic_cast<juce::RangedAudioParameter*>(
            proc.apvts.getParameter(paramId)))
    {
        param->setValueNotifyingHost(param->convertTo0to1(denormalisedValue));
    }
}

} // namespace

// ── Browser ───────────────────────────────────────────────────────────────────
WebUIEditor::Browser::Browser(WebUIEditor& owner)
    : juce::WebBrowserComponent([&owner]
    {
        const auto options = makeWebBrowserOptions(owner);

       #if JUCE_WINDOWS
        jassert(juce::WebBrowserComponent::areOptionsSupported(options));
       #endif

        return options;
    }()),
      owner_(owner)
{
}

bool WebUIEditor::Browser::pageAboutToLoad(const juce::String& newURL)
{
    if (newURL.startsWith("juce://"))
    {
        owner_.handleJuceURL(newURL);
        return false;
    }
    return true;
}

void WebUIEditor::Browser::pageFinishedLoading(const juce::String& /*url*/)
{
    owner_.pageReady_ = true;
    owner_.pushAllParamsToJS();
}

// ── WebUIEditor ───────────────────────────────────────────────────────────────
WebUIEditor::WebUIEditor(ReverbPluginProcessor& proc)
    : AudioProcessorEditor(proc),
      processor_(proc),
      presetManager_(proc.apvts),
      browser_(*this)
{
    addAndMakeVisible(browser_);
    setSize(720, 480);

    for (const char* id : kAllParamIds)
    {
        lastPushed_[id] = -1e9f;
        processor_.apvts.addParameterListener(id, this);
    }

    loadUI();
}

WebUIEditor::~WebUIEditor()
{
    for (const char* id : kAllParamIds)
        processor_.apvts.removeParameterListener(id, this);

    if (tempHtmlFile_.existsAsFile())
        tempHtmlFile_.deleteFile();
}

void WebUIEditor::resized()
{
    browser_.setBounds(getLocalBounds());
}

juce::String WebUIEditor::normaliseParamId(const juce::String& id)
{
    const auto lower = id.toLowerCase();

    if (lower == "time" || lower == "reverbtime") return P::kParamTime;
    if (lower == "size")                          return P::kParamSize;
    if (lower == "damping")                       return P::kParamDamping;
    if (lower == "predelay" || lower == "pre-dly" || lower == "pre_delay")
        return P::kParamPreDelay;
    if (lower == "mix" || lower == "masterwet" || lower == "wet")
        return P::kParamMix;

    return lower;
}

void WebUIEditor::loadUI()
{
    tempHtmlFile_ = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("tiaverb_ui.html");

    const juce::String html = juce::String::fromUTF8(kWebUIHTML);
    juce::MemoryOutputStream mos;
    static constexpr char kUtf8Bom[] = { '\xEF', '\xBB', '\xBF' };
    mos.write(kUtf8Bom, sizeof(kUtf8Bom));
    mos.write(html.toRawUTF8(), (size_t) html.getNumBytesAsUTF8());
    tempHtmlFile_.replaceWithData(mos.getData(), mos.getDataSize());

    browser_.goToURL(juce::URL(tempHtmlFile_).toString(false));
}

void WebUIEditor::handleSetParameterFromWeb(const juce::var& payload)
{
    if (! payload.isObject())
        return;

    const auto* obj = payload.getDynamicObject();
    if (obj == nullptr)
        return;

    const auto paramId = normaliseParamId(obj->getProperty("paramId").toString());
    const float value  = static_cast<float>(obj->getProperty("value"));

    setApvtsParameter(processor_, paramId, value);
    lastPushed_[paramId] = value;
}

void WebUIEditor::handleLoadPresetFromWeb(const juce::var& payload)
{
    if (! payload.isObject())
        return;

    const auto* obj = payload.getDynamicObject();
    if (obj == nullptr)
        return;

    if (obj->hasProperty("name"))
        presetManager_.loadPresetByName(obj->getProperty("name").toString());
    else
        presetManager_.loadPreset(static_cast<int>(obj->getProperty("idx")));

    pushAllParamsToJS();
}

void WebUIEditor::handleJuceURL(const juce::String& url)
{
    const auto withoutScheme = url.substring(7);
    const int  qPos          = withoutScheme.indexOfChar('?');
    const auto action        = qPos >= 0 ? withoutScheme.substring(0, qPos) : withoutScheme;
    auto params              = parseQuery(url);

    if (action == "setParam")
    {
        const auto idIt = params.find("id");
        const auto valIt = params.find("value");
        if (idIt == params.end() || valIt == params.end())
            return;

        const auto id   = normaliseParamId(idIt->second);
        const float val = valIt->second.getFloatValue();
        setApvtsParameter(processor_, id, val);
        lastPushed_[id] = val;
    }
    else if (action == "loadPreset")
    {
        const auto nameIt = params.find("name");
        if (nameIt != params.end())
            presetManager_.loadPresetByName(nameIt->second);
        else if (const auto idxIt = params.find("idx"); idxIt != params.end())
            presetManager_.loadPreset(idxIt->second.getIntValue());

        pushAllParamsToJS();
    }
}

void WebUIEditor::parameterChanged(const juce::String& parameterID, float newValue)
{
    if (std::abs(newValue - lastPushed_[parameterID]) <= 1e-6f)
        return;

    lastPushed_[parameterID] = newValue;
    pushParamUpdateToJS(parameterID, newValue);
}

void WebUIEditor::pushParamUpdateToJS(const juce::String& paramId, float value)
{
    if (! pageReady_)
        return;

    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("paramId", paramId);
    obj->setProperty("value", value);

    browser_.emitEventIfBrowserIsVisible(juce::Identifier("parameterUpdate"), juce::var(obj.get()));
}

void WebUIEditor::pushAllParamsToJS()
{
    if (! pageReady_)
        return;

    for (const char* id : kAllParamIds)
    {
        if (auto* raw = processor_.apvts.getRawParameterValue(id))
        {
            const float current = raw->load();
            lastPushed_[id] = current;
            pushParamUpdateToJS(id, current);
        }
    }
}

std::map<juce::String, juce::String> WebUIEditor::parseQuery(const juce::String& url)
{
    std::map<juce::String, juce::String> out;

    const int qPos = url.indexOfChar('?');
    if (qPos < 0)
        return out;

    for (const auto& pair : juce::StringArray::fromTokens(url.substring(qPos + 1), "&", ""))
    {
        const int eqPos = pair.indexOfChar('=');
        if (eqPos > 0)
        {
            out[juce::URL::removeEscapeChars(pair.substring(0, eqPos))] =
                juce::URL::removeEscapeChars(pair.substring(eqPos + 1));
        }
    }

    return out;
}
