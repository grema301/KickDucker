#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "EQDisplay.h"

class KickDuckerAudioProcessorEditor : public juce::AudioProcessorEditor,
                                        public juce::Timer
{
public:
    explicit KickDuckerAudioProcessorEditor(KickDuckerAudioProcessor&);
    ~KickDuckerAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    KickDuckerAudioProcessor& audioProcessor;

    //==========================================================================
    // EQ display — centrepiece visual
    EQDisplayComponent eqDisplay;

    //==========================================================================
    // DETECTION group
    juce::Slider smoothingSlider;
    juce::Label  smoothingLabel;
    juce::ToggleButton noteQuantizeButton { "Snap to Note" };
    juce::TextButton   freqLockButton     { "LOCK" };

    //==========================================================================
    // DYNAMICS group
    juce::Slider attackSlider;
    juce::Label  attackLabel;
    juce::Slider releaseSlider;
    juce::Label  releaseLabel;

    //==========================================================================
    // EQ group
    juce::Slider attenuationSlider;
    juce::Label  attenuationLabel;
    juce::Slider qWidthSlider;
    juce::Label  qWidthLabel;
    juce::Slider harmonicAmtSlider;
    juce::Label  harmonicAmtLabel;

    //==========================================================================
    // OUTPUT group
    juce::Slider mixSlider;
    juce::Label  mixLabel;
    juce::Slider outputGainSlider;
    juce::Label  outputGainLabel;

    //==========================================================================
    // Header controls
    juce::TextButton bypassButton { "BYPASS" };

    //==========================================================================
    // APVTS attachments — declared after the controls they attach to,
    // so they are destroyed first (RAII destruction order)
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> smoothingAttachment;
    std::unique_ptr<SliderAttachment> attackAttachment;
    std::unique_ptr<SliderAttachment> releaseAttachment;
    std::unique_ptr<SliderAttachment> attenuationAttachment;
    std::unique_ptr<SliderAttachment> qWidthAttachment;
    std::unique_ptr<SliderAttachment> harmonicAmtAttachment;
    std::unique_ptr<SliderAttachment> mixAttachment;
    std::unique_ptr<SliderAttachment> outputGainAttachment;
    std::unique_ptr<ButtonAttachment> noteQuantizeAttachment;
    std::unique_ptr<ButtonAttachment> freqLockAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;

    //==========================================================================
    // Cached display state — updated each timer tick from atomics
    bool  sidechainIsActive = false;
    float lastAppliedGainDB = 0.0f;
    float sidechainLevelDB  = -80.0f; // For header level meter

    // Weak reference to processor — becomes nullptr if processor is destroyed
    // before the editor. Guards timerCallback against the FL disable crash.
    juce::WeakReference<KickDuckerAudioProcessor> processorRef;

    //==========================================================================
    // Helpers
    void setupRotary(juce::Slider& slider, juce::Label& label,
                     const juce::String& labelText,
                     const juce::String& suffix = "");

    void setupToggle(juce::ToggleButton& button);

    void drawSectionLabel(juce::Graphics& g,
                          const juce::String& text,
                          juce::Rectangle<int> bounds);

    // Drain the processor's spectrum FIFO and push samples to the EQ display
    void drainSpectrumFifo();

    //==========================================================================
    // Layout constants — single source of truth used in both resized() and paint()
    static constexpr int kHeaderH      = 40;
    static constexpr int kDisplayH     = 200;
    static constexpr int kSectionLabelH= 14;
    static constexpr int kKnobH        = 90;
    static constexpr int kLabelH       = 14;
    static constexpr int kToggleH      = 22;
    static constexpr int kMargin       = 10;
    static constexpr int kGroupPad     = 8;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KickDuckerAudioProcessorEditor)
};