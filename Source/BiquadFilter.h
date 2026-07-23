#pragma once
#include <cmath>
#include <JuceHeader.h>

/**
 * A single biquad IIR filter.
 * Can be configured as a peaking bell EQ band.
 * Entirely self-contained — no heap allocation, no JUCE dependency beyond constants.
 */
class BiquadFilter
{
public:
    BiquadFilter() { reset(); }

    void reset()
    {
        z1 = z2 = 0.0f;
        w1 = w2 = 0.0f;
    }

    /**
     * Set as a peaking bell EQ band.
     * @param frequencyHz  Center frequency
     * @param gainDB       Attenuation/boost in dB (negative = cut)
     * @param Q            Quality factor — higher = narrower band
     * @param sampleRate   Current sample rate
     */
    void setBell(float frequencyHz, float gainDB, float Q, double sampleRate)
    {
        // Clamp to safe range — avoid NaN/Inf from extreme values
        frequencyHz = juce::jlimit(20.0f, (float)(sampleRate * 0.49), frequencyHz);
        Q           = juce::jlimit(0.1f, 20.0f, Q);
        gainDB      = juce::jlimit(-36.0f, 36.0f, gainDB);

        // Audio EQ Cookbook peaking EQ coefficients
        const float A     = std::pow(10.0f, gainDB / 40.0f);
        const float w0    = juce::MathConstants<float>::twoPi * frequencyHz / (float)sampleRate;
        const float alpha = std::sin(w0) / (2.0f * Q);
        const float cosW0 = std::cos(w0);

        const float a0 =  1.0f + alpha / A;
        b0 = (1.0f + alpha * A) / a0;
        b1 = (-2.0f * cosW0)    / a0;
        b2 = (1.0f - alpha * A) / a0;
        a1 = (-2.0f * cosW0)    / a0;
        a2 = (1.0f - alpha / A) / a0;
    }

    /**
     * Process a single sample. Call this inside your sample loop.
     */
    inline float processSample(float x) noexcept
    {
        // Direct Form II Transposed — numerically stable
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    /**
     * Process an entire channel of audio in-place.
     */
    void processBlock(float* samples, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
            samples[i] = processSample(samples[i]);
    }

    /**
     * Returns the magnitude response in dB at a given frequency.
     * Use this in the GUI to draw the EQ curve — NOT in the audio path.
     */
    float getMagnitudeDB(float frequencyHz, double sampleRate) const noexcept
    {
        const float w = juce::MathConstants<float>::twoPi * frequencyHz / (float)sampleRate;
        const float cosW = std::cos(w);
        const float sinW = std::sin(w);

        // Numerator: b0 + b1*e^-jw + b2*e^-2jw
        const float numReal = b0 + b1 * cosW + b2 * std::cos(2.0f * w);
        const float numImag =    - b1 * sinW - b2 * std::sin(2.0f * w);

        // Denominator: 1 + a1*e^-jw + a2*e^-2jw
        const float denReal = 1.0f + a1 * cosW + a2 * std::cos(2.0f * w);
        const float denImag =      - a1 * sinW - a2 * std::sin(2.0f * w);

        const float numMag = std::sqrt(numReal * numReal + numImag * numImag);
        const float denMag = std::sqrt(denReal * denReal + denImag * denImag);

        if (denMag < 1e-9f) return 0.0f;

        return 20.0f * std::log10(numMag / denMag);
    }

private:
    // Coefficients
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;

    // State (Direct Form II Transposed)
    float z1 = 0.0f, z2 = 0.0f;
    float w1 = 0.0f, w2 = 0.0f;
};