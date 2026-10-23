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
            get ("limRelease"), get ("outGain"), get ("bypass") };
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

    for (auto* g : { &inGain, &makeup, &drive, &outGain })
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

        kShelf.fill (shelf);
        kHighPass.fill (highPass);
    }

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

    if (prm.bypass->load() < 0.5f)
    {
        updateDSP();

        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> ctx (block);

        inGain.process (ctx);
        lowShelf.process (ctx);
        midPeak.process (ctx);
        highShelf.process (ctx);
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
    }

    // Peak metering (keep the max until the editor reads it)
    const float mL = buffer.getMagnitude (0, 0, numSamples);
    const float mR = buffer.getNumChannels() > 1 ? buffer.getMagnitude (1, 0, numSamples) : mL;
    if (mL > peakL.load()) peakL.store (mL);
    if (mR > peakR.load()) peakR.store (mR);

    pushToAnalyser (buffer);
    measureLoudness (buffer);
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
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MasteringProcessor();
}
