#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class SculptLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    SculptLookAndFeel();

    void drawRotarySlider (juce::Graphics&,
                           int x, int y, int width, int height,
                           float sliderPosProportional,
                           float rotaryStartAngle,
                           float rotaryEndAngle,
                           juce::Slider&) override;
};

class SculptChannelAudioProcessorEditor final
    : public juce::AudioProcessorEditor,
      private juce::Timer
{
public:
    explicit SculptChannelAudioProcessorEditor (SculptChannelAudioProcessor&);
    ~SculptChannelAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SculptChannelAudioProcessor& processor;
    SculptLookAndFeel look;

    struct BandUI
    {
        juce::Slider knob;
        juce::Label title;
        juce::Label freq;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    BandUI low, mid, high, presence;
    std::array<BandUI*, 4> bands { &low, &mid, &high, &presence };

    juce::Slider output;
    juce::Label outputLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;

    void setupBand (BandUI&,
                    const juce::String& title,
                    const juce::String& freq,
                    const juce::String& parameterID);

    void drawMeterStack (juce::Graphics&,
                         juce::Rectangle<float>,
                         int bandIndex);

    void drawAnalogScale (juce::Graphics&,
                          juce::Rectangle<float>);

    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SculptChannelAudioProcessorEditor)
};
