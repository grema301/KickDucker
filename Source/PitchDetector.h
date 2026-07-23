#pragma once
#include <JuceHeader.h>
#include <vector>
#include <cmath>

class PitchDetector
{
public:
    static constexpr float kMinFreq      = 25.0f;
    static constexpr float kMaxFreq      = 280.0f;
    static constexpr float kYINThreshold = 0.15f;
    static constexpr int   kWindowSize   = 8192;
    static constexpr int   kHopSize      = 1024;
    static constexpr int   kAnalysisLen  = 512;

    void prepare(double sampleRate)
    {
        this->sampleRate = sampleRate;
        const int maxLag = (int)std::ceil(sampleRate / (double)kMinFreq) + 2;
        circularBuffer.assign(kWindowSize, 0.0f);
        linearBuffer.assign(kWindowSize,   0.0f);
        yinBuffer.assign(maxLag + 1,       0.0f);
        writePos       = 0;
        hopCounter     = 0;
        lastFreq       = 0.0f;
        lastConfidence = 0.0f;
        lpCoeff = std::exp(-juce::MathConstants<double>::twoPi * 400.0 / sampleRate);
        lpState = 0.0f;
    }

    float process(const juce::AudioBuffer<float>& sidechainBuffer)
    {
        const int numSamples = sidechainBuffer.getNumSamples();
        const int numCh      = sidechainBuffer.getNumChannels();
        const float* dataL   = sidechainBuffer.getReadPointer(0);
        const float* dataR   = (numCh > 1) ? sidechainBuffer.getReadPointer(1) : nullptr;

        for (int i = 0; i < numSamples; ++i)
        {
            float sample = dataR ? (dataL[i] + dataR[i]) * 0.5f : dataL[i];
            lpState = (float)(lpState * lpCoeff + sample * (1.0 - lpCoeff));
            circularBuffer[writePos] = lpState;
            writePos = (writePos + 1) % kWindowSize;
            ++hopCounter;
        }

        if (hopCounter >= kHopSize)
        {
            hopCounter = 0;
            float detected = runYIN();
            if (detected > kMinFreq && detected < kMaxFreq && lastConfidence > kYINThreshold)
                lastFreq = detected;
        }
        return lastFreq;
    }

    // Snap a frequency to the nearest equal-tempered semitone.
    // Used when NOTE_QUANTIZE is enabled — stops the notch from
    // wobbling between micro-tuning variations in the 808.
    static float quantizeToSemitone(float hz)
    {
        if (hz <= 0.0f) return hz;
        const float midi      = 69.0f + 12.0f * std::log2(hz / 440.0f);
        const float quantized = std::round(midi);
        return 440.0f * std::pow(2.0f, (quantized - 69.0f) / 12.0f);
    }

    // Convert Hz to MIDI note name string, e.g. 130.81 -> "C3"
    static juce::String toNoteName(float hz)
    {
        if (hz <= 0.0f) return "--";
        const float midi = 69.0f + 12.0f * std::log2(hz / 440.0f);
        const int midiNote = (int)std::round(midi);
        if (midiNote < 0 || midiNote > 127) return "--";
        static const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
        return juce::String(names[midiNote % 12]) + juce::String((midiNote / 12) - 1);
    }

    float getLastFrequency()  const { return lastFreq; }
    float getLastConfidence() const { return lastConfidence; }

private:
    float runYIN()
    {
        const int minLag = (int)std::floor((float)sampleRate / kMaxFreq);
        const int maxLag = (int)std::ceil ((float)sampleRate / kMinFreq);

        if (maxLag + kAnalysisLen >= (int)linearBuffer.size()) return lastFreq;
        if (maxLag >= (int)yinBuffer.size())                   return lastFreq;

        for (int i = 0; i < kWindowSize; ++i)
            linearBuffer[i] = circularBuffer[(writePos + i) % kWindowSize];
        const float* x = linearBuffer.data();

        for (int tau = minLag; tau <= maxLag; ++tau)
        {
            float sum = 0.0f;
            for (int j = 0; j < kAnalysisLen; ++j)
            {
                float diff = x[j] - x[j + tau];
                sum += diff * diff;
            }
            yinBuffer[tau] = sum;
        }

        yinBuffer[0] = 1.0f;
        float runningSum = 0.0f;
        for (int tau = 1; tau < minLag; ++tau) { yinBuffer[tau] = 1.0f; runningSum += 1.0f; }
        for (int tau = minLag; tau <= maxLag; ++tau)
        {
            runningSum += yinBuffer[tau];
            yinBuffer[tau] = (runningSum > 0.0f) ? yinBuffer[tau] * (float)tau / runningSum : 1.0f;
        }

        int tauEstimate = -1;
        for (int tau = minLag; tau <= maxLag; ++tau)
        {
            if (yinBuffer[tau] < kYINThreshold)
            {
                while (tau + 1 <= maxLag && yinBuffer[tau + 1] < yinBuffer[tau]) ++tau;
                tauEstimate = tau;
                break;
            }
        }

        if (tauEstimate < 0) { lastConfidence = 0.0f; return lastFreq; }

        float betterTau = (float)tauEstimate;
        if (tauEstimate > minLag && tauEstimate < maxLag)
        {
            float s0 = yinBuffer[tauEstimate - 1];
            float s1 = yinBuffer[tauEstimate];
            float s2 = yinBuffer[tauEstimate + 1];
            float denom = 2.0f * (2.0f * s1 - s0 - s2);
            if (std::fabs(denom) > 1e-6f)
                betterTau = (float)tauEstimate - (s2 - s0) / denom;
        }

        lastConfidence = 1.0f - yinBuffer[tauEstimate];
        return (betterTau > 0.0f) ? (float)sampleRate / betterTau : lastFreq;
    }

    double sampleRate = 44100.0;
    std::vector<float> circularBuffer, linearBuffer, yinBuffer;
    int   writePos = 0, hopCounter = 0;
    float lastFreq = 0.0f, lastConfidence = 0.0f;
    double lpCoeff = 0.0;
    float  lpState = 0.0f;
};