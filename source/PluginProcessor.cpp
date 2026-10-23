#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MasteringProcessor::createLayout()
{
    using P = juce::AudioParameterFloat;
    using R = juce::NormalisableRange<float>;
    using Attr = juce::AudioParameterFloatAttributes;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto add = [&] (const char* id, const char* name, R range, float def, const char* unit)
    {
        layout.add (std::make_unique<P> (juce::ParameterID { id, 1 }, juce::String::fromUTF8 (name), range, def,
                                         Attr().withLabel (unit)));
    };

    // Entrée / égaliseur
    add ("inGain",   "Entrée",     R (-12.f, 12.f, 0.1f), 0.f, "dB");
    add ("lowGain",  "Graves",       R (-12.f, 12.f, 0.1f), 0.f, "dB");   // shelf @ 100 Hz
    add ("midGain",  "Médiums",       R (-12.f, 12.f, 0.1f), 0.f, "dB");
    add ("midFreq",  "Fréq. médiums",  R (200.f, 5000.f, 1.f, 0.3f), 1000.f, "Hz");
    add ("highGain", "Aigus",      R (-12.f, 12.f, 0.1f), 0.f, "dB");   // shelf @ 8 kHz

    // Compresseur "glue"
    add ("compThresh",  "Seuil", R (-40.f, 0.f, 0.1f), -12.f, "dB");
    add ("compRatio",   "Ratio",     R (1.f, 10.f, 0.1f), 2.f, ":1");
    add ("compAttack",  "Attaque",    R (1.f, 100.f, 0.1f, 0.4f), 30.f, "ms");
    add ("compRelease", "Relâchement",   R (20.f, 500.f, 1.f, 0.4f), 150.f, "ms");
    add ("compMakeup",  "Compensation",    R (0.f, 12.f, 0.1f), 0.f, "dB");

    // Stéréo / limiteur / sortie
    add ("width",      "Largeur",      R (0.f, 200.f, 1.f), 100.f, "%");
    add ("drive",      "Poussée",      R (0.f, 12.f, 0.1f), 0.f, "dB");
    add ("ceiling",    "Plafond",    R (-12.f, 0.f, 0.1f), -1.f, "dB");
    add ("limRelease", "Relâch. limiteur",    R (10.f, 500.f, 1.f, 0.4f), 100.f, "ms");
    add ("outGain",    "Sortie",     R (-12.f, 12.f, 0.1f), 0.f, "dB");

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "bypass", 1 },
                                                            juce::String::fromUTF8 ("Contourner"), false));

    juce::StringArray targets;
    for (const auto& t : loudnessTargets)
        targets.add (t.name);
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "target", 1 },
                                                              "Cible loudness", targets, 0));

    // Dé-esseur (0 % = désactivé) et Gain Match
    add ("deessAmount", "Dé-esseur",       R (0.f, 100.f, 1.f), 0.f, "%");
    add ("deessFreq",   "Fréq. dé-esseur", R (3000.f, 12000.f, 1.f, 0.5f), 6500.f, "Hz");
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "gainMatch", 1 },
                                                            "Gain Match", false));

    // Normalisation : gain final fixe, calculé par le bouton « Normaliser » depuis le LUFS intégré mesuré
    add ("normGain", "Normalisation", R (-12.f, 12.f, 0.1f), 0.f, "dB");
    return layout;
}

//==============================================================================
MasteringProcessor::MasteringProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };

    prm = { get ("inGain"), get ("lowGain"), get ("midGain"), get ("midFreq"), get ("highGain"),
            get ("compThresh"), get ("compRatio"), get ("compAttack"), get ("compRelease"),
            get ("compMakeup"), get ("width"), get ("drive"), get ("ceiling"),
            get ("limRelease"), get ("outGain"), get ("bypass"),
            get ("deessAmount"), get ("deessFreq"), get ("gainMatch"), get ("normGain") };
}

bool MasteringProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet()  == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void MasteringProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec { sampleRate,
                                  (juce::uint32) samplesPerBlock,
                                  (juce::uint32) getTotalNumOutputChannels() };

    for (auto* g : { &inGain, &makeup, &drive, &outGain, &normGain })
    {
        g->prepare (spec);
        g->setRampDurationSeconds (0.05);
    }

    lowShelf.prepare (spec);
    midPeak.prepare (spec);
    highShelf.prepare (spec);
    compressor.prepare (spec);
    limiter.prepare (spec);

    cache = {};          // force the filter coefficients to be recomputed
    updateDSP();

    // K-weighting coefficients for any sample rate (ITU-R BS.1770-4)
    {
        const double pi = juce::MathConstants<double>::pi;

        double K = std::tan (pi * 1681.974450955533 / sampleRate);
        double Q = 0.7071752369554196;
        const double Vh = std::pow (10.0, 3.999843853973347 / 20.0);
        const double Vb = std::pow (Vh, 0.4996667741545416);
        double a0 = 1.0 + K / Q + K * K;

        Biquad shelf;
        shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
        shelf.b1 = 2.0 * (K * K - Vh) / a0;
        shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
        shelf.a1 = 2.0 * (K * K - 1.0) / a0;
        shelf.a2 = (1.0 - K / Q + K * K) / a0;

        K = std::tan (pi * 38.13547087602444 / sampleRate);
        Q = 0.5003270373238773;
        a0 = 1.0 + K / Q + K * K;

        Biquad highPass;
        highPass.b0 = 1.0;
        highPass.b1 = -2.0;
        highPass.b2 = 1.0;
        highPass.a1 = 2.0 * (K * K - 1.0) / a0;
        highPass.a2 = (1.0 - K / Q + K * K) / a0;

        kShelfProto = shelf;
        kHighPassProto = highPass;
        kShelf.fill (shelf);
        kHighPass.fill (highPass);
    }

    // De-esser
    for (auto* f : { &deessLow, &deessHigh })
    {
        f->prepare (spec);
        f->setCutoffFrequency (prm.deessFreq->load());
    }
    deessLow.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    deessHigh.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
    deessAttack  = 1.0f - std::exp (-1.0f / (0.001f * (float) sampleRate));
    deessRelease = 1.0f - std::exp (-1.0f / (0.060f * (float) sampleRate));
    deessEnvHigh = deessEnvFull = 0.0f;
    deessActive = false;

    // Gain match
    gmInShelf.fill (kShelfProto);
    gmOutShelf.fill (kShelfProto);
    gmInHighPass.fill (kHighPassProto);
    gmOutHighPass.fill (kHighPassProto);
    gmInMs = gmOutMs = 0.0;
    gmCoef = std::exp (-1.0 / (3.0 * sampleRate));
    gainMatchGain.reset (sampleRate, 0.1);
    gainMatchGain.setCurrentAndTargetValue (1.0f);

    subBlockLength = juce::jmax (1, juce::roundToInt (sampleRate * 0.1));
    maxBlockSize = samplesPerBlock;
    truePeakOversampler.initProcessing ((size_t) samplesPerBlock);
    resetLoudness();
}

void MasteringProcessor::resetLoudness()
{
    for (auto* f : { &kShelf, &kHighPass })
        for (auto& b : *f)
            b.z1 = b.z2 = 0.0;

    subBlockEnergy.fill (0.0);
    subBlockPos = subBlocksFilled = subBlockCount = 0;
    subBlockSum = 0.0;
    histEnergy.fill (0.0);
    histCount.fill (0);
    truePeakOversampler.reset();

    momentaryLufs.store (noLoudness);
    shortTermLufs.store (noLoudness);
    integratedLufs.store (noLoudness);
    truePeakDb.store (noLoudness);
}

void MasteringProcessor::updateDSP()
{
    const auto sr = getSampleRate();
    using juce::Decibels;

    const float low = prm.lowGain->load(), mid = prm.midGain->load(),
                mf  = prm.midFreq->load(), high = prm.highGain->load();

    if (low != cache.low)
    {
        cache.low = low;
        *lowShelf.state = *Coeffs::makeLowShelf (sr, lowShelfFreq, shelfQ, Decibels::decibelsToGain (low));
    }
    if (mid != cache.mid || mf != cache.midFreq)
    {
        cache.mid = mid;
        cache.midFreq = mf;
        *midPeak.state = *Coeffs::makePeakFilter (sr, mf, midQ, Decibels::decibelsToGain (mid));
    }
    if (high != cache.high)
    {
        cache.high = high;
        *highShelf.state = *Coeffs::makeHighShelf (sr, highShelfFreq, shelfQ, Decibels::decibelsToGain (high));
    }

    inGain.setGainDecibels  (prm.inGain->load());
    makeup.setGainDecibels  (prm.compMakeup->load());
    drive.setGainDecibels   (prm.drive->load());
    outGain.setGainDecibels (prm.outGain->load());
    normGain.setGainDecibels (prm.normGain->load());

    compressor.setThreshold (prm.compThresh->load());
    compressor.setRatio     (prm.compRatio->load());
    compressor.setAttack    (prm.compAttack->load());
    compressor.setRelease   (prm.compRelease->load());

    limiter.setThreshold (prm.ceiling->load());
    limiter.setRelease   (prm.limRelease->load());
}

//==============================================================================
void MasteringProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    const bool inputToAnalyser = analyseInput.load();
    if (inputToAnalyser)
        pushToAnalyser (buffer);

    const bool active = prm.bypass->load() < 0.5f;

    if (active)
    {
        updateDSP();
        gmInMs = averageKWeighted (buffer, gmInShelf, gmInHighPass, gmInMs);

        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> ctx (block);

        inGain.process (ctx);
        lowShelf.process (ctx);
        midPeak.process (ctx);
        highShelf.process (ctx);
        processDeEsser (buffer);
        compressor.process (ctx);
        makeup.process (ctx);

        // Mid/Side stereo width
        if (buffer.getNumChannels() >= 2)
        {
            const float w = prm.width->load() * 0.01f;
            auto* l = buffer.getWritePointer (0);
            auto* r = buffer.getWritePointer (1);

            for (int i = 0; i < numSamples; ++i)
            {
                const float m = 0.5f * (l[i] + r[i]);
                const float s = 0.5f * (l[i] - r[i]) * w;
                l[i] = m + s;
                r[i] = m - s;
            }
        }

        drive.process (ctx);
        limiter.process (ctx);
        outGain.process (ctx);
        normGain.process (ctx);           // final level (normalisation), measured by the meters below

        gmOutMs = averageKWeighted (buffer, gmOutShelf, gmOutHighPass, gmOutMs);
    }

    // Peak metering (keep the max until the editor reads it)
    const float mL = buffer.getMagnitude (0, 0, numSamples);
    const float mR = buffer.getNumChannels() > 1 ? buffer.getMagnitude (1, 0, numSamples) : mL;
    if (mL > peakL.load()) peakL.store (mL);
    if (mR > peakR.load()) peakR.store (mR);

    if (! inputToAnalyser)
        pushToAnalyser (buffer);
    measureLoudness (buffer);

    // Gain match, after the meters (they always show the real master). Estimates freeze while bypassed.
    float matchGain = 1.0f;
    if (active && prm.gainMatch->load() >= 0.5f && gmInMs > 1e-10 && gmOutMs > 1e-10)
        matchGain = juce::jlimit (juce::Decibels::decibelsToGain (-24.0f), juce::Decibels::decibelsToGain (24.0f),
                                  (float) std::sqrt (gmInMs / gmOutMs));

    gainMatchDb.store (juce::Decibels::gainToDecibels (matchGain));
    gainMatchGain.setTargetValue (matchGain);
    gainMatchGain.applyGain (buffer, numSamples);
}

double MasteringProcessor::averageKWeighted (const juce::AudioBuffer<float>& buffer, std::array<Biquad, 2>& shelf,
                                             std::array<Biquad, 2>& highPass, double meanSquare) const
{
    const int numCh = juce::jmin (2, buffer.getNumChannels());

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        double sq = 0.0;
        for (int ch = 0; ch < numCh; ++ch)
        {
            const double y = highPass[(size_t) ch].process (shelf[(size_t) ch].process (buffer.getSample (ch, i)));
            sq += y * y;
        }
        meanSquare = gmCoef * meanSquare + (1.0 - gmCoef) * sq;
    }

    return meanSquare;
}

void MasteringProcessor::processDeEsser (juce::AudioBuffer<float>& buffer)
{
    const float amount = prm.deessAmount->load() * 0.01f;

    if (amount <= 0.0f || buffer.getNumChannels() < 2)
    {
        deessActive = false;
        deessReductionDb.store (0.0f);
        return;
    }

    if (! deessActive)
    {
        deessLow.reset();
        deessHigh.reset();
        deessEnvHigh = deessEnvFull = 0.0f;
        deessActive = true;
    }

    const float freq = prm.deessFreq->load();
    if (freq != deessLow.getCutoffFrequency())
    {
        deessLow.setCutoffFrequency (freq);
        deessHigh.setCutoffFrequency (freq);
    }

    // The more "Dé-esseur", the lower the threshold (high band vs full band) and the deeper the cut
    const float thresholdDb    = juce::jmap (amount, -3.0f, -15.0f);
    const float maxReductionDb = 12.0f * amount;
    float maxGr = 0.0f;

    auto* l = buffer.getWritePointer (0);
    auto* r = buffer.getWritePointer (1);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float lowL = deessLow.processSample (0, l[i]),  highL = deessHigh.processSample (0, l[i]);
        const float lowR = deessLow.processSample (1, r[i]),  highR = deessHigh.processSample (1, r[i]);

        // Stereo-linked envelopes: fast attack (1 ms), release 60 ms
        const float inHigh = juce::jmax (std::abs (highL), std::abs (highR));
        const float inFull = juce::jmax (std::abs (l[i]), std::abs (r[i]));
        deessEnvHigh += (inHigh > deessEnvHigh ? deessAttack : deessRelease) * (inHigh - deessEnvHigh);
        deessEnvFull += (inFull > deessEnvFull ? deessAttack : deessRelease) * (inFull - deessEnvFull);

        float grDb = 0.0f;
        if (deessEnvFull > 1.0e-4f)
        {
            const float over = juce::Decibels::gainToDecibels (deessEnvHigh / deessEnvFull, -100.0f) - thresholdDb;
            grDb = juce::jlimit (0.0f, maxReductionDb, over * 0.75f);          // ratio 4:1
        }

        const float g = juce::Decibels::decibelsToGain (-grDb);
        l[i] = lowL + highL * g;
        r[i] = lowR + highR * g;
        maxGr = juce::jmax (maxGr, grDb);
    }

    deessReductionDb.store (maxGr);
}

void MasteringProcessor::measureLoudness (juce::AudioBuffer<float>& buffer)
{
    if (loudnessResetRequested.exchange (false))
        resetLoudness();

    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin (2, buffer.getNumChannels());

    // True peak: max of the 4x oversampled signal, held until reset
    if (maxBlockSize > 0 && numCh == 2)
    {
        juce::dsp::AudioBlock<float> block (buffer);
        float peak = 0.0f;

        for (int start = 0; start < numSamples; start += maxBlockSize)
        {
            const auto len = (size_t) juce::jmin (maxBlockSize, numSamples - start);
            auto up = truePeakOversampler.processSamplesUp (block.getSubBlock ((size_t) start, len));

            for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
            {
                const auto range = juce::FloatVectorOperations::findMinAndMax (up.getChannelPointer (ch), (int) up.getNumSamples());
                peak = juce::jmax (peak, -range.getStart(), range.getEnd());
            }
        }

        const float peakDb = juce::Decibels::gainToDecibels (peak, noLoudness);
        if (peakDb > truePeakDb.load())
            truePeakDb.store (peakDb);
    }

    // K-weighted mean square, summed over the channels
    for (int i = 0; i < numSamples; ++i)
    {
        for (int ch = 0; ch < numCh; ++ch)
        {
            const double y = kHighPass[(size_t) ch].process (kShelf[(size_t) ch].process (buffer.getSample (ch, i)));
            subBlockSum += y * y;
        }

        if (++subBlockCount == subBlockLength)
            endLoudnessSubBlock();
    }
}

void MasteringProcessor::endLoudnessSubBlock()
{
    subBlockEnergy[(size_t) subBlockPos] = subBlockSum / subBlockCount;
    subBlockPos = (subBlockPos + 1) % numSubBlocks;
    subBlocksFilled = juce::jmin (subBlocksFilled + 1, numSubBlocks);
    subBlockSum = 0.0;
    subBlockCount = 0;

    auto toLufs = [] (double energy)
    {
        return energy > 0.0 ? (float) (-0.691 + 10.0 * std::log10 (energy)) : noLoudness;
    };
    auto meanOfLast = [this] (int n)
    {
        double sum = 0.0;
        for (int k = 1; k <= n; ++k)
            sum += subBlockEnergy[(size_t) ((subBlockPos - k + numSubBlocks) % numSubBlocks)];
        return sum / n;
    };

    if (subBlocksFilled >= 4)
    {
        // 400 ms block with 75 % overlap
        const double energy = meanOfLast (4);
        const float lufs = toLufs (energy);
        momentaryLufs.store (lufs);

        if (lufs > -70.0f)       // absolute gate
        {
            const int bin = juce::jlimit (0, histBins - 1, (int) ((lufs + 70.0f) * 10.0f));
            histEnergy[(size_t) bin] += energy;
            ++histCount[(size_t) bin];
        }
    }

    if (subBlocksFilled == numSubBlocks)
        shortTermLufs.store (toLufs (meanOfLast (numSubBlocks)));

    // Relative gate: drop the blocks more than 10 LU below the absolute-gated mean
    double totalEnergy = 0.0;
    int totalCount = 0;
    for (int b = 0; b < histBins; ++b)
    {
        totalEnergy += histEnergy[(size_t) b];
        totalCount  += histCount[(size_t) b];
    }

    if (totalCount == 0)
        return;

    const float relativeGate = toLufs (totalEnergy / totalCount) - 10.0f;
    const int firstBin = juce::jlimit (0, histBins, (int) std::ceil ((relativeGate + 70.0f) * 10.0f));

    double gatedEnergy = 0.0;
    int gatedCount = 0;
    for (int b = firstBin; b < histBins; ++b)
    {
        gatedEnergy += histEnergy[(size_t) b];
        gatedCount  += histCount[(size_t) b];
    }

    if (gatedCount > 0)
        integratedLufs.store (toLufs (gatedEnergy / gatedCount));
}

void MasteringProcessor::pushToAnalyser (const juce::AudioBuffer<float>& buffer)
{
    const auto* l = buffer.getReadPointer (0);
    const auto* r = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : l;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        fifo[(size_t) fifoIndex++] = 0.5f * (l[i] + r[i]);

        if (fifoIndex == fftSize)
        {
            if (! analyserReady.load())
            {
                analyserBlock = fifo;
                analyserReady.store (true);
            }

            // 50 % overlap: keep the second half for the next block
            std::copy (fifo.begin() + fftSize / 2, fifo.end(), fifo.begin());
            fifoIndex = fftSize / 2;
        }
    }
}

//==============================================================================
void MasteringProcessor::setParameters (std::initializer_list<std::pair<const char*, float>> values)
{
    for (const auto& [id, value] : values)
    {
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (value));
            p->endChangeGesture();
        }
    }
}

void MasteringProcessor::applyStyle (int index)
{
    // Tone: EQ, de-esser, stereo width, compressor speed.
    // Shelf gains are offsets added to a base: the analysis correction if there is one, else +1 dB of bass.
    struct Tone { float low, high, mid, midFreq, deess, width, attack, release; };
    static constexpr Tone tones[] = {
        { 1.5f, -1.0f, -1.0f, 2500.0f, 20.0f, 100.0f, 30.0f, 200.0f },   // Chaud : graves généreux, aigus adoucis, compression lente
        { 0.0f,  1.0f,  0.0f, 1000.0f, 15.0f, 105.0f, 20.0f, 150.0f },   // Équilibré : léger sourire, neutre
        { -1.0f, 2.5f,  1.0f, 3000.0f, 25.0f, 115.0f, 15.0f, 120.0f } }; // Ouvert : présence, brillance, plus large

    if (index < 0 || index >= (int) std::size (tones))
        return;

    const auto& t = tones[index];
    const auto& st = apvts.state;
    const bool analysed = st.getProperty ("anaValid", false);

    const float baseLow  = analysed ? (float) st.getProperty ("anaLow", 0.0f)  : 1.0f;
    const float baseHigh = analysed ? (float) st.getProperty ("anaHigh", 0.0f) : 0.0f;
    const float mid      = analysed ? (float) st.getProperty ("anaMid", 0.0f) : t.mid;
    const float midFreq  = analysed ? (float) st.getProperty ("anaMidFreq", 1000.0f) : t.midFreq;

    setParameters ({ { "lowGain", juce::jlimit (-6.0f, 6.0f, baseLow + t.low) }, { "midGain", mid }, { "midFreq", midFreq },
                     { "highGain", juce::jlimit (-6.0f, 6.0f, baseHigh + t.high) },
                     { "deessAmount", t.deess }, { "width", t.width },
                     { "compAttack", t.attack }, { "compRelease", t.release } });

    apvts.state.setProperty ("style", index, nullptr);
}

void MasteringProcessor::setAnalysisEq (const AnalysisEq& eq)
{
    apvts.state.setProperty ("anaValid", true, nullptr);
    apvts.state.setProperty ("anaLow", eq.low, nullptr);
    apvts.state.setProperty ("anaHigh", eq.high, nullptr);
    apvts.state.setProperty ("anaMid", eq.mid, nullptr);
    apvts.state.setProperty ("anaMidFreq", eq.midFreq, nullptr);
}

MasteringProcessor::AnalysisEq MasteringProcessor::computeAnalysisEq (const std::vector<double>& binPower, double sampleRate)
{
    // 1/3-octave band levels compared with a reference "balanced master" tilt of -3.5 dB/octave
    // (relative to 500 Hz - 2 kHz). Corrections are half the deviation, kept gentle.
    constexpr double slope = -3.5;
    const int lastBin = (int) binPower.size() - 1;
    const double binHz = sampleRate / (2.0 * lastBin);

    struct Band { double fc, dev; };
    std::vector<Band> bands;

    for (double fc = 31.5; fc <= 16000.0 && fc * 1.12 < sampleRate * 0.5; fc *= std::pow (2.0, 1.0 / 3.0))
    {
        const int b0 = juce::jlimit (1, lastBin, (int) std::ceil (fc / 1.1225 / binHz));
        const int b1 = juce::jlimit (b0, lastBin, (int) std::floor (fc * 1.1225 / binHz));

        double p = 0.0;
        for (int b = b0; b <= b1; ++b)
            p += binPower[(size_t) b];

        if (p > 0.0)
            bands.push_back ({ fc, 10.0 * std::log10 (p) - slope * std::log2 (fc / 1000.0) });
    }

    auto meanDev = [&] (double lo, double hi)
    {
        double sum = 0.0;
        int n = 0;
        for (const auto& b : bands)
            if (b.fc >= lo && b.fc <= hi) { sum += b.dev; ++n; }
        return n > 0 ? sum / n : 0.0;
    };

    const double ref = meanDev (500.0, 2000.0);
    const double lowDev   = meanDev (40.0, 160.0) - ref;
    const double mudDev   = meanDev (200.0, 400.0) - ref;
    const double harshDev = meanDev (2000.0, 5000.0) - ref;
    const double highDev  = meanDev (6300.0, 12500.0) - ref;

    AnalysisEq eq;
    eq.low  = juce::jlimit (-3.0f, 3.0f, (float) (-0.5 * lowDev));
    eq.high = juce::jlimit (-3.0f, 3.0f, (float) (-0.5 * highDev));

    // Mid band: fix the larger problem (mud or harshness) if it stands out by more than 1.5 dB
    const bool mudWorse = std::abs (mudDev) >= std::abs (harshDev);
    const double midDev = mudWorse ? mudDev : harshDev;
    if (std::abs (midDev) > 1.5)
    {
        eq.midFreq = mudWorse ? 300.0f : 3150.0f;
        eq.mid = juce::jlimit (-3.0f, 2.0f, (float) (-0.5 * midDev));
    }

    return eq;
}

void MasteringProcessor::applyIntensity (int index)
{
    // Level: compression amount, push into the limiter, ceiling
    switch (index)
    {
        case 0:  // Faible
            setParameters ({ { "compThresh", -10.0f }, { "compRatio", 1.5f }, { "compMakeup", 1.0f },
                             { "drive", 1.0f }, { "ceiling", -1.0f }, { "limRelease", 100.0f } });
            break;
        case 1:  // Moyenne
            setParameters ({ { "compThresh", -14.0f }, { "compRatio", 2.0f }, { "compMakeup", 2.0f },
                             { "drive", 3.0f }, { "ceiling", -1.0f }, { "limRelease", 80.0f } });
            break;
        case 2:  // Forte
            setParameters ({ { "compThresh", -18.0f }, { "compRatio", 3.0f }, { "compMakeup", 3.0f },
                             { "drive", 6.0f }, { "ceiling", -1.0f }, { "limRelease", 60.0f } });
            break;
        default:
            return;
    }

    apvts.state.setProperty ("intensity", index, nullptr);
}

//==============================================================================
juce::AudioProcessorEditor* MasteringProcessor::createEditor()
{
    return new MasteringEditor (*this);
}

void MasteringProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MasteringProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            // Gain Match is a listening aid: always off when a project is opened, so it is never exported by mistake
            for (auto* p : xml->getChildWithTagNameIterator ("PARAM"))
                if (p->getStringAttribute ("id") == "gainMatch")
                    p->setAttribute ("value", 0.0);

            apvts.replaceState (juce::ValueTree::fromXml (*xml));
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MasteringProcessor();
}
