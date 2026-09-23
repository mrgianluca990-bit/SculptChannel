#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class SculptOverlayLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional,
                           float rotaryStartAngle,
                           float rotaryEndAngle,
                           juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool, bool) override;
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
    SculptOverlayLookAndFeel look;
    juce::Image background;

    struct KnobUI
    {
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    KnobUI low, mid, high, presence, output;
    juce::ToggleButton variationButton { "VAR" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> variationAttachment;

    void setupKnob (KnobUI&, const juce::String& param, bool isOutput = false);
    juce::Rectangle<int> knobBoundsForIndex (int index) const;
    juce::Rectangle<int> outputKnobBounds() const;
    juce::Rectangle<int> varButtonBounds() const;

    void drawSegmentMeter (juce::Graphics&, juce::Rectangle<float>, float value,
                           int segments,
                           juce::Colour low, juce::Colour high,
                           float alpha = 0.9f);
    void drawAllMeters (juce::Graphics&);
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SculptChannelAudioProcessorEditor)
};
