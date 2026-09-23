#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

class SculptChannelAudioProcessor final : public juce::AudioProcessor
{
public:
    SculptChannelAudioProcessor();
    ~SculptChannelAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    float getResMeter (int band) const noexcept;
    float getCompMeter (int band) const noexcept;
    float getSatMeter (int band) const noexcept;

private:
    static constexpr int numBands = 4;

    struct BandState
    {
        juce::dsp::StateVariableTPTFilter<float> preFilter;
        juce::dsp::Compressor<float> compressor;
        juce::AudioBuffer<float> work;
        float detectorEnv = 0.0f;
        float smoothEnergy = 0.0f;
        float previousInput = 0.0f;
    };

    juce::AudioProcessorValueTreeState apvts;
    std::array<BandState, numBands> bands;

    juce::AudioBuffer<float> dryBuffer;
    juce::SmoothedValue<float> outputGain;

    double currentSampleRate = 44100.0;

    std::array<std::atomic<float>, numBands> resMeters;
    std::array<std::atomic<float>, numBands> compMeters;
    std::array<std::atomic<float>, numBands> satMeters;

    std::array<float, numBands> centreHz  { 85.0f, 700.0f, 3500.0f, 10000.0f };
    std::array<float, numBands> qValues   { 0.70f, 1.15f, 0.95f, 0.70f };

    void configureBandFilters();
    float processResonanceControl (BandState& state,
                                   float sample,
                                   float amount,
                                   int bandIndex) noexcept;

    float processSaturation (float sample,
                             float positiveAmount,
                             float negativeAmount,
                             int bandIndex) const noexcept;

    void processBand (juce::AudioBuffer<float>& source,
                      int bandIndex,
                      float macroValue);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SculptChannelAudioProcessor)
};
