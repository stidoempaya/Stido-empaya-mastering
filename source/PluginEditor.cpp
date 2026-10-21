#include "PluginEditor.h"

namespace
{
    constexpr int kCols = 5;
    constexpr int kRows = 3;
    const juce::Colour bg      { 0xff14161a };
    const juce::Colour panel   { 0xff1d2026 };
    const juce::Colour accent  { 0xff4fc3f7 };
    const juce::Colour textCol { 0xffd7dbe0 };

    // Les textes francais sont en UTF-8 (accents)
    juce::String fr (const char* text) { return juce::String::fromUTF8 (text); }
}

MasteringEditor::MasteringEditor (MasteringProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    // Rangée 1 : entrée + égaliseur
    addKnob ("inGain",   fr ("Entrée"));
    addKnob ("lowGain",  fr ("Graves 100 Hz"));
    addKnob ("midGain",  fr ("Médiums"));
    addKnob ("midFreq",  fr ("Fréq. médiums"));
    addKnob ("highGain", fr ("Aigus 8 kHz"));

    // Rangée 2 : compresseur
    addKnob ("compThresh",  fr ("Seuil"));
    addKnob ("compRatio",   fr ("Ratio"));
    addKnob ("compAttack",  fr ("Attaque"));
    addKnob ("compRelease", fr ("Relâchement"));
    addKnob ("compMakeup",  fr ("Compensation"));

    // Rangée 3 : stéréo, limiteur, sortie
    addKnob ("width",      fr ("Largeur"));
    addKnob ("drive",      fr ("Poussée"));
    addKnob ("ceiling",    fr ("Plafond"));
    addKnob ("limRelease", fr ("Relâch. limiteur"));
    addKnob ("outGain",    fr ("Sortie"));

    addAndMakeVisible (bypass);
    bypass.setButtonText (fr ("Contourner"));
    bypass.setColour (juce::ToggleButton::textColourId, textCol);
    bypass.setColour (juce::ToggleButton::tickColourId, accent);
    bypassAttach = std::make_unique<ButtonAttachment> (proc.apvts, "bypass", bypass);

    setSize (760, 540);
    startTimerHz (30);
}

void MasteringEditor::addKnob (const juce::String& id, const juce::String& name)
{
    auto k = std::make_unique<Knob>();

    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    k->slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    k->slider.setColour (juce::Slider::thumbColourId, textCol);
    k->slider.setColour (juce::Slider::textBoxTextColourId, textCol);
    k->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

    k->label.setText (name, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->label.setColour (juce::Label::textColourId, textCol);

    addAndMakeVisible (k->slider);
    addAndMakeVisible (k->label);

    k->attach = std::make_unique<SliderAttachment> (proc.apvts, id, k->slider);
    knobs.push_back (std::move (k));
}

void MasteringEditor::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto header = area.removeFromTop (36);
    bypass.setBounds (header.removeFromRight (90));

    meterArea = area.removeFromRight (70).reduced (10, 24);
    gridArea  = area.withTrimmedTop (8);
    rowH = gridArea.getHeight() / kRows;
    const int colW = gridArea.getWidth() / kCols;

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto cell = juce::Rectangle<int> (gridArea.getX() + (int) (i % kCols) * colW,
                                          gridArea.getY() + (int) (i / kCols) * rowH,
                                          colW, rowH).reduced (6, 4);
        cell.removeFromTop (16);                              // room for section title
        knobs[i]->label.setBounds (cell.removeFromTop (18));
        knobs[i]->slider.setBounds (cell);
    }
}

void MasteringEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);

    g.setColour (textCol);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("STIDO EMPAYA MASTERING", 16, 14, 400, 30, juce::Justification::centredLeft);

    // Section panels + titles
    const char* titles[kRows] = { "ENTRÉE / ÉGALISEUR", "COMPRESSEUR", "STÉRÉO / LIMITEUR / SORTIE" };
    for (int r = 0; r < kRows; ++r)
    {
        auto row = juce::Rectangle<int> (gridArea.getX(), gridArea.getY() + r * rowH,
                                         gridArea.getWidth(), rowH).reduced (2, 3);
        g.setColour (panel);
        g.fillRoundedRectangle (row.toFloat(), 8.0f);

        g.setColour (accent);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (fr (titles[r]), row.getX() + 10, row.getY() + 4, 300, 14, juce::Justification::centredLeft);
    }

    // Peak meters (-60 .. 0 dB)
    auto drawBar = [&] (juce::Rectangle<int> r, float db)
    {
        g.setColour (panel);
        g.fillRect (r);

        const float clamped = juce::jlimit (-60.0f, 0.0f, db);
        const float y = juce::jmap (clamped, -60.0f, 0.0f, (float) r.getBottom(), (float) r.getY());

        g.setColour (db > -1.0f ? juce::Colours::red : db > -9.0f ? juce::Colours::orange : accent);
        g.fillRect (juce::Rectangle<float> ((float) r.getX(), y, (float) r.getWidth(), r.getBottom() - y));
    };

    const int barW = meterArea.getWidth() / 2 - 3;
    drawBar ({ meterArea.getX(), meterArea.getY(), barW, meterArea.getHeight() }, dbL);
    drawBar ({ meterArea.getRight() - barW, meterArea.getY(), barW, meterArea.getHeight() }, dbR);

    g.setColour (textCol);
    g.setFont (11.0f);
    g.drawText ("L", meterArea.getX(), meterArea.getBottom() + 2, barW, 14, juce::Justification::centred);
    g.drawText ("R", meterArea.getRight() - barW, meterArea.getBottom() + 2, barW, 14, juce::Justification::centred);
    g.drawText (juce::String (juce::jmax (dbL, dbR), 1) + " dB", meterArea.getX() - 8, meterArea.getY() - 18,
                meterArea.getWidth() + 16, 14, juce::Justification::centred);
}

void MasteringEditor::timerCallback()
{
    auto decay = [] (float prev, float lin)
    {
        const float d = juce::Decibels::gainToDecibels (lin, -100.0f);
        return juce::jmax (d, prev - 1.5f);
    };

    dbL = decay (dbL, proc.peakL.exchange (0.0f));
    dbR = decay (dbR, proc.peakR.exchange (0.0f));
    repaint (meterArea.expanded (10, 24));
}
