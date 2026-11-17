#include "PluginEditor.h"

namespace
{
    const juce::Colour amberColour (0xffe0a030);
    const juce::Colour greenColour (0xff4ccf5a);
    const juce::Colour creamText   (0xfff2e8d0);
    constexpr int woodWidth = 14;
    constexpr float meterRangeDb = 24.0f;

    void drawWoodStrip (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffa0682e), r.getX(), r.getY(),
                                                 juce::Colour (0xff4a2810), r.getRight(), r.getY(), false));
        g.fillRect (r);
    }

    void styleKnob (juce::Slider& s)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
        s.setColour (juce::Slider::rotarySliderFillColourId, amberColour);
        s.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff1e1f21));
        s.setColour (juce::Slider::thumbColourId, creamText);
        s.setColour (juce::Slider::textBoxTextColourId, creamText);
        s.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff202224));
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff5a5f66));
    }

    void styleLabel (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (juce::Font (juce::FontOptions (11.0f)));
        l.setColour (juce::Label::textColourId, creamText);
    }
}

RTO119Editor::RTO119Editor (RTO119Processor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setSize (620, 300);

    styleLabel (amountLabel,    "COMPRESS / EXPAND");
    styleLabel (thresholdLabel, "THRESHOLD");
    styleLabel (outputLabel,    "OUTPUT");
    styleLabel (modeLabel,      "MODE");

    styleKnob (amountSlider);
    styleKnob (thresholdSlider);
    styleKnob (outputSlider);

    modeBox.addItem ("Linear", 1);
    modeBox.addItem ("Above Threshold", 2);
    modeBox.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff202224));
    modeBox.setColour (juce::ComboBox::textColourId, creamText);
    modeBox.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff5a5f66));

    linkButton.setButtonText ("Stereo Link");
    bypassButton.setButtonText ("Bypass");
    ironButton.setButtonText ("Iron");
    linkButton.setColour (juce::ToggleButton::textColourId, creamText);
    bypassButton.setColour (juce::ToggleButton::textColourId, creamText);
    ironButton.setColour (juce::ToggleButton::textColourId, creamText);
    ironButton.setTooltip ("Optional output-transformer color. Not part of the original unit. Off by default.");

    ironNoteLabel.setText ("transformer color, not on original", juce::dontSendNotification);
    ironNoteLabel.setJustificationType (juce::Justification::centred);
    ironNoteLabel.setFont (juce::Font (juce::FontOptions (10.0f)));
    ironNoteLabel.setColour (juce::Label::textColourId, creamText.withAlpha (0.6f));
    ironNoteLabel.setMinimumHorizontalScale (0.6f);   // keeps the text inside the 120 px column

    for (auto* c : { static_cast<juce::Component*> (&amountLabel), static_cast<juce::Component*> (&thresholdLabel),
                     static_cast<juce::Component*> (&outputLabel), static_cast<juce::Component*> (&modeLabel),
                     static_cast<juce::Component*> (&amountSlider), static_cast<juce::Component*> (&thresholdSlider),
                     static_cast<juce::Component*> (&outputSlider), static_cast<juce::Component*> (&modeBox),
                     static_cast<juce::Component*> (&linkButton), static_cast<juce::Component*> (&bypassButton),
                     static_cast<juce::Component*> (&ironButton), static_cast<juce::Component*> (&ironNoteLabel) })
        addAndMakeVisible (c);

    // Attachments are created after the components they bind to.
    amountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, ParamIDs::amount, amountSlider);
    thresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, ParamIDs::threshold, thresholdSlider);
    outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, ParamIDs::output, outputSlider);
    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, ParamIDs::mode, modeBox);
    linkAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, ParamIDs::link, linkButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, ParamIDs::bypass, bypassButton);
    ironAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, ParamIDs::iron, ironButton);

    startTimerHz (30);
}

RTO119Editor::~RTO119Editor()
{
    stopTimer();
}

void RTO119Editor::resized()
{
    amountLabel.setBounds (40, 52, 170, 16);
    amountSlider.setBounds (40, 72, 170, 150);

    thresholdLabel.setBounds (222, 52, 100, 16);
    ledBounds = juce::Rectangle<int> (265, 72, 14, 14);   // centred over the threshold knob
    thresholdSlider.setBounds (222, 92, 100, 130);

    outputLabel.setBounds (330, 52, 100, 16);
    outputSlider.setBounds (330, 92, 100, 130);

    modeLabel.setBounds (452, 52, 120, 16);
    modeBox.setBounds (452, 92, 120, 24);
    linkButton.setBounds (452, 132, 120, 24);
    bypassButton.setBounds (452, 164, 120, 24);
    ironButton.setBounds (452, 196, 120, 24);
    ironNoteLabel.setBounds (452, 220, 120, 14);

    meterBounds = juce::Rectangle<int> (60, 238, 500, 46);
}

void RTO119Editor::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    // Brushed-metal front panel.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff7d8389), 0.0f, 0.0f,
                                             juce::Colour (0xff2b2e33), 0.0f, (float) bounds.getHeight(), false));
    g.fillRect (bounds);
    g.setColour (juce::Colours::black.withAlpha (0.06f));
    for (int y = 0; y < bounds.getHeight(); y += 3)
        g.drawHorizontalLine (y, 0.0f, (float) bounds.getWidth());

    // Wood side strips.
    drawWoodStrip (g, bounds.removeFromLeft (woodWidth).toFloat());
    drawWoodStrip (g, bounds.removeFromRight (woodWidth).toFloat());

    // Title and subtitle.
    g.setColour (creamText);
    g.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
    g.drawText ("RTO 119 COMPANDER", juce::Rectangle<int> (28, 10, 400, 26),
                juce::Justification::centredLeft, false);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.setColour (creamText.withAlpha (0.75f));
    g.drawText ("dB-linear compressor / expander", juce::Rectangle<int> (28, 36, 400, 16),
                juce::Justification::centredLeft, false);

    // Threshold LED: bright red when above threshold, dark red otherwise.
    const bool lit = processor.meterAboveThreshold.load();
    const auto led = ledBounds.toFloat();
    if (lit)
    {
        g.setColour (juce::Colour (0xffff3b2f).withAlpha (0.35f));
        g.fillEllipse (led.expanded (4.0f));
    }
    g.setColour (lit ? juce::Colour (0xffff3b2f) : juce::Colour (0xff4a1412));
    g.fillEllipse (led);
    g.setColour (juce::Colours::black);
    g.drawEllipse (led, 1.0f);

    // Centre-zero gain meter, -24 dB .. +24 dB.
    const auto bar = meterBounds.withHeight (18).toFloat();
    g.setColour (juce::Colour (0xff111213));
    g.fillRect (bar);
    g.setColour (juce::Colour (0xff6a6f76));
    g.drawRect (bar, 1.0f);

    const float rawGain = processor.meterGainDb.load();
    const auto inner = bar.reduced (2.0f);
    const float centreX = inner.getCentreX();
    const float halfWidth = inner.getWidth() * 0.5f;
    const float fraction = juce::jlimit (-1.0f, 1.0f, rawGain / meterRangeDb);
    const float length = std::abs (fraction) * halfWidth;

    if (fraction < 0.0f)
    {
        g.setColour (amberColour);
        g.fillRect (juce::Rectangle<float> (centreX - length, inner.getY(), length, inner.getHeight()));
    }
    else if (fraction > 0.0f)
    {
        g.setColour (greenColour);
        g.fillRect (juce::Rectangle<float> (centreX, inner.getY(), length, inner.getHeight()));
    }

    g.setColour (creamText);
    g.fillRect (juce::Rectangle<float> (centreX - 0.5f, bar.getY(), 1.0f, bar.getHeight()));

    // "GAIN" caption and value readout beneath the bar.
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText ("GAIN", juce::Rectangle<int> (60, 260, 100, 18), juce::Justification::centredLeft, false);
    g.drawText (juce::String (rawGain, 1) + " dB", juce::Rectangle<int> (460, 260, 100, 18),
                juce::Justification::centredRight, false);
}

void RTO119Editor::timerCallback()
{
    repaint (ledBounds.expanded (5));
    repaint (meterBounds);
}
