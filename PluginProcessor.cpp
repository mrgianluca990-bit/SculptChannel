#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

SculptChannelAudioProcessor::SculptChannelAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    for (auto& m : resMeters)  m.store (0.0f);
    for (auto& m : compMeters) m.store (0.0f);
    for (auto& m : satMeters)  m.store (0.0f);
}

juce::AudioProcessorValueTreeState::ParameterLayout
SculptChannelAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto addMacro = [&params] (const char* id, const char* name)
    {
        params.push_back (std::make_unique<juce::AudioParameterFloat>(
            id, name,
            juce::NormalisableRange<float> { -100.0f, 100.0f, 0.1f },
            0.0f, "%"));
    };

    addMacro ("low",      "Low");
    addMacro ("mid",      "Mid");
    addMacro ("high",     "High");
    addMacro ("presence", "Presence");

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "output", "Output",
        juce::NormalisableRange<float> { -18.0f, 6.0f, 0.01f },
        0.0f, "dB"));

    return { params.begin(), params.end() };
}

bool SculptChannelAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;

    return out == juce::AudioChannelSet::mono()
        || out == juce::AudioChannelSet::stereo();
}

void SculptChannelAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    const auto channels = static_cast<juce::uint32> (juce::jmax (1, getTotalNumOutputChannels()));
    juce::dsp::ProcessSpec spec {
        sampleRate,
        static_cast<juce::uint32> (samplesPerBlock),
        channels
    };

    for (auto& band : bands)
    {
        band.preFilter.prepare (spec);
        band.preFilter.reset();
        band.compressor.prepare (spec);
        band.compressor.reset();
        band.work.setSize (static_cast<int> (channels), samplesPerBlock);
        band.detectorEnv = 0.0f;
        band.smoothEnergy = 0.0f;
        band.previousInput = 0.0f;
    }

    dryBuffer.setSize (static_cast<int> (channels), samplesPerBlock);

    outputGain.reset (sampleRate, 0.03);
    outputGain.setCurrentAndTargetValue (1.0f);

    configureBandFilters();
}

void SculptChannelAudioProcessor::configureBandFilters()
{
    for (int i = 0; i < numBands; ++i)
    {
        bands[static_cast<size_t> (i)].preFilter.setType (
            juce::dsp::StateVariableTPTFilterType::bandpass);

        bands[static_cast<size_t> (i)].preFilter.setCutoffFrequency (
            centreHz[static_cast<size_t> (i)]);

        bands[static_cast<size_t> (i)].preFilter.setResonance (
            qValues[static_cast<size_t> (i)]);
    }
}

float SculptChannelAudioProcessor::processResonanceControl (
    BandState& state,
    float sample,
    float amount,
    int bandIndex) noexcept
{
    // Lightweight resonance detector:
    // compares short-term energy to a slower envelope and only acts on local excesses.
    const float rectified = std::abs (sample);

    const float fastCoeff = std::exp (
        -1.0f / static_cast<float> (currentSampleRate * 0.004));

    const float slowCoeff = std::exp (
        -1.0f / static_cast<float> (currentSampleRate * 0.070));

    state.detectorEnv = fastCoeff * state.detectorEnv
                      + (1.0f - fastCoeff) * rectified;

    state.smoothEnergy = slowCoeff * state.smoothEnergy
                       + (1.0f - slowCoeff) * rectified;

    const float excess = juce::jmax (0.0f, state.detectorEnv - state.smoothEnergy);

    // Presence/high are allowed to react slightly faster/stronger than low.
    const float sensitivity = 1.5f + static_cast<float> (bandIndex) * 0.25f;
    const float reduction = juce::jlimit (0.0f, 0.55f, excess * sensitivity * amount);

    resMeters[static_cast<size_t> (bandIndex)].store (
        juce::jlimit (0.0f, 1.0f, reduction * 2.0f));

    return sample * (1.0f - reduction);
}

float SculptChannelAudioProcessor::processSaturation (
    float sample,
    float positiveAmount,
    float negativeAmount,
    int bandIndex) const noexcept
{
    // Positive side: progressively denser.
    // Negative side: intentionally cleaner, with much less saturation.
    const float pos = juce::jlimit (0.0f, 1.0f, positiveAmount);
    const float neg = juce::jlimit (0.0f, 1.0f, negativeAmount);

    float drive = 1.0f;
    float blend = 0.0f;

    switch (bandIndex)
    {
        case 0: // Low: tape/transformer-ish, round
            drive = 1.0f + pos * 4.2f + neg * 0.7f;
            blend = pos * 0.55f + neg * 0.08f;
            break;

        case 1: // Mid: console-ish
            drive = 1.0f + pos * 3.4f + neg * 0.5f;
            blend = pos * 0.50f + neg * 0.06f;
            break;

        case 2: // High: softer saturation
            drive = 1.0f + pos * 2.3f + neg * 0.3f;
            blend = pos * 0.36f + neg * 0.04f;
            break;

        default: // Presence: very gentle harmonics
            drive = 1.0f + pos * 1.6f + neg * 0.2f;
            blend = pos * 0.24f + neg * 0.025f;
            break;
    }

    const float shaped = std::tanh (sample * drive);
    return sample + (shaped - sample) * blend;
}

void SculptChannelAudioProcessor::processBand (
    juce::AudioBuffer<float>& source,
    int bandIndex,
    float macroValue)
{
    auto& band = bands[static_cast<size_t> (bandIndex)];

    const float signedAmount = juce::jlimit (-1.0f, 1.0f, macroValue / 100.0f);
    const float magnitude = std::abs (signedAmount);

    const float positive = juce::jmax (0.0f, signedAmount);
    const float negative = juce::jmax (0.0f, -signedAmount);

    // Progressive ordering:
    // resonance control is active first and strongest,
    // compression grows next,
    // saturation comes in last.
    const float resAmount = juce::jlimit (0.0f, 1.0f, magnitude * 1.15f);
    const float compAmount = juce::jlimit (0.0f, 1.0f,
                                          (magnitude - 0.12f) / 0.88f);
    const float satAmount = juce::jlimit (0.0f, 1.0f,
                                         (magnitude - 0.28f) / 0.72f);

    band.work.makeCopyOf (source, true);

    // Extract each analog-style region with a broad fixed band-pass.
    juce::dsp::AudioBlock<float> bandBlock (band.work);
    juce::dsp::ProcessContextReplacing<float> filterContext (bandBlock);
    band.preFilter.process (filterContext);

    float preRms = 0.0f;
    for (int ch = 0; ch < band.work.getNumChannels(); ++ch)
        preRms += band.work.getRMSLevel (ch, 0, band.work.getNumSamples());

    preRms /= static_cast<float> (juce::jmax (1, band.work.getNumChannels()));

    // Resonance suppression stage
    for (int ch = 0; ch < band.work.getNumChannels(); ++ch)
    {
        auto* data = band.work.getWritePointer (ch);

        for (int i = 0; i < band.work.getNumSamples(); ++i)
            data[i] = processResonanceControl (
                band, data[i], resAmount, bandIndex);
    }

    // Band-dependent compression.
    float attackMs = 15.0f;
    float releaseMs = 120.0f;

    switch (bandIndex)
    {
        case 0: attackMs = 26.0f; releaseMs = 170.0f; break;
        case 1: attackMs = 14.0f; releaseMs = 115.0f; break;
        case 2: attackMs = 7.0f;  releaseMs = 85.0f;  break;
        default: attackMs = 3.5f; releaseMs = 65.0f;  break;
    }

    // Negative side compresses more than it saturates.
    const float compBias = positive + negative * 1.15f;

    band.compressor.setThreshold (
        juce::jmap (compAmount * compBias,
                    0.0f, 1.0f,
                    -1.0f, -22.0f));

    band.compressor.setRatio (
        juce::jmap (compAmount,
                    0.0f, 1.0f,
                    1.0f, 3.6f));

    band.compressor.setAttack (attackMs);
    band.compressor.setRelease (releaseMs);

    juce::dsp::AudioBlock<float> compBlock (band.work);
    juce::dsp::ProcessContextReplacing<float> compContext (compBlock);
    band.compressor.process (compContext);

    float postCompRms = 0.0f;
    for (int ch = 0; ch < band.work.getNumChannels(); ++ch)
        postCompRms += band.work.getRMSLevel (ch, 0, band.work.getNumSamples());

    postCompRms /= static_cast<float> (juce::jmax (1, band.work.getNumChannels()));

    const float compGR = preRms > 0.000001f
        ? juce::jlimit (0.0f, 1.0f, (preRms - postCompRms) / preRms)
        : 0.0f;

    compMeters[static_cast<size_t> (bandIndex)].store (compGR);

    // Saturation stage
    float satActivity = 0.0f;

    for (int ch = 0; ch < band.work.getNumChannels(); ++ch)
    {
        auto* data = band.work.getWritePointer (ch);

        for (int i = 0; i < band.work.getNumSamples(); ++i)
        {
            const float before = data[i];
            const float after = processSaturation (
                before,
                satAmount * positive,
                satAmount * negative,
                bandIndex);

            satActivity += std::abs (after - before);
            data[i] = after;
        }
    }

    const float norm = static_cast<float> (
        juce::jmax (1, band.work.getNumSamples() * band.work.getNumChannels()));

    satMeters[static_cast<size_t> (bandIndex)].store (
        juce::jlimit (0.0f, 1.0f, satActivity / norm * 8.0f));

    // Perceptual tonal shift, not literal EQ gain:
    // positive = bring region forward with processed parallel energy.
    // negative = subtract controlled band energy, making it recede.
    const float forwardGain = positive * 0.48f;
    const float recessGain   = negative * 0.42f;

    for (int ch = 0; ch < source.getNumChannels(); ++ch)
    {
        auto* dst = source.getWritePointer (ch);
        const auto* bandData = band.work.getReadPointer (ch);

        for (int i = 0; i < source.getNumSamples(); ++i)
        {
            dst[i] += bandData[i] * forwardGain;
            dst[i] -= bandData[i] * recessGain;
        }
    }
}

void SculptChannelAudioProcessor::processBlock (
    juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    dryBuffer.setSize (buffer.getNumChannels(),
                       buffer.getNumSamples(),
                       false, false, true);

    dryBuffer.makeCopyOf (buffer, true);

    const float macros[numBands] = {
        apvts.getRawParameterValue ("low")->load(),
        apvts.getRawParameterValue ("mid")->load(),
        apvts.getRawParameterValue ("high")->load(),
        apvts.getRawParameterValue ("presence")->load()
    };

    for (int band = 0; band < numBands; ++band)
        processBand (buffer, band, macros[band]);

    const float outputDb = apvts.getRawParameterValue ("output")->load();
    outputGain.setTargetValue (
        juce::Decibels::decibelsToGain (outputDb));

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float g = outputGain.getNextValue();

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.setSample (ch, i, buffer.getSample (ch, i) * g);
    }
}

float SculptChannelAudioProcessor::getResMeter (int band) const noexcept
{
    return resMeters[static_cast<size_t> (juce::jlimit (0, numBands - 1, band))].load();
}

float SculptChannelAudioProcessor::getCompMeter (int band) const noexcept
{
    return compMeters[static_cast<size_t> (juce::jlimit (0, numBands - 1, band))].load();
}

float SculptChannelAudioProcessor::getSatMeter (int band) const noexcept
{
    return satMeters[static_cast<size_t> (juce::jlimit (0, numBands - 1, band))].load();
}

void SculptChannelAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SculptChannelAudioProcessor::setStateInformation (
    const void* data,
    int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }
}

juce::AudioProcessorEditor* SculptChannelAudioProcessor::createEditor()
{
    return new SculptChannelAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SculptChannelAudioProcessor();
}
