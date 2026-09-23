#include "PluginEditor.h"

SculptLookAndFeel::SculptLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId,       juce::Colour (0xffefe7d5));
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff17130f));
    setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
}

void SculptLookAndFeel::drawRotarySlider (
    juce::Graphics& g,
    int x, int y, int width, int height,
    float sliderPosProportional,
    float rotaryStartAngle,
    float rotaryEndAngle,
    juce::Slider&)
{
    auto bounds = juce::Rectangle<float> (
        static_cast<float> (x),
        static_cast<float> (y),
        static_cast<float> (width),
        static_cast<float> (height)).reduced (12.0f);

    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    auto dial = juce::Rectangle<float> (0, 0, size, size)
                    .withCentre (bounds.getCentre());

    const auto centre = dial.getCentre();

    const float angle = rotaryStartAngle
                      + sliderPosProportional
                      * (rotaryEndAngle - rotaryStartAngle);

    const float radius = dial.getWidth() * 0.5f;

    // outer shadow
    g.setColour (juce::Colour (0x77000000));
    g.fillEllipse (dial.translated (0.0f, 7.0f).expanded (3.0f));

    // brass outer ring
    g.setColour (juce::Colour (0xff7c6542));
    g.fillEllipse (dial);

    g.setColour (juce::Colour (0xffb99a64));
    g.drawEllipse (dial.reduced (1.5f), 1.5f);

    // dark knob
    auto knob = dial.reduced (9.0f);

    juce::ColourGradient grad (
        juce::Colour (0xff3a332b),
        knob.getCentreX(), knob.getY(),
        juce::Colour (0xff15110e),
        knob.getCentreX(), knob.getBottom(),
        false);

    g.setGradientFill (grad);
    g.fillEllipse (knob);

    g.setColour (juce::Colour (0xff5a4d3d));
    g.drawEllipse (knob, 1.0f);

    // pointer
    juce::Path pointer;
    pointer.addRoundedRectangle (
        -2.0f,
        -knob.getHeight() * 0.40f,
        4.0f,
        knob.getHeight() * 0.29f,
        2.0f);

    g.setColour (juce::Colour (0xfff0dfb6));
    g.fillPath (
        pointer,
        juce::AffineTransform::rotation (angle)
            .translated (centre.x, centre.y));

    g.setColour (juce::Colour (0xffa8864d));
    g.fillEllipse (juce::Rectangle<float> (5, 5).withCentre (centre));

    // centre zero marker
    const float zeroAngle = 0.5f * (rotaryStartAngle + rotaryEndAngle);
    const juce::Point<float> zeroPoint (
        centre.x + std::sin (zeroAngle) * (radius + 3.0f),
        centre.y - std::cos (zeroAngle) * (radius + 3.0f));

    g.setColour (juce::Colour (0xffd6bc86));
    g.fillEllipse (juce::Rectangle<float> (4, 4).withCentre (zeroPoint));
}

SculptChannelAudioProcessorEditor::SculptChannelAudioProcessorEditor (
    SculptChannelAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&look);

    setupBand (low,      "LOW",      "85 Hz",   "low");
    setupBand (mid,      "MID",      "700 Hz",  "mid");
    setupBand (high,     "HIGH",     "3.5 kHz", "high");
    setupBand (presence, "PRESENCE", "10 kHz",  "presence");

    output.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    output.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 24);
    addAndMakeVisible (output);

    outputLabel.setText ("OUTPUT", juce::dontSendNotification);
    outputLabel.setJustificationType (juce::Justification::centred);
    outputLabel.setColour (juce::Label::textColourId, juce::Colour (0xffd7c6a0));
    outputLabel.setFont (juce::Font (11.0f, juce::Font::bold));
    addAndMakeVisible (outputLabel);

    outputAttachment =
        std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.getAPVTS(), "output", output);

    setResizable (true, true);
    setResizeLimits (920, 540, 1500, 900);
    setSize (1180, 700);

    startTimerHz (30);
}

SculptChannelAudioProcessorEditor::~SculptChannelAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SculptChannelAudioProcessorEditor::setupBand (
    BandUI& band,
    const juce::String& title,
    const juce::String& freq,
    const juce::String& parameterID)
{
    band.knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    band.knob.setRotaryParameters (
        juce::MathConstants<float>::pi * 1.20f,
        juce::MathConstants<float>::pi * 2.80f,
        true);

    band.knob.setTextBoxStyle (
        juce::Slider::TextBoxBelow, false, 88, 25);

    addAndMakeVisible (band.knob);

    band.title.setText (title, juce::dontSendNotification);
    band.title.setJustificationType (juce::Justification::centred);
    band.title.setColour (
        juce::Label::textColourId,
        juce::Colour (0xfff0e3c2));

    band.title.setFont (
        juce::Font (15.0f, juce::Font::bold));

    addAndMakeVisible (band.title);

    band.freq.setText (freq, juce::dontSendNotification);
    band.freq.setJustificationType (juce::Justification::centred);
    band.freq.setColour (
        juce::Label::textColourId,
        juce::Colour (0xff8f7b5d));

    band.freq.setFont (juce::Font (10.0f));
    addAndMakeVisible (band.freq);

    band.attachment =
        std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.getAPVTS(), parameterID, band.knob);
}

void SculptChannelAudioProcessorEditor::drawAnalogScale (
    juce::Graphics& g,
    juce::Rectangle<float> area)
{
    g.setColour (juce::Colour (0xff6c5a42));
    g.setFont (juce::Font (9.0f, juce::Font::bold));

    g.drawText ("RECESS",
                static_cast<int> (area.getX()),
                static_cast<int> (area.getY()),
                60, 14,
                juce::Justification::centredLeft);

    g.drawText ("0",
                static_cast<int> (area.getCentreX() - 10.0f),
                static_cast<int> (area.getY()),
                20, 14,
                juce::Justification::centred);

    g.drawText ("FORWARD",
                static_cast<int> (area.getRight() - 70.0f),
                static_cast<int> (area.getY()),
                70, 14,
                juce::Justification::centredRight);
}

void SculptChannelAudioProcessorEditor::drawMeterStack (
    juce::Graphics& g,
    juce::Rectangle<float> area,
    int bandIndex)
{
    const float res  = processor.getResMeter (bandIndex);
    const float comp = processor.getCompMeter (bandIndex);
    const float sat  = processor.getSatMeter (bandIndex);

    const float gap = 9.0f;
    const float rowH = (area.getHeight() - gap * 2.0f) / 3.0f;

    struct MeterInfo
    {
        const char* name;
        float value;
        juce::Colour colour;
    };

    const MeterInfo meters[3] = {
        { "RES",  res,  juce::Colour (0xffc9a35f) },
        { "COMP", comp, juce::Colour (0xffb87544) },
        { "SAT",  sat,  juce::Colour (0xff9f553c) }
    };

    for (int i = 0; i < 3; ++i)
    {
        auto row = area.removeFromTop (rowH);

        g.setColour (juce::Colour (0xff0d0b09));
        g.fillRoundedRectangle (row, 4.0f);

        auto fill = row.reduced (3.0f);
        fill.setWidth (fill.getWidth()
                       * juce::jlimit (0.0f, 1.0f, meters[i].value));

        g.setColour (meters[i].colour);
        g.fillRoundedRectangle (fill, 3.0f);

        g.setColour (juce::Colour (0xff8f7b5d));
        g.setFont (juce::Font (9.0f, juce::Font::bold));

        g.drawText (
            meters[i].name,
            static_cast<int> (row.getX()),
            static_cast<int> (row.getY() - 14.0f),
            static_cast<int> (row.getWidth()),
            12,
            juce::Justification::centredLeft);

        area.removeFromTop (gap);
    }
}

void SculptChannelAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    juce::ColourGradient bg (
        juce::Colour (0xff2a2118),
        bounds.getCentreX(), bounds.getY(),
        juce::Colour (0xff0d0a08),
        bounds.getCentreX(), bounds.getBottom(),
        false);

    g.setGradientFill (bg);
    g.fillAll();

    auto panel = bounds.reduced (22.0f);

    g.setColour (juce::Colour (0xff1b1510));
    g.fillRoundedRectangle (panel, 18.0f);

    g.setColour (juce::Colour (0xff655237));
    g.drawRoundedRectangle (panel, 18.0f, 1.5f);

    // header
    g.setColour (juce::Colour (0xfff1e2bd));
    g.setFont (juce::Font (34.0f, juce::Font::bold));

    g.drawText (
        "SCULPT",
        48, 34,
        240, 42,
        juce::Justification::centredLeft);

    g.setColour (juce::Colour (0xffb89a67));
    g.setFont (juce::Font (13.0f, juce::Font::bold));

    g.drawText (
        "CHANNEL",
        51, 75,
        120, 20,
        juce::Justification::centredLeft);

    g.setColour (juce::Colour (0xff706049));
    g.setFont (juce::Font (10.0f));

    g.drawText (
        "DYNAMIC ANALOG TONE CONDITIONER",
        180, 75,
        310, 20,
        juce::Justification::centredLeft);

    g.setColour (juce::Colour (0xff3f3427));
    g.drawLine (48.0f, 105.0f,
                static_cast<float> (getWidth() - 48),
                105.0f,
                1.0f);

    // four vertical strips
    auto content = getLocalBounds().reduced (42);
    content.removeFromTop (82);

    const int outputWidth = 130;
    auto outputArea = content.removeFromRight (outputWidth);
    content.removeFromRight (14);

    const int gap = 12;
    const int stripW = (content.getWidth() - gap * 3) / 4;

    for (int i = 0; i < 4; ++i)
    {
        auto strip = juce::Rectangle<float> (
            static_cast<float> (content.getX() + i * (stripW + gap)),
            static_cast<float> (content.getY()),
            static_cast<float> (stripW),
            static_cast<float> (content.getHeight()));

        juce::ColourGradient stripGrad (
            juce::Colour (0xff262017),
            strip.getCentreX(), strip.getY(),
            juce::Colour (0xff15110d),
            strip.getCentreX(), strip.getBottom(),
            false);

        g.setGradientFill (stripGrad);
        g.fillRoundedRectangle (strip, 12.0f);

        g.setColour (juce::Colour (0xff493b2a));
        g.drawRoundedRectangle (strip, 12.0f, 1.0f);

        drawAnalogScale (
            g,
            strip.reduced (14.0f).withTrimmedTop (70.0f).withHeight (16.0f));

        auto meterArea = strip.reduced (16.0f);
        meterArea.removeFromTop (310.0f);
        meterArea.removeFromBottom (30.0f);

        drawMeterStack (g, meterArea, i);
    }

    // output side panel
    auto outF = outputArea.toFloat();

    g.setColour (juce::Colour (0xff19140f));
    g.fillRoundedRectangle (outF, 12.0f);

    g.setColour (juce::Colour (0xff493b2a));
    g.drawRoundedRectangle (outF, 12.0f, 1.0f);

    g.setColour (juce::Colour (0xff75634a));
    g.setFont (juce::Font (9.0f, juce::Font::bold));

    g.drawText (
        "MASTER",
        outputArea.getX(),
        outputArea.getBottom() - 40,
        outputArea.getWidth(),
        14,
        juce::Justification::centred);

    g.setColour (juce::Colour (0xff4c3f30));
    g.setFont (juce::Font (9.0f, juce::Font::bold));

    g.drawText (
        "RES → COMP → SAT",
        48,
        getHeight() - 25,
        getWidth() - 96,
        14,
        juce::Justification::centred);
}

void SculptChannelAudioProcessorEditor::resized()
{
    auto content = getLocalBounds().reduced (42);
    content.removeFromTop (82);

    const int outputWidth = 130;
    auto outputArea = content.removeFromRight (outputWidth);
    content.removeFromRight (14);

    const int gap = 12;
    const int stripW = (content.getWidth() - gap * 3) / 4;

    for (auto* band : bands)
    {
        auto strip = content.removeFromLeft (stripW).reduced (10, 10);

        band->title.setBounds (strip.removeFromTop (26));
        band->freq.setBounds  (strip.removeFromTop (17));

        strip.removeFromTop (10);

        auto knobArea = strip.removeFromTop (240);
        band->knob.setBounds (knobArea.reduced (6));

        content.removeFromLeft (gap);
    }

    auto out = outputArea.reduced (10, 16);
    outputLabel.setBounds (out.removeFromTop (24));
    out.removeFromTop (18);
    output.setBounds (out.removeFromTop (180).reduced (8));
}

void SculptChannelAudioProcessorEditor::timerCallback()
{
    repaint();
}
