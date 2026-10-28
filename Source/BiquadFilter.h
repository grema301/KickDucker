#pragma once
#include <cmath>
#include <JuceHeader.h>

//==============================================================================
// Constants shared by the audio thread (PluginProcessor) and the GUI curve
// drawing (EQDisplay). Having ONE copy means the picture can never drift away
// from what the DSP is actually doing.
namespace KickDuckerDSP
{
    // Harmonic notches sit at 2x / 3x / 4x the fundamental, each getting this
    // fraction of the fundamental's gain reduction (scaled by HARMONIC_AMT).
    inline constexpr float kHarmonicGain[3] = { 0.70f, 0.50f, 0.30f };

    // Sidechain envelope level (linear) that produces the full ATTENUATION.
    // 0.25 = -12 dBFS. Quieter sidechain signals duck proportionally less.
    inline constexpr float kEnvelopeFullScale = 0.25f;
}

/**
 * A single biquad IIR filter, configured as a peaking bell EQ band.
 *
 * Coefficients and state are held in DOUBLE precision. The notch lives at
 * 25-280 Hz, where the poles sit extremely close to the unit circle
 * (w0 is only ~0.003-0.04 rad). In single precision the pole angle and radius
 * are quantised noticeably at those frequencies; double removes the problem
 * for the price of a few extra flops. The public interface is still float, so
 * nothing else in the project has to change.
 */
class BiquadFilter
{
public:
    BiquadFilter() { reset(); }

    void reset() noexcept { z1 = z2 = 0.0; }

    /**
     * Set as a peaking bell EQ band (Audio EQ Cookbook).
     * @param frequencyHz  Centre frequency
     * @param gainDB       Boost/cut in dB (negative = cut)
     * @param Q            Quality factor - higher = narrower band
     * @param sampleRate   Current sample rate
     */
    void setBell(float frequencyHz, float gainDB, float Q, double sampleRate)
    {
        // A NaN/Inf here would poison the filter state forever - ignore it.
        if (! std::isfinite(frequencyHz) || ! std::isfinite(gainDB) || ! std::isfinite(Q)
            || sampleRate <= 0.0)
            return;

        const double f = juce::jlimit(20.0, sampleRate * 0.49, (double) frequencyHz);
        const double q = juce::jlimit(0.1, 20.0, (double) Q);
        const double g = juce::jlimit(-36.0, 36.0, (double) gainDB);

        const double A     = std::pow(10.0, g / 40.0);
        const double w0    = juce::MathConstants<double>::twoPi * f / sampleRate;
        const double alpha = std::sin(w0) / (2.0 * q);
        const double cosW0 = std::cos(w0);

        const double a0 = 1.0 + alpha / A;
        b0 = (1.0 + alpha * A) / a0;
        b1 = (-2.0 * cosW0)    / a0;
        b2 = (1.0 - alpha * A) / a0;
        a1 = (-2.0 * cosW0)    / a0;
        a2 = (1.0 - alpha / A) / a0;
    }

    /** Copy another filter's coefficients (not its state). Used so L and R can
        share one setBell() call instead of computing identical trig twice. */
    void setCoefficientsFrom(const BiquadFilter& other) noexcept
    {
        b0 = other.b0; b1 = other.b1; b2 = other.b2;
        a1 = other.a1; a2 = other.a2;
    }

    /** Process a single sample. Direct Form II Transposed. */
    inline float processSample(float x) noexcept
    {
        const double xd = (double) x;
        const double y  = b0 * xd + z1;
        z1 = b1 * xd - a1 * y + z2;
        z2 = b2 * xd - a2 * y;
        return (float) y;
    }

    /** Process an entire channel of audio in-place. */
    void processBlock(float* samples, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
            samples[i] = processSample(samples[i]);
    }

    /**
     * Magnitude response in dB at a given frequency.
     * GUI use only - never call this from the audio thread.
     */
    float getMagnitudeDB(float frequencyHz, double sampleRate) const noexcept
    {
        const double w    = juce::MathConstants<double>::twoPi * (double) frequencyHz / sampleRate;
        const double cosW = std::cos(w),     sinW = std::sin(w);
        const double cos2 = std::cos(2.0*w), sin2 = std::sin(2.0*w);

        // Numerator: b0 + b1*e^-jw + b2*e^-2jw
        const double numRe = b0 + b1 * cosW + b2 * cos2;
        const double numIm =    - b1 * sinW - b2 * sin2;

        // Denominator: 1 + a1*e^-jw + a2*e^-2jw
        const double denRe = 1.0 + a1 * cosW + a2 * cos2;
        const double denIm =     - a1 * sinW - a2 * sin2;

        const double numMag2 = numRe * numRe + numIm * numIm;
        const double denMag2 = denRe * denRe + denIm * denIm;

        if (denMag2 < 1e-18 || numMag2 < 1e-30) return 0.0f;

        return (float) (10.0 * std::log10(numMag2 / denMag2));
    }

private:
    double b0 = 1.0, b1 = 0.0, b2 = 0.0;
    double a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;   // Direct Form II Transposed state
};