#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
namespace Theme
{
    const juce::Colour background  { 0xff12121f };
    const juce::Colour headerBg    { 0xff0a0a15 };
    const juce::Colour panelBg     { 0xff191928 };
    const juce::Colour panelBorder { 0xff28284a };
    const juce::Colour accent      { 0xff00e5ff };
    const juce::Colour accentLock  { 0xffffaa00 };
    const juce::Colour textPrimary { 0xffffffff };
    const juce::Colour textSecond  { 0xff8888aa };
    const juce::Colour textDim     { 0xff444466 };
    const juce::Colour danger      { 0xffff4455 };
}

//==============================================================================
KickDuckerAudioProcessorEditor::KickDuckerAudioProcessorEditor(KickDuckerAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setSize(700, 500);
    setResizable(true, true);
    setResizeLimits(600, 440, 1200, 900);

    // --- EQ Display ---
    addAndMakeVisible(eqDisplay);

    // --- DETECTION ---
    setupRotary(smoothingSlider, smoothingLabel, "Smoothing", " ms");
    setupToggle(noteQuantizeButton);

    freqLockButton.setClickingTogglesState(true);
    freqLockButton.setColour(juce::TextButton::buttonColourId,   Theme::panelBg);
    freqLockButton.setColour(juce::TextButton::buttonOnColourId, Theme::accentLock.withAlpha(0.85f));
    freqLockButton.setColour(juce::TextButton::textColourOffId,  Theme::textSecond);
    freqLockButton.setColour(juce::TextButton::textColourOnId,   juce::Colours::black);
    addAndMakeVisible(freqLockButton);

    // --- DYNAMICS ---
    setupRotary(attackSlider,  attackLabel,  "Attack",  " ms");
    setupRotary(releaseSlider, releaseLabel, "Release", " ms");

    // --- EQ ---
    setupRotary(attenuationSlider, attenuationLabel, "Depth",     " dB");
    setupRotary(qWidthSlider,      qWidthLabel,      "Q Width",   "");
    setupRotary(harmonicAmtSlider, harmonicAmtLabel, "Harmonics", "");

    // --- OUTPUT ---
    setupRotary(mixSlider,        mixLabel,        "Mix",      "");
    setupRotary(outputGainSlider, outputGainLabel, "Out Gain", " dB");

    // --- Bypass ---
    bypassButton.setClickingTogglesState(true);
    bypassButton.setColour(juce::TextButton::buttonColourId,   Theme::panelBg);
    bypassButton.setColour(juce::TextButton::buttonOnColourId, Theme::danger.withAlpha(0.8f));
    bypassButton.setColour(juce::TextButton::textColourOffId,  Theme::textSecond);
    bypassButton.setColour(juce::TextButton::textColourOnId,   juce::Colours::white);
    addAndMakeVisible(bypassButton);

    // --- APVTS Attachments ---
    attenuationAttachment  = std::make_unique<SliderAttachment>(audioProcessor.apvts, "ATTENUATION",  attenuationSlider);
    qWidthAttachment       = std::make_unique<SliderAttachment>(audioProcessor.apvts, "Q_WIDTH",       qWidthSlider);
    smoothingAttachment    = std::make_unique<SliderAttachment>(audioProcessor.apvts, "SMOOTHING",     smoothingSlider);
    attackAttachment       = std::make_unique<SliderAttachment>(audioProcessor.apvts, "ATTACK",        attackSlider);
    releaseAttachment      = std::make_unique<SliderAttachment>(audioProcessor.apvts, "RELEASE",       releaseSlider);
    harmonicAmtAttachment  = std::make_unique<SliderAttachment>(audioProcessor.apvts, "HARMONIC_AMT", harmonicAmtSlider);
    mixAttachment          = std::make_unique<SliderAttachment>(audioProcessor.apvts, "MIX",           mixSlider);
    outputGainAttachment   = std::make_unique<SliderAttachment>(audioProcessor.apvts, "OUTPUT_GAIN",  outputGainSlider);
    noteQuantizeAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "NOTE_QUANTIZE",noteQuantizeButton);
    freqLockAttachment     = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "FREQ_LOCK",     freqLockButton);
    bypassAttachment       = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "BYPASS",        bypassButton);

    processorRef = &audioProcessor; // Must be last — enables crash-safe timer
    startTimerHz(30);
}

KickDuckerAudioProcessorEditor::~KickDuckerAudioProcessorEditor()
{
    stopTimer();
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
    const float scActive  = audioProcessor.sidechainActive.load(std::memory_order_relaxed);
    const float appliedDB = audioProcessor.currentAppliedGainDB.load(std::memory_order_relaxed);

    sidechainIsActive = (scActive > 0.5f);
    lastAppliedGainDB = appliedDB;
    sidechainLevelDB  = audioProcessor.sidechainLevelDB.load(std::memory_order_relaxed);

    const float gainDB      = audioProcessor.apvts.getRawParameterValue("ATTENUATION")->load();
    const float q           = audioProcessor.apvts.getRawParameterValue("Q_WIDTH")->load();
    const float harmonicAmt = audioProcessor.apvts.getRawParameterValue("HARMONIC_AMT")->load();
    const bool  locked      = audioProcessor.apvts.getRawParameterValue("FREQ_LOCK")->load() > 0.5f;

    double sr = audioProcessor.getSampleRate();
    if (sr <= 0.0) sr = 44100.0;

    eqDisplay.setParameters(freq, gainDB, q, sr, sidechainIsActive, appliedDB, harmonicAmt, locked);

    drainSpectrumFifo();

    eqDisplay.repaint();
    repaint(0, 0, getWidth(), kHeaderH); // Header only — sidechain dot
}

//==============================================================================
void KickDuckerAudioProcessorEditor::drainSpectrumFifo()
{
    int available = audioProcessor.spectrumFifo.getNumReady();
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
    const int w = getWidth();
    const int h = getHeight();

    // Background
    g.fillAll(Theme::background);

    // Header
    g.setColour(Theme::headerBg);
    g.fillRect(0, 0, w, kHeaderH);
    g.setColour(Theme::panelBorder);
    g.drawHorizontalLine(kHeaderH, 0.0f, (float)w);

    // Title
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 13.5f, juce::Font::bold));
    g.setColour(Theme::textPrimary);
    g.drawText("KICK  DUCKER", 0, 0, w, kHeaderH, juce::Justification::centred);

    // --- Sidechain section (header, left side) ---
    // Shows: label | activity dot | live RMS level meter
    // The meter tells the user instantly whether the sidechain is routed
    // and receiving signal, without needing to open the mixer routing panel.
    {
        const int scX      = 12;
        const int scY      = 8;
        const int meterX   = scX + 76;
        const int meterW   = 110;
        const int meterH   = 10;
        const int meterY   = scY + 12;

        // "SIDECHAIN" label
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::bold));
        g.setColour(Theme::textDim);
        g.drawText("SIDECHAIN", scX, scY, 72, 12, juce::Justification::centredLeft);

        // Activity dot
        juce::Colour dot = sidechainIsActive ? Theme::accent : Theme::textDim;
        g.setColour(dot);
        g.fillEllipse((float)(scX + 74), (float)(scY + 1), 9.0f, 9.0f);
        if (sidechainIsActive)
        {
            g.setColour(dot.withAlpha(0.2f));
            g.fillEllipse((float)(scX + 71), (float)(scY - 2), 15.0f, 15.0f);
        }

        // Level meter bar
        // Background track
        g.setColour(juce::Colour(0xff0a0a14));
        g.fillRoundedRectangle((float)meterX, (float)meterY,
                               (float)meterW, (float)meterH, 2.0f);
        g.setColour(juce::Colour(0xff1a1a30));
        g.drawRoundedRectangle((float)meterX + 0.5f, (float)meterY + 0.5f,
                               (float)meterW - 1.0f, (float)meterH - 1.0f, 2.0f, 0.8f);

        // Fill: map -60dB..0dB → 0..meterW
        const float normLevel = juce::jmap(sidechainLevelDB, -60.0f, 0.0f, 0.0f, 1.0f);
        const float fillW     = juce::jlimit(0.0f, (float)meterW, normLevel * meterW);

        if (fillW > 1.0f)
        {
            // Gradient: dim cyan at low levels → bright cyan → amber near 0dBFS
            juce::ColourGradient grad(
                juce::Colour(0xff006677), (float)meterX, 0.0f,
                juce::Colour(0xffffaa00), (float)(meterX + meterW), 0.0f,
                false);
            grad.addColour(0.75, Theme::accent);

            g.setGradientFill(grad);
            g.fillRoundedRectangle((float)meterX + 1.0f, (float)meterY + 1.0f,
                                   fillW - 2.0f, (float)meterH - 2.0f, 1.5f);
        }

        // Level text (dB readout, only when signal present)
        g.setFont(juce::Font(8.0f));
        if (sidechainIsActive)
        {
            juce::String dbStr = juce::String((int)sidechainLevelDB) + " dB";
            g.setColour(Theme::textSecond);
            g.drawText(dbStr, meterX + meterW + 4, meterY - 1, 36, 12,
                       juce::Justification::centredLeft);
        }
        else
        {
            g.setColour(Theme::textDim);
            g.drawText("no signal", meterX + meterW + 4, meterY - 1, 52, 12,
                       juce::Justification::centredLeft);
        }
    }

    // Version
    g.setFont(juce::Font(8.0f));
    g.setColour(Theme::textDim);
    g.drawText("v0.3", w - 36, h - 14, 30, 12, juce::Justification::centredRight);

    // Control panel background — spans the two control rows
    const int panelTop = kHeaderH + kMargin + kDisplayH + kMargin;
    const int panelH   = h - panelTop - kMargin;
    g.setColour(Theme::panelBg);
    g.fillRoundedRectangle((float)kMargin, (float)panelTop,
                           (float)(w - kMargin * 2), (float)panelH, 6.0f);
    g.setColour(Theme::panelBorder);
    g.drawRoundedRectangle((float)kMargin + 0.5f, (float)panelTop + 0.5f,
                           (float)(w - kMargin * 2) - 1.0f, (float)panelH - 1.0f, 6.0f, 1.0f);

    // --- Section dividers and labels ---
    // Layout: 9 controls in a 2-row grid.
    // Row 1 (top, knobs): Depth | Q | Harmonics | Attack | Release | Mix | OutGain  (7 knobs)
    // Row 2 (bottom, small controls): Smoothing | SnapToNote | FreqLock  (left aligned)
    // Dividers split EQ section (cols 0-2) from Dynamics (3-4) from Output (5-6)

    const int totalW  = w - kMargin * 2;
    const int col7    = totalW / 7; // 7-column grid for row 1

    const int row1Top = panelTop + kSectionLabelH;

    // Vertical dividers
    g.setColour(Theme::panelBorder);
    // After EQ (after col 2): x = kMargin + col7*3
    g.drawVerticalLine(kMargin + col7 * 3,
                       panelTop + 4, panelTop + panelH - 4);
    // After Dynamics (after col 4): x = kMargin + col7*5
    g.drawVerticalLine(kMargin + col7 * 5,
                       panelTop + 4, panelTop + panelH - 4);

    // Section labels centred over their column groups
    auto drawSect = [&](const juce::String& t, int x, int cols)
    {
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::bold));
        g.setColour(Theme::textDim);
        g.drawText(t, x, panelTop + 3, cols * col7, kSectionLabelH - 2,
                   juce::Justification::centred);
    };
    drawSect("EQ",        kMargin,                3);
    drawSect("DYNAMICS",  kMargin + col7 * 3,     2);
    drawSect("OUTPUT",    kMargin + col7 * 5,     2);
}

//==============================================================================
void KickDuckerAudioProcessorEditor::resized()
{
    const int w = getWidth();
    const int h = getHeight();

    // EQ display
    eqDisplay.setBounds(kMargin, kHeaderH + kMargin,
                        w - kMargin * 2, kDisplayH);

    // Bypass button — header top right
    bypassButton.setBounds(w - 80, (kHeaderH - 22) / 2, 72, 22);

    // Control panel geometry
    const int panelTop  = kHeaderH + kMargin + kDisplayH + kMargin;
    const int panelH    = h - panelTop - kMargin;
    const int totalW    = w - kMargin * 2;
    const int col7      = totalW / 7;
    const int innerPad  = 5;

    // Row 1: 7 knobs across the full width
    // EQ: Depth(0), Q(1), Harmonics(2) | Dynamics: Attack(3), Release(4) | Output: Mix(5), OutGain(6)
    const int row1Top  = panelTop + kSectionLabelH + innerPad;
    const int row1H    = kKnobH;

    auto knobBounds = [&](int colIdx) {
        return juce::Rectangle<int>(
            kMargin + colIdx * col7 + innerPad,
            row1Top,
            col7 - innerPad * 2,
            row1H);
    };
    // Labels sit BELOW the slider bounds (which include the text box at bottom).
    // Previously they were placed inside the slider rect and overlapped the
    // value text — fixed by placing them 2px below the bottom of the slider.
    auto labelBounds = [&](int colIdx) {
        return juce::Rectangle<int>(
            kMargin + colIdx * col7,
            row1Top + row1H + 2,
            col7,
            kLabelH);
    };

    // EQ group
    attenuationSlider .setBounds(knobBounds(0)); attenuationLabel .setBounds(labelBounds(0));
    qWidthSlider      .setBounds(knobBounds(1)); qWidthLabel      .setBounds(labelBounds(1));
    harmonicAmtSlider .setBounds(knobBounds(2)); harmonicAmtLabel .setBounds(labelBounds(2));

    // Dynamics group
    attackSlider      .setBounds(knobBounds(3)); attackLabel      .setBounds(labelBounds(3));
    releaseSlider     .setBounds(knobBounds(4)); releaseLabel     .setBounds(labelBounds(4));

    // Output group
    mixSlider         .setBounds(knobBounds(5)); mixLabel         .setBounds(labelBounds(5));
    outputGainSlider  .setBounds(knobBounds(6)); outputGainLabel  .setBounds(labelBounds(6));

    // Row 2: detection controls — sits below knob row, left-aligned
    // Smoothing knob | Snap toggle | Lock button
    const int row2Top = row1Top + row1H + innerPad + 2;
    const int row2H   = panelH - kSectionLabelH - row1H - innerPad * 3 - 4;

    // Smoothing knob takes first column, smaller
    const int smallKnobW = col7 - innerPad * 2;
    smoothingSlider.setBounds(kMargin + innerPad, row2Top, smallKnobW, row2H - kLabelH);
    smoothingLabel .setBounds(kMargin, row2Top + row2H - kLabelH - 2, col7, kLabelH);

    // Note quantize toggle — col 1
    const int toggleH = (row2H - 4) / 2;
    noteQuantizeButton.setBounds(kMargin + col7 + innerPad, row2Top,
                                 col7 * 2 - innerPad * 2, toggleH);

    // Freq lock button — below snap toggle in same columns
    freqLockButton.setBounds(kMargin + col7 + innerPad, row2Top + toggleH + 4,
                             col7 * 2 - innerPad * 2, toggleH);
}

//==============================================================================
void KickDuckerAudioProcessorEditor::setupRotary(juce::Slider& slider,
                                                  juce::Label& label,
                                                  const juce::String& labelText,
                                                  const juce::String& suffix)
{
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 58, 13);

    slider.setColour(juce::Slider::rotarySliderFillColourId,    Theme::accent);
    slider.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff222238));
    slider.setColour(juce::Slider::thumbColourId,               juce::Colours::white);
    slider.setColour(juce::Slider::textBoxTextColourId,         Theme::textSecond);
    slider.setColour(juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
    slider.setColour(juce::Slider::textBoxBackgroundColourId,   juce::Colours::transparentBlack);

    if (suffix.isNotEmpty()) slider.setTextValueSuffix(suffix);
    addAndMakeVisible(slider);

    label.setText(labelText, juce::dontSendNotification);
    label.setFont(juce::Font(9.0f));
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, Theme::textSecond);
    addAndMakeVisible(label);
}

void KickDuckerAudioProcessorEditor::setupToggle(juce::ToggleButton& button)
{
    button.setColour(juce::ToggleButton::textColourId,        Theme::textSecond);
    button.setColour(juce::ToggleButton::tickColourId,        Theme::accent);
    button.setColour(juce::ToggleButton::tickDisabledColourId,Theme::textDim);
    addAndMakeVisible(button);
}

void KickDuckerAudioProcessorEditor::drawSectionLabel(juce::Graphics& g,
                                                       const juce::String& text,
                                                       juce::Rectangle<int> bounds)
{
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::bold));
    g.setColour(Theme::textDim);
    g.drawText(text, bounds, juce::Justification::centred);
}