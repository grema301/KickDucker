#pragma once
#include <JuceHeader.h>
#include <array>
#include <functional>
#include "BiquadFilter.h"
#include "PitchDetector.h"
#include "KickDuckerUI.h"

/**
 * EQDisplayComponent - the centrepiece visual.
 *
 * Layers, back to front:
 *   1. Input spectrum        (FFT of the main input, own dBFS scale, filled)
 *   2. Target curve          (dashed: the notch shape at full Depth)
 *   3. Live curve            (solid + glow: what the filter is doing right now)
 *   4. Nodes                 (fundamental + harmonics ride ON the live curve)
 *   5. Depth handle          (ring at full Depth - drag it, scroll to change Q)
 *   6. Gain-reduction meter  (right edge, with peak hold)
 *
 * Top strip: pitch readout (Hz, note, GR, LOCKED tag). The editor places the
 * SNAP / LOCK chips over the right side of that strip.
 *
 * All FFT work happens here on the UI thread. The audio thread is never touched.
 */
class EQDisplayComponent : public juce::Component,
                           public juce::SettableTooltipClient
{
public:
    // Layout - shared with the editor so the chips line up with the strip
    static constexpr float kMeterGutter = 46.0f;   // right-hand strip holding the GR meter
    static constexpr float kTopStrip    = 40.0f;   // pitch readout strip above the plot
    static constexpr float kMaxDepthDB  = 18.0f;   // must match the ATTENUATION range (-18..0)

    // Interaction callbacks (wired up by the editor to the parameters)
    std::function<void()>      onDragStart;
    std::function<void()>      onDragEnd;
    std::function<void(float)> onDepthDragged;     // new Depth in dB
    std::function<void(float)> onQWheel;           // change in normalised Q (0..1 units)

    EQDisplayComponent()
    {
        setOpaque (false);
        buildHannWindow();
        spectrumDB.fill (-120.0f);
        fftData.fill (0.0f);
        ring.fill (0.0f);
        setTooltip ("Drag the ring to set Depth. Scroll to change Q (notch width).");
    }

    //==========================================================================
    // Called by editor timerCallback (UI thread, 30Hz)
    void setParameters (float freqHz, float gainDB, float q,
                        double sr, bool scActive, float appliedGainDB,
                        float harmonicAmt, bool locked)
    {
        displayFreq     = freqHz;
        targetDepthDB   = gainDB;
        displayQ        = q;
        sampleRate      = sr;
        sidechainActive = scActive;
        appliedGain     = appliedGainDB;
        harmonicAmount  = harmonicAmt;
        isLocked        = locked;

        // Gain-reduction peak hold: hold ~1.5 s, then fall back
        const float gr = juce::jlimit (0.0f, kMaxDepthDB, -appliedGainDB);
        if (gr >= grPeak)        { grPeak = gr; grHoldTicks = 45; }
        else if (grHoldTicks > 0) --grHoldTicks;
        else                      grPeak = juce::jmax (gr, grPeak - 0.4f);
    }

    // Feed audio samples from the processor FIFO. A sliding 8192-sample window
    // is re-analysed every kFFTHop new samples, so the display updates smoothly
    // while still resolving the low end (5.4 Hz per bin at 44.1 kHz).
    void pushSamples (const float* samples, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            ring[(size_t) ringPos] = samples[i];
            ringPos = (ringPos + 1) & (kFFTSize - 1);

            if (++samplesSinceFFT >= kFFTHop)
            {
                samplesSinceFFT = 0;
                runFFT();
            }
        }
    }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        using namespace KDTheme;

        const auto bounds = getLocalBounds().toFloat();
        const auto plot   = getPlotBounds();

        g.setGradientFill (juce::ColourGradient (displayTop, 0.0f, 0.0f,
                                                 displayBottom, 0.0f, bounds.getBottom(), false));
        g.fillRoundedRectangle (bounds, 10.0f);

        g.setColour (juce::Colours::black.withAlpha (0.22f));
        g.fillRoundedRectangle (plot, 6.0f);

        drawGrid (g, plot);

        if (sampleRate > 0.0)
        {
            g.saveState();
            g.reduceClipRegion (plot.toNearestInt());
            drawSpectrum (g, plot);
            drawCurves (g, plot);
            g.restoreState();

            drawNodes (g, plot);
        }

        drawReadout (g);
        drawMeter (g, plot);

        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 10.0f, 1.0f);
    }

    //==========================================================================
    // Mouse: drag the ring vertically to set Depth, scroll to change Q.
    void mouseMove (const juce::MouseEvent& e) override { updateHover (hitHandle (e.position)); }
    void mouseExit (const juce::MouseEvent&)   override { if (! dragging) updateHover (false); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! hitHandle (e.position)) return;

        dragging    = true;
        dragOffsetY = e.position.y - getHandlePosition (getPlotBounds()).y;
        if (onDragStart) onDragStart();
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;

        float db = yToDB (e.position.y - dragOffsetY, getPlotBounds());
        db = juce::jlimit (-kMaxDepthDB, 0.0f, std::round (db * 10.0f) / 10.0f);

        targetDepthDB = db;                 // instant feedback; timer re-syncs from the parameter
        repaint();
        if (onDepthDragged) onDepthDragged (db);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragging) return;
        dragging = false;
        if (onDragEnd) onDragEnd();
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        const float d = wheel.deltaY * (wheel.isReversed ? -1.0f : 1.0f);
        if (onQWheel != nullptr && d != 0.0f)
            onQWheel (d * 0.15f);
    }

private:
    //==========================================================================
    // Display ranges
    static constexpr float kMinHz       = 20.0f;
    static constexpr float kMaxHz       = 2000.0f;   // kick region + harmonics (4 x 280 Hz = 1.1 kHz)
    static constexpr float kTopDB       = 3.0f;      // EQ curve axis
    static constexpr float kBottomDB    = -24.0f;
    static constexpr float kSpecFloorDB = -96.0f;    // spectrum axis (own scale, dBFS)
    static constexpr float kSpecCeilDB  = 0.0f;

    static constexpr float kAxisLeft    = 36.0f;
    static constexpr float kAxisBottom  = 22.0f;

    // FFT
    static constexpr int kFFTOrder = 13;
    static constexpr int kFFTSize  = 1 << kFFTOrder;  // 8192
    static constexpr int kFFTHop   = 1024;

    juce::dsp::FFT fft { kFFTOrder };
    std::array<float, kFFTSize * 2>     fftData    {};   // 2x: JUCE needs room for the complex result
    std::array<float, kFFTSize>         hannWindow {};
    std::array<float, kFFTSize>         ring       {};
    std::array<float, kFFTSize / 2 + 1> spectrumDB {};
    int  ringPos = 0, samplesSinceFFT = 0;
    bool spectrumValid = false;

    void buildHannWindow()
    {
        for (int i = 0; i < kFFTSize; ++i)
            hannWindow[(size_t) i] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi
                                                               * (float) i / (float) kFFTSize));
    }

    void runFFT()
    {
        // Oldest sample sits at ringPos
        for (int i = 0; i < kFFTSize; ++i)
            fftData[(size_t) i] = ring[(size_t) ((ringPos + i) & (kFFTSize - 1))] * hannWindow[(size_t) i];
        std::fill (fftData.begin() + kFFTSize, fftData.end(), 0.0f);

        fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

        // A sine of amplitude A gives |X| = A*N/4 after a Hann window (coherent gain 0.5,
        // energy split across +/- bins), so 4/N makes the readout true dBFS.
        const float norm = 4.0f / (float) kFFTSize;

        // Time-based smoothing so the look doesn't change with sample rate
        const float sr      = (float) (sampleRate > 0.0 ? sampleRate : 44100.0);
        const float hopSec  = (float) kFFTHop / sr;
        const float attack  = 1.0f - std::exp (-hopSec / 0.025f);
        const float release = 1.0f - std::exp (-hopSec / 0.140f);

        for (int i = 0; i <= kFFTSize / 2; ++i)
        {
            const float db = juce::Decibels::gainToDecibels (fftData[(size_t) i] * norm, -120.0f);
            float& s = spectrumDB[(size_t) i];
            s += (db - s) * (db > s ? attack : release);
        }
        spectrumValid = true;
    }

    //==========================================================================
    // State - updated by setParameters() on the UI thread
    float  displayFreq     = 0.0f;
    float  targetDepthDB   = -6.0f;
    float  displayQ        = 2.0f;
    double sampleRate      = 0.0;
    bool   sidechainActive = false;
    float  appliedGain     = 0.0f;
    float  harmonicAmount  = 0.0f;
    bool   isLocked        = false;

    float grPeak = 0.0f;
    int   grHoldTicks = 0;

    bool  handleHover = false, dragging = false;
    float dragOffsetY = 0.0f;

    //==========================================================================
    // Geometry
    juce::Rectangle<float> getPlotBounds() const
    {
        return { kAxisLeft, kTopStrip,
                 (float) getWidth()  - kAxisLeft - kMeterGutter,
                 (float) getHeight() - kTopStrip - kAxisBottom };
    }

    static float freqToX (float freq, const juce::Rectangle<float>& plot) noexcept
    {
        const float logMin = std::log10 (kMinHz), logMax = std::log10 (kMaxHz);
        return plot.getX() + (std::log10 (juce::jmax (freq, 1.0f)) - logMin) / (logMax - logMin) * plot.getWidth();
    }

    static float xToFreq (float x, const juce::Rectangle<float>& plot) noexcept
    {
        const float logMin = std::log10 (kMinHz), logMax = std::log10 (kMaxHz);
        return std::pow (10.0f, logMin + ((x - plot.getX()) / plot.getWidth()) * (logMax - logMin));
    }

    static float dbToY (float db, const juce::Rectangle<float>& plot) noexcept
    {
        return plot.getY() + (kTopDB - db) / (kTopDB - kBottomDB) * plot.getHeight();
    }

    static float yToDB (float y, const juce::Rectangle<float>& plot) noexcept
    {
        return kTopDB - (y - plot.getY()) / plot.getHeight() * (kTopDB - kBottomDB);
    }

    static float specToY (float db, const juce::Rectangle<float>& plot) noexcept
    {
        const float c = juce::jlimit (kSpecFloorDB, kSpecCeilDB, db);
        return plot.getBottom() - (c - kSpecFloorDB) / (kSpecCeilDB - kSpecFloorDB) * plot.getHeight();
    }

    juce::Colour baseColour() const
    {
        return isLocked ? KDTheme::amber
                        : (sidechainActive ? KDTheme::accent : KDTheme::accentDim);
    }

    //==========================================================================
    // Mirror of what the processor does: fundamental + 3 harmonic bells.
    struct Bells
    {
        BiquadFilter b[4];

        float responseDB (float freq, double sr, float harmonicAmt) const
        {
            float db = b[0].getMagnitudeDB (freq, sr);
            if (harmonicAmt > 0.001f)
                for (int i = 1; i < 4; ++i)
                    db += b[i].getMagnitudeDB (freq, sr);
            return db;
        }
    };

    Bells makeBells (float gainDB) const
    {
        Bells s;
        s.b[0].setBell (displayFreq, gainDB, displayQ, sampleRate);
        for (int i = 0; i < 3; ++i)
            s.b[i + 1].setBell (displayFreq * (float) (i + 2),
                                gainDB * harmonicAmount * KickDuckerDSP::kHarmonicGain[i],
                                displayQ, sampleRate);
        return s;
    }

    //==========================================================================
    void drawGrid (juce::Graphics& g, const juce::Rectangle<float>& plot) const
    {
        using namespace KDTheme;

        static const float freqs[]        = { 20, 30, 50, 100, 200, 300, 500, 1000, 2000 };
        static const char* freqLabels[]   = { "20", "30", "50", "100", "200", "300", "500", "1k", "2k" };

        g.setFont (makeFont (10.0f));
        for (int i = 0; i < 9; ++i)
        {
            const float x = freqToX (freqs[i], plot);
            if (i > 0 && i < 8)
            {
                g.setColour (juce::Colours::white.withAlpha (0.05f));
                g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
            }
            g.setColour (textDim);
            g.drawText (freqLabels[i], juce::Rectangle<float> (x - 15.0f, plot.getBottom() + 4.0f, 30.0f, 14.0f),
                        juce::Justification::centred);
        }

        static const float dbLines[] = { 0.0f, -6.0f, -12.0f, -18.0f, -24.0f };
        for (float db : dbLines)
        {
            const float y = dbToY (db, plot);
            g.setColour (juce::Colours::white.withAlpha (db == 0.0f ? 0.16f : 0.05f));
            g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());

            g.setColour (textDim);
            g.drawText (db == 0.0f ? juce::String ("0") : minusSign() + juce::String ((int) std::abs (db)),
                        juce::Rectangle<float> (0.0f, y - 7.0f, kAxisLeft - 6.0f, 14.0f),
                        juce::Justification::centredRight);
        }
    }

    //==========================================================================
    float spectrumAt (float binA, float binB) const noexcept
    {
        const int last = kFFTSize / 2;
        binA = juce::jlimit (0.0f, (float) last, binA);
        binB = juce::jlimit (0.0f, (float) last, binB);

        if (binB - binA <= 1.0f)   // fewer than one bin per pixel: interpolate
        {
            const float pos = 0.5f * (binA + binB);
            const int   i0  = (int) pos;
            const int   i1  = juce::jmin (i0 + 1, last);
            return juce::jmap (pos - (float) i0, spectrumDB[(size_t) i0], spectrumDB[(size_t) i1]);
        }

        float m = -120.0f;         // several bins per pixel: take the peak so nothing is missed
        for (int i = (int) std::ceil (binA); i <= (int) binB; ++i)
            m = juce::jmax (m, spectrumDB[(size_t) i]);
        return m;
    }

    void drawSpectrum (juce::Graphics& g, const juce::Rectangle<float>& plot) const
    {
        if (! spectrumValid) return;

        const float binHz = (float) sampleRate / (float) kFFTSize;
        const int   x0    = (int) std::floor (plot.getX());
        const int   x1    = (int) std::ceil  (plot.getRight());

        juce::Path line;
        bool started = false;
        for (int px = x0; px <= x1; ++px)
        {
            const float fa = xToFreq ((float) px - 0.5f, plot);
            const float fb = xToFreq ((float) px + 0.5f, plot);
            const float y  = specToY (spectrumAt (fa / binHz, fb / binHz), plot);

            if (! started) { line.startNewSubPath ((float) px, y); started = true; }
            else             line.lineTo ((float) px, y);
        }
        if (! started) return;

        juce::Path fill (line);
        fill.lineTo (plot.getRight(), plot.getBottom());
        fill.lineTo (plot.getX(),     plot.getBottom());
        fill.closeSubPath();

        g.setGradientFill (juce::ColourGradient (KDTheme::accent.withAlpha (0.30f), 0.0f, plot.getY(),
                                                 KDTheme::accent.withAlpha (0.02f), 0.0f, plot.getBottom(), false));
        g.fillPath (fill);

        g.setColour (KDTheme::accent.withAlpha (0.45f));
        g.strokePath (line, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved));
    }

    //==========================================================================
    void drawCurves (juce::Graphics& g, const juce::Rectangle<float>& plot) const
    {
        if (displayFreq <= 20.0f) return;

        const Bells live   = makeBells (appliedGain);
        const Bells target = makeBells (targetDepthDB);

        juce::Path livePath, targetPath;
        bool started = false;

        const int x0 = (int) std::floor (plot.getX());
        const int x1 = (int) std::ceil  (plot.getRight());

        auto addPoint = [] (juce::Path& p, bool first, float x, float y)
        {
            if (first) p.startNewSubPath (x, y); else p.lineTo (x, y);
        };

        for (int px = x0; px <= x1; px += 2)
        {
            const float freq = xToFreq ((float) px, plot);
            const float yL = dbToY (juce::jlimit (kBottomDB - 3.0f, kTopDB, live  .responseDB (freq, sampleRate, harmonicAmount)), plot);
            const float yT = dbToY (juce::jlimit (kBottomDB - 3.0f, kTopDB, target.responseDB (freq, sampleRate, harmonicAmount)), plot);
            addPoint (livePath,   ! started, (float) px, yL);
            addPoint (targetPath, ! started, (float) px, yT);
            started = true;
        }
        if (! started) return;

        const auto  base  = baseColour();
        const float zeroY = dbToY (0.0f, plot);

        // Target (what full Depth would look like) - thin dashed outline
        {
            const float dashes[] = { 4.0f, 4.0f };
            juce::Path dashed;
            juce::PathStrokeType (1.1f).createDashedStroke (dashed, targetPath, dashes, 2);
            g.setColour (juce::Colours::white.withAlpha (0.30f));
            g.fillPath (dashed);
        }

        // Live curve: soft fill between the 0 dB line and the curve
        juce::Path fill (livePath);
        fill.lineTo (plot.getRight(), zeroY);
        fill.lineTo (plot.getX(),     zeroY);
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (base.withAlpha (0.06f), 0.0f, zeroY,
                                                 base.withAlpha (sidechainActive ? 0.38f : 0.20f), 0.0f, dbToY (kBottomDB, plot), false));
        g.fillPath (fill);

        // Glow pass, then crisp line
        g.setColour (base.withAlpha (0.20f));
        g.strokePath (livePath, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (base);
        g.strokePath (livePath, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    //==========================================================================
    juce::Point<float> getHandlePosition (const juce::Rectangle<float>& plot) const
    {
        return { freqToX (displayFreq, plot), dbToY (targetDepthDB, plot) };
    }

    bool hitHandle (juce::Point<float> p) const
    {
        if (displayFreq <= 20.0f) return false;
        return p.getDistanceFrom (getHandlePosition (getPlotBounds())) <= 12.0f;
    }

    void updateHover (bool hover)
    {
        if (hover == handleHover) return;
        handleHover = hover;
        setMouseCursor (hover ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }

    void drawNodes (juce::Graphics& g, const juce::Rectangle<float>& plot) const
    {
        using namespace KDTheme;

        if (displayFreq <= 20.0f)
        {
            g.setFont (makeFont (12.0f));
            g.setColour (textDim);
            g.drawText ("Waiting for sidechain signal...", plot, juce::Justification::centred);
            return;
        }

        const auto  base = baseColour();
        const Bells live = makeBells (appliedGain);
        const float fx   = freqToX (displayFreq, plot);

        g.saveState();
        g.reduceClipRegion (plot.toNearestInt());

        // Vertical guide through the fundamental
        {
            const float dashes[] = { 3.0f, 4.0f };
            juce::Path guide, dashed;
            guide.startNewSubPath (fx, plot.getY());
            guide.lineTo (fx, plot.getBottom());
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, guide, dashes, 2);
            g.setColour (base.withAlpha (0.22f));
            g.fillPath (dashed);
        }

        // Harmonic nodes (sit on the live curve, fade with their share of the gain)
        if (harmonicAmount > 0.01f)
        {
            for (int i = 0; i < 3; ++i)
            {
                const float hf = displayFreq * (float) (i + 2);
                if (hf >= kMaxHz) break;

                const float hx    = freqToX (hf, plot);
                const float hy    = dbToY (live.responseDB (hf, sampleRate, harmonicAmount), plot);
                const float alpha = juce::jlimit (0.35f, 1.0f, 0.35f + harmonicAmount * KickDuckerDSP::kHarmonicGain[i]);
                const float r     = 4.0f;

                g.setColour (base.withAlpha (alpha * 0.30f));
                g.fillEllipse (hx - r, hy - r, r * 2.0f, r * 2.0f);
                g.setColour (base.withAlpha (alpha));
                g.drawEllipse (hx - r, hy - r, r * 2.0f, r * 2.0f, 1.2f);

                g.setFont (makeFont (9.5f, true));
                g.setColour (base.withAlpha (alpha * 0.85f));
                g.drawText (juce::String (i + 2) + timesSign(),
                            juce::Rectangle<float> (hx - 14.0f, hy - 21.0f, 28.0f, 14.0f),
                            juce::Justification::centred);
            }
        }

        // Fundamental node, on the live curve
        const float fy    = dbToY (live.responseDB (displayFreq, sampleRate, harmonicAmount), plot);
        const float glowR = sidechainActive ? 15.0f : 10.0f;
        g.setColour (base.withAlpha (0.16f));
        g.fillEllipse (fx - glowR, fy - glowR, glowR * 2.0f, glowR * 2.0f);
        g.setColour (base);
        g.fillEllipse (fx - 5.0f, fy - 5.0f, 10.0f, 10.0f);
        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.drawEllipse (fx - 5.0f, fy - 5.0f, 10.0f, 10.0f, 1.2f);

        g.restoreState();

        // Depth handle: ring at the full-Depth position (outside the clip so it never gets cut)
        const auto  hp  = getHandlePosition (plot);
        const bool  hot = handleHover || dragging;
        g.setColour (juce::Colours::white.withAlpha (hot ? 0.95f : 0.55f));
        g.drawEllipse (hp.x - 7.0f, hp.y - 7.0f, 14.0f, 14.0f, hot ? 2.0f : 1.4f);

        if (hot)
        {
            const bool above = hp.y - 32.0f > plot.getY();
            const juce::Rectangle<float> pill (hp.x - 36.0f, above ? hp.y - 32.0f : hp.y + 14.0f, 72.0f, 18.0f);
            g.setColour (headerBg.withAlpha (0.92f));
            g.fillRoundedRectangle (pill, 9.0f);
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawRoundedRectangle (pill.reduced (0.5f), 9.0f, 1.0f);
            g.setFont (makeMonoFont (10.5f, true));
            g.setColour (textPrimary);
            g.drawText (formatDB (targetDepthDB, 1), pill, juce::Justification::centred);
        }
    }

    //==========================================================================
    static juce::String formatFrequency (float hz)
    {
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, 2) + " kHz"
                             : juce::String (hz, 1) + " Hz";
    }

    void drawReadout (juce::Graphics& g) const
    {
        using namespace KDTheme;

        const bool  hasPitch = displayFreq > 20.0f;
        const auto  base     = baseColour();
        const float cy       = kTopStrip * 0.5f;

        g.setFont (makeMonoFont (19.0f, true));
        g.setColour (hasPitch ? base : textDim);
        g.drawText (hasPitch ? formatFrequency (displayFreq) : juce::String ("-- Hz"),
                    juce::Rectangle<float> (14.0f, cy - 14.0f, 116.0f, 28.0f), juce::Justification::centredLeft);

        if (! hasPitch) return;

        // Note pill
        const juce::Rectangle<float> pill (134.0f, cy - 10.0f, 40.0f, 20.0f);
        g.setColour (base.withAlpha (0.14f));
        g.fillRoundedRectangle (pill, 10.0f);
        g.setColour (base.withAlpha (0.55f));
        g.drawRoundedRectangle (pill.reduced (0.5f), 10.0f, 1.0f);
        g.setFont (makeFont (11.0f, true));
        g.setColour (base);
        g.drawText (PitchDetector::toNoteName (displayFreq), pill, juce::Justification::centred);

        // Live gain reduction
        g.setFont (makeMonoFont (11.0f));
        g.setColour (textSecondary);
        g.drawText ("GR " + formatDB (appliedGain, 1),
                    juce::Rectangle<float> (186.0f, cy - 8.0f, 110.0f, 16.0f), juce::Justification::centredLeft);

        if (isLocked)
        {
            const juce::Rectangle<float> tag (300.0f, cy - 9.0f, 62.0f, 18.0f);
            g.setColour (amber.withAlpha (0.14f));
            g.fillRoundedRectangle (tag, 9.0f);
            g.setColour (amber.withAlpha (0.7f));
            g.drawRoundedRectangle (tag.reduced (0.5f), 9.0f, 1.0f);
            g.setFont (makeFont (9.5f, true, 0.1f));
            g.setColour (amber);
            g.drawText ("LOCKED", tag, juce::Justification::centred);
        }
    }

    //==========================================================================
    void drawMeter (juce::Graphics& g, const juce::Rectangle<float>& plot) const
    {
        using namespace KDTheme;

        const juce::Rectangle<float> track (plot.getRight() + 12.0f, plot.getY(), 8.0f, plot.getHeight());

        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (track, 4.0f);

        // Scale
        static const float ticks[] = { 3.0f, 6.0f, 12.0f, 18.0f };
        g.setFont (makeFont (9.0f));
        for (float t : ticks)
        {
            const float ty = track.getY() + track.getHeight() * t / kMaxDepthDB;
            g.setColour (juce::Colours::white.withAlpha (0.16f));
            g.drawHorizontalLine ((int) ty, track.getX() - 2.0f, track.getRight() + 2.0f);
            g.setColour (textDim);
            g.drawText (juce::String ((int) t),
                        juce::Rectangle<float> (track.getRight() + 5.0f, ty - 7.0f, 18.0f, 14.0f),
                        juce::Justification::centredLeft);
        }

        // Fill grows downward from the top
        const float gr    = juce::jlimit (0.0f, kMaxDepthDB, -appliedGain);
        const float fillH = track.getHeight() * gr / kMaxDepthDB;
        if (fillH > 0.5f)
        {
            juce::ColourGradient grad (accent, 0.0f, track.getY(), danger, 0.0f, track.getBottom(), false);
            grad.addColour (0.45, amber);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (track.getX(), track.getY(), track.getWidth(), fillH, 4.0f);
        }

        // Peak hold
        if (grPeak > 0.1f)
        {
            const float py = track.getY() + track.getHeight() * grPeak / kMaxDepthDB;
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.fillRect (track.getX() - 1.0f, py - 1.0f, track.getWidth() + 2.0f, 2.0f);
        }

        g.setFont (makeFont (9.5f, true, 0.12f));
        g.setColour (textDim);
        g.drawText ("GR", juce::Rectangle<float> (track.getX() - 8.0f, plot.getBottom() + 4.0f, track.getWidth() + 16.0f, 14.0f),
                    juce::Justification::centred);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EQDisplayComponent)
};