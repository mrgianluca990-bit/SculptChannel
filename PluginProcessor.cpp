#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <algorithm>

SculptChannelAudioProcessor::SculptChannelAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    for (auto& m : resMeters)      m.store (0.0f);
    for (auto& m : compMeters)     m.store (0.0f);
    for (auto& m : satMeters)      m.store (0.0f);
    for (auto& m : resBandMeters)  m.store (0.0f);
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

    params.push_back (std::make_unique<juce::AudioParameterBool>(
        "levelmatch", "Level Match", false));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "output", "Output",
        juce::NormalisableRange<float> { -18.0f, 6.0f, 0.01f },
        0.0f, "dB"));

    return { params.begin(), params.end() };
}

bool SculptChannelAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;

    return out == juce::AudioChannelSet::mono()
        || out == juce::AudioChannelSet::stereo();
}

void SculptChannelAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    baseSampleRate = sampleRate;

    const auto channels = static_cast<size_t> (
        juce::jmax (1, getTotalNumOutputChannels()));

    oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
        channels,
        2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true,
        true);

    oversampler->initProcessing (static_cast<size_t> (samplesPerBlock));
    oversampler->reset();

    setLatencySamples (
        static_cast<int> (
            std::round (oversampler->getLatencyInSamples())));

    internalSampleRate = sampleRate * 4.0;
    maxInternalBlockSize = samplesPerBlock * 4 + 64;

    juce::dsp::ProcessSpec internalSpec {
        internalSampleRate,
        static_cast<juce::uint32> (maxInternalBlockSize),
        static_cast<juce::uint32> (channels)
    };

    for (auto& band : macroBands)
    {
        band.filter.prepare (internalSpec);
        band.filter.reset();

        band.work.setSize (
            static_cast<int> (channels),
            maxInternalBlockSize,
            false, false, true);

        band.compressorEnvelope = 0.0f;
        band.compressorGain = 1.0f;
    }

    for (auto& band : resBands)
    {
        band.filter.prepare (internalSpec);
        band.filter.reset();

        band.work.setSize (
            static_cast<int> (channels),
            maxInternalBlockSize,
            false, false, true);

        band.slowEnergy = 0.0f;
        band.currentReduction = 0.0f;
    }

    dryBaseRate.setSize (
        static_cast<int> (channels),
        samplesPerBlock,
        false, false, true);

    outputGain.reset (sampleRate, 0.04);
    outputGain.setCurrentAndTargetValue (1.0f);

    levelMatchDbState = 0.0f;

    configureMacroFilters();
    configureResFilters();
    computeResWeighting();
}

void SculptChannelAudioProcessor::configureMacroFilters()
{
    auto setup = [] (auto& filter,
                     juce::dsp::StateVariableTPTFilterType type,
                     float frequency,
                     float q)
    {
        filter.setType (type);
        filter.setCutoffFrequency (frequency);
        filter.setResonance (q);
    };

    setup (macroBands[0].filter,
           juce::dsp::StateVariableTPTFilterType::lowpass,
           210.0f, 0.62f);

    setup (macroBands[1].filter,
           juce::dsp::StateVariableTPTFilterType::bandpass,
           700.0f, 0.82f);

    setup (macroBands[2].filter,
           juce::dsp::StateVariableTPTFilterType::bandpass,
           3500.0f, 0.82f);

    setup (macroBands[3].filter,
           juce::dsp::StateVariableTPTFilterType::highpass,
           6500.0f, 0.64f);
}

void SculptChannelAudioProcessor::configureResFilters()
{
    constexpr float oneThirdOctaveQ = 4.25f;

    for (int i = 0; i < numResBands; ++i)
    {
        auto& f = resBands[static_cast<size_t> (i)].filter;

        f.setType (juce::dsp::StateVariableTPTFilterType::bandpass);
        f.setCutoffFrequency (
            juce::jmin (
                resFrequencies[static_cast<size_t> (i)],
                static_cast<float> (internalSampleRate * 0.44)));

        f.setResonance (oneThirdOctaveQ);
    }
}

void SculptChannelAudioProcessor::computeResWeighting()
{
    constexpr std::array<float, numMacroBands> centres {
        90.0f, 700.0f, 3500.0f, 10000.0f
    };

    constexpr std::array<float, numMacroBands> widthsInOctaves {
        1.55f, 1.35f, 1.25f, 1.35f
    };

    constexpr std::array<float, 6> variationCentres {
        200.0f, 500.0f, 1000.0f, 3000.0f, 5000.0f, 8000.0f
    };

    for (int b = 0; b < numResBands; ++b)
    {
        const float f = resFrequencies[static_cast<size_t> (b)];

        float total = 0.0f;

        for (int m = 0; m < numMacroBands; ++m)
        {
            const float distance =
                std::log2 (f / centres[static_cast<size_t> (m)]);

            const float width =
                widthsInOctaves[static_cast<size_t> (m)];

            const float weight =
                std::exp (-0.5f * (distance * distance) / (width * width));

            resMacroWeights[static_cast<size_t> (b)][static_cast<size_t> (m)] =
                weight;

            total += weight;
        }

        if (total > 0.0001f)
        {
            for (int m = 0; m < numMacroBands; ++m)
                resMacroWeights[static_cast<size_t> (b)][static_cast<size_t> (m)] /= total;
        }

        float focus = 0.0f;

        for (const auto target : variationCentres)
        {
            const float distance = std::log2 (f / target);
            const float local =
                std::exp (-0.5f * (distance * distance) / (0.19f * 0.19f));

            focus = juce::jmax (focus, local);
        }

        variationSensitivity[static_cast<size_t> (b)] =
            1.0f + 0.95f * focus;
    }
}

float SculptChannelAudioProcessor::macroActivityCurve (float magnitude) const noexcept
{
    magnitude = juce::jlimit (0.0f, 1.0f, magnitude);

    // More action in the first half, without losing the last quarter.
    return juce::jlimit (
        0.0f, 1.0f,
        0.52f * magnitude
        + 0.48f * std::sqrt (magnitude));
}

float SculptChannelAudioProcessor::getBlockRMS (
    const juce::AudioBuffer<float>& buffer) const noexcept
{
    if (buffer.getNumSamples() <= 0 || buffer.getNumChannels() <= 0)
        return 0.0f;

    double sumSquares = 0.0;
    const auto count =
        static_cast<double> (
            buffer.getNumSamples() * buffer.getNumChannels());

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const auto* data = buffer.getReadPointer (ch);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const double s = data[i];
            sumSquares += s * s;
        }
    }

    return static_cast<float> (
        std::sqrt (sumSquares / juce::jmax (1.0, count)));
}

void SculptChannelAudioProcessor::processResEngine (
    juce::AudioBuffer<float>& source,
    const std::array<float, numMacroBands>& macros,
    bool variationEnabled)
{
    std::array<float, numResBands> energies {};
    std::array<float, numResBands> energiesDb {};

    std::array<float, numMacroBands> macroResPeak {};
    macroResPeak.fill (0.0f);

    // First pass: 32 narrow analysis bands from the same pre-RES signal.
    for (int b = 0; b < numResBands; ++b)
    {
        auto& state = resBands[static_cast<size_t> (b)];

        state.work.setSize (
            source.getNumChannels(),
            source.getNumSamples(),
            false, false, true);

        state.work.makeCopyOf (source, true);

        juce::dsp::AudioBlock<float> block (state.work);
        juce::dsp::ProcessContextReplacing<float> context (block);
        state.filter.process (context);

        const float rms = getBlockRMS (state.work);

        energies[static_cast<size_t> (b)] = rms;
        energiesDb[static_cast<size_t> (b)] =
            juce::Decibels::gainToDecibels (rms + 1.0e-7f, -120.0f);

        const float blockSeconds =
            static_cast<float> (
                source.getNumSamples() / internalSampleRate);

        const float slowSeconds =
            juce::jmap (
                std::log2 (
                    juce::jlimit (20.0f, 20000.0f,
                                  resFrequencies[static_cast<size_t> (b)]) / 20.0f)
                    / std::log2 (1000.0f),
                0.34f, 0.12f);

        const float slowCoeff =
            std::exp (-blockSeconds / juce::jmax (0.04f, slowSeconds));

        if (state.slowEnergy <= 1.0e-7f)
            state.slowEnergy = rms;
        else
            state.slowEnergy =
                slowCoeff * state.slowEnergy
                + (1.0f - slowCoeff) * rms;
    }

    // Second pass: compare each band against local neighbours + temporal baseline.
    for (int b = 0; b < numResBands; ++b)
    {
        auto& state = resBands[static_cast<size_t> (b)];

        float weightedMagnitude = 0.0f;
        float weightedSigned = 0.0f;

        for (int m = 0; m < numMacroBands; ++m)
        {
            const float normalized =
                juce::jlimit (
                    -1.0f, 1.0f,
                    macros[static_cast<size_t> (m)] / 100.0f);

            const float w =
                resMacroWeights[static_cast<size_t> (b)][static_cast<size_t> (m)];

            weightedMagnitude += std::abs (normalized) * w;
            weightedSigned += normalized * w;
        }

        const float activity =
            macroActivityCurve (
                juce::jlimit (0.0f, 1.0f, weightedMagnitude));

        if (activity <= 0.0005f)
        {
            state.currentReduction *= 0.90f;
            resBandMeters[static_cast<size_t> (b)].store (
                state.currentReduction);
            continue;
        }

        const float localPolarity =
            weightedMagnitude > 0.0001f
                ? weightedSigned / weightedMagnitude
                : 0.0f;

        const float negativeBoost =
            1.0f + 0.38f * juce::jmax (0.0f, -localPolarity);

        const float positiveBoost =
            1.0f + 0.10f * juce::jmax (0.0f,  localPolarity);

        float neighbourDb = 0.0f;
        float neighbourWeight = 0.0f;

        for (int offset = -2; offset <= 2; ++offset)
        {
            if (offset == 0)
                continue;

            const int n = b + offset;

            if (n < 0 || n >= numResBands)
                continue;

            const float w = (std::abs (offset) == 1 ? 1.0f : 0.55f);

            neighbourDb += energiesDb[static_cast<size_t> (n)] * w;
            neighbourWeight += w;
        }

        if (neighbourWeight > 0.0f)
            neighbourDb /= neighbourWeight;
        else
            neighbourDb = energiesDb[static_cast<size_t> (b)];

        const float variationBoost =
            variationEnabled
                ? variationSensitivity[static_cast<size_t> (b)]
                : 1.0f;

        // At high macro values, RES begins acting on much smaller protrusions.
        float spectralThresholdDb =
            juce::jmap (activity, 4.0f, 1.10f);

        if (variationEnabled)
            spectralThresholdDb /=
                juce::jlimit (
                    1.0f, 1.75f,
                    variationBoost);

        const float spectralExcessDb =
            juce::jmax (
                0.0f,
                energiesDb[static_cast<size_t> (b)]
                - neighbourDb
                - spectralThresholdDb);

        const float slowDb =
            juce::Decibels::gainToDecibels (
                state.slowEnergy + 1.0e-7f,
                -120.0f);

        const float temporalThresholdDb =
            juce::jmap (activity, 3.0f, 0.80f);

        const float temporalExcessDb =
            juce::jmax (
                0.0f,
                energiesDb[static_cast<size_t> (b)]
                - slowDb
                - temporalThresholdDb);

        float desiredReductionDb =
            (spectralExcessDb * 1.55f
             + temporalExcessDb * 0.48f)
            * activity
            * negativeBoost
            * positiveBoost
            * variationBoost;

        float maxReductionDb =
            juce::jmap (activity, 2.0f, 11.5f);

        if (localPolarity < 0.0f)
            maxReductionDb += 2.0f * activity;

        if (variationEnabled)
            maxReductionDb +=
                2.0f
                * (variationBoost - 1.0f)
                * activity;

        desiredReductionDb =
            juce::jlimit (
                0.0f,
                juce::jmin (15.0f, maxReductionDb),
                desiredReductionDb);

        const float targetReduction =
            1.0f
            - juce::Decibels::decibelsToGain (
                -desiredReductionDb);

        // Smooth at block rate: quick attack, slower release.
        const float blockSeconds =
            static_cast<float> (
                source.getNumSamples() / internalSampleRate);

        const float attackSeconds =
            juce::jmap (
                resFrequencies[static_cast<size_t> (b)],
                20.0f, 20000.0f,
                0.030f, 0.004f);

        const float releaseSeconds =
            juce::jmap (
                resFrequencies[static_cast<size_t> (b)],
                20.0f, 20000.0f,
                0.240f, 0.075f);

        const float coeff =
            std::exp (
                -blockSeconds
                / (targetReduction > state.currentReduction
                    ? attackSeconds
                    : releaseSeconds));

        const float previousReduction =
            state.currentReduction;

        state.currentReduction =
            coeff * state.currentReduction
            + (1.0f - coeff) * targetReduction;

        const float meterValue =
            juce::jlimit (
                0.0f, 1.0f,
                desiredReductionDb / 12.0f);

        resBandMeters[static_cast<size_t> (b)].store (meterValue);

        for (int m = 0; m < numMacroBands; ++m)
        {
            const float weightedMeter =
                meterValue
                * resMacroWeights[static_cast<size_t> (b)][static_cast<size_t> (m)];

            macroResPeak[static_cast<size_t> (m)] =
                juce::jmax (
                    macroResPeak[static_cast<size_t> (m)],
                    weightedMeter);
        }

        const int samples = source.getNumSamples();

        for (int ch = 0; ch < source.getNumChannels(); ++ch)
        {
            auto* dst = source.getWritePointer (ch);
            const auto* narrow = state.work.getReadPointer (ch);

            for (int i = 0; i < samples; ++i)
            {
                const float t =
                    samples > 1
                        ? static_cast<float> (i) / static_cast<float> (samples - 1)
                        : 1.0f;

                const float reduction =
                    previousReduction
                    + (state.currentReduction - previousReduction) * t;

                dst[i] -= narrow[i] * reduction;
            }
        }
    }

    for (int m = 0; m < numMacroBands; ++m)
        resMeters[static_cast<size_t> (m)].store (
            juce::jlimit (
                0.0f, 1.0f,
                macroResPeak[static_cast<size_t> (m)] * 1.35f));
}

float SculptChannelAudioProcessor::applyBandSaturation (
    float sample,
    float positiveAmount,
    float negativeAmount,
    int bandIndex) const noexcept
{
    const float pos =
        juce::jlimit (0.0f, 1.0f, positiveAmount);

    const float neg =
        juce::jlimit (0.0f, 1.0f, negativeAmount);

    float maxDrive = 3.2f;
    float maxBlend = 0.45f;
    float asymmetry = 0.0f;

    switch (bandIndex)
    {
        case 0:
            maxDrive = 4.0f;
            maxBlend = 0.58f;
            asymmetry = 0.040f;
            break;

        case 1:
            maxDrive = 3.6f;
            maxBlend = 0.52f;
            asymmetry = 0.030f;
            break;

        case 2:
            maxDrive = 3.0f;
            maxBlend = 0.42f;
            asymmetry = 0.018f;
            break;

        default:
            maxDrive = 2.55f;
            maxBlend = 0.34f;
            asymmetry = 0.010f;
            break;
    }

    const float satAmount =
        juce::jlimit (
            0.0f, 1.0f,
            pos + neg * 0.10f);

    const float drive =
        1.0f
        + (maxDrive - 1.0f)
          * satAmount;

    const float blend =
        maxBlend
        * satAmount;

    const float asym =
        asymmetry * pos;

    const float preShaped =
        sample
        + asym
          * sample * sample
          * (sample >= 0.0f ? 1.0f : -1.0f);

    const float saturated =
        std::tanh (preShaped * drive)
        / juce::jmax (1.0f, drive);

    return sample
        + (saturated - sample) * blend;
}

void SculptChannelAudioProcessor::processMacroBand (
    juce::AudioBuffer<float>& source,
    int bandIndex,
    float macroValue)
{
    auto& state =
        macroBands[static_cast<size_t> (bandIndex)];

    const float normalized =
        juce::jlimit (
            -1.0f, 1.0f,
            macroValue / 100.0f);

    const float magnitude =
        std::abs (normalized);

    const float activity =
        macroActivityCurve (magnitude);

    const float positive =
        juce::jmax (0.0f, normalized);

    const float negative =
        juce::jmax (0.0f, -normalized);

    state.work.setSize (
        source.getNumChannels(),
        source.getNumSamples(),
        false, false, true);

    state.work.makeCopyOf (source, true);

    juce::dsp::AudioBlock<float> block (state.work);
    juce::dsp::ProcessContextReplacing<float> context (block);
    state.filter.process (context);

    // Compression: deliberately stronger on the positive side.
    const float positiveComp =
        activity
        * (0.35f + 0.65f * positive);

    const float negativeComp =
        activity
        * negative * 0.62f;

    const float compAmount =
        juce::jlimit (
            0.0f, 1.0f,
            positive > 0.0f
                ? positiveComp
                : negativeComp);

    float attackMs = 15.0f;
    float releaseMs = 120.0f;

    switch (bandIndex)
    {
        case 0: attackMs = 30.0f; releaseMs = 210.0f; break;
        case 1: attackMs = 15.0f; releaseMs = 135.0f; break;
        case 2: attackMs = 7.0f;  releaseMs = 92.0f;  break;
        default: attackMs = 3.5f; releaseMs = 68.0f;  break;
    }

    const float attackCoeff =
        std::exp (
            -1.0f
            / static_cast<float> (
                internalSampleRate
                * attackMs
                * 0.001));

    const float releaseCoeff =
        std::exp (
            -1.0f
            / static_cast<float> (
                internalSampleRate
                * releaseMs
                * 0.001));

    const float thresholdDb =
        positive > 0.0f
            ? (-5.0f - 23.0f * compAmount)
            : (-6.0f - 14.0f * compAmount);

    const float ratio =
        positive > 0.0f
            ? (1.0f + 4.7f * compAmount)
            : (1.0f + 2.3f * compAmount);

    float maxGainReductionDb = 0.0f;

    for (int i = 0; i < state.work.getNumSamples(); ++i)
    {
        float detector = 0.0f;

        for (int ch = 0; ch < state.work.getNumChannels(); ++ch)
        {
            detector =
                juce::jmax (
                    detector,
                    std::abs (
                        state.work.getSample (ch, i)));
        }

        const float envCoeff =
            detector > state.compressorEnvelope
                ? attackCoeff
                : releaseCoeff;

        state.compressorEnvelope =
            envCoeff * state.compressorEnvelope
            + (1.0f - envCoeff) * detector;

        const float envDb =
            juce::Decibels::gainToDecibels (
                state.compressorEnvelope + 1.0e-8f,
                -120.0f);

        const float overDb =
            juce::jmax (
                0.0f,
                envDb - thresholdDb);

        const float grDb =
            overDb
            * (1.0f - 1.0f / juce::jmax (1.0f, ratio));

        maxGainReductionDb =
            juce::jmax (
                maxGainReductionDb,
                grDb);

        const float targetGain =
            juce::Decibels::decibelsToGain (-grDb);

        const float gainCoeff =
            targetGain < state.compressorGain
                ? attackCoeff
                : releaseCoeff;

        state.compressorGain =
            gainCoeff * state.compressorGain
            + (1.0f - gainCoeff) * targetGain;

        // modest density makeup only on the positive side
        const float makeup =
            1.0f
            + positive
              * compAmount
              * 0.18f;

        for (int ch = 0; ch < state.work.getNumChannels(); ++ch)
        {
            state.work.setSample (
                ch, i,
                state.work.getSample (ch, i)
                * state.compressorGain
                * makeup);
        }
    }

    compMeters[static_cast<size_t> (bandIndex)].store (
        juce::jlimit (
            0.0f, 1.0f,
            maxGainReductionDb / 12.0f));

    // Saturation enters later than compression and tops out lower than v0.3.
    const float satEntrance =
        juce::jlimit (
            0.0f, 1.0f,
            (magnitude - 0.32f) / 0.68f);

    const float satCurve =
        std::pow (satEntrance, 1.20f);

    const float positiveSat =
        satCurve * positive;

    const float negativeSat =
        satCurve * negative * 0.12f;

    float saturationActivity = 0.0f;

    for (int ch = 0; ch < state.work.getNumChannels(); ++ch)
    {
        auto* data =
            state.work.getWritePointer (ch);

        for (int i = 0; i < state.work.getNumSamples(); ++i)
        {
            const float before = data[i];

            const float after =
                applyBandSaturation (
                    before,
                    positiveSat,
                    negativeSat,
                    bandIndex);

            saturationActivity +=
                std::abs (after - before);

            data[i] = after;
        }
    }

    const float normaliser =
        static_cast<float> (
            juce::jmax (
                1,
                state.work.getNumSamples()
                * state.work.getNumChannels()));

    satMeters[static_cast<size_t> (bandIndex)].store (
        juce::jlimit (
            0.0f, 1.0f,
            saturationActivity
            / normaliser
            * 8.0f));

    const float forwardCurve =
        std::pow (activity, 0.95f);

    const float recessCurve =
        std::pow (activity, 0.72f);

    float maxForward = 0.45f;
    float maxRecess  = 0.62f;

    switch (bandIndex)
    {
        case 0: maxForward = 0.58f; maxRecess = 0.78f; break;
        case 1: maxForward = 0.49f; maxRecess = 0.70f; break;
        case 2: maxForward = 0.43f; maxRecess = 0.64f; break;
        default: maxForward = 0.38f; maxRecess = 0.56f; break;
    }

    const float forwardMix =
        maxForward
        * forwardCurve
        * positive;

    const float recessMix =
        maxRecess
        * recessCurve
        * negative;

    for (int ch = 0; ch < source.getNumChannels(); ++ch)
    {
        auto* dst =
            source.getWritePointer (ch);

        const auto* wet =
            state.work.getReadPointer (ch);

        for (int i = 0; i < source.getNumSamples(); ++i)
        {
            dst[i] += wet[i] * forwardMix;
            dst[i] -= wet[i] * recessMix;
        }
    }
}

void SculptChannelAudioProcessor::processInternal (
    juce::AudioBuffer<float>& buffer)
{
    const std::array<float, numMacroBands> macros {
        apvts.getRawParameterValue ("low")->load(),
        apvts.getRawParameterValue ("mid")->load(),
        apvts.getRawParameterValue ("high")->load(),
        apvts.getRawParameterValue ("presence")->load()
    };

    const bool variationEnabled =
        apvts.getRawParameterValue ("variation")->load() > 0.5f;

    processResEngine (
        buffer,
        macros,
        variationEnabled);

    for (int band = 0; band < numMacroBands; ++band)
    {
        processMacroBand (
            buffer,
            band,
            macros[static_cast<size_t> (band)]);
    }
}

void SculptChannelAudioProcessor::applyLevelMatchAndOutput (
    juce::AudioBuffer<float>& buffer,
    float inputRms,
    bool levelMatchEnabled)
{
    const float processedRms =
        getBlockRMS (buffer);

    float targetLevelMatchDb = 0.0f;

    if (levelMatchEnabled
        && inputRms > 1.0e-5f
        && processedRms > 1.0e-5f)
    {
        const float inDb =
            juce::Decibels::gainToDecibels (
                inputRms, -120.0f);

        const float outDb =
            juce::Decibels::gainToDecibels (
                processedRms, -120.0f);

        targetLevelMatchDb =
            juce::jlimit (
                -6.0f, 6.0f,
                inDb - outDb);
    }

    const float blockSeconds =
        static_cast<float> (
            buffer.getNumSamples()
            / baseSampleRate);

    const float timeConstant =
        levelMatchEnabled ? 0.55f : 0.22f;

    const float coeff =
        std::exp (
            -blockSeconds / timeConstant);

    levelMatchDbState =
        coeff * levelMatchDbState
        + (1.0f - coeff) * targetLevelMatchDb;

    const float levelMatchGain =
        juce::Decibels::decibelsToGain (
            levelMatchDbState);

    const float outputDb =
        apvts.getRawParameterValue ("output")->load();

    outputGain.setTargetValue (
        juce::Decibels::decibelsToGain (outputDb));

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float gain =
            levelMatchGain
            * outputGain.getNextValue();

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            buffer.setSample (
                ch, i,
                buffer.getSample (ch, i) * gain);
        }
    }
}

void SculptChannelAudioProcessor::processBlock (
    juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    dryBaseRate.setSize (
        buffer.getNumChannels(),
        buffer.getNumSamples(),
        false, false, true);

    dryBaseRate.makeCopyOf (buffer, true);

    const float inputRms =
        getBlockRMS (dryBaseRate);

    float inPeak = 0.0f;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        inPeak =
            juce::jmax (
                inPeak,
                buffer.getMagnitude (
                    ch, 0, buffer.getNumSamples()));
    }

    inputMeter.store (
        juce::jlimit (
            0.0f, 1.0f,
            inPeak));

    if (oversampler != nullptr)
    {
        juce::dsp::AudioBlock<float> baseBlock (buffer);

        auto upBlock =
            oversampler->processSamplesUp (baseBlock);

        std::array<float*, 2> pointers { nullptr, nullptr };

        for (size_t ch = 0; ch < upBlock.getNumChannels() && ch < pointers.size(); ++ch)
            pointers[ch] = upBlock.getChannelPointer (ch);

        juce::AudioBuffer<float> internalBuffer (
            pointers.data(),
            static_cast<int> (upBlock.getNumChannels()),
            static_cast<int> (upBlock.getNumSamples()));

        processInternal (internalBuffer);

        oversampler->processSamplesDown (baseBlock);
    }
    else
    {
        processInternal (buffer);
    }

    const bool levelMatchEnabled =
        apvts.getRawParameterValue ("levelmatch")->load() > 0.5f;

    applyLevelMatchAndOutput (
        buffer,
        inputRms,
        levelMatchEnabled);

    float outPeak = 0.0f;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        outPeak =
            juce::jmax (
                outPeak,
                buffer.getMagnitude (
                    ch, 0, buffer.getNumSamples()));
    }

    outputMeter.store (
        juce::jlimit (
            0.0f, 1.0f,
            outPeak));
}

float SculptChannelAudioProcessor::getResMeter (int band) const noexcept
{
    return resMeters[
        static_cast<size_t> (
            juce::jlimit (
                0, numMacroBands - 1, band))].load();
}

float SculptChannelAudioProcessor::getCompMeter (int band) const noexcept
{
    return compMeters[
        static_cast<size_t> (
            juce::jlimit (
                0, numMacroBands - 1, band))].load();
}

float SculptChannelAudioProcessor::getSatMeter (int band) const noexcept
{
    return satMeters[
        static_cast<size_t> (
            juce::jlimit (
                0, numMacroBands - 1, band))].load();
}

float SculptChannelAudioProcessor::getResBandMeter (int band) const noexcept
{
    return resBandMeters[
        static_cast<size_t> (
            juce::jlimit (
                0, numResBands - 1, band))].load();
}

void SculptChannelAudioProcessor::getStateInformation (
    juce::MemoryBlock& destData)
{
    if (auto xml =
        apvts.copyState().createXml())
    {
        copyXmlToBinary (
            *xml, destData);
    }
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
