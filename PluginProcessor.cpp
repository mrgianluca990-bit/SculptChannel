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

    juce::dsp::ProcessSpec spec {
        internalSampleRate,
        static_cast<juce::uint32> (maxInternalBlockSize),
        static_cast<juce::uint32> (channels)
    };

    for (auto& band : macroBands)
    {
        band.filter.prepare (spec);
        band.filter.reset();

        band.band.setSize (
            static_cast<int> (channels),
            maxInternalBlockSize,
            false, false, true);

        band.originalBand.setSize (
            static_cast<int> (channels),
            maxInternalBlockSize,
            false, false, true);

        band.compressorEnvelope = 0.0f;
        band.compressorGain = 1.0f;
    }

    for (auto& band : resBands)
    {
        band.filter.prepare (spec);
        band.filter.reset();

        band.work.setSize (
            static_cast<int> (channels),
            maxInternalBlockSize,
            false, false, true);

        band.slowEnergy = 0.0f;
        band.reduction = 0.0f;
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
           185.0f, 0.56f);

    setup (macroBands[1].filter,
           juce::dsp::StateVariableTPTFilterType::bandpass,
           700.0f, 0.76f);

    setup (macroBands[2].filter,
           juce::dsp::StateVariableTPTFilterType::bandpass,
           3500.0f, 0.72f);

    setup (macroBands[3].filter,
           juce::dsp::StateVariableTPTFilterType::highpass,
           7000.0f, 0.58f);
}

void SculptChannelAudioProcessor::configureResFilters()
{
    constexpr float sootheQ = 4.8f;

    for (int i = 0; i < numResBands; ++i)
    {
        auto& f = resBands[static_cast<size_t> (i)].filter;

        f.setType (juce::dsp::StateVariableTPTFilterType::bandpass);

        f.setCutoffFrequency (
            juce::jmin (
                resFrequencies[static_cast<size_t> (i)],
                static_cast<float> (internalSampleRate * 0.44)));

        f.setResonance (sootheQ);
    }
}

void SculptChannelAudioProcessor::computeResWeighting()
{
    constexpr std::array<float, numMacroBands> centres {
        90.0f, 700.0f, 3500.0f, 10000.0f
    };

    constexpr std::array<float, numMacroBands> widths {
        1.55f, 1.35f, 1.25f, 1.30f
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
                widths[static_cast<size_t> (m)];

            const float weight =
                std::exp (
                    -0.5f
                    * (distance * distance)
                    / (width * width));

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
            const float distance =
                std::log2 (f / target);

            const float local =
                std::exp (
                    -0.5f
                    * (distance * distance)
                    / (0.18f * 0.18f));

            focus = juce::jmax (focus, local);
        }

        variationSensitivity[static_cast<size_t> (b)] =
            1.0f + 0.70f * focus;
    }
}

float SculptChannelAudioProcessor::macroCurve (float magnitude) const noexcept
{
    magnitude = juce::jlimit (0.0f, 1.0f, magnitude);

    // Audible from the first third, progressive through the full travel.
    return juce::jlimit (
        0.0f, 1.0f,
        0.42f * magnitude
        + 0.58f * std::pow (magnitude, 0.72f));
}

float SculptChannelAudioProcessor::getBlockRMS (
    const juce::AudioBuffer<float>& buffer) const noexcept
{
    if (buffer.getNumSamples() <= 0 || buffer.getNumChannels() <= 0)
        return 0.0f;

    double sum = 0.0;
    const double count =
        static_cast<double> (
            buffer.getNumSamples()
            * buffer.getNumChannels());

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const auto* data =
            buffer.getReadPointer (ch);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const double s = data[i];
            sum += s * s;
        }
    }

    return static_cast<float> (
        std::sqrt (
            sum / juce::jmax (1.0, count)));
}

float SculptChannelAudioProcessor::saturateBandSample (
    float sample,
    float positiveAmount,
    float negativeAmount,
    int bandIndex) const noexcept
{
    const float pos =
        juce::jlimit (
            0.0f, 1.0f,
            positiveAmount);

    const float neg =
        juce::jlimit (
            0.0f, 1.0f,
            negativeAmount);

    float maxDrive = 3.0f;
    float maxBlend = 0.52f;
    float asymmetry = 0.0f;

    switch (bandIndex)
    {
        case 0:
            maxDrive = 3.5f;
            maxBlend = 0.60f;
            asymmetry = 0.038f;
            break;

        case 1:
            maxDrive = 3.25f;
            maxBlend = 0.57f;
            asymmetry = 0.030f;
            break;

        case 2:
            maxDrive = 2.9f;
            maxBlend = 0.50f;
            asymmetry = 0.018f;
            break;

        default:
            maxDrive = 2.55f;
            maxBlend = 0.42f;
            asymmetry = 0.010f;
            break;
    }

    const float satAmount =
        juce::jlimit (
            0.0f, 1.0f,
            pos + neg * 0.08f);

    const float drive =
        1.0f
        + (maxDrive - 1.0f)
          * satAmount;

    const float blend =
        maxBlend
        * satAmount;

    const float asym =
        asymmetry * pos;

    const float biased =
        sample
        + asym
          * sample * sample
          * (sample >= 0.0f ? 1.0f : -1.0f);

    // Intentionally NOT fully level-normalised:
    // at the top of the control the stage must audibly distort.
    const float distorted =
        std::tanh (
            biased * drive);

    return sample
        + (distorted - sample)
          * blend;
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

    const float positive =
        juce::jmax (0.0f, normalized);

    const float negative =
        juce::jmax (0.0f, -normalized);

    const float magnitude =
        std::abs (normalized);

    const float activity =
        macroCurve (magnitude);

    state.band.setSize (
        source.getNumChannels(),
        source.getNumSamples(),
        false, false, true);

    state.band.makeCopyOf (
        source, true);

    juce::dsp::AudioBlock<float> bandBlock (
        state.band);

    juce::dsp::ProcessContextReplacing<float> filterContext (
        bandBlock);

    state.filter.process (
        filterContext);

    state.originalBand.setSize (
        source.getNumChannels(),
        source.getNumSamples(),
        false, false, true);

    state.originalBand.makeCopyOf (
        state.band, true);

    // 1) EQ DRIVE
    // Positive = broad boost into compressor.
    // Negative = broad cut, still with a little dynamics but almost no saturation.
    const float eqDb =
        positive > 0.0f
            ? 11.0f * activity * positive
            : -12.0f * activity * negative;

    const float eqGain =
        juce::Decibels::decibelsToGain (
            eqDb);

    state.band.applyGain (
        eqGain);

    // 2) LINKED COMPRESSION
    // Positive side: stronger and increasingly obvious.
    // Negative side: lighter, keeps the cut controlled.
    const float positiveComp =
        positive
        * juce::jlimit (
            0.0f, 1.0f,
            (activity - 0.15f) / 0.85f);

    const float negativeComp =
        negative
        * juce::jlimit (
            0.0f, 1.0f,
            (activity - 0.25f) / 0.75f)
        * 0.45f;

    const float compAmount =
        juce::jlimit (
            0.0f, 1.0f,
            positiveComp + negativeComp);

    float attackMs = 12.0f;
    float releaseMs = 110.0f;

    switch (bandIndex)
    {
        case 0: attackMs = 28.0f; releaseMs = 190.0f; break;
        case 1: attackMs = 14.0f; releaseMs = 125.0f; break;
        case 2: attackMs = 7.0f;  releaseMs = 85.0f;  break;
        default: attackMs = 3.5f; releaseMs = 62.0f;  break;
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
            ? juce::jmap (
                compAmount,
                0.0f, 1.0f,
                -4.0f, -24.0f)
            : juce::jmap (
                compAmount,
                0.0f, 1.0f,
                -3.0f, -13.0f);

    const float ratio =
        positive > 0.0f
            ? juce::jmap (
                compAmount,
                0.0f, 1.0f,
                1.0f, 5.2f)
            : juce::jmap (
                compAmount,
                0.0f, 1.0f,
                1.0f, 2.2f);

    float maxGrDb = 0.0f;

    for (int i = 0; i < state.band.getNumSamples(); ++i)
    {
        float detector = 0.0f;

        for (int ch = 0; ch < state.band.getNumChannels(); ++ch)
        {
            detector =
                juce::jmax (
                    detector,
                    std::abs (
                        state.band.getSample (
                            ch, i)));
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
                state.compressorEnvelope
                + 1.0e-8f,
                -120.0f);

        const float overDb =
            juce::jmax (
                0.0f,
                envDb - thresholdDb);

        const float grDb =
            overDb
            * (1.0f
               - 1.0f / juce::jmax (
                    1.0f, ratio));

        maxGrDb =
            juce::jmax (
                maxGrDb, grDb);

        const float targetGain =
            juce::Decibels::decibelsToGain (
                -grDb);

        const float gainCoeff =
            targetGain < state.compressorGain
                ? attackCoeff
                : releaseCoeff;

        state.compressorGain =
            gainCoeff * state.compressorGain
            + (1.0f - gainCoeff)
              * targetGain;

        // Slight make-up to preserve the feeling of pushing an analogue stage.
        const float makeup =
            1.0f
            + positive
              * compAmount
              * 0.14f;

        for (int ch = 0; ch < state.band.getNumChannels(); ++ch)
        {
            state.band.setSample (
                ch, i,
                state.band.getSample (ch, i)
                * state.compressorGain
                * makeup);
        }
    }

    compMeters[static_cast<size_t> (bandIndex)].store (
        juce::jlimit (
            0.0f, 1.0f,
            maxGrDb / 12.0f));

    // 3) SATURATION
    // Starts only after the compressor is already working.
    const float satEntrance =
        juce::jlimit (
            0.0f, 1.0f,
            (positive - 0.52f) / 0.48f);

    const float positiveSat =
        std::pow (
            satEntrance,
            1.05f);

    const float negativeSat =
        negative
        * juce::jlimit (
            0.0f, 1.0f,
            (activity - 0.72f) / 0.28f)
        * 0.08f;

    float nonlinearDifference = 0.0f;

    for (int ch = 0; ch < state.band.getNumChannels(); ++ch)
    {
        auto* data =
            state.band.getWritePointer (ch);

        for (int i = 0; i < state.band.getNumSamples(); ++i)
        {
            const float before =
                data[i];

            const float after =
                saturateBandSample (
                    before,
                    positiveSat,
                    negativeSat,
                    bandIndex);

            nonlinearDifference +=
                std::abs (
                    after - before);

            data[i] = after;
        }
    }

    const float norm =
        static_cast<float> (
            juce::jmax (
                1,
                state.band.getNumSamples()
                * state.band.getNumChannels()));

    satMeters[static_cast<size_t> (bandIndex)].store (
        juce::jlimit (
            0.0f, 1.0f,
            nonlinearDifference
            / norm
            * 7.0f));

    // Replace only the broad band with the EQ -> COMP -> SAT version.
    // This makes the macro behave much more like a real driven channel strip.
    for (int ch = 0; ch < source.getNumChannels(); ++ch)
    {
        auto* dst =
            source.getWritePointer (ch);

        const auto* original =
            state.originalBand.getReadPointer (ch);

        const auto* processed =
            state.band.getReadPointer (ch);

        for (int i = 0; i < source.getNumSamples(); ++i)
        {
            dst[i] +=
                processed[i]
                - original[i];
        }
    }
}

void SculptChannelAudioProcessor::processSootheGuardrail (
    juce::AudioBuffer<float>& source,
    const std::array<float, numMacroBands>& macros,
    bool variationEnabled)
{
    std::array<float, numResBands> bandDb {};
    std::array<float, numMacroBands> macroPeak {};
    macroPeak.fill (0.0f);

    // Analyse all 32 narrow regions from the already coloured signal.
    for (int b = 0; b < numResBands; ++b)
    {
        auto& state =
            resBands[static_cast<size_t> (b)];

        state.work.setSize (
            source.getNumChannels(),
            source.getNumSamples(),
            false, false, true);

        state.work.makeCopyOf (
            source, true);

        juce::dsp::AudioBlock<float> block (
            state.work);

        juce::dsp::ProcessContextReplacing<float> context (
            block);

        state.filter.process (
            context);

        const float rms =
            getBlockRMS (
                state.work);

        bandDb[static_cast<size_t> (b)] =
            juce::Decibels::gainToDecibels (
                rms + 1.0e-8f,
                -120.0f);

        const float blockSeconds =
            static_cast<float> (
                source.getNumSamples()
                / internalSampleRate);

        const float timeConstant =
            juce::jmap (
                resFrequencies[static_cast<size_t> (b)],
                20.0f,
                20000.0f,
                0.36f,
                0.13f);

        const float coeff =
            std::exp (
                -blockSeconds
                / juce::jmax (
                    0.05f,
                    timeConstant));

        if (state.slowEnergy <= 1.0e-7f)
            state.slowEnergy = rms;
        else
            state.slowEnergy =
                coeff * state.slowEnergy
                + (1.0f - coeff)
                  * rms;
    }

    for (int b = 0; b < numResBands; ++b)
    {
        auto& state =
            resBands[static_cast<size_t> (b)];

        float macroMagnitude = 0.0f;

        for (int m = 0; m < numMacroBands; ++m)
        {
            const float knob =
                std::abs (
                    macros[static_cast<size_t> (m)]
                    / 100.0f);

            macroMagnitude +=
                knob
                * resMacroWeights[static_cast<size_t> (b)][static_cast<size_t> (m)];
        }

        const float activity =
            macroCurve (
                juce::jlimit (
                    0.0f, 1.0f,
                    macroMagnitude));

        if (activity < 0.08f)
        {
            state.reduction *= 0.92f;
            continue;
        }

        float neighbourDb = 0.0f;
        float weightSum = 0.0f;

        for (int offset = -2; offset <= 2; ++offset)
        {
            if (offset == 0)
                continue;

            const int n = b + offset;

            if (n < 0 || n >= numResBands)
                continue;

            const float w =
                std::abs (offset) == 1
                    ? 1.0f
                    : 0.55f;

            neighbourDb +=
                bandDb[static_cast<size_t> (n)]
                * w;

            weightSum += w;
        }

        if (weightSum > 0.0f)
            neighbourDb /= weightSum;
        else
            neighbourDb =
                bandDb[static_cast<size_t> (b)];

        const float slowDb =
            juce::Decibels::gainToDecibels (
                state.slowEnergy
                + 1.0e-8f,
                -120.0f);

        float variationBoost =
            variationEnabled
                ? variationSensitivity[static_cast<size_t> (b)]
                : 1.0f;

        // High threshold: only true protrusions should trigger the guardrail.
        float spectralThresholdDb =
            juce::jmap (
                activity,
                6.2f,
                4.3f);

        float temporalThresholdDb =
            juce::jmap (
                activity,
                5.2f,
                3.6f);

        if (variationEnabled)
        {
            const float focus =
                variationBoost - 1.0f;

            spectralThresholdDb -=
                1.5f * focus;

            temporalThresholdDb -=
                1.0f * focus;
        }

        const float spectralExcessDb =
            juce::jmax (
                0.0f,
                bandDb[static_cast<size_t> (b)]
                - neighbourDb
                - spectralThresholdDb);

        const float temporalExcessDb =
            juce::jmax (
                0.0f,
                bandDb[static_cast<size_t> (b)]
                - slowDb
                - temporalThresholdDb);

        float desiredDb =
            (spectralExcessDb * 0.42f
             + temporalExcessDb * 0.18f)
            * activity;

        desiredDb *= variationBoost;

        // Normal mode max ≈ 0.5–3 dB.
        // Variation can focus up to about 4.5 dB on selected areas.
        float maxDb =
            0.45f
            + 2.35f * activity;

        if (variationEnabled)
        {
            maxDb +=
                2.0f
                * (variationBoost - 1.0f);
        }

        maxDb =
            juce::jlimit (
                0.5f, 4.6f,
                maxDb);

        desiredDb =
            juce::jlimit (
                0.0f, maxDb,
                desiredDb);

        const float targetReduction =
            1.0f
            - juce::Decibels::decibelsToGain (
                -desiredDb);

        const float blockSeconds =
            static_cast<float> (
                source.getNumSamples()
                / internalSampleRate);

        const float attackSeconds =
            juce::jmap (
                resFrequencies[static_cast<size_t> (b)],
                20.0f, 20000.0f,
                0.035f, 0.006f);

        const float releaseSeconds =
            juce::jmap (
                resFrequencies[static_cast<size_t> (b)],
                20.0f, 20000.0f,
                0.280f, 0.095f);

        const float coeff =
            std::exp (
                -blockSeconds
                / (targetReduction > state.reduction
                    ? attackSeconds
                    : releaseSeconds));

        const float previous =
            state.reduction;

        state.reduction =
            coeff * state.reduction
            + (1.0f - coeff)
              * targetReduction;

        const float meter =
            juce::jlimit (
                0.0f, 1.0f,
                desiredDb / 4.6f);

        for (int m = 0; m < numMacroBands; ++m)
        {
            macroPeak[static_cast<size_t> (m)] =
                juce::jmax (
                    macroPeak[static_cast<size_t> (m)],
                    meter
                    * resMacroWeights[static_cast<size_t> (b)][static_cast<size_t> (m)]);
        }

        for (int ch = 0; ch < source.getNumChannels(); ++ch)
        {
            auto* dst =
                source.getWritePointer (ch);

            const auto* narrow =
                state.work.getReadPointer (ch);

            const int samples =
                source.getNumSamples();

            for (int i = 0; i < samples; ++i)
            {
                const float t =
                    samples > 1
                        ? static_cast<float> (i)
                          / static_cast<float> (samples - 1)
                        : 1.0f;

                const float reduction =
                    previous
                    + (state.reduction - previous)
                      * t;

                dst[i] -=
                    narrow[i]
                    * reduction;
            }
        }
    }

    for (int m = 0; m < numMacroBands; ++m)
    {
        resMeters[static_cast<size_t> (m)].store (
            juce::jlimit (
                0.0f, 1.0f,
                macroPeak[static_cast<size_t> (m)] * 1.25f));
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

    for (int band = 0; band < numMacroBands; ++band)
    {
        processMacroBand (
            buffer,
            band,
            macros[static_cast<size_t> (band)]);
    }

    const bool variationEnabled =
        apvts.getRawParameterValue ("variation")->load() > 0.5f;

    // Soothe happens AFTER the colour chain:
    // it only catches the resonances created/exposed by EQ -> COMP -> SAT.
    processSootheGuardrail (
        buffer,
        macros,
        variationEnabled);
}

void SculptChannelAudioProcessor::applyLevelMatchAndOutput (
    juce::AudioBuffer<float>& buffer,
    float inputRms,
    bool levelMatchEnabled)
{
    const float processedRms =
        getBlockRMS (buffer);

    float targetDb = 0.0f;

    if (levelMatchEnabled
        && inputRms > 1.0e-5f
        && processedRms > 1.0e-5f)
    {
        const float inputDb =
            juce::Decibels::gainToDecibels (
                inputRms, -120.0f);

        const float outputDb =
            juce::Decibels::gainToDecibels (
                processedRms, -120.0f);

        targetDb =
            juce::jlimit (
                -6.0f, 6.0f,
                inputDb - outputDb);
    }

    const float blockSeconds =
        static_cast<float> (
            buffer.getNumSamples()
            / baseSampleRate);

    const float coeff =
        std::exp (
            -blockSeconds
            / (levelMatchEnabled
                ? 0.60f
                : 0.22f));

    levelMatchDbState =
        coeff * levelMatchDbState
        + (1.0f - coeff)
          * targetDb;

    const float matchGain =
        juce::Decibels::decibelsToGain (
            levelMatchDbState);

    const float outputDb =
        apvts.getRawParameterValue ("output")->load();

    outputGain.setTargetValue (
        juce::Decibels::decibelsToGain (
            outputDb));

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float gain =
            matchGain
            * outputGain.getNextValue();

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            buffer.setSample (
                ch, i,
                buffer.getSample (ch, i)
                * gain);
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

    dryBaseRate.makeCopyOf (
        buffer, true);

    const float inputRms =
        getBlockRMS (
            dryBaseRate);

    float inPeak = 0.0f;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        inPeak =
            juce::jmax (
                inPeak,
                buffer.getMagnitude (
                    ch, 0,
                    buffer.getNumSamples()));
    }

    inputMeter.store (
        juce::jlimit (
            0.0f, 1.0f,
            inPeak));

    if (oversampler != nullptr)
    {
        juce::dsp::AudioBlock<float> baseBlock (
            buffer);

        auto upBlock =
            oversampler->processSamplesUp (
                baseBlock);

        std::array<float*, 2> pointers {
            nullptr, nullptr
        };

        for (size_t ch = 0;
             ch < upBlock.getNumChannels()
             && ch < pointers.size();
             ++ch)
        {
            pointers[ch] =
                upBlock.getChannelPointer (ch);
        }

        juce::AudioBuffer<float> internalBuffer (
            pointers.data(),
            static_cast<int> (
                upBlock.getNumChannels()),
            static_cast<int> (
                upBlock.getNumSamples()));

        processInternal (
            internalBuffer);

        oversampler->processSamplesDown (
            baseBlock);
    }
    else
    {
        processInternal (
            buffer);
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
                    ch, 0,
                    buffer.getNumSamples()));
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
                0, numMacroBands - 1,
                band))].load();
}

float SculptChannelAudioProcessor::getCompMeter (int band) const noexcept
{
    return compMeters[
        static_cast<size_t> (
            juce::jlimit (
                0, numMacroBands - 1,
                band))].load();
}

float SculptChannelAudioProcessor::getSatMeter (int band) const noexcept
{
    return satMeters[
        static_cast<size_t> (
            juce::jlimit (
                0, numMacroBands - 1,
                band))].load();
}

void SculptChannelAudioProcessor::getStateInformation (
    juce::MemoryBlock& destData)
{
    if (auto xml =
        apvts.copyState().createXml())
    {
        copyXmlToBinary (
            *xml,
            destData);
    }
}

void SculptChannelAudioProcessor::setStateInformation (
    const void* data,
    int sizeInBytes)
{
    if (auto xml =
        getXmlFromBinary (
            data,
            sizeInBytes))
    {
        if (xml->hasTagName (
                apvts.state.getType()))
        {
            apvts.replaceState (
                juce::ValueTree::fromXml (
                    *xml));
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
