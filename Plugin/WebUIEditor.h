#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

/**
 * @file  WebUIEditor.h
 *
 * Hosts the modern HTML/CSS/JS plugin UI inside a JUCE WebBrowserComponent.
 *
 * ── Architecture ─────────────────────────────────────────────────────────────
 *
 *   JS → C++   "juce://action?key=val"  navigation (sub-frame trick)
 *               intercepted in pageAboutToLoad(); navigation cancelled;
 *               parameter set via APVTS.
 *
 *   C++ → JS   evaluateJavascript("window._juceUpdate('id', value)")
 *               called from a juce::Timer at ~60 fps to sync APVTS values.
 *
 * ── Supported juce:// actions ────────────────────────────────────────────────
 *   setParam    id=<paramId>   value=<float>   — set one APVTS parameter
 *   loadPreset  idx=<int>                      — load factory preset
 *   advancedView open=<0|1>                    — (no-op; UI manages its own size)
 *
 * ── Size ─────────────────────────────────────────────────────────────────────
 *   720 × 480 px  (fixed; matches the web layout at 1 : 1 device pixels).
 */
class WebUIEditor : public juce::AudioProcessorEditor,
                    private juce::Timer
{
public:
    explicit WebUIEditor(ReverbPluginProcessor& proc);
    ~WebUIEditor() override;

    void resized() override;

    // Called from the WebBrowserComponent subclass to handle juce:// URLs
    void handleJuceURL(const juce::String& url);

    // Called by tests / host to push a value to the web view
    void pushParamToJS(const juce::String& paramId, float value);

private:
    // ── Nested WebBrowserComponent ────────────────────────────────────────────
    // Intercepts juce:// URLs in pageAboutToLoad() without leaving the page.
    struct Browser : public juce::WebBrowserComponent
    {
        explicit Browser(WebUIEditor& owner) : owner_(owner) {}

        bool pageAboutToLoad(const juce::String& newURL) override;
        void pageFinishedLoading(const juce::String& url) override;

        WebUIEditor& owner_;
    };

    // ── Helpers ───────────────────────────────────────────────────────────────
    /** Parse a juce:// URL and return a map of query-string key→value. */
    static std::map<juce::String,juce::String> parseQuery(const juce::String& url);

    /** Write HTML to a temp file and load it; called once on construction. */
    void loadUI();

    // ── Timer: push APVTS → JS at ~60 fps ────────────────────────────────────
    void timerCallback() override;

    // ── Members ───────────────────────────────────────────────────────────────
    ReverbPluginProcessor&    processor_;
    PresetManager             presetManager_;
    Browser                   browser_;

    // Shadow copies — only push to JS when a value actually changed
    std::map<juce::String, float> lastPushed_;

    // Temp HTML file path
    juce::File tempHtmlFile_;

    // Is the page fully loaded and ready to receive evaluateJavascript calls?
    bool pageReady_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WebUIEditor)
};
