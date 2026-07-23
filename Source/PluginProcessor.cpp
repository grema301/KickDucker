#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
KickDuckerAudioProcessor::KickDuckerAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput ("Input",     juce::AudioChannelSet::stereo(), true)
                         .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output",    juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    pAttenuation  = apvts.getRawParameterValue("ATTENUATION");
    pQ            = apvts.getRawParameterValue("Q_WIDTH");
    pSmoothing    = apvts.getRawParameterValue("SMOOTHING");
    pAttack       = apvts.getRawParameterValue("ATTACK");
    pRelease      = apvts.getRawParameterValue("RELEASE");
    pHarmonicAmt  = apvts.getRawParameterValue("HARMONIC_AMT");
    pMix          = apvts.getRawParameterValue("MIX");
    pOutputGain   = apvts.getRawParameterValue("OUTPUT_GAIN");
    pNoteQuantize = apvts.getRawParameterValue("NOTE_QUANTIZE");
    pFreqLock     = apvts.getRawParameterValue("FREQ_LOCK");
    pBypass       = apvts.getRawParameterValue("BYPASS");
}

KickDuckerAudioProcessor::~KickDuckerAudioProcessor() {}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
KickDuckerAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"ATTENUATION", 1}, "Attenuation",
        juce::NormalisableRange<float>(-18.0f, 0.0f, 0.1f), -6.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"Q_WIDTH", 1}, "Q Width",
        juce::NormalisableRange<float>(0.5f, 8.0f, 0.01f, 0.5f), 2.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"SMOOTHING", 1}, "Smoothing",
        juce::NormalisableRange<float>(10.0f, 500.0f, 1.0f), 150.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"ATTACK", 1}, "Attack",
        juce::NormalisableRange<float>(1.0f, 150.0f, 0.1f, 0.3f), 5.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"RELEASE", 1}, "Release",
        juce::NormalisableRange<float>(10.0f, 1000.0f, 0.1f, 0.3f), 120.0f));

    // Harmonic amount: 0=off, 1=full harmonic ducking at 2x/3x/4x fundamental
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"HARMONIC_AMT", 1}, "Harmonic Amt",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f));

    // Mix: 0=fully dry (no effect), 1=fully wet (full processing)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"MIX", 1}, "Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));

    // Output gain: compensate for level lost to notch attenuation
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"OUTPUT_GAIN", 1}, "Output Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));

    // Snap detected pitch to nearest semitone before driving the EQ
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"NOTE_QUANTIZE", 1}, "Note Quantize", false));

    // Freeze detection at the current frequency
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"FREQ_LOCK", 1}, "Freq Lock", false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"BYPASS", 1}, "Bypass", false));

    return { params.begin(), params.end() };
}

//==============================================================================
void KickDuckerAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    pitchDetector.prepare(sampleRate);

    // Reset all filter states
    eqL.reset(); eqR.reset();
    h2L.reset(); h2R.reset();
    h3L.reset(); h3R.reset();
    h4L.reset(); h4R.reset();

    // Pre-allocate dry buffer — never allocate in processBlock
    dryBuffer.setSize(2, samplesPerBlock);
    dryBuffer.clear();

    // Smoothed values
    smoothedFrequency.reset(sampleRate, 0.15);
    smoothedFrequency.setCurrentAndTargetValue(80.0f);

    smoothedQ.reset(sampleRate, 0.05);
    smoothedQ.setCurrentAndTargetValue(pQ->load());

    smoothedAppliedGain.reset(sampleRate, 0.01);
    smoothedAppliedGain.setCurrentAndTargetValue(0.0f);

    smoothedMix.reset(sampleRate, 0.05);
    smoothedMix.setCurrentAndTargetValue(pMix->load());

    // Output gain: smoothed in linear domain to prevent clicks
    smoothedOutputGain.reset(sampleRate, 0.05);
    smoothedOutputGain.setCurrentAndTargetValue(
        juce::Decibels::decibelsToGain(pOutputGain->load()));

    // Envelope coefficients
    const float attackMs  = pAttack->load();
    const float releaseMs = pRelease->load();
    attackCoeff  = std::exp(-1.0f / (attackMs  * (float)sampleRate / 1000.0f));
    releaseCoeff = std::exp(-1.0f / (releaseMs * (float)sampleRate / 1000.0f));
    lastAttackMs  = attackMs;
    lastReleaseMs = releaseMs;

    envelopeLevel      = 0.0f;
    coeffUpdateCounter = 0;
    lastSmoothingMs    = -1.0f;
    lastLockState      = false;
    lockedFrequency    = 0.0f;
}

void KickDuckerAudioProcessor::releaseResources() {}

//==============================================================================
bool KickDuckerAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    auto mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != juce::AudioChannelSet::mono() &&
        mainOut != juce::AudioChannelSet::stereo()) return false;
    if (layouts.getMainInputChannelSet() != mainOut) return false;

    auto sc = layouts.getChannelSet(true, 1);
    if (!sc.isDisabled() &&
        sc != juce::AudioChannelSet::mono() &&
        sc != juce::AudioChannelSet::stereo()) return false;

    return true;
}

//==============================================================================
void KickDuckerAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (pBypass->load() > 0.5f)
    {
        currentAppliedGainDB.store(0.0f, std::memory_order_relaxed);
        return;
    }

    // ------------------------------------------------------------------
    // Push main input (dry) to spectrum FIFO before any processing.
    // The spectrum display shows the unprocessed kick signal — this way
    // the user sees their signal and the EQ curve being applied to it.
    // ------------------------------------------------------------------
    pushToSpectrumFifo(buffer.getReadPointer(0), numSamples);

    // ------------------------------------------------------------------
    // Save dry signal for mix blend — copy before any processing
    // ------------------------------------------------------------------
    for (int ch = 0; ch < juce::jmin(numChannels, 2); ++ch)
        dryBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

    // ------------------------------------------------------------------
    // Read parameters
    // ------------------------------------------------------------------
    const float targetAttenuation = pAttenuation->load();
    const float targetQ           = pQ->load();
    const float smoothingMs       = pSmoothing->load();
    const float attackMs          = pAttack->load();
    const float releaseMs         = pRelease->load();
    const float harmonicAmt       = pHarmonicAmt->load();
    const float targetMix         = pMix->load();
    const float outputGainDB      = pOutputGain->load();
    const bool  noteQuantize      = pNoteQuantize->load() > 0.5f;
    const bool  freqLock          = pFreqLock->load()     > 0.5f;

    // Update smoothing ramp time only when the parameter changes
    if (smoothingMs != lastSmoothingMs)
    {
        smoothedFrequency.reset(currentSampleRate, smoothingMs / 1000.0);
        lastSmoothingMs = smoothingMs;
    }
    smoothedQ.setTargetValue(targetQ);
    smoothedMix.setTargetValue(targetMix);
    smoothedOutputGain.setTargetValue(juce::Decibels::decibelsToGain(outputGainDB));

    // Rebuild envelope coefficients only when attack/release params change
    if (attackMs != lastAttackMs || releaseMs != lastReleaseMs)
    {
        attackCoeff  = std::exp(-1.0f / (attackMs  * (float)currentSampleRate / 1000.0f));
        releaseCoeff = std::exp(-1.0f / (releaseMs * (float)currentSampleRate / 1000.0f));
        lastAttackMs  = attackMs;
        lastReleaseMs = releaseMs;
    }

    // ------------------------------------------------------------------
    // Frequency lock: capture current frequency on rising edge of LOCK
    // ------------------------------------------------------------------
    if (freqLock && !lastLockState)
        lockedFrequency = smoothedFrequency.getCurrentValue();
    lastLockState = freqLock;

    // ------------------------------------------------------------------
    // Sidechain: pitch detection + envelope follower
    // ------------------------------------------------------------------
    auto* sidechainBus = getBus(true, 1);
    bool  hasSidechain = (sidechainBus != nullptr && sidechainBus->isEnabled());

    if (hasSidechain)
    {
        auto scBuffer = sidechainBus->getBusBuffer(buffer);

        // RMS power check — skip detection on silence
        float scPower = 0.0f;
        for (int ch = 0; ch < scBuffer.getNumChannels(); ++ch)
        {
            auto* d = scBuffer.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i) scPower += d[i] * d[i];
        }
        scPower /= (float)(numSamples * scBuffer.getNumChannels());
        const bool scHasSignal = scPower > 1e-7f;
        sidechainActive.store(scHasSignal ? 1.0f : 0.0f, std::memory_order_relaxed);

        // Store RMS level in dB for the header meter display.
        // 10*log10 because scPower is already mean-squared.
        const float scDB = scHasSignal ? 10.0f * std::log10(scPower) : -80.0f;
        sidechainLevelDB.store(juce::jlimit(-80.0f, 0.0f, scDB), std::memory_order_relaxed);

        if (scHasSignal && !freqLock)
        {
            float detected    = pitchDetector.process(scBuffer);
            float confidence  = pitchDetector.getLastConfidence();

            if (detected > PitchDetector::kMinFreq &&
                detected < PitchDetector::kMaxFreq &&
                confidence > PitchDetector::kYINThreshold)
            {
                // Apply note quantization if enabled
                if (noteQuantize)
                    detected = PitchDetector::quantizeToSemitone(detected);

                smoothedFrequency.setTargetValue(detected);
            }
        }
        else if (freqLock && lockedFrequency > 0.0f)
        {
            smoothedFrequency.setTargetValue(lockedFrequency);
        }

        // Envelope follower on sidechain amplitude
        const float* scData = scBuffer.getReadPointer(0);
        for (int i = 0; i < numSamples; ++i)
        {
            float absLevel = std::fabs(scData[i]);
            if (absLevel > envelopeLevel)
                envelopeLevel = attackCoeff  * envelopeLevel + (1.0f - attackCoeff)  * absLevel;
            else
                envelopeLevel = releaseCoeff * envelopeLevel + (1.0f - releaseCoeff) * absLevel;
        }
    }
    else
    {
        sidechainActive.store(0.0f, std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i)
            envelopeLevel = releaseCoeff * envelopeLevel;
    }

    // ------------------------------------------------------------------
    // Envelope → gain modulation
    // kNormRef: sidechain level at which full attenuation is applied.
    // At -12dBFS (0.25 linear), the 808 is loud enough to warrant max ducking.
    // Quieter signals get proportionally less attenuation — smooth tails.
    // ------------------------------------------------------------------
    const float kNormRef    = 0.25f;
    float normalizedEnv     = juce::jlimit(0.0f, 1.0f, envelopeLevel / kNormRef);
    float targetGainDB      = targetAttenuation * normalizedEnv;
    smoothedAppliedGain.setTargetValue(targetGainDB);

    // Publish for GUI display
    detectedFrequencyHz.store(smoothedFrequency.getCurrentValue(), std::memory_order_relaxed);
    currentAppliedGainDB.store(smoothedAppliedGain.getCurrentValue(), std::memory_order_relaxed);

    // ------------------------------------------------------------------
    // Per-sample processing loop
    // ------------------------------------------------------------------
    float* chL = buffer.getWritePointer(0);
    float* chR = (numChannels > 1) ? buffer.getWritePointer(1) : nullptr;

    const float* dryL = dryBuffer.getReadPointer(0);
    const float* dryR = (numChannels > 1) ? dryBuffer.getReadPointer(1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const float currentFreq    = smoothedFrequency.getNextValue();
        const float currentQ       = smoothedQ.getNextValue();
        const float currentGain    = smoothedAppliedGain.getNextValue();
        const float currentMix     = smoothedMix.getNextValue();
        const float currentOutGain = smoothedOutputGain.getNextValue();

        // Recompute biquad coefficients every kCoeffUpdateInterval samples.
        // At 44100Hz and interval=32: ~1378 updates/sec — perceptually seamless.
        if (++coeffUpdateCounter >= kCoeffUpdateInterval)
        {
            coeffUpdateCounter = 0;
            const float sr = (float)currentSampleRate;

            // Fundamental
            eqL.setBell(currentFreq, currentGain, currentQ, sr);
            eqR.setBell(currentFreq, currentGain, currentQ, sr);

            // 2nd harmonic: 70% of fundamental gain, rolled off by harmonicAmt
            const float h2g = currentGain * harmonicAmt * 0.70f;
            h2L.setBell(currentFreq * 2.0f, h2g, currentQ, sr);
            h2R.setBell(currentFreq * 2.0f, h2g, currentQ, sr);

            // 3rd harmonic: 50%
            const float h3g = currentGain * harmonicAmt * 0.50f;
            h3L.setBell(currentFreq * 3.0f, h3g, currentQ, sr);
            h3R.setBell(currentFreq * 3.0f, h3g, currentQ, sr);

            // 4th harmonic: 30%
            const float h4g = currentGain * harmonicAmt * 0.30f;
            h4L.setBell(currentFreq * 4.0f, h4g, currentQ, sr);
            h4R.setBell(currentFreq * 4.0f, h4g, currentQ, sr);
        }

        // Process left channel through all active biquads
        float wetL = eqL.processSample(chL[i]);
        if (harmonicAmt > 0.001f)
        {
            wetL = h2L.processSample(wetL);
            wetL = h3L.processSample(wetL);
            wetL = h4L.processSample(wetL);
        }

        // Wet/dry mix then output gain
        chL[i] = (dryL[i] * (1.0f - currentMix) + wetL * currentMix) * currentOutGain;

        // Right channel
        if (chR != nullptr)
        {
            float wetR = eqR.processSample(chR[i]);
            if (harmonicAmt > 0.001f)
            {
                wetR = h2R.processSample(wetR);
                wetR = h3R.processSample(wetR);
                wetR = h4R.processSample(wetR);
            }
            chR[i] = (dryR[i] * (1.0f - currentMix) + wetR * currentMix) * currentOutGain;
        }
    }
}

//==============================================================================
void KickDuckerAudioProcessor::pushToSpectrumFifo(const float* data, int numSamples)
{
    // Called on audio thread. AbstractFifo is SPSC-safe.
    int start1, size1, start2, size2;
    spectrumFifo.prepareToWrite(numSamples, start1, size1, start2, size2);
    if (size1 > 0) juce::FloatVectorOperations::copy(spectrumFifoData.data() + start1, data, size1);
    if (size2 > 0) juce::FloatVectorOperations::copy(spectrumFifoData.data() + start2, data + size1, size2);
    spectrumFifo.finishedWrite(size1 + size2);
}

//==============================================================================
juce::AudioProcessorEditor* KickDuckerAudioProcessor::createEditor()
{
    return new KickDuckerAudioProcessorEditor(*this);
}

void KickDuckerAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void KickDuckerAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState && xmlState->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KickDuckerAudioProcessor();
}