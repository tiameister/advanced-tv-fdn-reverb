#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PresetManager.h"

/**
 * Web UI editor with bulletproof 2-way APVTS binding.
 *
 * JS → C++  WebView2 postMessage / __JUCE__.backend.emitEvent("setParameter", …)
 * C++ → JS  emitEvent("parameterUpdate", …) driven by APVTS::Listener
 */
class WebUIEditor : public juce::AudioProcessorEditor,
                    private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit WebUIEditor(ReverbPluginProcessor& proc);
    ~WebUIEditor() override;

    void resized() override;

    void handleJuceURL(const juce::String& url);
    void handleSetParameterFromWeb(const juce::var& payload);
    void handleLoadPresetFromWeb(const juce::var& payload);

    void pushParamUpdateToJS(const juce::String& paramId, float value);
    void pushAllParamsToJS();

    static juce::String normaliseParamId(const juce::String& id);

private:
    struct Browser : public juce::WebBrowserComponent
    {
        explicit Browser(WebUIEditor& owner);

        bool pageAboutToLoad(const juce::String& newURL) override;
        void pageFinishedLoading(const juce::String& url) override;

        WebUIEditor& owner_;
    };

    static std::map<juce::String, juce::String> parseQuery(const juce::String& url);
    void loadUI();

    void parameterChanged(const juce::String& parameterID, float newValue) override;

    ReverbPluginProcessor&    processor_;
    PresetManager             presetManager_;
    Browser                   browser_;

    std::map<juce::String, float> lastPushed_;
    juce::File tempHtmlFile_;
    bool pageReady_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WebUIEditor)
};
