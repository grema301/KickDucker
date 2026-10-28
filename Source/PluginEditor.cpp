#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::Rectangle<int> KickDuckerAudioProcessorEditor::cardBounds(int index)
{
    int x = kMargin;
    for (int i = 0; i < index; ++i)
        x += kCardKnobs[i] * kUnit + kCardGap;

    return { x, kCardsY, kCardKnobs[index] * kUnit, kCardsH };
}

//==============================================================================
KickDuckerAudioProcessorEditor::KickDuckerAudioProcessorEditor(KickDuckerAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    using namespace KDTheme;

    setLookAndFeel(&laf);

    addAndMakeVisible(content);
    content.addAndMakeVisible(eqDisplay);
    content.addAndMakeVisible(sidechainMeter);

    // --- TRACKING ---
    setupRotary(smoothingSlider, smoothingLabel, "SMOOTHING",
                "How slowly the notch glides to a newly detected pitch. Lower = snappier, higher = steadier.");
    setupRotary(attackSlider,  attackLabel,  "ATTACK",
                "How fast the notch digs in when the sidechain gets loud.");
    setupRotary(releaseSlider, releaseLabel, "RELEASE",
                "How fast the notch lets go once the sidechain falls away.");

    // --- NOTCH ---
    setupRotary(attenuationSlider, attenuationLabel, "DEPTH",
                "Maximum cut at the detected frequency (reached when the sidechain is at -12 dBFS or louder).",
                KnobStyle::Inverted);
    setupRotary(qWidthSlider, qWidthLabel, "Q",
                "Notch width. Higher Q = narrower notch.");
    setupRotary(harmonicAmtSlider, harmonicAmtLabel, "HARMONICS",
                "Also duck the 2nd, 3rd and 4th harmonics (70% / 50% / 30% of the Depth, scaled by this knob).");

    // --- OUTPUT ---
    setupRotary(mixSlider, mixLabel, "MIX",
                "Blend between the original signal (0%) and the ducked signal (100%).");
    setupRotary(outputGainSlider, outputGainLabel, "OUT GAIN",
                "Make-up gain to compensate for level lost to the notch.",
                KnobStyle::Bipolar);

    // --- Chips ---
    setupChip(noteQuantizeButton, accent, "Snap the detected pitch to the nearest note so the notch doesn't wobble.");
    setupChip(freqLockButton,     amber,  "Freeze the notch at its current frequency.");
    setupChip(bypassButton,       danger, "Bypass the effect (crossfades, so it won't click).");

    // --- APVTS Attachments ---
    attenuationAttachment  = std::make_unique<SliderAttachment>(audioProcessor.apvts, "ATTENUATION",  attenuationSlider);
    qWidthAttachment       = std::make_unique<SliderAttachment>(audioProcessor.apvts, "Q_WIDTH",       qWidthSlider);
    smoothingAttachment    = std::make_unique<SliderAttachment>(audioProcessor.apvts, "SMOOTHING",     smoothingSlider);
    attackAttachment       = std::make_unique<SliderAttachment>(audioProcessor.apvts, "ATTACK",        attackSlider);
    releaseAttachment      = std::make_unique<SliderAttachment>(audioProcessor.apvts, "RELEASE",       releaseSlider);
    harmonicAmtAttachment  = std::make_unique<SliderAttachment>(audioProcessor.apvts, "HARMONIC_AMT",  harmonicAmtSlider);
    mixAttachment          = std::make_unique<SliderAttachment>(audioProcessor.apvts, "MIX",           mixSlider);
    outputGainAttachment   = std::make_unique<SliderAttachment>(audioProcessor.apvts, "OUTPUT_GAIN",   outputGainSlider);
    noteQuantizeAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "NOTE_QUANTIZE", noteQuantizeButton);
    freqLockAttachment     = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "FREQ_LOCK",     freqLockButton);
    bypassAttachment       = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "BYPASS",        bypassButton);

    // Value text with units. This MUST come after the attachments: a SliderAttachment
    // installs its own text functions on the slider, which silently replaces
    // setTextValueSuffix() - that's why the old knobs showed numbers with no units.
    auto msText   = [](double v) { return juce::String(v, v < 100.0 ? 1 : 0) + " ms"; };
    auto pctText  = [](double v) { return juce::String((int) std::round(v * 100.0)) + "%"; };
    auto numParse = [](const juce::String& t) { return parseNumber(t); };
    auto pctParse = [](const juce::String& t) { return parseNumber(t) / 100.0; };

    smoothingSlider.textFromValueFunction   = [](double v) { return juce::String((int) std::round(v)) + " ms"; };
    attackSlider.textFromValueFunction      = msText;
    releaseSlider.textFromValueFunction     = msText;
    attenuationSlider.textFromValueFunction = [](double v) { return formatDB(v, 1); };
    qWidthSlider.textFromValueFunction      = [](double v) { return juce::String(v, 2); };
    harmonicAmtSlider.textFromValueFunction = pctText;
    mixSlider.textFromValueFunction         = pctText;
    outputGainSlider.textFromValueFunction  = [](double v) { return formatDB(v, 1, true); };

    for (auto* s : { &smoothingSlider, &attackSlider, &releaseSlider, &attenuationSlider,
                     &qWidthSlider, &outputGainSlider })
        s->valueFromTextFunction = numParse;
    harmonicAmtSlider.valueFromTextFunction = pctParse;
    mixSlider.valueFromTextFunction         = pctParse;

    for (auto* s : { &smoothingSlider, &attackSlider, &releaseSlider, &attenuationSlider,
                     &qWidthSlider, &harmonicAmtSlider, &mixSlider, &outputGainSlider })
        s->updateText();

    // --- Layout (design space, 760 x 520) ---
    eqDisplay.setBounds(kMargin, kDisplayY, kBaseW - kMargin * 2, kDisplayH);

    const int chipW = 66, chipH = 22;
    const int chipY = kDisplayY + (int) ((EQDisplayComponent::kTopStrip - (float) chipH) * 0.5f);
    const int lockX = kBaseW - kMargin - 14 - chipW;
    freqLockButton    .setBounds(lockX,                 chipY, chipW, chipH);
    noteQuantizeButton.setBounds(lockX - 6 - chipW,     chipY, chipW, chipH);

    sidechainMeter.setBounds(300, 11, 300, 30);
    bypassButton  .setBounds(kBaseW - kMargin - 88, 15, 88, 22);

    placeKnob(0, 0, smoothingSlider,   smoothingLabel);
    placeKnob(0, 1, attackSlider,      attackLabel);
    placeKnob(0, 2, releaseSlider,     releaseLabel);
    placeKnob(1, 0, attenuationSlider, attenuationLabel);
    placeKnob(1, 1, qWidthSlider,      qWidthLabel);
    placeKnob(1, 2, harmonicAmtSlider, harmonicAmtLabel);
    placeKnob(2, 0, mixSlider,         mixLabel);
    placeKnob(2, 1, outputGainSlider,  outputGainLabel);

    // --- Display interaction: drag the ring = Depth, scroll = Q ---
    // Each gesture is bracketed with begin/endChangeGesture so hosts record it as
    // proper automation (same as turning the knob).
    eqDisplay.onDragStart = [this]
    {
        if (processorRef == nullptr) return;
        if (auto* prm = audioProcessor.apvts.getParameter("ATTENUATION")) prm->beginChangeGesture();
    };
    eqDisplay.onDepthDragged = [this](float db)
    {
        if (processorRef == nullptr) return;
        if (auto* prm = audioProcessor.apvts.getParameter("ATTENUATION"))
            prm->setValueNotifyingHost(prm->convertTo0to1(db));
    };
    eqDisplay.onDragEnd = [this]
    {
        if (processorRef == nullptr) return;
        if (auto* prm = audioProcessor.apvts.getParameter("ATTENUATION")) prm->endChangeGesture();
    };
    eqDisplay.onQWheel = [this](float delta)
    {
        if (processorRef == nullptr) return;
        if (auto* prm = audioProcessor.apvts.getParameter("Q_WIDTH"))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, prm->getValue() + delta));
            prm->endChangeGesture();
        }
    };

    // --- Window: resizable, fixed aspect, scales the design space ---
    setResizable(true, true);
    setResizeLimits(kBaseW * 3 / 4, kBaseH * 3 / 4, kBaseW * 2, kBaseH * 2);
    getConstrainer()->setFixedAspectRatio((double) kBaseW / (double) kBaseH);
    setSize(kBaseW, kBaseH);

    // --- Hook up the live data ---
    pAttenuation = audioProcessor.apvts.getRawParameterValue("ATTENUATION");
    pQ           = audioProcessor.apvts.getRawParameterValue("Q_WIDTH");
    pHarmonicAmt = audioProcessor.apvts.getRawParameterValue("HARMONIC_AMT");
    pFreqLock    = audioProcessor.apvts.getRawParameterValue("FREQ_LOCK");

    // Start with a clean FIFO, then tell the audio thread we're listening.
    // (While no editor is open the processor skips writing to it altogether.)
    audioProcessor.spectrumFifo.reset();
    audioProcessor.spectrumActive.store(true);

    // The sliders were constructed (with their value text boxes) before our look-and-feel
    // was attached, so ask every child to rebuild itself with it.
    sendLookAndFeelChange();

    processorRef = &audioProcessor; // Must be last - enables crash-safe timer
    startTimerHz(30);
}

KickDuckerAudioProcessorEditor::~KickDuckerAudioProcessorEditor()
{
    stopTimer();

    // Nothing may call back into the processor from here on
    eqDisplay.onDragStart = nullptr;
    eqDisplay.onDepthDragged = nullptr;
    eqDisplay.onDragEnd = nullptr;
    eqDisplay.onQWheel = nullptr;

    if (processorRef != nullptr)
    {
        audioProcessor.spectrumActive.store(false);
    }
    else
    {
        // Some hosts (FL Studio's wrapper among them) can tear the processor down
        // BEFORE the editor, e.g. when a mixer slot is switched off while the plugin
        // window is open. The parameter attachments would then unregister themselves
        // from parameters that no longer exist and crash, so in that case we let go of
        // them without destroying them. It's a few hundred bytes, and only on that path.
        (void) smoothingAttachment.release();
        (void) attackAttachment.release();
        (void) releaseAttachment.release();
        (void) attenuationAttachment.release();
        (void) qWidthAttachment.release();
        (void) harmonicAmtAttachment.release();
        (void) mixAttachment.release();
        (void) outputGainAttachment.release();
        (void) noteQuantizeAttachment.release();
        (void) freqLockAttachment.release();
        (void) bypassAttachment.release();
    }

    setLookAndFeel(nullptr);
}

//==============================================================================
void KickDuckerAudioProcessorEditor::timerCallback()
{
    // CRASH FIX: if FL Studio has destroyed the processor (e.g. on plugin
    // disable), processorRef will be nullptr. Bail out immediately rather
    // than dereferencing the dangling audioProcessor reference.
    if (processorRef == nullptr) return;
    if (!isShowing()) return;

    const float freq      = audioProcessor.detectedFrequencyHz.load(std::memory_order_relaxed);
    const bool  scActive  = audioProcessor.sidechainActive.load(std::memory_order_relaxed) > 0.5f;
    const float appliedDB = audioProcessor.currentAppliedGainDB.load(std::memory_order_relaxed);
    const float levelDB   = audioProcessor.sidechainLevelDB.load(std::memory_order_relaxed);

    double sr = audioProcessor.getSampleRate();
    if (sr <= 0.0) sr = 44100.0;

    sidechainMeter.setState(levelDB, scActive);

    eqDisplay.setParameters(freq, pAttenuation->load(), pQ->load(), sr, scActive, appliedDB,
                            pHarmonicAmt->load(), pFreqLock->load() > 0.5f);

    drainSpectrumFifo();
    eqDisplay.repaint();
}

//==============================================================================
void KickDuckerAudioProcessorEditor::drainSpectrumFifo()
{
    const int available = audioProcessor.spectrumFifo.getNumReady();
    if (available <= 0) return;

    int s1, sz1, s2, sz2;
    audioProcessor.spectrumFifo.prepareToRead(available, s1, sz1, s2, sz2);
    if (sz1 > 0) eqDisplay.pushSamples(audioProcessor.spectrumFifoData.data() + s1, sz1);
    if (sz2 > 0) eqDisplay.pushSamples(audioProcessor.spectrumFifoData.data() + s2, sz2);
    audioProcessor.spectrumFifo.finishedRead(sz1 + sz2);
}

//==============================================================================
void KickDuckerAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(KDTheme::bgBottom); // covers any rounding gap at odd window sizes
}

void KickDuckerAudioProcessorEditor::resized()
{
    const float scale = (float) getWidth() / (float) kBaseW;
    content.setTransform(juce::AffineTransform::scale(scale));
    content.setBounds(0, 0, kBaseW, kBaseH);
}

//==============================================================================
void KickDuckerAudioProcessorEditor::Content::paint(juce::Graphics& g)
{
    using namespace KDTheme;

    // Background
    g.setGradientFill(juce::ColourGradient(bgTop, 0.0f, 0.0f, bgBottom, 0.0f, (float) kBaseH, false));
    g.fillAll();

    // Header
    g.setColour(headerBg);
    g.fillRect(0, 0, kBaseW, kHeaderH);
    g.setColour(border);
    g.drawHorizontalLine(kHeaderH - 1, 0.0f, (float) kBaseW);

    // Logo mark: a little notch curve in a rounded tile
    const juce::Rectangle<float> mark((float) kMargin, 12.0f, 28.0f, 28.0f);
    g.setColour(displayTop);
    g.fillRoundedRectangle(mark, 7.0f);
    g.setColour(accent.withAlpha(0.55f));
    g.drawRoundedRectangle(mark.reduced(0.5f), 7.0f, 1.0f);

    const auto m = mark.reduced(6.0f, 8.5f);
    juce::Path notch;
    notch.startNewSubPath(m.getX(), m.getY());
    notch.cubicTo(m.getX() + m.getWidth() * 0.30f, m.getY(),
                  m.getX() + m.getWidth() * 0.34f, m.getBottom(), m.getCentreX(), m.getBottom());
    notch.cubicTo(m.getX() + m.getWidth() * 0.66f, m.getBottom(),
                  m.getX() + m.getWidth() * 0.70f, m.getY(), m.getRight(), m.getY());
    g.setColour(accent);
    g.strokePath(notch, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Wordmark
    g.setFont(makeFont(15.0f, true, 0.14f));
    g.setColour(textPrimary);
    g.drawText("KICK DUCKER", 52, 9, 200, 20, juce::Justification::centredLeft);
    g.setFont(makeFont(9.0f, true, 0.16f));
    g.setColour(textDim);
    g.drawText("SIDECHAIN NOTCH   v0.3", 52, 29, 200, 12, juce::Justification::centredLeft);

    // Cards
    static const char* titles[3] = { "TRACKING", "NOTCH", "OUTPUT" };
    for (int i = 0; i < 3; ++i)
    {
        const auto r = cardBounds(i).toFloat();

        g.setGradientFill(juce::ColourGradient(cardTop, 0.0f, r.getY(), cardBottom, 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle(r, 10.0f);
        g.setColour(border);
        g.drawRoundedRectangle(r.reduced(0.5f), 10.0f, 1.0f);

        g.setColour(accent.withAlpha(0.85f));
        g.fillRoundedRectangle(r.getX() + 12.0f, r.getY() + 13.0f, 3.0f, 11.0f, 1.5f);
        g.setFont(makeFont(10.0f, true, 0.18f));
        g.setColour(textSecondary);
        g.drawText(titles[i], juce::Rectangle<float>(r.getX() + 22.0f, r.getY() + 8.0f, 140.0f, 20.0f),
                   juce::Justification::centredLeft);
    }
}

//==============================================================================
void KickDuckerAudioProcessorEditor::placeKnob(int card, int slot, juce::Slider& slider, juce::Label& label)
{
    const auto c = cardBounds(card);
    const int  x = c.getX() + slot * kUnit;

    label .setBounds(x,     c.getY() + 30, kUnit,     14);
    slider.setBounds(x + 4, c.getY() + 46, kUnit - 8, 96);
}

void KickDuckerAudioProcessorEditor::setupRotary(juce::Slider& slider, juce::Label& label,
                                                  const juce::String& labelText,
                                                  const juce::String& tooltip,
                                                  KnobStyle style)
{
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 76, 18);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                               juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity(180);
    slider.setTooltip(tooltip);
    slider.getProperties().set("bipolar", style == KnobStyle::Bipolar);
    slider.getProperties().set("invert",  style == KnobStyle::Inverted);
    content.addAndMakeVisible(slider);

    label.setText(labelText, juce::dontSendNotification);
    label.setFont(KDTheme::makeFont(10.0f, true, 0.14f));
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, KDTheme::textSecondary);
    label.setTooltip(tooltip);
    content.addAndMakeVisible(label);
}

void KickDuckerAudioProcessorEditor::setupChip(juce::TextButton& button, juce::Colour onColour,
                                                const juce::String& tooltip)
{
    button.setClickingTogglesState(true);
    button.setColour(juce::TextButton::buttonOnColourId, onColour);
    button.setTooltip(tooltip);
    content.addAndMakeVisible(button);
}