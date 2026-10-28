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
void KickDuckerAudioProcessor::resetFilters() noexcept
{
    eqL.reset(); eqR.reset();
    h2L.reset(); h2R.reset();
    h3L.reset(); h3R.reset();
    h4L.reset(); h4R.reset();
}

void KickDuckerAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;

    pitchDetector.prepare(sampleRate);
    resetFilters();

    // Smoothed values
    smoothedFrequency.reset(sampleRate, 0.15);
    smoothedFrequency.setCurrentAndTargetValue(80.0f);

    smoothedQ.reset(sampleRate, 0.05);
    smoothedQ.setCurrentAndTargetValue(pQ->load());

    // Short ramp: the envelope follower already provides the musical Attack/Release,
    // this only de-zippers the per-sample gain value that feeds the biquads.
    smoothedAppliedGain.reset(sampleRate, 0.002);
    smoothedAppliedGain.setCurrentAndTargetValue(0.0f);

    const bool bypassed = pBypass->load() > 0.5f;

    smoothedMix.reset(sampleRate, 0.05);
    smoothedMix.setCurrentAndTargetValue(bypassed ? 0.0f : pMix->load());

    // Output gain: smoothed in linear domain to prevent clicks
    smoothedOutputGain.reset(sampleRate, 0.05);
    smoothedOutputGain.setCurrentAndTargetValue(
        bypassed ? 1.0f : juce::Decibels::decibelsToGain(pOutputGain->load()));

    // Envelope coefficients
    const float attackMs  = pAttack->load();
    const float releaseMs = pRelease->load();
    attackCoeff  = std::exp(-1.0f / (attackMs  * (float) sampleRate / 1000.0f));
    releaseCoeff = std::exp(-1.0f / (releaseMs * (float) sampleRate / 1000.0f));
    lastAttackMs  = attackMs;
    lastReleaseMs = releaseMs;

    envelopeLevel      = 0.0f;
    coeffUpdateCounter = 0;
    lastSmoothingMs    = -1.0f;
    hasPitch           = false;

    detectedFrequencyHz .store(0.0f);
    sidechainActive     .store(0.0f);
    currentAppliedGainDB.store(0.0f);
    sidechainLevelDB    .store(-80.0f);
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

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0) return;

    // The buffer JUCE hands us contains the main bus AND the sidechain channels.
    // Work on the main bus explicitly: with a mono main bus and a stereo sidechain,
    // buffer.getNumChannels() is 3 and "channel 1" would be the sidechain's left channel.
    auto mainBuffer = getBusBuffer(buffer, true, 0);
    const int numChannels = mainBuffer.getNumChannels();
    if (numChannels <= 0) return;

    // Spectrum display taps the unprocessed main input, only while an editor is open
    if (spectrumActive.load(std::memory_order_relaxed))
        pushToSpectrumFifo(mainBuffer.getReadPointer(0), numSamples);

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
    const bool  bypassed          = pBypass->load()       > 0.5f;

    // Update the glide time only when the knob moves. SmoothedValue::reset() jumps
    // the current value to the target, so remember where we were and restore it.
    if (smoothingMs != lastSmoothingMs)
    {
        const float cur = smoothedFrequency.getCurrentValue();
        const float tgt = smoothedFrequency.getTargetValue();
        smoothedFrequency.reset(currentSampleRate, smoothingMs / 1000.0);
        smoothedFrequency.setCurrentAndTargetValue(cur);
        smoothedFrequency.setTargetValue(tgt);
        lastSmoothingMs = smoothingMs;
    }

    smoothedQ.setTargetValue(targetQ);

    // Bypass is a crossfade instead of an early return: no click, filters stay
    // warm, and the meters keep working while bypassed.
    smoothedMix.setTargetValue(bypassed ? 0.0f : targetMix);
    smoothedOutputGain.setTargetValue(bypassed ? 1.0f : juce::Decibels::decibelsToGain(outputGainDB));

    // Rebuild envelope coefficients only when attack/release params change
    if (attackMs != lastAttackMs || releaseMs != lastReleaseMs)
    {
        attackCoeff  = std::exp(-1.0f / (attackMs  * (float) currentSampleRate / 1000.0f));
        releaseCoeff = std::exp(-1.0f / (releaseMs * (float) currentSampleRate / 1000.0f));
        lastAttackMs  = attackMs;
        lastReleaseMs = releaseMs;
    }

    // ------------------------------------------------------------------
    // Frequency lock. lockedFrequency == 0 means "not locked".
    // Locking before any pitch has been found simply arms the lock: detection
    // keeps running until the first pitch arrives, then freezes on it.
    // lockedFrequency is saved with the session so a locked project reopens locked.
    // ------------------------------------------------------------------
    if (! freqLock)
        lockedFrequency.store(0.0f);
    else if (lockedFrequency.load() <= 0.0f && hasPitch)
        lockedFrequency.store(smoothedFrequency.getCurrentValue());

    const float lockedHz = lockedFrequency.load();
    const bool  locked   = freqLock && lockedHz > 0.0f;

    if (locked)
    {
        if (! hasPitch)                       // restored from a saved session
        {
            smoothedFrequency.setCurrentAndTargetValue(lockedHz);
            hasPitch = true;
        }
        smoothedFrequency.setTargetValue(lockedHz);
    }

    // ------------------------------------------------------------------
    // Sidechain: pitch detection + level
    // ------------------------------------------------------------------
    const float* scL = nullptr;
    const float* scR = nullptr;

    auto* sidechainBus = getBus(true, 1);
    const bool hasSidechain = (sidechainBus != nullptr && sidechainBus->isEnabled());

    if (hasSidechain)
    {
        auto scBuffer = getBusBuffer(buffer, true, 1);
        const int scChannels = scBuffer.getNumChannels();

        // RMS power check - skip detection on silence
        float scPower = 0.0f;
        for (int ch = 0; ch < scChannels; ++ch)
        {
            auto* d = scBuffer.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i) scPower += d[i] * d[i];
        }
        scPower /= (float) (numSamples * juce::jmax(1, scChannels));
        const bool scHasSignal = scPower > 1e-7f;
        sidechainActive.store(scHasSignal ? 1.0f : 0.0f, std::memory_order_relaxed);

        // 10*log10 because scPower is already mean-squared.
        const float scDB = scHasSignal ? 10.0f * std::log10(scPower) : -80.0f;
        sidechainLevelDB.store(juce::jlimit(-80.0f, 0.0f, scDB), std::memory_order_relaxed);

        // The detector's ring buffer is always fed (so it never holds stale audio);
        // the expensive YIN analysis only runs when there is signal and no lock.
        const bool  analyse  = scHasSignal && ! locked;
        const float detected = pitchDetector.process(scBuffer, analyse);

        if (analyse &&
            detected > PitchDetector::kMinFreq &&
            detected < PitchDetector::kMaxFreq &&
            pitchDetector.getLastConfidence() > PitchDetector::kMinConfidence)
        {
            const float target = noteQuantize ? PitchDetector::quantizeToSemitone(detected) : detected;

            // First detection: jump straight there instead of gliding in from the 80 Hz placeholder
            if (! hasPitch)
                smoothedFrequency.setCurrentAndTargetValue(target);

            hasPitch = true;
            smoothedFrequency.setTargetValue(target);
        }

        scL = scBuffer.getReadPointer(0);
        scR = (scChannels > 1) ? scBuffer.getReadPointer(1) : nullptr;
    }
    else
    {
        sidechainActive.store(0.0f, std::memory_order_relaxed);
    }

    // ------------------------------------------------------------------
    // Per-sample processing
    //   envelope -> gain -> (every 32 samples) biquad coefficients -> filters -> mix
    // The envelope runs per sample, so the ducking no longer depends on the host's
    // buffer size (it used to be sampled once per block).
    // ------------------------------------------------------------------
    float* chL = mainBuffer.getWritePointer(0);
    float* chR = (numChannels > 1) ? mainBuffer.getWritePointer(1) : nullptr;

    const float invFullScale = 1.0f / KickDuckerDSP::kEnvelopeFullScale;
    const float sr           = (float) currentSampleRate;

    for (int i = 0; i < numSamples; ++i)
    {
        // Envelope follower on the louder of the sidechain channels
        float level = 0.0f;
        if (scL != nullptr)
        {
            level = std::fabs(scL[i]);
            if (scR != nullptr) level = juce::jmax(level, std::fabs(scR[i]));
        }
        const float coeff = (level > envelopeLevel) ? attackCoeff : releaseCoeff;
        envelopeLevel = coeff * envelopeLevel + (1.0f - coeff) * level;

        // No pitch yet -> nothing sensible to duck
        const float normalizedEnv = hasPitch ? juce::jmin(1.0f, envelopeLevel * invFullScale) : 0.0f;
        smoothedAppliedGain.setTargetValue(targetAttenuation * normalizedEnv);

        const float currentFreq    = smoothedFrequency.getNextValue();
        const float currentQ       = smoothedQ.getNextValue();
        const float currentGain    = smoothedAppliedGain.getNextValue();
        const float currentMix     = smoothedMix.getNextValue();
        const float currentOutGain = smoothedOutputGain.getNextValue();

        // Recompute biquad coefficients every kCoeffUpdateInterval samples.
        // L and R share one set of coefficients (each setBell costs several trig calls).
        if (++coeffUpdateCounter >= kCoeffUpdateInterval)
        {
            coeffUpdateCounter = 0;

            eqL.setBell(currentFreq,        currentGain, currentQ, sr);
            h2L.setBell(currentFreq * 2.0f, currentGain * harmonicAmt * KickDuckerDSP::kHarmonicGain[0], currentQ, sr);
            h3L.setBell(currentFreq * 3.0f, currentGain * harmonicAmt * KickDuckerDSP::kHarmonicGain[1], currentQ, sr);
            h4L.setBell(currentFreq * 4.0f, currentGain * harmonicAmt * KickDuckerDSP::kHarmonicGain[2], currentQ, sr);

            eqR.setCoefficientsFrom(eqL);
            h2R.setCoefficientsFrom(h2L);
            h3R.setCoefficientsFrom(h3L);
            h4R.setCoefficientsFrom(h4L);
        }

        // Harmonic bells always run: at 0 dB gain a bell is an exact identity, so
        // there's no need for a branch (and no stale filter state when the
        // Harmonics knob is brought up from zero).
        const float dryL = chL[i];
        float wetL = h4L.processSample(h3L.processSample(h2L.processSample(eqL.processSample(dryL))));
        if (! std::isfinite(wetL)) { resetFilters(); wetL = dryL; }   // a stray NaN/Inf must not latch
        chL[i] = (dryL + (wetL - dryL) * currentMix) * currentOutGain;

        if (chR != nullptr)
        {
            const float dryR = chR[i];
            float wetR = h4R.processSample(h3R.processSample(h2R.processSample(eqR.processSample(dryR))));
            if (! std::isfinite(wetR)) { resetFilters(); wetR = dryR; }
            chR[i] = (dryR + (wetR - dryR) * currentMix) * currentOutGain;
        }
    }

    // Publish for GUI display (0 Hz = "no pitch detected yet")
    detectedFrequencyHz .store(hasPitch ? smoothedFrequency.getCurrentValue() : 0.0f, std::memory_order_relaxed);
    currentAppliedGainDB.store(smoothedAppliedGain.getCurrentValue(), std::memory_order_relaxed);
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

    // The locked frequency isn't a parameter, so store it alongside them
    state.removeProperty("lockedFrequencyHz", nullptr);
    const float lockedHz = lockedFrequency.load();
    if (pFreqLock->load() > 0.5f && lockedHz > 0.0f)
        state.setProperty("lockedFrequencyHz", (double) lockedHz, nullptr);

    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void KickDuckerAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState && xmlState->hasTagName(apvts.state.getType()))
    {
        auto newState = juce::ValueTree::fromXml(*xmlState);
        const double lockedHz = (double) newState.getProperty("lockedFrequencyHz", 0.0);

        apvts.replaceState(newState);
        lockedFrequency.store(pFreqLock->load() > 0.5f ? (float) lockedHz : 0.0f);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KickDuckerAudioProcessor();
}