#pragma once
#include <JuceHeader.h>
#include "BiquadFilter.h"

/**
 * EQDisplayComponent — the centrepiece visual.
 *
 * Draws three layers in order:
 *   1. FFT spectrum analyser (background) — shows the kick signal
 *   2. Combined EQ response curve — sum of all active biquads
 *   3. Notch nodes — fundamental (large) + harmonics (smaller)
 *   4. Gain reduction meter — vertical bar on right edge
 *
 * The editor's timer calls:
 *   setParameters()   — updates display state
 *   pushSamples()     — feeds audio data for FFT
 * then calls repaint().
 *
 * All FFT work happens here on the UI thread at 30Hz.
 * No audio processing. No thread safety concerns.
 */
class EQDisplayComponent : public juce::Component
{
public:
    EQDisplayComponent()
    {
        setOpaque(false);
        buildHannWindow();
        spectrumMag.fill(-80.0f);
        fftData.fill(0.0f);
    }

    //==========================================================================
    // Called by editor timerCallback (UI thread, 30Hz)
    void setParameters(float freqHz, float gainDB, float q,
                       double sr, bool scActive, float appliedGainDB,
                       float harmonicAmt, bool locked)
    {
        displayFreq     = freqHz;
        displayGainDB   = gainDB;
        displayQ        = q;
        sampleRate      = sr;
        sidechainActive = scActive;
        appliedGain     = appliedGainDB;
        harmonicAmount  = harmonicAmt;
        isLocked        = locked;
    }

    // Feed audio samples from the processor FIFO.
    // Accumulates samples until kFFTSize are ready, then fires FFT.
    void pushSamples(const float* samples, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            fftData[fftFillCount] = samples[i];
            ++fftFillCount;

            if (fftFillCount >= kFFTSize)
            {
                processFFT();
                fftFillCount = 0;
            }
        }
    }

    //==========================================================================
    void paint(juce::Graphics& g) override
    {
        const float w = (float)getWidth();
        const float h = (float)getHeight();

        // Background
        g.setColour(juce::Colour(0xff0d1117));
        g.fillRoundedRectangle(0.0f, 0.0f, w, h, 6.0f);

        if (sampleRate <= 0.0) return;

        const float contentH = h - kFreqLabelH; // Height above frequency label strip

        drawGrid(g, w, h, contentH);
        drawSpectrum(g, w, contentH);
        drawEQCurve(g, w, contentH);
        drawNodes(g, w, contentH);
        drawGainReductionMeter(g, w, contentH);

        // Border
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.drawRoundedRectangle(0.5f, 0.5f, w - 1.0f, h - 1.0f, 6.0f, 1.0f);
    }

private:
    //==========================================================================
    // FFT constants
    static constexpr int kFFTOrder = 11;
    static constexpr int kFFTSize  = 1 << kFFTOrder; // 2048
    static constexpr float kFreqLabelH = 18.0f;       // Height reserved for Hz labels

    juce::dsp::FFT fft { kFFTOrder };

    // Double the FFT size as required by JUCE's performFrequencyOnlyForwardTransform
    std::array<float, kFFTSize * 2> fftData  {};
    std::array<float, kFFTSize>     hannWindow {};
    std::array<float, kFFTSize / 2> spectrumMag {}; // Smoothed magnitudes in dB
    int fftFillCount = 0;

    void buildHannWindow()
    {
        for (int i = 0; i < kFFTSize; ++i)
            hannWindow[i] = 0.5f * (1.0f - std::cos(
                juce::MathConstants<float>::twoPi * i / (float)(kFFTSize - 1)));
    }

    void processFFT()
    {
        // Apply Hann window to accumulated samples (first kFFTSize elements)
        for (int i = 0; i < kFFTSize; ++i)
            fftData[i] *= hannWindow[i];

        // Zero imaginary part
        std::fill(fftData.begin() + kFFTSize, fftData.end(), 0.0f);

        // Transform in-place — output magnitudes in first kFFTSize/2 bins
        fft.performFrequencyOnlyForwardTransform(fftData.data(), true);

        // Update display magnitudes with fast-attack, slow-decay smoothing.
        // This gives the classic "spectrum hold" look.
        for (int i = 0; i < kFFTSize / 2; ++i)
        {
            float level = fftData[i] / (float)kFFTSize;
            float db    = juce::Decibels::gainToDecibels(level, -80.0f);

            // Fast attack: snap up immediately when signal rises
            // Slow decay:  fade down at ~0.85 per FFT frame (looks smooth at 30Hz)
            if (db > spectrumMag[i])
                spectrumMag[i] = db;
            else
                spectrumMag[i] = spectrumMag[i] * 0.85f + db * 0.15f;
        }
    }

    //==========================================================================
    void drawGrid(juce::Graphics& g, float w, float h, float contentH)
    {
        // Frequency grid verticals
        const float freqLines[] = { 20,30,50,100,200,500,1000,2000,5000,10000,20000 };
        const char* freqLabels[]= { "20","30","50","100","200","500","1k","2k","5k","10k","20k" };
        const int numFreqLines  = 11;

        for (int i = 0; i < numFreqLines; ++i)
        {
            float x = freqToX(freqLines[i], w);
            g.setColour(juce::Colours::white.withAlpha(0.05f));
            g.drawVerticalLine((int)x, 0.0f, contentH);

            g.setFont(juce::Font(9.0f));
            g.setColour(juce::Colours::white.withAlpha(0.3f));
            g.drawText(freqLabels[i], (int)(x - 12), (int)(contentH + 2), 24, 14,
                       juce::Justification::centred);
        }

        // dB grid horizontals
        const float dbLines[] = { -24.0f, -18.0f, -12.0f, -6.0f, 0.0f };
        for (float db : dbLines)
        {
            float y = dbToY(db, contentH);
            g.setColour(db == 0.0f
                        ? juce::Colours::white.withAlpha(0.12f)
                        : juce::Colours::white.withAlpha(0.04f));
            g.drawHorizontalLine((int)y, 0.0f, w - kMeterW - 4.0f);

            g.setFont(juce::Font(8.0f));
            g.setColour(juce::Colours::white.withAlpha(0.25f));
            g.drawText(juce::String((int)db),
                       4, (int)(y - 6), 22, 12,
                       juce::Justification::centredLeft);
        }
    }

    //==========================================================================
    void drawSpectrum(juce::Graphics& g, float w, float contentH)
    {
        if (sampleRate <= 0.0) return;

        juce::Path specPath;
        bool pathStarted = false;
        const float nyquist = (float)(sampleRate * 0.5);

        for (int px = 0; px < (int)(w - kMeterW - 4.0f); ++px)
        {
            float freq = xToFreq((float)px, w);
            if (freq <= 0.0f || freq > nyquist) continue;

            int bin = (int)(freq / nyquist * (float)(kFFTSize / 2));
            bin = juce::jlimit(0, kFFTSize / 2 - 1, bin);

            float db = juce::jlimit(-80.0f, 0.0f, spectrumMag[bin]);
            // Map -80dB..0dB to contentH..0 (bottom to top)
            float y  = juce::jmap(db, -80.0f, 0.0f, contentH, 0.0f);

            if (!pathStarted) { specPath.startNewSubPath((float)px, y); pathStarted = true; }
            else              specPath.lineTo((float)px, y);
        }

        if (!pathStarted) return;

        // Filled area — subdued teal so the EQ curve remains dominant
        juce::Path fill = specPath;
        fill.lineTo(w - kMeterW - 4.0f, contentH);
        fill.lineTo(0.0f, contentH);
        fill.closeSubPath();
        g.setColour(juce::Colour(0xff0a4050).withAlpha(0.5f));
        g.fillPath(fill);

        // Spectrum outline
        g.setColour(juce::Colour(0xff00c8e0).withAlpha(0.35f));
        g.strokePath(specPath, juce::PathStrokeType(1.0f));
    }

    //==========================================================================
    void drawEQCurve(juce::Graphics& g, float w, float contentH)
    {
        if (displayFreq <= 20.0f) return;

        // Build temporary biquads mirroring what the processor is doing.
        // These are only for display — they don't process audio.
        BiquadFilter dispEq, dispH2, dispH3, dispH4;
        const double sr = sampleRate;

        dispEq.setBell(displayFreq,        appliedGain,                       displayQ, sr);
        dispH2.setBell(displayFreq * 2.0f, appliedGain * harmonicAmount * 0.70f, displayQ, sr);
        dispH3.setBell(displayFreq * 3.0f, appliedGain * harmonicAmount * 0.50f, displayQ, sr);
        dispH4.setBell(displayFreq * 4.0f, appliedGain * harmonicAmount * 0.30f, displayQ, sr);

        juce::Path curvePath;
        bool pathStarted = false;
        const float plotW = w - kMeterW - 4.0f;

        for (int px = 0; px < (int)plotW; ++px)
        {
            float freq = xToFreq((float)px, w);
            if (freq < 10.0f) continue;

            // Sum magnitude responses (additive in dB = multiplicative in linear)
            float magDB = dispEq.getMagnitudeDB(freq, sr);
            if (harmonicAmount > 0.001f)
            {
                magDB += dispH2.getMagnitudeDB(freq, sr);
                magDB += dispH3.getMagnitudeDB(freq, sr);
                magDB += dispH4.getMagnitudeDB(freq, sr);
            }
            magDB = juce::jlimit(-30.0f, 6.0f, magDB);
            float y = dbToY(magDB, contentH);

            if (!pathStarted) { curvePath.startNewSubPath((float)px, y); pathStarted = true; }
            else              curvePath.lineTo((float)px, y);
        }

        if (!pathStarted) return;

        // Filled area under curve
        juce::Path fill = curvePath;
        fill.lineTo(plotW, dbToY(0.0f, contentH));
        fill.lineTo(0.0f,  dbToY(0.0f, contentH));
        fill.closeSubPath();

        juce::Colour curveColour = isLocked
            ? juce::Colour(0xffffaa00)   // Amber when locked
            : (sidechainActive ? juce::Colour(0xff00e5ff) : juce::Colour(0xff4a9eff));

        g.setColour(curveColour.withAlpha(sidechainActive ? 0.10f : 0.05f));
        g.fillPath(fill);

        g.setColour(curveColour.withAlpha(0.9f));
        g.strokePath(curvePath, juce::PathStrokeType(1.8f));
    }

    //==========================================================================
    void drawNodes(juce::Graphics& g, float w, float contentH)
    {
        if (displayFreq <= 20.0f)
        {
            // No detection: show flat line hint
            float y0 = dbToY(0.0f, contentH);
            g.setColour(juce::Colours::white.withAlpha(0.10f));
            g.drawHorizontalLine((int)y0, 0.0f, w - kMeterW - 4.0f);
            g.setFont(juce::Font(11.0f));
            g.setColour(juce::Colours::white.withAlpha(0.2f));
            g.drawText("Waiting for sidechain signal...",
                       0, (int)(contentH * 0.4f), (int)(w - kMeterW), 20,
                       juce::Justification::centred);
            return;
        }

        juce::Colour baseColour = isLocked
            ? juce::Colour(0xffffaa00)
            : (sidechainActive ? juce::Colour(0xff00e5ff) : juce::Colour(0xff4a9eff));

        // Vertical dashed line at fundamental
        drawDashedVertical(g, freqToX(displayFreq, w), contentH, baseColour.withAlpha(0.2f));

        // --- Fundamental node ---
        float nodeX  = freqToX(displayFreq, w);
        float nodeY  = dbToY(appliedGain * 0.5f, contentH);

        // Outer glow
        float glowR = sidechainActive ? 16.0f : 10.0f;
        g.setColour(baseColour.withAlpha(0.15f));
        g.fillEllipse(nodeX - glowR, nodeY - glowR, glowR * 2.0f, glowR * 2.0f);

        // Node fill
        float nodeR = 5.5f;
        g.setColour(baseColour);
        g.fillEllipse(nodeX - nodeR, nodeY - nodeR, nodeR * 2.0f, nodeR * 2.0f);
        g.setColour(juce::Colours::white.withAlpha(0.7f));
        g.drawEllipse(nodeX - nodeR, nodeY - nodeR, nodeR * 2.0f, nodeR * 2.0f, 1.2f);

        // Frequency label above node
        juce::String freqText = formatFrequency(displayFreq);
        juce::String noteText = PitchDetector::toNoteName(displayFreq);
        float labelX = juce::jlimit(2.0f, w - kMeterW - 70.0f, nodeX - 28.0f);
        float labelY = nodeY - 26.0f;
        if (labelY < 4.0f) labelY = nodeY + 14.0f;

        g.setFont(juce::Font(10.5f, juce::Font::bold));
        g.setColour(baseColour);
        g.drawText(freqText + "  " + noteText, (int)labelX, (int)labelY, 72, 14,
                   juce::Justification::centredLeft);

        // Lock indicator text
        if (isLocked)
        {
            g.setFont(juce::Font(8.5f));
            g.setColour(juce::Colour(0xffffaa00).withAlpha(0.7f));
            g.drawText("LOCKED", (int)labelX, (int)(labelY + 13), 72, 12,
                       juce::Justification::centredLeft);
        }

        // --- Harmonic nodes ---
        if (harmonicAmount > 0.01f)
        {
            struct Harmonic { float mult; float gainScale; };
            const Harmonic harmonics[] = { {2.0f, 0.70f}, {3.0f, 0.50f}, {4.0f, 0.30f} };

            for (auto& h : harmonics)
            {
                float hFreq = displayFreq * h.mult;
                if (hFreq > 20000.0f) break; // Above display range

                float hGain  = appliedGain * harmonicAmount * h.gainScale;
                float hX     = freqToX(hFreq, w);
                float hY     = dbToY(hGain * 0.5f, contentH);
                float hAlpha = harmonicAmount * h.gainScale;

                // Smaller nodes for harmonics
                float hR = 3.5f;
                g.setColour(baseColour.withAlpha(hAlpha * 0.6f));
                g.fillEllipse(hX - hR, hY - hR, hR * 2.0f, hR * 2.0f);
                g.setColour(baseColour.withAlpha(hAlpha * 0.8f));
                g.drawEllipse(hX - hR, hY - hR, hR * 2.0f, hR * 2.0f, 1.0f);

                // Tiny freq label for harmonics
                g.setFont(juce::Font(8.0f));
                g.setColour(baseColour.withAlpha(hAlpha * 0.6f));
                float hlX = juce::jlimit(2.0f, w - kMeterW - 40.0f, hX - 16.0f);
                g.drawText(formatFrequency(hFreq), (int)hlX, (int)(hY - 18.0f), 36, 12,
                           juce::Justification::centred);
            }
        }
    }

    //==========================================================================
    void drawGainReductionMeter(juce::Graphics& g, float w, float contentH)
    {
        const float mX  = w - kMeterW - 1.0f;
        const float mY  = 0.0f;
        const float mH  = contentH;

        // Background
        g.setColour(juce::Colour(0xff080810));
        g.fillRect(mX, mY, kMeterW, mH);

        // Tick marks and labels at -3, -6, -12, -18 dB
        const float ticks[]      = { -3.0f, -6.0f, -12.0f, -18.0f };
        const char* tickLabels[] = { "3",   "6",   "12",   "18"   };
        g.setFont(juce::Font(7.0f));
        for (int i = 0; i < 4; ++i)
        {
            float ty = mH * std::fabs(ticks[i]) / 18.0f;
            g.setColour(juce::Colours::white.withAlpha(0.15f));
            g.drawHorizontalLine((int)(mY + ty), mX, mX + kMeterW);
            g.setColour(juce::Colours::white.withAlpha(0.3f));
            g.drawText(tickLabels[i], (int)mX, (int)(mY + ty - 4), (int)kMeterW, 9,
                       juce::Justification::centred);
        }

        // Gain reduction fill — grows downward from top
        float normalizedGR = juce::jlimit(0.0f, 1.0f, std::fabs(appliedGain) / 18.0f);
        float fillH = mH * normalizedGR;

        if (fillH > 0.5f)
        {
            // Gradient: cyan (low reduction) → yellow → red (heavy reduction)
            juce::ColourGradient grad(
                juce::Colour(0xff00e5ff), mX, mY,
                juce::Colour(0xffff3333), mX, mY + mH, false);
            grad.addColour(0.4, juce::Colour(0xffffcc00));
            g.setGradientFill(grad);
            g.fillRect(mX + 1.0f, mY, kMeterW - 2.0f, fillH);
        }

        // Border
        g.setColour(juce::Colours::white.withAlpha(0.1f));
        g.drawRect(mX, mY, kMeterW, mH, 1.0f);

        // "GR" label below meter
        g.setFont(juce::Font(7.5f, juce::Font::bold));
        g.setColour(juce::Colours::white.withAlpha(0.35f));
        g.drawText("GR", (int)mX, (int)(mH + 3), (int)kMeterW, 12,
                   juce::Justification::centred);
    }

    //==========================================================================
    // Coordinate helpers
    float freqToX(float freq, float width) const noexcept
    {
        const float logMin = std::log10(20.0f);
        const float logMax = std::log10(20000.0f);
        return (std::log10(juce::jmax(freq, 1.0f)) - logMin) / (logMax - logMin) * width;
    }

    float xToFreq(float x, float width) const noexcept
    {
        const float logMin = std::log10(20.0f);
        const float logMax = std::log10(20000.0f);
        return std::pow(10.0f, logMin + (x / width) * (logMax - logMin));
    }

    // dB to Y: 0dB maps to top third, more negative maps lower
    float dbToY(float db, float contentH) const noexcept
    {
        const float dbMin = -30.0f;
        const float dbMax =  6.0f;
        return ((db - dbMax) / (dbMin - dbMax)) * contentH;
    }

    juce::String formatFrequency(float hz) const
    {
        return hz >= 1000.0f
            ? juce::String(hz / 1000.0f, 1) + "k"
            : juce::String((int)std::round(hz)) + "Hz";
    }

    void drawDashedVertical(juce::Graphics& g, float x, float h, juce::Colour colour)
    {
        const float dash = 4.0f, gap = 3.0f;
        g.setColour(colour);
        for (float y = 0.0f; y < h; y += dash + gap)
            g.drawVerticalLine((int)x, y, juce::jmin(y + dash, h));
    }

    //==========================================================================
    // State — updated by setParameters() / pushSamples() on UI thread
    float  displayFreq     = 0.0f;
    float  displayGainDB   = -6.0f;
    float  displayQ        = 2.0f;
    double sampleRate      = 0.0;
    bool   sidechainActive = false;
    float  appliedGain     = 0.0f;
    float  harmonicAmount  = 0.0f;
    bool   isLocked        = false;

    static constexpr float kMeterW = 16.0f; // Width of GR meter strip

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EQDisplayComponent)
};