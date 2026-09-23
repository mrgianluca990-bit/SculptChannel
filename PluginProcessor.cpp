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

        band.compEnv = 0.0f;
        band.compGain = 1.0f;
        band.sootheGain = 1.0f;
    }

    for (auto& detector : resDetectors)
    {
        detector.filter.prepare (spec);
        detector.filter.reset();

        detector.work.setSize (
            static_cast<int> (channels),
            maxInternalBlockSize,
            false, false, true);

        detector.slowEnergy = 0.0f;
    }

    outputGain.reset (sampleRate, 0.04);
    outputGain.setCurrentAndTargetValue (1.0f);

    soothePressure.fill (0.0f);

    configureMacroFilters();
    configureResDetectors();
    computeDetectorWeights();
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
           190.0f, 0.56f);

    setup (macroBands[1].filter,
           juce::dsp::StateVariableTPTFilterType::bandpass,
           720.0f, 0.74f);

    setup (macroBands[2].filter,
           juce::dsp::StateVariableTPTFilterType::bandpass,
           3500.0f, 0.72f);

    setup (macroBands[3].filter,
           juce::dsp::StateVariableTPTFilterType::highpass,
           6800.0f, 0.58f);
}

void SculptChannelAudioProcessor::configureResDetectors()
{
    constexpr float detectorQ = 4.2f;

    for (int i = 0; i < numResBands; ++i)
    {
        auto& f = resDetectors[static_cast<size_t> (i)].filter;

        f.setType (juce::dsp::StateVariableTPTFilterType::bandpass);

        f.setCutoffFrequency (
            juce::jmin (
                resFrequencies[static_cast<size_t> (i)],
                static_cast<float> (internalSampleRate * 0.44)));

        f.setResonance (detectorQ);
    }
}

void SculptChannelAudioProcessor::computeDetectorWeights()
{
    constexpr std::array<float, numMacroBands> centres {
        95.0f, 700.0f, 3500.0f, 10000.0f
    };

    constexpr std::array<float, numMacroBands> widths {
        1.45f, 1.30f, 1.20f, 1.28f
    };

    constexpr std::array<float, 6> variationCentres {
        200.0f, 500.0f, 1000.0f, 3000.0f, 5000.0f, 8000.0f
    };

    for (int b = 0; b < numResBands; ++b)
    {
        const float f =
            resFrequencies[static_cast<size_t> (b)];

        float total = 0.0f;

        for (int m = 0; m < numMacroBands; ++m)
        {
            const float d =
                std::log2 (
                    f / centres[static_cast<size_t> (m)]);

            const float w =
                std::exp (
                    -0.5f
                    * (d * d)
                    / (widths[static_cast<size_t> (m)]
                       * widths[static_cast<size_t> (m)]));

            detectorWeights[static_cast<size_t> (b)][static_cast<size_t> (m)] = w;
            total += w;
        }

        if (total > 0.0001f)
        {
            for (int m = 0; m < numMacroBands; ++m)
                detectorWeights[static_cast<size_t> (b)][static_cast<size_t> (m)] /= total;
        }

        float focus = 0.0f;

        for (const auto target : variationCentres)
        {
            const float d =
                std::log2 (f / target);

            const float local =
                std::exp (
                    -0.5f
                    * (d * d)
                    / (0.20f * 0.20f));

            focus = juce::jmax (focus, local);
        }

        // VAR now only changes detector sensitivity.
        // It does NOT insert or subtract any additional filter from the audio path.
        variationSensitivity[static_cast<size_t> (b)] =
            1.0f + 0.55f * focus;
    }
}

float SculptChannelAudioProcessor::curve (float magnitude) const noexcept
{
    magnitude = juce::jlimit (0.0f, 1.0f, magnitude);

    return juce::jlimit (
        0.0f, 1.0f,
        0.35f * magnitude
        + 0.65f * std::pow (magnitude, 0.72f));
}

float SculptChannelAudioProcessor::getBlockRMS (
    const juce::AudioBuffer<float>& buffer) const noexcept
{
    if (buffer.getNumChannels() <= 0 || buffer.getNumSamples() <= 0)
        return 0.0f;

    double sum = 0.0;
    const double count =
        static_cast<double> (
            buffer.getNumChannels()
            * buffer.getNumSamples());

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const auto* p = buffer.getReadPointer (ch);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const double s = p[i];
            sum += s * s;
        }
    }

    return static_cast<float> (
        std::sqrt (
            sum / juce::jmax (1.0, count)));
}

void SculptChannelAudioProcessor::analyseResonance (
    const juce::AudioBuffer<float>& source,
    const std::array<float, numMacroBands>& macros,
    bool variationEnabled)
{
    std::array<float, numResBands> bandDb {};
    std::array<float, numMacroBands> pressure {};
    pressure.fill (0.0f);

    for (int b = 0; b < numResBands; ++b)
    {
        auto& detector =
            resDetectors[static_cast<size_t> (b)];

        detector.work.setSize (
            source.getNumChannels(),
            source.getNumSamples(),
            false, false, true);

        detector.work.makeCopyOf (
            source, true);

        juce::dsp::AudioBlock<float> block (
            detector.work);

        juce::dsp::ProcessContextReplacing<float> context (
            block);

        detector.filter.process (
            context);

        const float rms =
            getBlockRMS (
                detector.work);

        bandDb[static_cast<size_t> (b)] =
            juce::Decibels::gainToDecibels (
                rms + 1.0e-8f,
                -120.0f);

        const float blockSeconds =
            static_cast<float> (
                source.getNumSamples()
                / internalSampleRate);

        const float tc =
            juce::jmap (
                resFrequencies[static_cast<size_t> (b)],
                20.0f, 20000.0f,
                0.34f, 0.13f);

        const float coeff =
            std::exp (
                -blockSeconds
                / juce::jmax (0.05f, tc));

        if (detector.slowEnergy <= 1.0e-7f)
            detector.slowEnergy = rms;
        else
            detector.slowEnergy =
                coeff * detector.slowEnergy
                + (1.0f - coeff) * rms;
    }

    for (int b = 0; b < numResBands; ++b)
    {
        float macroAmount = 0.0f;

        for (int m = 0; m < numMacroBands; ++m)
        {
            const float knob =
                std::abs (
                    macros[static_cast<size_t> (m)]
                    / 100.0f);

            macroAmount +=
                knob
                * detectorWeights[static_cast<size_t> (b)][static_cast<size_t> (m)];
        }

        const float activity =
            curve (
                juce::jlimit (0.0f, 1.0f, macroAmount));

        if (activity < 0.08f)
            continue;

        float neighbourDb = 0.0f;
        float weightSum = 0.0f;

        for (int off = -2; off <= 2; ++off)
        {
            if (off == 0)
                continue;

            const int n = b + off;

            if (n < 0 || n >= numResBands)
                continue;

            const float w =
                std::abs (off) == 1
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
            neighbourDb = bandDb[static_cast<size_t> (b)];

        const float slowDb =
            juce::Decibels::gainToDecibels (
                resDetectors[static_cast<size_t> (b)].slowEnergy + 1.0e-8f,
                -120.0f);

        const float varBoost =
            variationEnabled
                ? variationSensitivity[static_cast<size_t> (b)]
                : 1.0f;

        const float spectralExcess =
            juce::jmax (
                0.0f,
                bandDb[static_cast<size_t> (b)]
                - neighbourDb
                - (5.0f / varBoost));

        const float temporalExcess =
            juce::jmax (
                0.0f,
                bandDb[static_cast<size_t> (b)]
                - slowDb
                - (4.0f / varBoost));

        const float resonanceScore =
            juce::jlimit (
                0.0f, 1.0f,
                (spectralExcess * 0.16f
                 + temporalExcess * 0.07f)
                * activity
                * varBoost);

        for (int m = 0; m < numMacroBands; ++m)
        {
            pressure[static_cast<size_t> (m)] =
                juce::jmax (
                    pressure[static_cast<size_t> (m)],
                    resonanceScore
                    * detectorWeights[static_cast<size_t> (b)][static_cast<size_t> (m)]);
        }
    }

    // Smooth only four macro damping values.
    // This is why VAR can no longer create comb-filter artefacts.
    for (int m = 0; m < numMacroBands; ++m)
    {
        const float target =
            juce::jlimit (
                0.0f, 1.0f,
                pressure[static_cast<size_t> (m)]);

        soothePressure[static_cast<size_t> (m)] =
            0.82f * soothePressure[static_cast<size_t> (m)]
            + 0.18f * target;

        resMeters[static_cast<size_t> (m)].store (
            soothePressure[static_cast<size_t> (m)]);
    }
}

float SculptChannelAudioProcessor::saturate (
    float sample,
    float satAmount,
    int bandIndex) const noexcept
{
    satAmount =
        juce::jlimit (
            0.0f, 1.0f,
            satAmount);

    float maxDrive = 5.5f;
    float maxBlend = 0.72f;
    float asym = 0.03f;

    switch (bandIndex)
    {
        case 0:
            maxDrive = 6.4f;
            maxBlend = 0.78f;
            asym = 0.045f;
            break;

        case 1:
            maxDrive = 6.0f;
            maxBlend = 0.76f;
            asym = 0.038f;
            break;

        case 2:
            maxDrive = 5.2f;
            maxBlend = 0.68f;
            asym = 0.022f;
            break;

        default:
            maxDrive = 4.4f;
            maxBlend = 0.58f;
            asym = 0.012f;
            break;
    }

    const float drive =
        1.0f
        + (maxDrive - 1.0f)
          * satAmount;

    const float biased =
        sample
        + asym
          * satAmount
          * sample * sample
          * (sample >= 0.0f ? 1.0f : -1.0f);

    const float stage1 =
        std::tanh (
            biased * drive);

    const float stage2 =
        std::tanh (
            stage1
            * (1.0f + 0.65f * satAmount));

    const float shaped =
        stage1
        + (stage2 - stage1)
          * (0.55f * satAmount);

    const float blend =
        maxBlend
        * satAmount;

    return sample
        + (shaped - sample)
          * blend;
}

void SculptChannelAudioProcessor::processMacroBand (
    juce::AudioBuffer<float>& source,
    int bandIndex,
    float macroValue,
    float sootheAmount)
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
        curve (magnitude);

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

    // EQ: still the first and most obvious sensation.
    const float eqDb =
        positive > 0.0f
            ? 10.5f * activity * positive
            : -11.0f * activity * negative;

    state.band.applyGain (
        juce::Decibels::decibelsToGain (
            eqDb));

    // SOOTHE GUARDRAIL:
    // only four smooth macro dampers, no narrow subtraction in the audio path.
    // max around 2.2 dB at very high detected resonance pressure.
    const float sootheDb =
        -2.2f
        * juce::jlimit (
            0.0f, 1.0f,
            sootheAmount);

    const float targetSootheGain =
        juce::Decibels::decibelsToGain (
            sootheDb);

    const float blockSeconds =
        static_cast<float> (
            source.getNumSamples()
            / internalSampleRate);

    const float sootheCoeff =
        std::exp (
            -blockSeconds / 0.10f);

    state.sootheGain =
        sootheCoeff * state.sootheGain
        + (1.0f - sootheCoeff)
          * targetSootheGain;

    state.band.applyGain (
        state.sootheGain);

    // COMP significantly reduced vs v0.5.
    const float compEntrance =
        juce::jlimit (
            0.0f, 1.0f,
            (positive - 0.28f) / 0.72f);

    const float compAmount =
        positive > 0.0f
            ? 0.68f * std::pow (compEntrance, 1.10f)
            : 0.16f * negative * activity;

    float attackMs = 18.0f;
    float releaseMs = 120.0f;

    switch (bandIndex)
    {
        case 0: attackMs = 34.0f; releaseMs = 190.0f; break;
        case 1: attackMs = 18.0f; releaseMs = 130.0f; break;
        case 2: attackMs = 10.0f; releaseMs = 92.0f;  break;
        default: attackMs = 5.0f; releaseMs = 68.0f; break;
    }

    const float attackCoeff =
        std::exp (
            -1.0f
            / static_cast<float> (
                internalSampleRate
                * attackMs * 0.001));

    const float releaseCoeff =
        std::exp (
            -1.0f
            / static_cast<float> (
                internalSampleRate
                * releaseMs * 0.001));

    const float thresholdDb =
        positive > 0.0f
            ? juce::jmap (
                compAmount,
                0.0f, 0.68f,
                -3.0f, -12.5f)
            : -5.0f;

    const float ratio =
        positive > 0.0f
            ? juce::jmap (
                compAmount,
                0.0f, 0.68f,
                1.0f, 2.6f)
            : 1.35f;

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
                        state.band.getSample (ch, i)));
        }

        const float envCoeff =
            detector > state.compEnv
                ? attackCoeff
                : releaseCoeff;

        state.compEnv =
            envCoeff * state.compEnv
            + (1.0f - envCoeff)
              * detector;

        const float envDb =
            juce::Decibels::gainToDecibels (
                state.compEnv + 1.0e-8f,
                -120.0f);

        const float overDb =
            juce::jmax (
                0.0f,
                envDb - thresholdDb);

        const float grDb =
            overDb
            * (1.0f
               - 1.0f / juce::jmax (1.0f, ratio));

        maxGrDb =
            juce::jmax (
                maxGrDb,
                grDb);

        const float targetGain =
            juce::Decibels::decibelsToGain (
                -grDb);

        const float gainCoeff =
            targetGain < state.compGain
                ? attackCoeff
                : releaseCoeff;

        state.compGain =
            gainCoeff * state.compGain
            + (1.0f - gainCoeff)
              * targetGain;

        for (int ch = 0; ch < state.band.getNumChannels(); ++ch)
        {
            state.band.setSample (
                ch, i,
                state.band.getSample (ch, i)
                * state.compGain);
        }
    }

    compMeters[static_cast<size_t> (bandIndex)].store (
        juce::jlimit (
            0.0f, 1.0f,
            maxGrDb / 8.0f));

    // SAT starts earlier and rises much more strongly.
    const float satEntrance =
        juce::jlimit (
            0.0f, 1.0f,
            (positive - 0.34f) / 0.66f);

    const float satAmount =
        std::pow (
            satEntrance,
            0.90f);

    float nonlinear = 0.0f;

    for (int ch = 0; ch < state.band.getNumChannels(); ++ch)
    {
        auto* data =
            state.band.getWritePointer (ch);

        for (int i = 0; i < state.band.getNumSamples(); ++i)
        {
            const float before = data[i];
            const float after =
                saturate (
                    before,
                    satAmount,
                    bandIndex);

            nonlinear +=
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
            nonlinear / norm * 5.5f));

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

    analyseResonance (
        buffer,
        macros,
        variationEnabled);

    for (int band = 0; band < numMacroBands; ++band)
    {
        processMacroBand (
            buffer,
            band,
            macros[static_cast<size_t> (band)],
            soothePressure[static_cast<size_t> (band)]);
    }
}

void SculptChannelAudioProcessor::processBlock (
    juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

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

    const float outputDb =
        apvts.getRawParameterValue ("output")->load();

    outputGain.setTargetValue (
        juce::Decibels::decibelsToGain (
            outputDb));

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float g =
            outputGain.getNextValue();

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            buffer.setSample (
                ch, i,
                buffer.getSample (ch, i)
                * g);
        }
    }

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
            *xml, destData);
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
