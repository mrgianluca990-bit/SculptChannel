#include "PluginEditor.h"

SculptLookAndFeel::SculptLookAndFeel()
{
    setColour (
        juce::Slider::textBoxTextColourId,
        juce::Colour (0xffefe7d7));

    setColour (
        juce::Slider::textBoxBackgroundColourId,
        juce::Colour (0xff171b20));

    setColour (
        juce::Slider::textBoxOutlineColourId,
        juce::Colours::transparentBlack);
}

void SculptLookAndFeel::drawRotarySlider (
    juce::Graphics& g,
    int x, int y, int width, int height,
    float sliderPosProportional,
    float rotaryStartAngle,
    float rotaryEndAngle,
    juce::Slider&)
{
    auto bounds =
        juce::Rectangle<float> (
            static_cast<float> (x),
            static_cast<float> (y),
            static_cast<float> (width),
            static_cast<float> (height))
        .reduced (12.0f);

    const float size =
        juce::jmin (
            bounds.getWidth(),
            bounds.getHeight());

    auto dial =
        juce::Rectangle<float> (
            0, 0, size, size)
        .withCentre (bounds.getCentre());

    const auto centre =
        dial.getCentre();

    const float angle =
        rotaryStartAngle
        + sliderPosProportional
          * (rotaryEndAngle - rotaryStartAngle);

    g.setColour (juce::Colour (0x70000000));
    g.fillEllipse (
        dial.translated (0.0f, 7.0f)
            .expanded (3.0f));

    g.setColour (juce::Colour (0xff7d7b72));
    g.fillEllipse (dial);

    g.setColour (juce::Colour (0xffb1ac9d));
    g.drawEllipse (
        dial.reduced (1.5f), 1.2f);

    auto knob = dial.reduced (9.0f);

    juce::ColourGradient knobGrad (
        juce::Colour (0xff363c42),
        knob.getCentreX(),
        knob.getY(),
        juce::Colour (0xff15181c),
        knob.getCentreX(),
        knob.getBottom(),
        false);

    g.setGradientFill (knobGrad);
    g.fillEllipse (knob);

    g.setColour (juce::Colour (0xff596068));
    g.drawEllipse (knob, 1.0f);

    g.setColour (juce::Colour (0x22ffffff));
    g.drawEllipse (
        knob.reduced (4.0f), 1.0f);

    juce::Path pointer;

    pointer.addRoundedRectangle (
        -2.0f,
        -knob.getHeight() * 0.42f,
        4.0f,
        knob.getHeight() * 0.28f,
        2.0f);

    g.setColour (juce::Colour (0xffead8b4));

    g.fillPath (
        pointer,
        juce::AffineTransform::rotation (angle)
            .translated (centre.x, centre.y));

    g.setColour (juce::Colour (0xff67c7db));

    g.fillEllipse (
        juce::Rectangle<float> (5, 5)
            .withCentre (centre));
}

SculptChannelAudioProcessorEditor::
SculptChannelAudioProcessorEditor (
    SculptChannelAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p)
{
    setLookAndFeel (&look);

    setupBand (
        low,
        "LOW",
        "85 Hz",
        "low");

    setupBand (
        mid,
        "MID",
        "700 Hz",
        "mid");

    setupBand (
        high,
        "HIGH",
        "3.5 kHz",
        "high");

    setupBand (
        presence,
        "PRESENCE",
        "10 kHz",
        "presence");

    variationButton.setClickingTogglesState (true);

    variationButton.setColour (
        juce::TextButton::buttonColourId,
        juce::Colour (0xff1c2329));

    variationButton.setColour (
        juce::TextButton::buttonOnColourId,
        juce::Colour (0xffb88748));

    variationButton.setColour (
        juce::TextButton::textColourOffId,
        juce::Colour (0xff98a4ae));

    variationButton.setColour (
        juce::TextButton::textColourOnId,
        juce::Colour (0xff101418));

    addAndMakeVisible (variationButton);

    variationAttachment =
        std::make_unique<
            juce::AudioProcessorValueTreeState::ButtonAttachment> (
                processor.getAPVTS(),
                "variation",
                variationButton);

    output.setSliderStyle (
        juce::Slider::RotaryHorizontalVerticalDrag);

    output.setTextBoxStyle (
        juce::Slider::TextBoxBelow,
        false,
        78,
        24);

    addAndMakeVisible (output);

    outputLabel.setText (
        "OUTPUT",
        juce::dontSendNotification);

    outputLabel.setJustificationType (
        juce::Justification::centred);

    outputLabel.setColour (
        juce::Label::textColourId,
        juce::Colour (0xffd4c4a3));

    outputLabel.setFont (
        juce::Font (11.0f,
                    juce::Font::bold));

    addAndMakeVisible (outputLabel);

    outputAttachment =
        std::make_unique<
            juce::AudioProcessorValueTreeState::SliderAttachment> (
                processor.getAPVTS(),
                "output",
                output);

    setResizable (true, true);

    setResizeLimits (
        940, 580,
        1500, 920);

    setSize (1200, 740);

    startTimerHz (30);
}

SculptChannelAudioProcessorEditor::
~SculptChannelAudioProcessorEditor()
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
    band.knob.setSliderStyle (
        juce::Slider::RotaryHorizontalVerticalDrag);

    band.knob.setRotaryParameters (
        juce::MathConstants<float>::pi * 1.20f,
        juce::MathConstants<float>::pi * 2.80f,
        true);

    band.knob.setTextBoxStyle (
        juce::Slider::TextBoxBelow,
        false,
        88,
        25);

    addAndMakeVisible (band.knob);

    band.title.setText (
        title,
        juce::dontSendNotification);

    band.title.setJustificationType (
        juce::Justification::centred);

    band.title.setColour (
        juce::Label::textColourId,
        juce::Colour (0xfff0e7d2));

    band.title.setFont (
        juce::Font (
            15.0f,
            juce::Font::bold));

    addAndMakeVisible (band.title);

    band.freq.setText (
        freq,
        juce::dontSendNotification);

    band.freq.setJustificationType (
        juce::Justification::centred);

    band.freq.setColour (
        juce::Label::textColourId,
        juce::Colour (0xff8fa0ad));

    band.freq.setFont (
        juce::Font (10.0f));

    addAndMakeVisible (band.freq);

    band.attachment =
        std::make_unique<
            juce::AudioProcessorValueTreeState::SliderAttachment> (
                processor.getAPVTS(),
                parameterID,
                band.knob);
}

void SculptChannelAudioProcessorEditor::drawScale (
    juce::Graphics& g,
    juce::Rectangle<float> area)
{
    g.setColour (
        juce::Colour (0xff76828d));

    g.setFont (
        juce::Font (
            9.0f,
            juce::Font::bold));

    g.drawText (
        "RECESS",
        area.removeFromLeft (70.0f)
            .toNearestInt(),
        juce::Justification::centredLeft);

    g.drawText (
        "0",
        juce::Rectangle<int> (
            static_cast<int> (
                area.getCentreX()) - 8,
            static_cast<int> (area.getY()),
            16,
            static_cast<int> (
                area.getHeight())),
        juce::Justification::centred);

    g.drawText (
        "FORWARD",
        juce::Rectangle<int> (
            static_cast<int> (
                area.getRight() - 80.0f),
            static_cast<int> (area.getY()),
            80,
            static_cast<int> (
                area.getHeight())),
        juce::Justification::centredRight);
}

void SculptChannelAudioProcessorEditor::drawLevelMeter (
    juce::Graphics& g,
    juce::Rectangle<float> area,
    float value,
    juce::Colour colour,
    const juce::String& label)
{
    value =
        juce::jlimit (
            0.0f, 1.0f, value);

    g.setColour (
        juce::Colour (0xff11161b));

    g.fillRoundedRectangle (
        area, 3.0f);

    g.setColour (
        juce::Colour (0xff2f3941));

    g.drawRoundedRectangle (
        area, 3.0f, 1.0f);

    auto fill =
        area.reduced (3.0f);

    fill.setWidth (
        fill.getWidth() * value);

    juce::ColourGradient grad (
        colour.brighter (0.12f),
        fill.getX(),
        fill.getCentreY(),
        colour.darker (0.15f),
        fill.getRight(),
        fill.getCentreY(),
        false);

    g.setGradientFill (grad);

    g.fillRoundedRectangle (
        fill, 2.0f);

    g.setColour (
        juce::Colour (0xff8d99a3));

    g.setFont (
        juce::Font (
            9.0f,
            juce::Font::bold));

    g.drawText (
        label,
        juce::Rectangle<int> (
            static_cast<int> (area.getX()),
            static_cast<int> (area.getY()) - 13,
            static_cast<int> (area.getWidth()),
            12),
        juce::Justification::centredLeft);
}

void SculptChannelAudioProcessorEditor::drawMeterStack (
    juce::Graphics& g,
    juce::Rectangle<float> area,
    int bandIndex)
{
    const float gap = 9.0f;

    const float rowH =
        (area.getHeight()
         - gap * 2.0f)
        / 3.0f;

    drawLevelMeter (
        g,
        area.removeFromTop (rowH),
        processor.getResMeter (bandIndex),
        juce::Colour (0xffd7b06c),
        "RES");

    area.removeFromTop (gap);

    drawLevelMeter (
        g,
        area.removeFromTop (rowH),
        processor.getCompMeter (bandIndex),
        juce::Colour (0xff68bfd3),
        "COMP");

    area.removeFromTop (gap);

    drawLevelMeter (
        g,
        area.removeFromTop (rowH),
        processor.getSatMeter (bandIndex),
        juce::Colour (0xffe28b54),
        "SAT");
}

void SculptChannelAudioProcessorEditor::paint (
    juce::Graphics& g)
{
    const auto bounds =
        getLocalBounds().toFloat();

    juce::ColourGradient background (
        juce::Colour (0xff21262c),
        bounds.getCentreX(),
        bounds.getY(),
        juce::Colour (0xff0d1014),
        bounds.getCentreX(),
        bounds.getBottom(),
        false);

    g.setGradientFill (background);
    g.fillAll();

    juce::ColourGradient warmGlow (
        juce::Colour (0x18f1b76a),
        bounds.getX(),
        bounds.getY(),
        juce::Colours::transparentBlack,
        bounds.getCentreX(),
        bounds.getHeight() * 0.38f,
        false);

    g.setGradientFill (warmGlow);
    g.fillRect (
        bounds.withHeight (230.0f));

    juce::ColourGradient coolGlow (
        juce::Colour (0x1067c7db),
        bounds.getRight(),
        bounds.getY(),
        juce::Colours::transparentBlack,
        bounds.getRight() - 220.0f,
        bounds.getHeight() * 0.45f,
        false);

    g.setGradientFill (coolGlow);
    g.fillRect (bounds);

    auto panel =
        bounds.reduced (22.0f);

    g.setColour (
        juce::Colour (0xff171b20));

    g.fillRoundedRectangle (
        panel, 18.0f);

    g.setColour (
        juce::Colour (0x10ffffff));

    for (int i = 0; i < 70; ++i)
    {
        const float x =
            panel.getX()
            + static_cast<float> (
                (i * 73)
                % static_cast<int> (
                    panel.getWidth()));

        const float y =
            panel.getY()
            + static_cast<float> (
                (i * 41)
                % static_cast<int> (
                    panel.getHeight()));

        g.fillEllipse (
            x, y,
            1.5f
                + static_cast<float> (
                    i % 3),
            1.5f
                + static_cast<float> (
                    (i + 1) % 3));
    }

    g.setColour (
        juce::Colour (0xff4d575f));

    g.drawRoundedRectangle (
        panel, 18.0f, 1.3f);

    g.setColour (
        juce::Colour (0xfff3ead6));

    g.setFont (
        juce::Font (
            34.0f,
            juce::Font::bold));

    g.drawText (
        "SCULPT",
        48, 34,
        240, 42,
        juce::Justification::centredLeft);

    g.setColour (
        juce::Colour (0xffe0b06c));

    g.setFont (
        juce::Font (
            13.0f,
            juce::Font::bold));

    g.drawText (
        "CHANNEL",
        51, 75,
        120, 20,
        juce::Justification::centredLeft);

    g.setColour (
        juce::Colour (0xff7d8a94));

    g.setFont (
        juce::Font (10.5f));

    g.drawText (
        "DYNAMIC TONE CONDITIONER  •  RES → COMP → SAT",
        180, 75,
        360, 20,
        juce::Justification::centredLeft);

    const bool variationOn =
        variationButton.getToggleState();

    if (variationOn)
    {
        g.setColour (
            juce::Colour (0xffd5aa6d));

        g.setFont (
            juce::Font (
                9.5f,
                juce::Font::bold));

        g.drawText (
            "FOCUS  200 • 500 • 1k • 3k • 5k • 8k",
            555, 75,
            260, 20,
            juce::Justification::centredLeft);
    }

    drawLevelMeter (
        g,
        juce::Rectangle<float> (
            static_cast<float> (
                getWidth()) - 245.0f,
            43.0f,
            165.0f,
            11.0f),
        processor.getInputMeter(),
        juce::Colour (0xff67c7db),
        "IN");

    drawLevelMeter (
        g,
        juce::Rectangle<float> (
            static_cast<float> (
                getWidth()) - 245.0f,
            72.0f,
            165.0f,
            11.0f),
        processor.getOutputMeter(),
        juce::Colour (0xffe0b06c),
        "OUT");

    g.setColour (
        juce::Colour (0xff38414a));

    g.drawLine (
        48.0f, 108.0f,
        static_cast<float> (
            getWidth()) - 48.0f,
        108.0f,
        1.0f);

    auto content =
        getLocalBounds()
            .reduced (42);

    content.removeFromTop (112);

    const int outputWidth = 130;

    auto outputArea =
        content.removeFromRight (
            outputWidth);

    content.removeFromRight (14);

    const int gap = 12;

    const int stripW =
        (content.getWidth()
         - gap * 3) / 4;

    for (int i = 0; i < 4; ++i)
    {
        auto strip =
            juce::Rectangle<float> (
                static_cast<float> (
                    content.getX()
                    + i * (stripW + gap)),
                static_cast<float> (
                    content.getY()),
                static_cast<float> (
                    stripW),
                static_cast<float> (
                    content.getHeight()));

        juce::ColourGradient stripGrad (
            juce::Colour (0xff22282e),
            strip.getCentreX(),
            strip.getY(),
            juce::Colour (0xff11161b),
            strip.getCentreX(),
            strip.getBottom(),
            false);

        g.setGradientFill (stripGrad);

        g.fillRoundedRectangle (
            strip, 12.0f);

        g.setColour (
            juce::Colour (0xff3f4952));

        g.drawRoundedRectangle (
            strip, 12.0f, 1.0f);

        g.setColour (
            juce::Colour (0xff8c744a));

        g.fillEllipse (
            strip.getX() + 8.0f,
            strip.getY() + 8.0f,
            5.0f, 5.0f);

        g.fillEllipse (
            strip.getRight() - 13.0f,
            strip.getY() + 8.0f,
            5.0f, 5.0f);

        g.fillEllipse (
            strip.getX() + 8.0f,
            strip.getBottom() - 13.0f,
            5.0f, 5.0f);

        g.fillEllipse (
            strip.getRight() - 13.0f,
            strip.getBottom() - 13.0f,
            5.0f, 5.0f);

        drawScale (
            g,
            strip.reduced (14.0f)
                .withTrimmedTop (70.0f)
                .withHeight (16.0f));

        auto meterArea =
            strip.reduced (16.0f);

        meterArea.removeFromTop (
            300.0f);

        meterArea.removeFromBottom (
            28.0f);

        drawMeterStack (
            g,
            meterArea,
            i);
    }

    auto outF =
        outputArea.toFloat();

    g.setColour (
        juce::Colour (0xff1a1f24));

    g.fillRoundedRectangle (
        outF, 12.0f);

    g.setColour (
        juce::Colour (0xff404a53));

    g.drawRoundedRectangle (
        outF, 12.0f, 1.0f);

    g.setColour (
        juce::Colour (0xff6b7882));

    g.setFont (
        juce::Font (
            9.0f,
            juce::Font::bold));

    g.drawText (
        "MASTER",
        outputArea.getX(),
        outputArea.getBottom() - 42,
        outputArea.getWidth(),
        14,
        juce::Justification::centred);

    g.setColour (
        juce::Colour (0xff54606a));

    g.setFont (
        juce::Font (
            9.0f,
            juce::Font::bold));

    g.drawText (
        "v0.3  •  BROAD / VARIATION",
        48,
        getHeight() - 24,
        getWidth() - 96,
        14,
        juce::Justification::centred);
}

void SculptChannelAudioProcessorEditor::resized()
{
    variationButton.setBounds (
        548, 41, 120, 27);

    auto content =
        getLocalBounds()
            .reduced (42);

    content.removeFromTop (112);

    const int outputWidth = 130;

    auto outputArea =
        content.removeFromRight (
            outputWidth);

    content.removeFromRight (14);

    const int gap = 12;

    const int stripW =
        (content.getWidth()
         - gap * 3) / 4;

    for (int i = 0; i < 4; ++i)
    {
        auto* band =
            bands[static_cast<size_t> (i)];

        auto strip =
            content.removeFromLeft (
                stripW)
            .reduced (10, 10);

        band->title.setBounds (
            strip.removeFromTop (26));

        band->freq.setBounds (
            strip.removeFromTop (17));

        strip.removeFromTop (10);

        auto knobArea =
            strip.removeFromTop (238);

        band->knob.setBounds (
            knobArea.reduced (6));

        content.removeFromLeft (gap);
    }

    auto out =
        outputArea.reduced (
            10, 16);

    outputLabel.setBounds (
        out.removeFromTop (24));

    out.removeFromTop (18);

    output.setBounds (
        out.removeFromTop (180)
            .reduced (8));
}

void SculptChannelAudioProcessorEditor::timerCallback()
{
    repaint();
}
