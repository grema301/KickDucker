#pragma once
#include <JuceHeader.h>
#include <vector>
#include <cmath>

/**
 * YIN pitch detector tuned for kick / 808 fundamentals (25-280 Hz).
 *
 * Signal flow:  sidechain -> mono -> 400 Hz one-pole low-pass -> ring buffer
 *               -> (every kHopSize samples) YIN on the NEWEST samples.
 */
class PitchDetector
{
public:
    static constexpr float kMinFreq       = 25.0f;
    static constexpr float kMaxFreq       = 280.0f;
    static constexpr float kYINThreshold  = 0.15f;                 // absolute threshold on the CMNDF (lower = stricter)
    static constexpr float kMinConfidence = 1.0f - kYINThreshold;  // confidence = 1 - CMNDF at the chosen lag
    static constexpr int   kHopSize       = 1024;                  // run YIN once per this many samples
    static constexpr int   kAnalysisLen   = 512;                   // samples integrated per lag

    void prepare(double newSampleRate)
    {
        sampleRate = newSampleRate;
        minLag     = (int) std::floor(sampleRate / (double) kMaxFreq);
        maxLag     = (int) std::ceil (sampleRate / (double) kMinFreq);

        // We only ever need the newest (kAnalysisLen + maxLag) samples, so the
        // ring is sized from the sample rate instead of a fixed 8192. (The old
        // fixed size silently stopped detecting at 192 kHz.)
        span       = kAnalysisLen + maxLag;
        windowSize = juce::nextPowerOfTwo(span);
        mask       = windowSize - 1;

        circularBuffer.assign((size_t) windowSize, 0.0f);
        linearBuffer  .assign((size_t) span,       0.0f);
        yinBuffer     .assign((size_t) maxLag + 2, 1.0f);

        writePos       = 0;
        hopCounter     = 0;
        lastFreq       = 0.0f;
        lastConfidence = 0.0f;
        lpCoeff = std::exp(-juce::MathConstants<double>::twoPi * 400.0 / sampleRate);
        lpState = 0.0;
    }

    /**
     * Feed one block of sidechain audio.
     * The ring buffer is ALWAYS fed, so it never contains stale audio from
     * before a silence / lock period. YIN itself (the expensive part) only runs
     * when allowAnalysis is true.
     * Returns the last accepted frequency (0 until the first detection).
     */
    float process(const juce::AudioBuffer<float>& sidechainBuffer, bool allowAnalysis = true)
    {
        if (windowSize == 0) return lastFreq; // prepare() not called yet

        const int numSamples = sidechainBuffer.getNumSamples();
        const int numCh      = sidechainBuffer.getNumChannels();
        if (numSamples <= 0 || numCh <= 0) return lastFreq;

        const float* dataL = sidechainBuffer.getReadPointer(0);
        const float* dataR = (numCh > 1) ? sidechainBuffer.getReadPointer(1) : nullptr;

        for (int i = 0; i < numSamples; ++i)
        {
            const float sample = dataR ? (dataL[i] + dataR[i]) * 0.5f : dataL[i];
            lpState = lpState * lpCoeff + (double) sample * (1.0 - lpCoeff);
            circularBuffer[(size_t) writePos] = (float) lpState;
            writePos = (writePos + 1) & mask;
            ++hopCounter;
        }

        if (hopCounter >= kHopSize)
        {
            hopCounter = 0;
            if (allowAnalysis)
            {
                const float detected = runYIN();
                if (detected > kMinFreq && detected < kMaxFreq && lastConfidence > kMinConfidence)
                    lastFreq = detected;
            }
        }
        return lastFreq;
    }

    // Snap a frequency to the nearest equal-tempered semitone.
    // Used when NOTE_QUANTIZE is enabled - stops the notch from
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
        const int midiNote = (int) std::round(midi);
        if (midiNote < 0 || midiNote > 127) return "--";
        static const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
        return juce::String(names[midiNote % 12]) + juce::String((midiNote / 12) - 1);
    }

    float getLastFrequency()  const { return lastFreq; }
    float getLastConfidence() const { return lastConfidence; }

private:
    float runYIN()
    {
        // 1. Linearise the NEWEST `span` samples in chronological order.
        //    (The old code started at writePos, which is the OLDEST sample, so it
        //    analysed audio from ~130-190 ms ago and the notch trailed the kick.)
        int idx = (writePos - span + windowSize) & mask;
        for (int i = 0; i < span; ++i)
        {
            linearBuffer[(size_t) i] = circularBuffer[(size_t) idx];
            idx = (idx + 1) & mask;
        }

        // The reference segment is the most recent kAnalysisLen samples; each lag
        // compares it with the same segment `tau` samples earlier.
        const float* ref = linearBuffer.data() + maxLag;

        // 2. Difference function d(tau) for EVERY lag 1..maxLag.
        //    The cumulative-mean normalisation needs the real d(1..minLag-1) values;
        //    the old code substituted 1.0, which is in different units from d() and
        //    made detection depend on signal amplitude.
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            float sum = 0.0f;
            for (int j = 0; j < kAnalysisLen; ++j)
            {
                const float diff = ref[j] - ref[j - tau];
                sum += diff * diff;
            }
            yinBuffer[(size_t) tau] = sum;
        }

        // 3. Cumulative mean normalised difference (CMNDF)
        yinBuffer[0] = 1.0f;
        float runningSum = 0.0f;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            runningSum += yinBuffer[(size_t) tau];
            yinBuffer[(size_t) tau] = (runningSum > 0.0f)
                                        ? yinBuffer[(size_t) tau] * (float) tau / runningSum
                                        : 1.0f;
        }

        // 4. First lag under the threshold, walked down to its local minimum
        int tauEstimate = -1;
        for (int tau = minLag; tau <= maxLag; ++tau)
        {
            if (yinBuffer[(size_t) tau] < kYINThreshold)
            {
                while (tau + 1 <= maxLag && yinBuffer[(size_t) tau + 1] < yinBuffer[(size_t) tau]) ++tau;
                tauEstimate = tau;
                break;
            }
        }

        if (tauEstimate < 0) { lastConfidence = 0.0f; return lastFreq; }

        // 5. Parabolic interpolation around the minimum for sub-sample accuracy.
        //    Vertex of the parabola through (-1,s0) (0,s1) (+1,s2) is
        //    x = (s0 - s2) / (2 * (s0 - 2*s1 + s2)).  The old code had this sign flipped.
        float betterTau = (float) tauEstimate;
        if (tauEstimate > minLag && tauEstimate < maxLag)
        {
            const float s0 = yinBuffer[(size_t) tauEstimate - 1];
            const float s1 = yinBuffer[(size_t) tauEstimate];
            const float s2 = yinBuffer[(size_t) tauEstimate + 1];
            const float denom = 2.0f * (s0 - 2.0f * s1 + s2);
            if (std::fabs(denom) > 1e-6f)
                betterTau += (s0 - s2) / denom;
        }

        lastConfidence = 1.0f - yinBuffer[(size_t) tauEstimate];
        return (betterTau > 0.0f) ? (float) sampleRate / betterTau : lastFreq;
    }

    double sampleRate = 44100.0;
    int    minLag = 0, maxLag = 0, span = 0, windowSize = 0, mask = 0;
    std::vector<float> circularBuffer, linearBuffer, yinBuffer;
    int   writePos = 0, hopCounter = 0;
    float lastFreq = 0.0f, lastConfidence = 0.0f;
    double lpCoeff = 0.0;
    double lpState = 0.0;
};