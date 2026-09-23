#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    constexpr float startAngle = juce::MathConstants<float>::pi * 1.20f;
    constexpr float endAngle   = juce::MathConstants<float>::pi * 2.80f;
}

void SculptOverlayLookAndFeel::drawRotarySlider (juce::Graphics& g,
                                                 int x, int y, int width, int height,
                                                 float sliderPosProportional,
                                                 float rotaryStartAngle,
                                                 float rotaryEndAngle,
                                                 juce::Slider&)
{
    auto area = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    auto c = area.getCentre();
    const float radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.44f;

    const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    // subtle glow arc
    juce::Path arc;
    arc.addCentredArc (c.x, c.y, radius * 0.93f, radius * 0.93f, 0.0f, rotaryStartAngle, angle, true);
    g.setColour (juce::Colour (0xccff8d17));
    g.strokePath (arc, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // needle / pointer overlay over the baked knob
    auto inner = juce::Point<float> (std::sin (angle), -std::cos (angle)) * (radius * 0.18f);
    auto outer = juce::Point<float> (std::sin (angle), -std::cos (angle)) * (radius * 0.70f);

    g.setColour (juce::Colour (0xfff1e7d0));
    g.drawLine ({ c + inner, c + outer }, 3.0f);

    g.setColour (juce::Colour (0x50ff8d17));
    g.drawLine ({ c + inner, c + outer }, 6.5f);
}

void SculptOverlayLookAndFeel::drawToggleButton (juce::Graphics& g,
                                                 juce::ToggleButton& button,
                                                 bool,
                                                 bool)
{
    auto area = button.getLocalBounds().toFloat();
    const bool on = button.getToggleState();

    if (on)
    {
        g.setColour (juce::Colour (0x35ff5a47));
        g.fillRoundedRectangle (area.reduced (2.0f), 6.0f);
        g.setColour (juce::Colour (0x95fff4dd));
        g.drawRoundedRectangle (area.reduced (4.0f), 5.0f, 1.2f);
    }
    else
    {
        g.setColour (juce::Colours::transparentBlack);
        g.fillRect (area);
    }
}

SculptChannelAudioProcessorEditor::SculptChannelAudioProcessorEditor (SculptChannelAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    background = juce::ImageFileFormat::loadFrom (BinaryData::sculpt_gui_bg_png, BinaryData::sculpt_gui_bg_pngSize);
    setLookAndFeel (&look);

    setupKnob (low, "low");
    setupKnob (mid, "mid");
    setupKnob (high, "high");
    setupKnob (presence, "presence");
    setupKnob (output, "output", true);

    variationButton.setClickingTogglesState (true);
    variationButton.setColour (juce::ToggleButton::textColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (variationButton);
    variationAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.getAPVTS(), "variation", variationButton);

    setSize (1672 / 2, 941 / 2);
    setResizable (true, true);
    setResizeLimits (836, 470, 1672, 941);
    startTimerHz (30);
}

SculptChannelAudioProcessorEditor::~SculptChannelAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SculptChannelAudioProcessorEditor::setupKnob (KnobUI& k, const juce::String& param, bool isOutput)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setRotaryParameters (startAngle, endAngle, true);
    k.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::thumbColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (k.slider);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.getAPVTS(), param, k.slider);
    k.slider.setMouseDragSensitivity (isOutput ? 220 : 180);
}

juce::Rectangle<int> SculptChannelAudioProcessorEditor::knobBoundsForIndex (int index) const
{
    const float sx = (float) getWidth() / 1672.0f;
    const float sy = (float) getHeight() / 941.0f;

    static const int xs[] = { 170, 470, 768, 1064 };
    const int y = 274;
    const int w = 196;
    const int h = 196;

    return juce::Rectangle<int> ((int) std::round (xs[index] * sx),
                                 (int) std::round (y * sy),
                                 (int) std::round (w * sx),
                                 (int) std::round (h * sy));
}

juce::Rectangle<int> SculptChannelAudioProcessorEditor::outputKnobBounds() const
{
    const float sx = (float) getWidth() / 1672.0f;
    const float sy = (float) getHeight() / 941.0f;
    return { (int) std::round (1333 * sx), (int) std::round (305 * sy),
             (int) std::round (160 * sx), (int) std::round (160 * sy) };
}

juce::Rectangle<int> SculptChannelAudioProcessorEditor::varButtonBounds() const
{
    const float sx = (float) getWidth() / 1672.0f;
    const float sy = (float) getHeight() / 941.0f;
    return { (int) std::round (1372 * sx), (int) std::round (575 * sy),
             (int) std::round (132 * sx), (int) std::round (92 * sy) };
}

void SculptChannelAudioProcessorEditor::drawSegmentMeter (juce::Graphics& g,
                                                          juce::Rectangle<float> area,
                                                          float value,
                                                          int segments,
                                                          juce::Colour low,
                                                          juce::Colour high,
                                                          float alpha)
{
    value = juce::jlimit (0.0f, 1.0f, value);
    const float gap = area.getWidth() * 0.022f;
    const float w = (area.getWidth() - gap * (segments - 1)) / (float) segments;
    const int lit = (int) std::round (value * segments);

    for (int i = 0; i < segments; ++i)
    {
        auto seg = juce::Rectangle<float> (area.getX() + i * (w + gap), area.getY(), w, area.getHeight());
        if (i < lit)
        {
            auto c = low.interpolatedWith (high, (float) i / (float) juce::jmax (1, segments - 1)).withAlpha (alpha);
            g.setColour (c);
            g.fillRoundedRectangle (seg, 1.0f);

            g.setColour (c.withAlpha (0.25f));
            g.fillRoundedRectangle (seg.expanded (1.2f, 1.2f), 1.8f);
        }
    }
}

void SculptChannelAudioProcessorEditor::drawAllMeters (juce::Graphics& g)
{
    const float sx = (float) getWidth() / 1672.0f;
    const float sy = (float) getHeight() / 941.0f;

    // Top IN/OUT meters
    drawSegmentMeter (g, { 1154.0f * sx, 122.0f * sy, 262.0f * sx, 22.0f * sy }, processor.getInputMeter(), 18,
                      juce::Colour (0xfffff0c9), juce::Colour (0xffff2c28), 0.90f);
    drawSegmentMeter (g, { 1154.0f * sx, 200.0f * sy, 262.0f * sx, 22.0f * sy }, processor.getOutputMeter(), 18,
                      juce::Colour (0xfffff0c9), juce::Colour (0xffff2c28), 0.90f);

    const int meterXs[] = { 223, 521, 818, 1115 };
    const int ys[] = { 585, 647, 710 };

    for (int b = 0; b < 4; ++b)
    {
        drawSegmentMeter (g, { meterXs[b] * sx, ys[0] * sy, 164.0f * sx, 19.0f * sy }, processor.getResMeter (b), 11,
                          juce::Colour (0xffffefc2), juce::Colour (0xffff2c28));
        drawSegmentMeter (g, { meterXs[b] * sx, ys[1] * sy, 164.0f * sx, 19.0f * sy }, processor.getCompMeter (b), 11,
                          juce::Colour (0xffffefc2), juce::Colour (0xffff2c28));
        drawSegmentMeter (g, { meterXs[b] * sx, ys[2] * sy, 164.0f * sx, 19.0f * sy }, processor.getSatMeter (b), 11,
                          juce::Colour (0xffffefc2), juce::Colour (0xffff2c28));
    }
}

void SculptChannelAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    if (background.isValid())
        g.drawImage (background, getLocalBounds().toFloat());
    drawAllMeters (g);
}

void SculptChannelAudioProcessorEditor::resized()
{
    low.slider.setBounds (knobBoundsForIndex (0));
    mid.slider.setBounds (knobBoundsForIndex (1));
    high.slider.setBounds (knobBoundsForIndex (2));
    presence.slider.setBounds (knobBoundsForIndex (3));
    output.slider.setBounds (outputKnobBounds());
    variationButton.setBounds (varButtonBounds());
}

void SculptChannelAudioProcessorEditor::timerCallback()
{
    repaint();
}
