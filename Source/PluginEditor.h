#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "EQDisplay.h"
#include "KickDuckerUI.h"

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
    // The whole UI is laid out in a fixed 760 x 520 "design space" inside
    // `content`, and resized() simply scales that component. One set of
    // constants, no per-control resize maths, fonts and strokes scale with it.
    static constexpr int kBaseW   = 760;
    static constexpr int kBaseH   = 520;
    static constexpr int kMargin  = 14;
    static constexpr int kHeaderH = 52;

    static constexpr int kDisplayY = 60;
    static constexpr int kDisplayH = 284;
    static constexpr int kCardsY   = kDisplayY + kDisplayH + 10;   // 354
    static constexpr int kCardsH   = 152;
    static constexpr int kCardGap  = 10;
    static constexpr int kUnit     = 89;     // width of one knob column

    // Cards, left to right (signal flow): TRACKING -> NOTCH -> OUTPUT
    static constexpr int kCardKnobs[3] = { 3, 3, 2 };
    static juce::Rectangle<int> cardBounds(int index);

    //==========================================================================
    // Order matters: the look-and-feel must outlive every component that uses it,
    // and children must be destroyed before the `content` component they live in.
    KickDuckerLookAndFeel laf;
    juce::TooltipWindow   tooltipWindow { this, 600 };

    struct Content : public juce::Component
    {
        void paint(juce::Graphics&) override;
    } content;

    EQDisplayComponent eqDisplay;
    SidechainMeter     sidechainMeter;

    //==========================================================================
    // TRACKING card
    juce::Slider smoothingSlider;   juce::Label smoothingLabel;
    juce::Slider attackSlider;      juce::Label attackLabel;
    juce::Slider releaseSlider;     juce::Label releaseLabel;

    // NOTCH card
    juce::Slider attenuationSlider; juce::Label attenuationLabel;
    juce::Slider qWidthSlider;      juce::Label qWidthLabel;
    juce::Slider harmonicAmtSlider; juce::Label harmonicAmtLabel;

    // OUTPUT card
    juce::Slider mixSlider;         juce::Label mixLabel;
    juce::Slider outputGainSlider;  juce::Label outputGainLabel;

    // Toggle chips
    juce::TextButton noteQuantizeButton { "SNAP" };
    juce::TextButton freqLockButton     { "LOCK" };
    juce::TextButton bypassButton       { "BYPASS" };

    //==========================================================================
    // APVTS attachments - declared after the controls they attach to,
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
    // Parameter pointers cached once (the timer used to do string lookups 30x/s)
    std::atomic<float>* pAttenuation  = nullptr;
    std::atomic<float>* pQ            = nullptr;
    std::atomic<float>* pHarmonicAmt  = nullptr;
    std::atomic<float>* pFreqLock     = nullptr;

    // Weak reference to processor - becomes nullptr if the processor is destroyed
    // before the editor. Guards timerCallback against the FL disable crash.
    juce::WeakReference<KickDuckerAudioProcessor> processorRef;

    //==========================================================================
    enum class KnobStyle { Unipolar, Bipolar, Inverted };

    void setupRotary(juce::Slider& slider, juce::Label& label,
                     const juce::String& labelText, const juce::String& tooltip,
                     KnobStyle style = KnobStyle::Unipolar);

    void setupChip(juce::TextButton& button, juce::Colour onColour, const juce::String& tooltip);

    void placeKnob(int card, int slot, juce::Slider& slider, juce::Label& label);

    // Drain the processor's spectrum FIFO and push samples to the EQ display
    void drainSpectrumFifo();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KickDuckerAudioProcessorEditor)
};