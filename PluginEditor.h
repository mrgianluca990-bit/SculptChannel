#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class SculptVintageLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    SculptVintageLookAndFeel();

    void drawRotarySlider (juce::Graphics&,
                           int x, int y, int width, int height,
                           float sliderPosProportional,
                           float rotaryStartAngle,
                           float rotaryEndAngle,
                           juce::Slider&) override;

    void drawToggleButton (juce::Graphics&,
                           juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;
};

class SculptChannelAudioProcessorEditor final
    : public juce::AudioProcessorEditor,
      private juce::Timer
{
public:
    explicit SculptChannelAudioProcessorEditor (
        SculptChannelAudioProcessor&);
    ~SculptChannelAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SculptChannelAudioProcessor& processor;
    SculptVintageLookAndFeel look;

    struct BandUI
    {
        juce::Slider knob;
        juce::Label title;
        juce::Label freq;

        std::unique_ptr<
            juce::AudioProcessorValueTreeState::SliderAttachment>
            attachment;
    };

    BandUI low, mid, high, presence;

    std::array<BandUI*, 4> bands {
        &low, &mid, &high, &presence
    };

    juce::ToggleButton variationButton { "VARIATION" };
    juce::ToggleButton levelMatchButton { "LEVEL MATCH" };

    std::unique_ptr<
        juce::AudioProcessorValueTreeState::ButtonAttachment>
        variationAttachment;

    std::unique_ptr<
        juce::AudioProcessorValueTreeState::ButtonAttachment>
        levelMatchAttachment;

    juce::Slider output;
    juce::Label outputLabel;

    std::unique_ptr<
        juce::AudioProcessorValueTreeState::SliderAttachment>
        outputAttachment;

    void setupBand (BandUI&,
                    const juce::String& title,
                    const juce::String& freq,
                    const juce::String& parameterID);

    void drawFaceplateTexture (juce::Graphics&, juce::Rectangle<float>);
    void drawWoodCheek (juce::Graphics&, juce::Rectangle<float>, bool left);
    void drawModuleFrame (juce::Graphics&, juce::Rectangle<float>);
    void drawKnobScale (juce::Graphics&, juce::Rectangle<float>);

    void drawMeterStack (juce::Graphics&,
                         juce::Rectangle<float>,
                         int bandIndex);

    void drawBarMeter (juce::Graphics&,
                       juce::Rectangle<float>,
                       float value,
                       juce::Colour colour,
                       const juce::String& label);

    void drawResMatrix (juce::Graphics&,
                        juce::Rectangle<float>);

    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (
        SculptChannelAudioProcessorEditor)
};
