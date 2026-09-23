#include "PluginEditor.h"
#include <cmath>

SculptVintageLookAndFeel::SculptVintageLookAndFeel()
{
    setColour (
        juce::Slider::textBoxTextColourId,
        juce::Colour (0xffeadfc6));

    setColour (
        juce::Slider::textBoxBackgroundColourId,
        juce::Colour (0xff111315));

    setColour (
        juce::Slider::textBoxOutlineColourId,
        juce::Colour (0xff5d574c));
}

void SculptVintageLookAndFeel::drawRotarySlider (
    juce::Graphics& g,
    int x, int y, int width, int height,
    float sliderPosProportional,
    float rotaryStartAngle,
    float rotaryEndAngle,
    juce::Slider&)
{
    auto area =
        juce::Rectangle<float> (
            static_cast<float> (x),
            static_cast<float> (y),
            static_cast<float> (width),
            static_cast<float> (height))
        .reduced (13.0f);

    const float diameter =
        juce::jmin (
            area.getWidth(),
            area.getHeight());

    auto dial =
        juce::Rectangle<float> (
            0.0f, 0.0f,
            diameter, diameter)
        .withCentre (
            area.getCentre());

    const auto centre =
        dial.getCentre();

    const float angle =
        rotaryStartAngle
        + sliderPosProportional
          * (rotaryEndAngle
             - rotaryStartAngle);

    g.setColour (
        juce::Colour (0x85000000));

    g.fillEllipse (
        dial.translated (
            0.0f, 7.0f)
        .expanded (5.0f));

    juce::ColourGradient brass (
        juce::Colour (0xff9a825d),
        dial.getX(),
        dial.getY(),
        juce::Colour (0xff493e30),
        dial.getRight(),
        dial.getBottom(),
        false);

    g.setGradientFill (
        brass);

    g.fillEllipse (
        dial);

    g.setColour (
        juce::Colour (0xffc1a979));

    g.drawEllipse (
        dial.reduced (1.0f),
        1.3f);

    auto knob =
        dial.reduced (10.0f);

    juce::ColourGradient bakelite (
        juce::Colour (0xff30302d),
        knob.getCentreX(),
        knob.getY(),
        juce::Colour (0xff090a0a),
        knob.getCentreX(),
        knob.getBottom(),
        false);

    g.setGradientFill (
        bakelite);

    g.fillEllipse (
        knob);

    g.setColour (
        juce::Colour (0x443f403d));

    for (int i = 0; i < 28; ++i)
    {
        const float a =
            juce::MathConstants<float>::twoPi
            * static_cast<float> (i)
            / 28.0f;

        const float r1 =
            knob.getWidth() * 0.42f;

        const float r2 =
            knob.getWidth() * 0.49f;

        g.drawLine (
            centre.x
                + std::sin (a) * r1,
            centre.y
                - std::cos (a) * r1,
            centre.x
                + std::sin (a) * r2,
            centre.y
                - std::cos (a) * r2,
            1.0f);
    }

    g.setColour (
        juce::Colour (0xff070808));

    g.drawEllipse (
        knob, 1.5f);

    const float pointerLength =
        knob.getWidth()
        * 0.5f
        * 0.72f;

    const auto end =
        centre
        + juce::Point<float> (
            std::sin (angle),
            -std::cos (angle))
          * pointerLength;

    g.setColour (
        juce::Colour (0xffeadfc6));

    g.drawLine (
        centre.x,
        centre.y,
        end.x,
        end.y,
        3.0f);

    g.setColour (
        juce::Colour (0xff67c7d7));

    g.fillEllipse (
        juce::Rectangle<float> (
            5.5f, 5.5f)
        .withCentre (
            centre));
}

void SculptVintageLookAndFeel::drawToggleButton (
    juce::Graphics& g,
    juce::ToggleButton& button,
    bool,
    bool)
{
    auto bounds =
        button.getLocalBounds()
        .toFloat()
        .reduced (2.0f);

    const bool on =
        button.getToggleState();

    auto plate =
        bounds.removeFromLeft (
            juce::jmin (
                56.0f,
                bounds.getWidth()
                * 0.42f));

    g.setColour (
        juce::Colour (0xff0b0d0e));

    g.fillRoundedRectangle (
        plate, 5.0f);

    g.setColour (
        juce::Colour (0xff6f6555));

    g.drawRoundedRectangle (
        plate, 5.0f, 1.0f);

    const auto pivot =
        plate.getCentre();

    g.setColour (
        juce::Colour (0xff282724));

    g.fillRoundedRectangle (
        juce::Rectangle<float> (
            7.0f, 26.0f)
        .withCentre (
            pivot),
        3.0f);

    const float leverY =
        on
            ? pivot.y - 7.0f
            : pivot.y + 7.0f;

    g.setColour (
        juce::Colour (0xffa69b87));

    g.drawLine (
        pivot.x,
        pivot.y,
        pivot.x,
        leverY,
        4.0f);

    g.setColour (
        juce::Colour (0xffd2c3a4));

    g.fillEllipse (
        juce::Rectangle<float> (
            9.0f, 9.0f)
        .withCentre (
            { pivot.x, leverY }));

    auto lamp =
        juce::Rectangle<float> (
            8.0f, 8.0f)
        .withCentre (
            { plate.getRight() - 10.0f,
              plate.getY() + 10.0f });

    g.setColour (
        on
            ? juce::Colour (0xffe1a756)
            : juce::Colour (0xff322a20));

    g.fillEllipse (
        lamp);

    if (on)
    {
        g.setColour (
            juce::Colour (0x55e7b266));

        g.fillEllipse (
            lamp.expanded (4.0f));
    }

    g.setColour (
        juce::Colour (0xffd9ceb7));

    g.setFont (
        juce::Font (
            10.0f,
            juce::Font::bold));

    g.drawText (
        button.getButtonText(),
        bounds.toNearestInt(),
        juce::Justification::centredLeft);
}

SculptChannelAudioProcessorEditor::
SculptChannelAudioProcessorEditor (
    SculptChannelAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p)
{
    setLookAndFeel (
        &look);

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

    variationButton.setClickingTogglesState (
        true);

    levelMatchButton.setClickingTogglesState (
        true);

    addAndMakeVisible (
        variationButton);

    addAndMakeVisible (
        levelMatchButton);

    variationAttachment =
        std::make_unique<
            juce::AudioProcessorValueTreeState::ButtonAttachment> (
                processor.getAPVTS(),
                "variation",
                variationButton);

    levelMatchAttachment =
        std::make_unique<
            juce::AudioProcessorValueTreeState::ButtonAttachment> (
                processor.getAPVTS(),
                "levelmatch",
                levelMatchButton);

    output.setSliderStyle (
        juce::Slider::RotaryHorizontalVerticalDrag);

    output.setRotaryParameters (
        juce::MathConstants<float>::pi
            * 1.20f,
        juce::MathConstants<float>::pi
            * 2.80f,
        true);

    output.setTextBoxStyle (
        juce::Slider::TextBoxBelow,
        false,
        78,
        24);

    addAndMakeVisible (
        output);

    outputLabel.setText (
        "OUTPUT",
        juce::dontSendNotification);

    outputLabel.setJustificationType (
        juce::Justification::centred);

    outputLabel.setColour (
        juce::Label::textColourId,
        juce::Colour (0xffdfd2b8));

    outputLabel.setFont (
        juce::Font (
            11.0f,
            juce::Font::bold));

    addAndMakeVisible (
        outputLabel);

    outputAttachment =
        std::make_unique<
            juce::AudioProcessorValueTreeState::SliderAttachment> (
                processor.getAPVTS(),
                "output",
                output);

    setResizable (
        true, true);

    setResizeLimits (
        1040, 590,
        1600, 920);

    setSize (
        1260, 700);

    startTimerHz (
        30);
}

SculptChannelAudioProcessorEditor::
~SculptChannelAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (
        nullptr);
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
        24);

    addAndMakeVisible (
        band.knob);

    band.title.setText (
        title,
        juce::dontSendNotification);

    band.title.setJustificationType (
        juce::Justification::centred);

    band.title.setColour (
        juce::Label::textColourId,
        juce::Colour (0xffeadfc6));

    band.title.setFont (
        juce::Font (
            15.0f,
            juce::Font::bold));

    addAndMakeVisible (
        band.title);

    band.freq.setText (
        freq,
        juce::dontSendNotification);

    band.freq.setJustificationType (
        juce::Justification::centred);

    band.freq.setColour (
        juce::Label::textColourId,
        juce::Colour (0xff9b927f));

    band.freq.setFont (
        juce::Font (
            10.0f));

    addAndMakeVisible (
        band.freq);

    band.attachment =
        std::make_unique<
            juce::AudioProcessorValueTreeState::SliderAttachment> (
                processor.getAPVTS(),
                parameterID,
                band.knob);
}

void SculptChannelAudioProcessorEditor::drawFaceplateTexture (
    juce::Graphics& g,
    juce::Rectangle<float> area)
{
    juce::ColourGradient plate (
        juce::Colour (0xff282927),
        area.getCentreX(),
        area.getY(),
        juce::Colour (0xff101211),
        area.getCentreX(),
        area.getBottom(),
        false);

    g.setGradientFill (
        plate);

    g.fillRoundedRectangle (
        area, 12.0f);

    for (int y = static_cast<int> (area.getY());
         y < static_cast<int> (area.getBottom());
         y += 4)
    {
        const int seed =
            (y * 37) % 19;

        g.setColour (
            seed < 8
                ? juce::Colour (0x0cffffff)
                : juce::Colour (0x09000000));

        g.drawLine (
            area.getX() + 4.0f,
            static_cast<float> (y),
            area.getRight() - 4.0f,
            static_cast<float> (y),
            1.0f);
    }

    g.setColour (
        juce::Colour (0xff655d50));

    g.drawRoundedRectangle (
        area, 12.0f, 1.4f);
}

void SculptChannelAudioProcessorEditor::drawWoodCheek (
    juce::Graphics& g,
    juce::Rectangle<float> area)
{
    juce::ColourGradient wood (
        juce::Colour (0xff5b3c29),
        area.getX(),
        area.getCentreY(),
        juce::Colour (0xff2b1c14),
        area.getRight(),
        area.getCentreY(),
        false);

    g.setGradientFill (
        wood);

    g.fillRoundedRectangle (
        area, 11.0f);

    g.setColour (
        juce::Colour (0x55301b10));

    for (int i = 0; i < 13; ++i)
    {
        const float x =
            area.getX()
            + 6.0f
            + i
              * area.getWidth()
              / 13.0f;

        g.drawLine (
            x,
            area.getY() + 8.0f,
            x + 3.0f,
            area.getBottom() - 8.0f,
            1.0f);
    }

    g.setColour (
        juce::Colour (0xff8a6546));

    g.drawRoundedRectangle (
        area, 11.0f, 1.0f);
}

void SculptChannelAudioProcessorEditor::drawModuleFrame (
    juce::Graphics& g,
    juce::Rectangle<float> area)
{
    g.setColour (
        juce::Colour (0xff0c0e0e));

    g.fillRoundedRectangle (
        area, 7.0f);

    g.setColour (
        juce::Colour (0xff3e3d38));

    g.drawRoundedRectangle (
        area, 7.0f, 1.0f);

    auto inner =
        area.reduced (5.0f);

    g.setColour (
        juce::Colour (0xff1c1e1d));

    g.fillRoundedRectangle (
        inner, 5.0f);

    for (auto point : {
            juce::Point<float> { inner.getX() + 7.0f, inner.getY() + 7.0f },
            juce::Point<float> { inner.getRight() - 7.0f, inner.getY() + 7.0f },
            juce::Point<float> { inner.getX() + 7.0f, inner.getBottom() - 7.0f },
            juce::Point<float> { inner.getRight() - 7.0f, inner.getBottom() - 7.0f } })
    {
        g.setColour (
            juce::Colour (0xff83745e));

        g.fillEllipse (
            juce::Rectangle<float> (
                6.0f, 6.0f)
            .withCentre (
                point));

        g.setColour (
            juce::Colour (0xff382f25));

        g.drawLine (
            point.x - 2.0f,
            point.y,
            point.x + 2.0f,
            point.y,
            1.0f);
    }
}

void SculptChannelAudioProcessorEditor::drawKnobScale (
    juce::Graphics& g,
    juce::Rectangle<float> area)
{
    const auto centre =
        area.getCentre();

    const float radius =
        juce::jmin (
            area.getWidth(),
            area.getHeight())
        * 0.43f;

    const float start =
        juce::MathConstants<float>::pi
        * 1.20f;

    const float end =
        juce::MathConstants<float>::pi
        * 2.80f;

    for (int i = 0; i <= 20; ++i)
    {
        const float t =
            static_cast<float> (i)
            / 20.0f;

        const float angle =
            start
            + (end - start)
              * t;

        const bool major =
            (i % 5) == 0;

        const float r1 =
            radius
            + (major ? 5.0f : 7.0f);

        const float r2 =
            radius
            + (major ? 13.0f : 11.0f);

        g.setColour (
            major
                ? juce::Colour (0xffb6a789)
                : juce::Colour (0xff675f52));

        g.drawLine (
            centre.x
                + std::sin (angle) * r1,
            centre.y
                - std::cos (angle) * r1,
            centre.x
                + std::sin (angle) * r2,
            centre.y
                - std::cos (angle) * r2,
            major ? 1.5f : 1.0f);
    }

    g.setFont (
        juce::Font (
            8.5f,
            juce::Font::bold));

    g.setColour (
        juce::Colour (0xff968c78));

    g.drawText (
        "CUT",
        static_cast<int> (
            area.getX()),
        static_cast<int> (
            area.getBottom() - 20.0f),
        42, 15,
        juce::Justification::centredLeft);

    g.drawText (
        "0",
        static_cast<int> (
            centre.x - 8.0f),
        static_cast<int> (
            area.getY() + 1.0f),
        16, 15,
        juce::Justification::centred);

    g.drawText (
        "DRIVE",
        static_cast<int> (
            area.getRight() - 46.0f),
        static_cast<int> (
            area.getBottom() - 20.0f),
        46, 15,
        juce::Justification::centredRight);
}

void SculptChannelAudioProcessorEditor::drawBarMeter (
    juce::Graphics& g,
    juce::Rectangle<float> area,
    float value,
    juce::Colour colour,
    const juce::String& label)
{
    value =
        juce::jlimit (
            0.0f, 1.0f,
            value);

    g.setColour (
        juce::Colour (0xff090b0b));

    g.fillRoundedRectangle (
        area, 3.0f);

    g.setColour (
        juce::Colour (0xff534e44));

    g.drawRoundedRectangle (
        area, 3.0f, 1.0f);

    auto inner =
        area.reduced (3.0f);

    const int segments = 12;
    const float gap = 2.0f;

    const float segmentWidth =
        (inner.getWidth()
         - gap * (segments - 1))
        / static_cast<float> (
            segments);

    const int lit =
        static_cast<int> (
            std::round (
                value * segments));

    for (int i = 0; i < segments; ++i)
    {
        auto seg =
            juce::Rectangle<float> (
                inner.getX()
                    + i
                      * (segmentWidth + gap),
                inner.getY(),
                segmentWidth,
                inner.getHeight());

        g.setColour (
            i < lit
                ? colour
                : juce::Colour (
                    0xff22231f));

        g.fillRoundedRectangle (
            seg, 1.5f);
    }

    g.setColour (
        juce::Colour (0xffa59b87));

    g.setFont (
        juce::Font (
            8.5f,
            juce::Font::bold));

    g.drawText (
        label,
        static_cast<int> (
            area.getX()),
        static_cast<int> (
            area.getY() - 13.0f),
        static_cast<int> (
            area.getWidth()),
        12,
        juce::Justification::centredLeft);
}

void SculptChannelAudioProcessorEditor::drawMeterStack (
    juce::Graphics& g,
    juce::Rectangle<float> area,
    int bandIndex)
{
    const float gap = 11.0f;

    const float h =
        (area.getHeight()
         - gap * 2.0f)
        / 3.0f;

    drawBarMeter (
        g,
        area.removeFromTop (h),
        processor.getResMeter (bandIndex),
        juce::Colour (0xffd8a55c),
        "RES");

    area.removeFromTop (gap);

    drawBarMeter (
        g,
        area.removeFromTop (h),
        processor.getCompMeter (bandIndex),
        juce::Colour (0xff67bfd2),
        "COMP");

    area.removeFromTop (gap);

    drawBarMeter (
        g,
        area.removeFromTop (h),
        processor.getSatMeter (bandIndex),
        juce::Colour (0xffd87849),
        "SAT");
}

void SculptChannelAudioProcessorEditor::paint (
    juce::Graphics& g)
{
    g.fillAll (
        juce::Colour (0xff090a0a));

    auto full =
        getLocalBounds()
        .toFloat();

    auto leftCheek =
        full.removeFromLeft (
            24.0f)
        .reduced (
            2.0f, 10.0f);

    auto rightCheek =
        full.removeFromRight (
            24.0f)
        .reduced (
            2.0f, 10.0f);

    drawWoodCheek (
        g, leftCheek);

    drawWoodCheek (
        g, rightCheek);

    auto faceplate =
        getLocalBounds()
        .toFloat()
        .withTrimmedLeft (27.0f)
        .withTrimmedRight (27.0f)
        .reduced (6.0f);

    drawFaceplateTexture (
        g, faceplate);

    auto titlePlate =
        juce::Rectangle<float> (
            faceplate.getX() + 22.0f,
            faceplate.getY() + 17.0f,
            390.0f,
            64.0f);

    g.setColour (
        juce::Colour (0xff111312));

    g.fillRoundedRectangle (
        titlePlate, 4.0f);

    g.setColour (
        juce::Colour (0xff756a58));

    g.drawRoundedRectangle (
        titlePlate, 4.0f, 1.0f);

    g.setColour (
        juce::Colour (0xffeadfc6));

    g.setFont (
        juce::Font (
            31.0f,
            juce::Font::bold));

    g.drawText (
        "SCULPT",
        titlePlate.toNearestInt()
            .withTrimmedLeft (16)
            .withTrimmedBottom (18),
        juce::Justification::centredLeft);

    g.setColour (
        juce::Colour (0xffd5a25b));

    g.setFont (
        juce::Font (
            10.0f,
            juce::Font::bold));

    g.drawText (
        "CHANNEL  ·  EQ → COMP → SAT",
        static_cast<int> (
            titlePlate.getX() + 18.0f),
        static_cast<int> (
            titlePlate.getBottom() - 22.0f),
        static_cast<int> (
            titlePlate.getWidth() - 30.0f),
        15,
        juce::Justification::centredLeft);

    const float topMeterX =
        faceplate.getRight()
        - 250.0f;

    drawBarMeter (
        g,
        { topMeterX,
          faceplate.getY() + 27.0f,
          178.0f,
          11.0f },
        processor.getInputMeter(),
        juce::Colour (0xff67bfd2),
        "INPUT");

    drawBarMeter (
        g,
        { topMeterX,
          faceplate.getY() + 59.0f,
          178.0f,
          11.0f },
        processor.getOutputMeter(),
        juce::Colour (0xffd5a25b),
        "OUTPUT");

    auto content =
        faceplate.reduced (24.0f);

    content.removeFromTop (
        102.0f);

    content.removeFromBottom (
        52.0f);

    auto masterArea =
        content.removeFromRight (
            145.0f);

    content.removeFromRight (
        12.0f);

    const int gap = 12;

    const int moduleW =
        (content.getWidth()
         - gap * 3)
        / 4;

    auto modules =
        content;

    for (int i = 0; i < 4; ++i)
    {
        auto module =
            modules.removeFromLeft (
                moduleW)
            .toFloat();

        drawModuleFrame (
            g, module);

        auto knobZone =
            module.reduced (
                16.0f)
            .withTrimmedTop (
                44.0f)
            .withHeight (
                255.0f);

        drawKnobScale (
            g, knobZone);

        auto meters =
            module.reduced (
                18.0f)
            .withTrimmedTop (
                315.0f)
            .withTrimmedBottom (
                18.0f);

        drawMeterStack (
            g,
            meters,
            i);

        modules.removeFromLeft (
            gap);
    }

    drawModuleFrame (
        g,
        masterArea.toFloat());

    g.setColour (
        juce::Colour (0xff9b907c));

    g.setFont (
        juce::Font (
            8.5f,
            juce::Font::bold));

    g.drawText (
        "MASTER",
        masterArea.getX(),
        masterArea.getBottom() - 28,
        masterArea.getWidth(),
        14,
        juce::Justification::centred);

    g.setColour (
        juce::Colour (0xff716959));

    g.setFont (
        juce::Font (
            8.0f,
            juce::Font::bold));

    g.drawText (
        "32-BAND RESONANCE GUARDRAIL  ·  HIDDEN / DYNAMIC  ·  v0.5",
        static_cast<int> (
            faceplate.getX() + 28.0f),
        static_cast<int> (
            faceplate.getBottom() - 24.0f),
        static_cast<int> (
            faceplate.getWidth() - 56.0f),
        12,
        juce::Justification::centredRight);
}

void SculptChannelAudioProcessorEditor::resized()
{
    auto faceplate =
        getLocalBounds()
        .withTrimmedLeft (33)
        .withTrimmedRight (33)
        .reduced (24);

    auto switches =
        faceplate.removeFromTop (
            86);

    switches.removeFromLeft (
        420);

    variationButton.setBounds (
        switches.removeFromLeft (
            160)
        .withHeight (36)
        .withY (
            switches.getY() + 18));

    switches.removeFromLeft (
        12);

    levelMatchButton.setBounds (
        switches.removeFromLeft (
            180)
        .withHeight (36)
        .withY (
            switches.getY() + 18));

    auto content =
        faceplate;

    content.removeFromTop (
        16);

    content.removeFromBottom (
        52);

    auto masterArea =
        content.removeFromRight (
            145);

    content.removeFromRight (
        12);

    const int gap = 12;

    const int moduleW =
        (content.getWidth()
         - gap * 3)
        / 4;

    for (int i = 0; i < 4; ++i)
    {
        auto module =
            content.removeFromLeft (
                moduleW)
            .reduced (
                14, 12);

        auto* band =
            bands[static_cast<size_t> (i)];

        band->title.setBounds (
            module.removeFromTop (
                26));

        band->freq.setBounds (
            module.removeFromTop (
                17));

        module.removeFromTop (
            8);

        band->knob.setBounds (
            module.removeFromTop (
                260));

        content.removeFromLeft (
            gap);
    }

    auto master =
        masterArea.reduced (
            12, 18);

    outputLabel.setBounds (
        master.removeFromTop (
            24));

    master.removeFromTop (
        22);

    output.setBounds (
        master.removeFromTop (
            190)
        .reduced (6));
}

void SculptChannelAudioProcessorEditor::timerCallback()
{
    repaint();
}
