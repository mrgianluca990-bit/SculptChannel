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

    params.push_back (std::make_unique<juce::AudioParameterBool>(
        "variation", "Variation", false));

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

    const auto channels = static_cast<juce::uint32> (
        juce::jmax (1, getTotalNumOutputChannels()));

    juce::dsp::ProcessSpec spec {
        sampleRate,
        static_cast<juce::uint32> (samplesPerBlock),
        channels
    };

    for (auto& band : bands)
    {
        band.filter.prepare (spec);
        band.filter.reset();

        band.compressor.prepare (spec);
        band.compressor.reset();

        band.work.setSize (static_cast<int> (channels), samplesPerBlock);

        band.fastEnv = 0.0f;
        band.slowEnv = 0.0f;
    }

    for (auto& band : variationBands)
    {
        band.filter.prepare (spec);
        band.filter.reset();

        band.work.setSize (static_cast<int> (channels), samplesPerBlock);

        band.fastEnv = 0.0f;
        band.slowEnv = 0.0f;
    }

    dryBuffer.setSize (static_cast<int> (channels), samplesPerBlock);

    outputGain.reset (sampleRate, 0.03);
    outputGain.setCurrentAndTargetValue (1.0f);

    configureBandFilters();
    configureVariationFilters();
}

void SculptChannelAudioProcessor::configureBandFilters()
{
    for (int i = 0; i < numBands; ++i)
    {
        auto& filter = bands[static_cast<size_t> (i)].filter;

        switch (i)
        {
            case 0:
                filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
                filter.setCutoffFrequency (175.0f);
                filter.setResonance (0.58f);
                break;

            case 1:
                filter.setType (juce::dsp::StateVariableTPTFilterType::bandpass);
                filter.setCutoffFrequency (700.0f);
                filter.setResonance (1.08f);
                break;

            case 2:
                filter.setType (juce::dsp::StateVariableTPTFilterType::bandpass);
                filter.setCutoffFrequency (3500.0f);
                filter.setResonance (0.92f);
                break;

            default:
                filter.setType (juce::dsp::StateVariableTPTFilterType::highpass);
                filter.setCutoffFrequency (7000.0f);
                filter.setResonance (0.62f);
                break;
        }
    }
}

void SculptChannelAudioProcessor::configureVariationFilters()
{
    static constexpr float frequencies[numVariationBands] {
        200.0f, 500.0f, 1000.0f, 3000.0f, 5000.0f, 8000.0f
    };

    static constexpr float qValues[numVariationBands] {
        4.2f, 4.8f, 5.2f, 5.5f, 5.8f, 6.2f
    };

    for (int i = 0; i < numVariationBands; ++i)
    {
        auto& filter = variationBands[static_cast<size_t> (i)].filter;
        filter.setType (juce::dsp::StateVariableTPTFilterType::bandpass);
        filter.setCutoffFrequency (frequencies[i]);
        filter.setResonance (qValues[i]);
    }
}

float SculptChannelAudioProcessor::processResonanceControl (
    BandState& state,
    float sample,
    float amount,
    int bandIndex) noexcept
{
    const float rectified = std::abs (sample);

    const float fastMs =
        (bandIndex == 0 ? 8.0f :
         bandIndex == 3 ? 2.0f : 4.0f);

    const float slowMs =
        (bandIndex == 0 ? 90.0f :
         bandIndex == 3 ? 42.0f : 65.0f);

    const float fastCoeff = std::exp (
        -1.0f / static_cast<float> (
            currentSampleRate * (fastMs * 0.001f)));

    const float slowCoeff = std::exp (
        -1.0f / static_cast<float> (
            currentSampleRate * (slowMs * 0.001f)));

    state.fastEnv =
        fastCoeff * state.fastEnv
        + (1.0f - fastCoeff) * rectified;

    state.slowEnv =
        slowCoeff * state.slowEnv
        + (1.0f - slowCoeff) * rectified;

    const float excess =
        juce::jmax (0.0f, state.fastEnv - state.slowEnv);

    float sensitivity = 1.45f;

    switch (bandIndex)
    {
        case 0: sensitivity = 1.35f; break;
        case 1: sensitivity = 1.55f; break;
        case 2: sensitivity = 1.72f; break;
        default: sensitivity = 1.95f; break;
    }

    const float reduction =
        juce::jlimit (0.0f, 0.62f,
                      excess * sensitivity * amount);

    resMeters[static_cast<size_t> (bandIndex)].store (
        juce::jlimit (0.0f, 1.0f, reduction * 1.9f));

    return sample * (1.0f - reduction);
}

float SculptChannelAudioProcessor::processSaturation (
    float sample,
    float positiveAmount,
    float negativeAmount,
    int bandIndex) const noexcept
{
    const float pos =
        juce::jlimit (0.0f, 1.6f, positiveAmount);

    const float neg =
        juce::jlimit (0.0f, 1.2f, negativeAmount);

    float drive = 1.0f;
    float blend = 0.0f;
    float asym = 0.0f;

    switch (bandIndex)
    {
        case 0:
            drive = 1.0f + pos * 11.0f + neg * 1.4f;
            blend = pos * 0.92f + neg * 0.14f;
            asym = 0.05f;
            break;

        case 1:
            drive = 1.0f + pos * 8.0f + neg * 1.0f;
            blend = pos * 0.80f + neg * 0.10f;
            asym = 0.03f;
            break;

        case 2:
            drive = 1.0f + pos * 5.3f + neg * 0.75f;
            blend = pos * 0.60f + neg * 0.08f;
            asym = 0.02f;
            break;

        default:
            drive = 1.0f + pos * 3.8f + neg * 0.45f;
            blend = pos * 0.44f + neg * 0.05f;
            asym = 0.01f;
            break;
    }

    const float biased =
        sample
        + sample * sample * asym
          * (sample > 0.0f ? 1.0f : -1.0f);

    const float stage1 = std::tanh (biased * drive);
    const float stage2 =
        std::tanh (stage1 * (1.0f + pos * 0.55f));

    const float shaped =
        stage1
        + (stage2 - stage1)
          * juce::jlimit (0.0f, 1.0f, pos * 0.65f);

    return sample + (shaped - sample) * blend;
}

float SculptChannelAudioProcessor::getVariationAmount (
    int variationBand,
    const std::array<float, numBands>& macroValues) const noexcept
{
    std::array<float, numBands> m {};

    for (int i = 0; i < numBands; ++i)
        m[static_cast<size_t> (i)] =
            juce::jlimit (0.0f, 1.0f,
                          std::abs (macroValues[static_cast<size_t> (i)]) / 100.0f);

    float amount = 0.0f;

    switch (variationBand)
    {
        case 0: // 200 Hz
            amount = juce::jmax (m[0], m[1] * 0.25f);
            break;

        case 1: // 500 Hz
            amount = juce::jmax (m[0] * 0.35f, m[1] * 0.95f);
            break;

        case 2: // 1 kHz
            amount = juce::jmax (m[1], m[2] * 0.28f);
            break;

        case 3: // 3 kHz
            amount = juce::jmax (m[2], m[1] * 0.18f);
            break;

        case 4: // 5 kHz
            amount = juce::jmax (m[2] * 0.72f, m[3] * 0.68f);
            break;

        default: // 8 kHz
            amount = juce::jmax (m[3], m[2] * 0.18f);
            break;
    }

    return juce::jlimit (0.0f, 1.0f, amount);
}

int SculptChannelAudioProcessor::variationBandToMacroMeter (
    int variationBand) const noexcept
{
    switch (variationBand)
    {
        case 0:  return 0; // 200 -> low
        case 1:
        case 2:  return 1; // 500 / 1k -> mid
        case 3:
        case 4:  return 2; // 3k / 5k -> high
        default: return 3; // 8k -> presence
    }
}

void SculptChannelAudioProcessor::processVariationSoothe (
    juce::AudioBuffer<float>& source,
    const std::array<float, numBands>& macroValues)
{
    std::array<float, numBands> meterPeaks {};
    meterPeaks.fill (0.0f);

    for (int bandIndex = 0;
         bandIndex < numVariationBands;
         ++bandIndex)
    {
        auto& state =
            variationBands[static_cast<size_t> (bandIndex)];

        const float amount =
            getVariationAmount (bandIndex, macroValues);

        if (amount <= 0.0001f)
            continue;

        state.work.makeCopyOf (source, true);

        juce::dsp::AudioBlock<float> block (state.work);
        juce::dsp::ProcessContextReplacing<float> context (block);
        state.filter.process (context);

        float fastMs = 3.5f;
        float slowMs = 70.0f;

        switch (bandIndex)
        {
            case 0: fastMs = 6.5f; slowMs = 110.0f; break;
            case 1: fastMs = 5.5f; slowMs = 95.0f;  break;
            case 2: fastMs = 4.5f; slowMs = 82.0f;  break;
            case 3: fastMs = 3.5f; slowMs = 68.0f;  break;
            case 4: fastMs = 2.8f; slowMs = 58.0f;  break;
            default: fastMs = 2.2f; slowMs = 48.0f; break;
        }

        const float fastCoeff =
            std::exp (-1.0f / static_cast<float> (
                currentSampleRate * fastMs * 0.001f));

        const float slowCoeff =
            std::exp (-1.0f / static_cast<float> (
                currentSampleRate * slowMs * 0.001f));

        float localPeak = 0.0f;

        for (int i = 0; i < source.getNumSamples(); ++i)
        {
            float detector = 0.0f;

            for (int ch = 0;
                 ch < state.work.getNumChannels();
                 ++ch)
            {
                detector = juce::jmax (
                    detector,
                    std::abs (state.work.getSample (ch, i)));
            }

            // One stereo-linked envelope for both channels.
            state.fastEnv =
                fastCoeff * state.fastEnv
                + (1.0f - fastCoeff) * detector;

            state.slowEnv =
                slowCoeff * state.slowEnv
                + (1.0f - slowCoeff) * detector;

            const float threshold =
                state.slowEnv * 1.12f + 0.00004f;

            const float excess =
                juce::jmax (0.0f,
                            state.fastEnv - threshold);

            const float normalisedExcess =
                excess / (state.slowEnv + 0.0015f);

            float sensitivity = 0.42f;

            switch (bandIndex)
            {
                case 0: sensitivity = 0.38f; break;
                case 1: sensitivity = 0.43f; break;
                case 2: sensitivity = 0.47f; break;
                case 3: sensitivity = 0.52f; break;
                case 4: sensitivity = 0.55f; break;
                default: sensitivity = 0.58f; break;
            }

            const float reduction =
                juce::jlimit (
                    0.0f,
                    0.68f,
                    normalisedExcess
                    * sensitivity
                    * (0.35f + 0.85f * amount)
                    * amount);

            localPeak = juce::jmax (localPeak, reduction);

            for (int ch = 0;
                 ch < source.getNumChannels();
                 ++ch)
            {
                const float narrow =
                    state.work.getSample (ch, i);

                source.setSample (
                    ch, i,
                    source.getSample (ch, i)
                    - narrow * reduction);
            }
        }

        const int macroMeter =
            variationBandToMacroMeter (bandIndex);

        meterPeaks[static_cast<size_t> (macroMeter)] =
            juce::jmax (
                meterPeaks[static_cast<size_t> (macroMeter)],
                localPeak);
    }

    for (int i = 0; i < numBands; ++i)
    {
        resMeters[static_cast<size_t> (i)].store (
            juce::jlimit (
                0.0f, 1.0f,
                meterPeaks[static_cast<size_t> (i)] * 1.45f));
    }
}

void SculptChannelAudioProcessor::processBand (
    juce::AudioBuffer<float>& source,
    int bandIndex,
    float macroValue,
    bool broadResonanceEnabled)
{
    auto& band = bands[static_cast<size_t> (bandIndex)];

    const float signedAmount =
        juce::jlimit (-1.0f, 1.0f,
                      macroValue / 100.0f);

    const float magnitude = std::abs (signedAmount);

    const float positive =
        juce::jmax (0.0f, signedAmount);

    const float negative =
        juce::jmax (0.0f, -signedAmount);

    const float resAmount =
        juce::jlimit (0.0f, 1.0f,
                      magnitude * 1.18f);

    const float compAmount =
        juce::jlimit (0.0f, 1.0f,
                      (magnitude - 0.08f) / 0.92f);

    const float satEntrance =
        juce::jlimit (0.0f, 1.0f,
                      (magnitude - 0.18f) / 0.82f);

    const float lateShape =
        satEntrance
        * (0.55f + 1.35f * magnitude * magnitude);

    const float satAmount =
        juce::jlimit (0.0f, 1.55f, lateShape);

    band.work.makeCopyOf (source, true);

    juce::dsp::AudioBlock<float> bandBlock (band.work);
    juce::dsp::ProcessContextReplacing<float> filterContext (bandBlock);
    band.filter.process (filterContext);

    if (broadResonanceEnabled)
    {
        for (int ch = 0;
             ch < band.work.getNumChannels();
             ++ch)
        {
            auto* data =
                band.work.getWritePointer (ch);

            for (int i = 0;
                 i < band.work.getNumSamples();
                 ++i)
            {
                data[i] =
                    processResonanceControl (
                        band,
                        data[i],
                        resAmount,
                        bandIndex);
            }
        }
    }

    float preCompRms = 0.0f;

    for (int ch = 0;
         ch < band.work.getNumChannels();
         ++ch)
    {
        preCompRms +=
            band.work.getRMSLevel (
                ch, 0, band.work.getNumSamples());
    }

    preCompRms /=
        static_cast<float> (
            juce::jmax (1, band.work.getNumChannels()));

    float attackMs = 15.0f;
    float releaseMs = 120.0f;

    switch (bandIndex)
    {
        case 0: attackMs = 30.0f; releaseMs = 190.0f; break;
        case 1: attackMs = 15.0f; releaseMs = 120.0f; break;
        case 2: attackMs = 8.0f;  releaseMs = 90.0f;  break;
        default: attackMs = 4.0f; releaseMs = 68.0f;  break;
    }

    const float compBias =
        juce::jlimit (
            0.0f, 1.2f,
            positive + negative * 1.20f);

    band.compressor.setThreshold (
        juce::jmap (
            compAmount * compBias,
            0.0f, 1.0f,
            -1.0f, -24.0f));

    band.compressor.setRatio (
        juce::jmap (
            compAmount,
            0.0f, 1.0f,
            1.0f, 3.8f));

    band.compressor.setAttack (attackMs);
    band.compressor.setRelease (releaseMs);

    juce::dsp::AudioBlock<float> compBlock (band.work);
    juce::dsp::ProcessContextReplacing<float> compContext (compBlock);
    band.compressor.process (compContext);

    float postCompRms = 0.0f;

    for (int ch = 0;
         ch < band.work.getNumChannels();
         ++ch)
    {
        postCompRms +=
            band.work.getRMSLevel (
                ch, 0, band.work.getNumSamples());
    }

    postCompRms /=
        static_cast<float> (
            juce::jmax (1, band.work.getNumChannels()));

    const float compGR =
        preCompRms > 0.000001f
        ? juce::jlimit (
            0.0f, 1.0f,
            (preCompRms - postCompRms)
            / preCompRms)
        : 0.0f;

    compMeters[static_cast<size_t> (bandIndex)].store (compGR);

    float satActivity = 0.0f;

    for (int ch = 0;
         ch < band.work.getNumChannels();
         ++ch)
    {
        auto* data =
            band.work.getWritePointer (ch);

        for (int i = 0;
             i < band.work.getNumSamples();
             ++i)
        {
            const float before = data[i];

            const float after =
                processSaturation (
                    before,
                    satAmount * positive,
                    satAmount * negative,
                    bandIndex);

            satActivity +=
                std::abs (after - before);

            data[i] = after;
        }
    }

    const float norm =
        static_cast<float> (
            juce::jmax (
                1,
                band.work.getNumSamples()
                * band.work.getNumChannels()));

    satMeters[static_cast<size_t> (bandIndex)].store (
        juce::jlimit (
            0.0f, 1.0f,
            satActivity / norm * 7.5f));

    float forwardGain = 0.46f;
    float recessGain  = 0.40f;

    switch (bandIndex)
    {
        case 0:
            forwardGain = 0.78f;
            recessGain  = 0.52f;
            break;

        case 1:
            forwardGain = 0.56f;
            recessGain  = 0.44f;
            break;

        case 2:
            forwardGain = 0.50f;
            recessGain  = 0.40f;
            break;

        default:
            forwardGain = 0.46f;
            recessGain  = 0.36f;
            break;
    }

    for (int ch = 0;
         ch < source.getNumChannels();
         ++ch)
    {
        auto* dst =
            source.getWritePointer (ch);

        const auto* bandData =
            band.work.getReadPointer (ch);

        for (int i = 0;
             i < source.getNumSamples();
             ++i)
        {
            dst[i] +=
                bandData[i]
                * (forwardGain * positive);

            dst[i] -=
                bandData[i]
                * (recessGain * negative);
        }
    }
}

void SculptChannelAudioProcessor::processBlock (
    juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    dryBuffer.setSize (
        buffer.getNumChannels(),
        buffer.getNumSamples(),
        false, false, true);

    dryBuffer.makeCopyOf (buffer, true);

    float inPeak = 0.0f;

    for (int ch = 0;
         ch < buffer.getNumChannels();
         ++ch)
    {
        inPeak = juce::jmax (
            inPeak,
            buffer.getMagnitude (
                ch, 0, buffer.getNumSamples()));
    }

    inputMeter.store (
        juce::jlimit (0.0f, 1.0f, inPeak));

    const std::array<float, numBands> macros {
        apvts.getRawParameterValue ("low")->load(),
        apvts.getRawParameterValue ("mid")->load(),
        apvts.getRawParameterValue ("high")->load(),
        apvts.getRawParameterValue ("presence")->load()
    };

    const bool variationEnabled =
        apvts.getRawParameterValue ("variation")->load() > 0.5f;

    if (variationEnabled)
    {
        processVariationSoothe (buffer, macros);
    }
    else
    {
        // Broad RES meters are written by processBand.
        for (auto& meter : resMeters)
            meter.store (0.0f);
    }

    for (int band = 0;
         band < numBands;
         ++band)
    {
        processBand (
            buffer,
            band,
            macros[static_cast<size_t> (band)],
            ! variationEnabled);
    }

    const float outputDb =
        apvts.getRawParameterValue ("output")->load();

    outputGain.setTargetValue (
        juce::Decibels::decibelsToGain (outputDb));

    for (int i = 0;
         i < buffer.getNumSamples();
         ++i)
    {
        const float g =
            outputGain.getNextValue();

        for (int ch = 0;
             ch < buffer.getNumChannels();
             ++ch)
        {
            buffer.setSample (
                ch, i,
                buffer.getSample (ch, i) * g);
        }
    }

    float outPeak = 0.0f;

    for (int ch = 0;
         ch < buffer.getNumChannels();
         ++ch)
    {
        outPeak = juce::jmax (
            outPeak,
            buffer.getMagnitude (
                ch, 0, buffer.getNumSamples()));
    }

    outputMeter.store (
        juce::jlimit (0.0f, 1.0f, outPeak));
}

float SculptChannelAudioProcessor::getResMeter (int band) const noexcept
{
    return resMeters[
        static_cast<size_t> (
            juce::jlimit (0, numBands - 1, band))].load();
}

float SculptChannelAudioProcessor::getCompMeter (int band) const noexcept
{
    return compMeters[
        static_cast<size_t> (
            juce::jlimit (0, numBands - 1, band))].load();
}

float SculptChannelAudioProcessor::getSatMeter (int band) const noexcept
{
    return satMeters[
        static_cast<size_t> (
            juce::jlimit (0, numBands - 1, band))].load();
}

void SculptChannelAudioProcessor::getStateInformation (
    juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SculptChannelAudioProcessor::setStateInformation (
    const void* data,
    int sizeInBytes)
{
    if (auto xml =
        getXmlFromBinary (
            data, sizeInBytes))
    {
        if (xml->hasTagName (
                apvts.state.getType()))
        {
            apvts.replaceState (
                juce::ValueTree::fromXml (*xml));
        }
    }
}

juce::AudioProcessorEditor*
SculptChannelAudioProcessor::createEditor()
{
    return new SculptChannelAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SculptChannelAudioProcessor();
}
