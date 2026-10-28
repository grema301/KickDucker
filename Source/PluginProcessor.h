#pragma once
#include <JuceHeader.h>
#include "BiquadFilter.h"
#include "PitchDetector.h"

class KickDuckerAudioProcessor : public juce::AudioProcessor
{
public:
    KickDuckerAudioProcessor();
    ~KickDuckerAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi()  const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms() override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    //==========================================================================
    // Atomics - written on audio thread, read by UI timer at 30Hz.
    // memory_order_relaxed throughout: stale display values are fine.
    std::atomic<float> detectedFrequencyHz  { 0.0f };   // 0 until a pitch has been detected
    std::atomic<float> sidechainActive      { 0.0f };
    std::atomic<float> currentAppliedGainDB { 0.0f };
    std::atomic<float> sidechainLevelDB     { -80.0f }; // RMS level of sidechain input

    // Allows the editor to hold a WeakReference that safely becomes nullptr
    // when this processor is destroyed - prevents the timer crash on FL disable.
    JUCE_DECLARE_WEAK_REFERENCEABLE(KickDuckerAudioProcessor)

    //==========================================================================
    // Spectrum FIFO - audio thread writes the main input (pre-processing),
    // UI thread reads and runs the FFT. The audio thread only writes while an
    // editor is open (spectrumActive), so the FIFO never holds stale audio.
    static constexpr int kSpectrumFifoSize = 4096;
    juce::AbstractFifo            spectrumFifo { kSpectrumFifoSize };
    std::array<float, kSpectrumFifoSize> spectrumFifoData {};
    std::atomic<bool>             spectrumActive { false };

private:
    //==========================================================================
    // DSP - audio thread only
    PitchDetector pitchDetector;

    // Fundamental - one biquad per channel
    BiquadFilter eqL, eqR;

    // Harmonics - three additional biquads per channel (2nd, 3rd, 4th)
    BiquadFilter h2L, h2R;
    BiquadFilter h3L, h3R;
    BiquadFilter h4L, h4R;

    void resetFilters() noexcept;

    //==========================================================================
    // Smoothed values - all audio-thread-only
    juce::SmoothedValue<float> smoothedFrequency;
    juce::SmoothedValue<float> smoothedQ;
    juce::SmoothedValue<float> smoothedAppliedGain;
    juce::SmoothedValue<float> smoothedMix;        // also carries the bypass crossfade
    juce::SmoothedValue<float> smoothedOutputGain; // linear gain

    //==========================================================================
    // Envelope follower state
    float envelopeLevel  = 0.0f;
    float attackCoeff    = 0.0f;
    float releaseCoeff   = 0.0f;

    //==========================================================================
    // Pitch / lock state
    bool  hasPitch        = false;   // true once a frequency has been detected (or locked)
    std::atomic<float> lockedFrequency { 0.0f };  // 0 = not locked. Atomic: also read when saving state

    //==========================================================================
    // Session state
    double currentSampleRate = 44100.0;
    int    coeffUpdateCounter = 0;
    static constexpr int kCoeffUpdateInterval = 32;

    // Change guards - only update coefficients when parameter values change
    float lastAttackMs    = -1.0f;
    float lastReleaseMs   = -1.0f;
    float lastSmoothingMs = -1.0f;

    //==========================================================================
    // Raw parameter pointers - set once in constructor, valid for plugin lifetime
    std::atomic<float>* pAttenuation  = nullptr;
    std::atomic<float>* pQ            = nullptr;
    std::atomic<float>* pSmoothing    = nullptr;
    std::atomic<float>* pAttack       = nullptr;
    std::atomic<float>* pRelease      = nullptr;
    std::atomic<float>* pHarmonicAmt  = nullptr;
    std::atomic<float>* pMix          = nullptr;
    std::atomic<float>* pOutputGain   = nullptr;
    std::atomic<float>* pNoteQuantize = nullptr;
    std::atomic<float>* pFreqLock     = nullptr;
    std::atomic<float>* pBypass       = nullptr;

    //==========================================================================
    void pushToSpectrumFifo(const float* data, int numSamples);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KickDuckerAudioProcessor)
};