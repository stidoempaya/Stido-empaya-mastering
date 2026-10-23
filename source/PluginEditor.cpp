#include "PluginEditor.h"

namespace
{
    constexpr int kCols = 5;
    constexpr int kRows = 4;
    const juce::Colour bg      { 0xff14161a };
    const juce::Colour panel   { 0xff1d2026 };
    const juce::Colour accent  { 0xff4fc3f7 };
    const juce::Colour textCol { 0xffd7dbe0 };
    const juce::Colour eqCol   { 0xffffb74d };
    const juce::Colour gridCol { 0xff2c313a };
    const juce::Colour okCol   { 0xff81c784 };
    const juce::Colour warnCol { 0xffffb74d };
    const juce::Colour badCol  { 0xffef5350 };

    constexpr float minFreq = 20.0f, maxFreq = 20000.0f;
    constexpr float minDb = -90.0f, eqRangeDb = 18.0f;

    // Position 0..1 on the log frequency axis -> frequency
    float freqAt (float pos) { return minFreq * std::pow (maxFreq / minFreq, pos); }

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

    // Rangée 4 : dé-esseur
    addKnob ("deessAmount", fr ("Quantité"));
    addKnob ("deessFreq",   fr ("Fréquence"));

    addAndMakeVisible (bypass);
    bypass.setButtonText (fr ("Contourner"));
    bypass.setColour (juce::ToggleButton::textColourId, textCol);
    bypass.setColour (juce::ToggleButton::tickColourId, accent);
    bypassAttach = std::make_unique<ButtonAttachment> (proc.apvts, "bypass", bypass);

    addAndMakeVisible (gainMatch);
    gainMatch.setColour (juce::ToggleButton::textColourId, textCol);
    gainMatch.setColour (juce::ToggleButton::tickColourId, warnCol);
    gainMatch.setTooltip (fr ("Écoute le master au même volume que l'original, pour comparer avec Contourner. "
                              "Les mesures restent celles du master."));
    gainMatchAttach = std::make_unique<ButtonAttachment> (proc.apvts, "gainMatch", gainMatch);

    // Loudness: target platform + reset of the integrated measurement
    int itemId = 1;
    for (const auto& t : MasteringProcessor::loudnessTargets)
        targetBox.addItem (juce::String (t.name) + "  (" + juce::String ((int) t.lufs) + " LUFS)", itemId++);
    targetBox.setColour (juce::ComboBox::backgroundColourId, bg);
    targetBox.setColour (juce::ComboBox::textColourId, textCol);
    targetBox.setColour (juce::ComboBox::outlineColourId, gridCol);
    addAndMakeVisible (targetBox);
    targetAttach = std::make_unique<ComboAttachment> (proc.apvts, "target", targetBox);

    resetLoudness.setButtonText (fr ("Réinitialiser"));
    resetLoudness.setColour (juce::TextButton::buttonColourId, bg);
    resetLoudness.setColour (juce::TextButton::textColourOffId, textCol);
    resetLoudness.onClick = [this] { proc.loudnessResetRequested.store (true); };
    addAndMakeVisible (resetLoudness);

    normaliseButton.setButtonText ("Normaliser");
    normaliseButton.setColour (juce::TextButton::buttonColourId, accent);
    normaliseButton.setColour (juce::TextButton::textColourOffId, bg);
    normaliseButton.setTooltip (fr ("Après avoir joué le morceau en entier : calcule le gain final à partir du LUFS intégré "
                                    "mesuré, sans dépasser le plafond true peak du profil."));
    normaliseButton.onClick = [this] { normalise(); };
    addAndMakeVisible (normaliseButton);

    // Styles (tone) and intensities (level): one click sets several knobs
    auto setupGroup = [this] (std::array<juce::TextButton, 3>& buttons, const char* const* names, int group,
                              std::function<void (int)> apply)
    {
        for (int i = 0; i < 3; ++i)
        {
            auto& b = buttons[(size_t) i];
            b.setButtonText (fr (names[i]));
            b.setClickingTogglesState (true);
            b.setRadioGroupId (group);
            b.setColour (juce::TextButton::buttonColourId, bg);
            b.setColour (juce::TextButton::buttonOnColourId, accent);
            b.setColour (juce::TextButton::textColourOffId, textCol);
            b.setColour (juce::TextButton::textColourOnId, bg);
            b.onClick = [apply, i, &b] { if (b.getToggleState()) apply (i); };
            addAndMakeVisible (b);
        }
    };
    setupGroup (styleButtons, MasteringProcessor::styleNames, 1001, [this] (int i) { proc.applyStyle (i); });
    setupGroup (intensityButtons, MasteringProcessor::intensityNames, 1002, [this] (int i) { proc.applyIntensity (i); });
    refreshPresetButtons();

    analyseButton.setButtonText ("Analyser");
    analyseButton.setColour (juce::TextButton::buttonColourId, accent);
    analyseButton.setColour (juce::TextButton::textColourOffId, bg);
    analyseButton.setTooltip (fr ("Jouez le refrain (la partie la plus forte) : le plugin corrige l'égaliseur "
                                  "puis règle la Poussée pour atteindre la cible de la plateforme (environ 20 s)."));
    analyseButton.onClick = [this]
    {
        if (analysisPhase == AnalysisPhase::idle)
            startAnalysis();
        else
            finishAnalysis (fr ("Analyse annulée."));
    };
    addAndMakeVisible (analyseButton);

    targetDb.fill (minDb);
    spectrumDb.fill (minDb);

    setSize (760, 800);
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
    gainMatch.setBounds (header.removeFromRight (100));
    gainMatchInfo = header.removeFromRight (70);
    header.removeFromRight (10);
    analyseButton.setBounds (header.removeFromRight (140).reduced (0, 4));

    auto top = area.removeFromTop (220);
    loudnessArea = top.removeFromRight (220).reduced (2, 3);
    spectrumArea = top.withTrimmedRight (4).reduced (2, 3);
    area.removeFromTop (6);

    // Style / intensity row
    presetArea = area.removeFromTop (40).reduced (2, 3);
    {
        auto p = presetArea.reduced (10, 6);
        p.removeFromLeft (50);                                // "STYLE" label
        for (auto& b : styleButtons)
            b.setBounds (p.removeFromLeft (95).reduced (2, 0));

        p.removeFromLeft (30);
        p.removeFromLeft (75);                                // "INTENSITÉ" label
        for (auto& b : intensityButtons)
            b.setBounds (p.removeFromLeft (85).reduced (2, 0));
    }

    auto l = loudnessArea.reduced (10, 6);
    l.removeFromTop (18);
    targetBox.setBounds (l.removeFromTop (22));
    auto buttons = l.removeFromBottom (20);
    normaliseButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).withTrimmedRight (3));
    resetLoudness.setBounds (buttons.withTrimmedLeft (3));

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
    const char* titles[kRows] = { "ENTRÉE / ÉGALISEUR", "COMPRESSEUR", "STÉRÉO / LIMITEUR / SORTIE", "DÉ-ESSEUR" };
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

    // De-esser activity, to the right of its two knobs
    {
        const auto row = juce::Rectangle<int> (gridArea.getX(), gridArea.getY() + 3 * rowH, gridArea.getWidth(), rowH);
        const auto info = row.withTrimmedLeft (2 * gridArea.getWidth() / kCols + 20).reduced (10, 0);
        const bool on = proc.apvts.getRawParameterValue ("deessAmount")->load() > 0.0f;

        g.setFont (juce::FontOptions (13.0f));
        g.setColour (on ? (deessGr > 0.1f ? eqCol : textCol) : textCol.withAlpha (0.5f));
        g.drawText (on ? fr ("Réduction des « s » : ") + (deessGr < 0.05f ? juce::String ("0") : "-" + juce::String (deessGr, 1)) + " dB"
                       : fr ("Désactivé (montez Quantité pour adoucir les « s »)"),
                    info, juce::Justification::centredLeft);
    }

    // Gain match correction, next to its button
    if (gainMatch.getToggleState() && ! bypass.getToggleState())
    {
        g.setColour (warnCol);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ((matchDb >= 0.0f ? "+" : "") + juce::String (matchDb, 1) + " dB", gainMatchInfo,
                    juce::Justification::centredRight);
    }

    drawSpectrum (g);
    drawLoudness (g);

    // Gain Match warning banner, over the spectrum: the listening level is not the real master level
    if (gainMatch.getToggleState())
    {
        auto banner = spectrumArea.reduced (14, 0).removeFromBottom (26).translated (0, -8);
        g.setColour (warnCol);
        g.fillRoundedRectangle (banner.toFloat(), 5.0f);
        g.setColour (bg);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (fr ("GAIN MATCH ACTIF : volume baissé pour comparer. Décochez avant d'exporter !"),
                    banner, juce::Justification::centred);
    }

    // Style / intensity row
    g.setColour (panel);
    g.fillRoundedRectangle (presetArea.toFloat(), 8.0f);
    g.setColour (accent);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("STYLE", presetArea.getX() + 10, presetArea.getY(), 50, presetArea.getHeight(),
                juce::Justification::centredLeft);
    g.drawText (fr ("INTENSITÉ"), styleButtons[2].getRight() + 32, presetArea.getY(), 75, presetArea.getHeight(),
                juce::Justification::centredLeft);

    // Peak meters (-60 .. 0 dB)
    auto drawBar = [&] (juce::Rectangle<int> r, float db)
    {
        g.setColour (panel);
        g.fillRect (r);

        auto yFor = [&] (float v) { return juce::jmap (juce::jlimit (-60.0f, 0.0f, v), -60.0f, 0.0f, (float) r.getBottom(), (float) r.getY()); };

        // Zones: blue up to -9 dB, orange up to -1 dB, red above
        const struct { float lo, hi; juce::Colour col; } zones[] = {
            { -60.0f, -9.0f, accent }, { -9.0f, -1.0f, juce::Colours::orange }, { -1.0f, 0.0f, juce::Colours::red } };

        for (const auto& z : zones)
        {
            if (db <= z.lo)
                break;
            const float top = yFor (juce::jmin (db, z.hi)), bottom = yFor (z.lo);
            g.setColour (z.col);
            g.fillRect (juce::Rectangle<float> ((float) r.getX(), top, (float) r.getWidth(), bottom - top));
        }
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

    if (proc.analyserReady.load())
    {
        std::fill (fftData.begin(), fftData.end(), 0.0f);
        std::copy (proc.analyserBlock.begin(), proc.analyserBlock.end(), fftData.begin());
        proc.analyserReady.store (false);
        updateSpectrum();
    }

    // Fast rise, slow fall (about 36 dB/s)
    for (int i = 0; i < numPoints; ++i)
        spectrumDb[(size_t) i] = juce::jmax (targetDb[(size_t) i], spectrumDb[(size_t) i] - 1.2f);

    repaint (spectrumArea);

    updateAnalysis();

    deessGr = juce::jmax (proc.deessReductionDb.exchange (0.0f), deessGr - 0.5f);
    matchDb = proc.gainMatchDb.load();
    repaint (gridArea.withTrimmedTop (3 * rowH));
    repaint (gainMatchInfo);

    lufsM    = proc.momentaryLufs.load();
    lufsS    = proc.shortTermLufs.load();
    lufsI    = proc.integratedLufs.load();
    truePeak = proc.truePeakDb.load();
    repaint (loudnessArea);
}

void MasteringEditor::drawLoudness (juce::Graphics& g)
{
    g.setColour (panel);
    g.fillRoundedRectangle (loudnessArea.toFloat(), 8.0f);

    const auto& target = currentTarget();
    auto valid = [] (float v) { return v > -70.0f; };
    auto fmt = [&] (float v, const char* unit) { return valid (v) ? juce::String (v, 1) + unit : juce::String (fr ("—")); };

    auto r = loudnessArea.reduced (10, 6);
    g.setColour (accent);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("LOUDNESS", r.removeFromTop (18), juce::Justification::centredLeft);
    r.removeFromTop (26);                                  // target combo box

    // Integrated loudness, coloured by distance to the target
    const float diff = lufsI - target.lufs;
    const auto lufsCol = ! valid (lufsI) ? textCol : std::abs (diff) <= 1.0f ? okCol : warnCol;

    auto row = r.removeFromTop (30);
    g.setColour (textCol.withAlpha (0.7f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (fr ("Intégré"), row.removeFromLeft (60), juce::Justification::centredLeft);
    g.setColour (lufsCol);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText (fmt (lufsI, " LUFS"), row, juce::Justification::centredRight);

    auto line = [&] (const juce::String& name, const juce::String& value, juce::Colour col)
    {
        auto lr = r.removeFromTop (16);
        g.setFont (juce::FontOptions (11.0f));
        g.setColour (textCol.withAlpha (0.7f));
        g.drawText (name, lr, juce::Justification::centredLeft);
        g.setColour (col);
        g.drawText (value, lr, juce::Justification::centredRight);
    };

    const float norm = proc.getParameterValue ("normGain");
    const auto tpCol = ! valid (truePeak) ? textCol : truePeak > target.truePeak ? badCol : okCol;
    line (fr ("Court terme (3 s)"), fmt (lufsS, " LUFS"), textCol);
    line (fr ("Momentané (400 ms)"), fmt (lufsM, " LUFS"), textCol);
    line ("True peak (max)", fmt (truePeak, " dBTP") + fr (" / ") + juce::String (target.truePeak, 0), tpCol);
    line ("Normalisation", (norm > 0.0f ? "+" : "") + juce::String (norm, 1) + " dB", norm != 0.0f ? accent : textCol);

    // Advice
    r.removeFromTop (4);
    juce::String advice;
    juce::Colour adviceCol = textCol.withAlpha (0.7f);

    if (! valid (lufsI))
    {
        advice = fr ("Lancez la lecture du morceau en entier.");
    }
    else if (std::abs (diff) <= 1.0f)
    {
        advice = fr ("Niveau idéal pour ") + juce::String (target.name) + ".";
        adviceCol = okCol;
    }
    else
    {
        advice = fr (diff < 0.0f ? "Trop faible de " : "Trop fort de ") + juce::String (std::abs (diff), 1)
               + fr (" LU : cliquez sur Normaliser.");
        adviceCol = warnCol;
    }

    if (valid (truePeak) && truePeak > target.truePeak)
    {
        advice += fr ("\nTrue peak au-dessus du plafond : cliquez sur Normaliser.");
        adviceCol = badCol;
    }

    if (analysisPhase != AnalysisPhase::idle)
    {
        advice = fr ("Analyse en cours : laissez jouer le refrain (la partie la plus forte).");
        adviceCol = accent;
    }
    else if (analysisMessage.isNotEmpty() && juce::Time::getMillisecondCounterHiRes() < messageUntilMs)
    {
        advice = analysisMessage;
        adviceCol = accent;
    }

    g.setColour (adviceCol);
    g.setFont (juce::FontOptions (11.0f));
    g.drawFittedText (advice, r.withTrimmedBottom (22), juce::Justification::topLeft, 2);
}

void MasteringEditor::updateSpectrum()
{
    const double sr = proc.getSampleRate();
    if (sr <= 0.0)
        return;

    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    const float binHz = (float) sr / (float) fftSize;
    const float norm  = (float) fftSize * 0.5f;           // normalised Hann window: 0 dBFS sine -> 0 dB
    const int lastBin = fftSize / 2;

    // Analysis: average power per bin of the (non-silent) input
    if (analysisPhase == AnalysisPhase::listening)
    {
        double total = 0.0;
        for (int b = 1; b <= lastBin; ++b)
            total += juce::square ((double) fftData[(size_t) b] / norm);

        if (total > 1.0e-7)                                   // ignore silence (below about -70 dBFS)
        {
            for (int b = 1; b <= lastBin; ++b)
                analysisPower[(size_t) b] += juce::square ((double) fftData[(size_t) b] / norm);
            ++analysisFrames;
        }
    }

    for (int i = 0; i < numPoints; ++i)
    {
        const int b0 = juce::jlimit (1, lastBin, (int) std::floor (freqAt ((float) i / numPoints) / binHz));
        const int b1 = juce::jlimit (b0, lastBin, (int) std::ceil (freqAt ((float) (i + 1) / numPoints) / binHz));

        float mag = 0.0f;
        for (int b = b0; b <= b1; ++b)
            mag = juce::jmax (mag, fftData[(size_t) b]);

        targetDb[(size_t) i] = juce::Decibels::gainToDecibels (mag / norm, minDb);
    }
}

void MasteringEditor::drawSpectrum (juce::Graphics& g)
{
    g.setColour (panel);
    g.fillRoundedRectangle (spectrumArea.toFloat(), 8.0f);

    const auto plot = spectrumArea.toFloat().reduced (10.0f, 22.0f).withTrimmedBottom (-10.0f);
    const float logRange = std::log (maxFreq / minFreq);

    auto xFor = [&] (float f) { return plot.getX() + plot.getWidth() * std::log (f / minFreq) / logRange; };
    auto yFor = [&] (float db) { return juce::jmap (juce::jlimit (minDb, 0.0f, db), minDb, 0.0f, plot.getBottom(), plot.getY()); };
    auto yForEq = [&] (float db)
    {
        return juce::jmap (juce::jlimit (-eqRangeDb, eqRangeDb, db), -eqRangeDb, eqRangeDb, plot.getBottom(), plot.getY());
    };

    // Grid
    g.setFont (juce::FontOptions (10.0f));
    const float freqs[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000 };
    const char* freqLabels[] = { "50", "100", "200", "500", "1k", "2k", "5k", "10k" };
    for (int i = 0; i < 8; ++i)
    {
        const float x = xFor (freqs[i]);
        g.setColour (gridCol);
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (textCol.withAlpha (0.5f));
        g.drawText (freqLabels[i], (int) x + 2, (int) plot.getBottom() - 12, 30, 12, juce::Justification::centredLeft);
    }
    for (float db = -18.0f; db > minDb; db -= 18.0f)
    {
        const float y = yFor (db);
        g.setColour (gridCol);
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (textCol.withAlpha (0.5f));
        g.drawText (juce::String ((int) db), (int) plot.getRight() - 30, (int) y - 12, 28, 12, juce::Justification::centredRight);
    }

    // Spectrum
    juce::Path spectrum;
    spectrum.startNewSubPath (plot.getX(), plot.getBottom());
    for (int i = 0; i < numPoints; ++i)
        spectrum.lineTo (xFor (freqAt (((float) i + 0.5f) / numPoints)), yFor (spectrumDb[(size_t) i]));
    spectrum.lineTo (plot.getRight(), plot.getBottom());
    spectrum.closeSubPath();

    g.setColour (accent.withAlpha (0.25f));
    g.fillPath (spectrum);
    g.setColour (accent);
    g.strokePath (spectrum, juce::PathStrokeType (1.5f));

    // EQ curve (same filters as the processor)
    const double sr = proc.getSampleRate();
    if (sr > 0.0)
    {
        using Coeffs = juce::dsp::IIR::Coefficients<float>;
        using juce::Decibels;
        auto param = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };

        auto low  = Coeffs::makeLowShelf (sr, MasteringProcessor::lowShelfFreq, MasteringProcessor::shelfQ,
                                          Decibels::decibelsToGain (param ("lowGain")));
        auto mid  = Coeffs::makePeakFilter (sr, param ("midFreq"), MasteringProcessor::midQ,
                                            Decibels::decibelsToGain (param ("midGain")));
        auto high = Coeffs::makeHighShelf (sr, MasteringProcessor::highShelfFreq, MasteringProcessor::shelfQ,
                                           Decibels::decibelsToGain (param ("highGain")));

        juce::Path eq;
        bool first = true;
        for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
        {
            const double f = freqAt ((x - plot.getX()) / plot.getWidth());
            const double mag = low->getMagnitudeForFrequency (f, sr) * mid->getMagnitudeForFrequency (f, sr)
                             * high->getMagnitudeForFrequency (f, sr);
            const float y = yForEq (Decibels::gainToDecibels ((float) mag));

            if (first) eq.startNewSubPath (x, y);
            else       eq.lineTo (x, y);
            first = false;
        }

        g.setColour (eqCol);
        g.strokePath (eq, juce::PathStrokeType (2.0f));
    }

    // Title / legend
    auto top = spectrumArea.reduced (10, 4).removeFromTop (14);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.setColour (accent);
    g.drawText (analysisPhase == AnalysisPhase::listening ? fr ("SPECTRE (ENTRÉE, ANALYSE)") : fr ("SPECTRE (SORTIE)"),
                top, juce::Justification::centredLeft);
    g.setColour (eqCol);
    g.drawText (fr ("COURBE EQ (±18 dB)"), top, juce::Justification::centredRight);
}

void MasteringEditor::refreshPresetButtons()
{
    // Show the last style / intensity applied (saved with the project)
    const int style     = proc.apvts.state.getProperty ("style", -1);
    const int intensity = proc.apvts.state.getProperty ("intensity", -1);

    for (int i = 0; i < 3; ++i)
    {
        styleButtons[(size_t) i].setToggleState (i == style, juce::dontSendNotification);
        intensityButtons[(size_t) i].setToggleState (i == intensity, juce::dontSendNotification);
    }
}

MasteringEditor::~MasteringEditor()
{
    proc.analyseInput.store (false);
}

const LoudnessTarget& MasteringEditor::currentTarget() const
{
    constexpr int last = (int) std::size (MasteringProcessor::loudnessTargets) - 1;
    return MasteringProcessor::loudnessTargets[juce::jlimit (0, last, targetBox.getSelectedItemIndex())];
}

void MasteringEditor::startAnalysis()
{
    analysisPower.assign ((size_t) fftSize / 2 + 1, 0.0);
    analysisFrames = 0;
    analysisMessage = {};
    analysisPhase = AnalysisPhase::listening;
    phaseStartMs = juce::Time::getMillisecondCounterHiRes();

    proc.setParameter ("bypass", 0.0f);
    proc.setParameter ("normGain", 0.0f);                   // the analysis sets the engine; normalisation comes after
    proc.analyseInput.store (true);
}

void MasteringEditor::beginCalibration()
{
    // Phase 2: EQ correction + style + intensity, then Poussée adjusted by steps of ~3.5 s
    proc.analyseInput.store (false);
    analysisEq = MasteringProcessor::computeAnalysisEq (analysisPower, proc.getSampleRate());
    proc.setAnalysisEq (analysisEq);

    const int style     = proc.apvts.state.getProperty ("style", -1);
    const int intensity = proc.apvts.state.getProperty ("intensity", -1);
    proc.applyStyle (style >= 0 ? style : 1);                // Équilibré by default
    proc.applyIntensity (intensity >= 0 ? intensity : 1);    // Moyenne by default
    refreshPresetButtons();

    analysisPhase = AnalysisPhase::calibrating;
    calibStep = 0;
    phaseStartMs = windowStartMs = juce::Time::getMillisecondCounterHiRes();
    proc.loudnessResetRequested.store (true);
}

void MasteringEditor::finishAnalysis (const juce::String& message)
{
    analysisPhase = AnalysisPhase::idle;
    proc.analyseInput.store (false);
    proc.loudnessResetRequested.store (true);           // fresh integrated measurement with the new settings

    analysisMessage = message;
    messageUntilMs = juce::Time::getMillisecondCounterHiRes() + 15000.0;
    analyseButton.setButtonText ("Analyser");
}

void MasteringEditor::updateAnalysis()
{
    constexpr double listenSeconds = 8.0, windowMs = 3500.0;
    constexpr int maxCalibSteps = 4;
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double sr = proc.getSampleRate();

    if (analysisPhase == AnalysisPhase::listening)
    {
        const int needed = juce::jmax (1, (int) (listenSeconds * sr / (fftSize / 2)));

        if (analysisFrames >= needed)
            beginCalibration();
        else if (now - phaseStartMs > 60000.0)
            finishAnalysis (fr ("Aucun son détecté : lancez la lecture, puis recliquez sur Analyser."));
        else
            analyseButton.setButtonText (analysisFrames == 0 ? fr ("Lancez la lecture…")
                                                             : fr ("Écoute… ") + juce::String (50 * analysisFrames / needed) + " %");
        return;
    }

    if (analysisPhase != AnalysisPhase::calibrating)
        return;

    if (now - phaseStartMs > 90000.0)
    {
        finishAnalysis (fr ("Analyse interrompue : la lecture s'est arrêtée."));
        return;
    }

    const double elapsed = now - windowStartMs;
    const int progress = 50 + (int) (50.0 * (calibStep + juce::jmin (1.0, elapsed / windowMs)) / maxCalibSteps);
    analyseButton.setButtonText (fr ("Réglage… ") + juce::String (juce::jmin (99, progress)) + " %");

    if (elapsed < windowMs)
        return;

    const float shortTerm = proc.shortTermLufs.load();
    if (shortTerm <= -70.0f)                              // silence: wait for the music
    {
        windowStartMs = now;
        return;
    }

    lastMeasuredLufs = shortTerm;
    ++calibStep;

    // True peak above -1 dBTP: lower the ceiling (down to -3 dB)
    const float truePeak = proc.truePeakDb.load();
    const float tpCeiling = currentTarget().truePeak;
    if (truePeak > tpCeiling)
        proc.setParameter ("ceiling", juce::jlimit (tpCeiling - 2.0f, tpCeiling, proc.getParameterValue ("ceiling")
                                                                                 - (truePeak - tpCeiling) - 0.2f));

    const float error = currentTarget().lufs - shortTerm;

    if (std::abs (error) > 0.5f)
    {
        // Poussée (0..12 dB) first, then Sortie (only downwards) when the input is already too loud
        const float total = proc.getParameterValue ("drive") + proc.getParameterValue ("outGain") + error;
        const float drive = juce::jlimit (0.0f, 12.0f, total);
        proc.setParameter ("drive", drive);
        proc.setParameter ("outGain", juce::jlimit (-12.0f, 0.0f, total - drive));
    }

    if (std::abs (error) <= 0.5f || calibStep >= maxCalibSteps)
    {
        auto db = [] (float v) { return (v >= 0.0f ? "+" : "") + juce::String (v, 1); };
        juce::String msg = fr ("Analyse terminée : graves ") + db (analysisEq.low) + fr (", aigus ") + db (analysisEq.high) + " dB";
        if (analysisEq.mid != 0.0f)
            msg += fr (", ") + db (analysisEq.mid) + " dB @ " + juce::String ((int) analysisEq.midFreq) + " Hz";
        msg += fr (". Poussée ") + juce::String (proc.getParameterValue ("drive"), 1) + " dB";
        if (std::abs (error) > 0.5f)
            msg += fr (". Cible proche, vérifiez le LUFS intégré.");
        else
            msg += fr (" → ") + juce::String (shortTerm, 1) + " LUFS.";
        finishAnalysis (msg);
        return;
    }

    proc.loudnessResetRequested.store (true);
    windowStartMs = now;
}

void MasteringEditor::normalise()
{
    // Final gain from the integrated loudness really measured on the output (normalisation included),
    // capped so that the true peak stays under the profile ceiling
    const auto& target = currentTarget();
    const float measured = proc.integratedLufs.load();
    const float peak = proc.truePeakDb.load();
    const float current = proc.getParameterValue ("normGain");

    auto show = [this] (const juce::String& msg)
    {
        analysisMessage = msg;
        messageUntilMs = juce::Time::getMillisecondCounterHiRes() + 15000.0;
    };
    auto db = [] (float v) { return (v >= 0.0f ? "+" : "") + juce::String (v, 1); };

    if (measured <= -70.0f || peak <= -70.0f)
    {
        show (fr ("Jouez d'abord le morceau en entier (après Réinitialiser), puis cliquez sur Normaliser."));
        return;
    }

    const float wanted = target.lufs - measured;
    const float headroom = target.truePeak - peak;

    if (std::abs (wanted) < 0.3f && headroom >= 0.0f)
    {
        show (fr ("Déjà à la cible (") + juce::String (measured, 1) + fr (" LUFS) : aucune normalisation nécessaire."));
        return;
    }

    const float delta = juce::jmin (wanted, headroom);
    const float newGain = juce::jlimit (-12.0f, 12.0f, current + delta);
    proc.setParameter ("normGain", newGain);
    proc.loudnessResetRequested.store (true);              // measure again with the new gain

    juce::String msg = fr ("Normalisation ") + db (newGain) + fr (" dB (mesuré ") + juce::String (measured, 1)
                     + fr (", cible ") + juce::String (target.lufs, 0) + fr (").");
    if (headroom < wanted)
        msg += fr (" Limitée par le true peak : montez la Poussée.");
    else
        msg += fr (" Rejouez le morceau pour vérifier.");
    show (msg);
}
