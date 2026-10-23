#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

// Platform profile: integrated loudness target and true peak ceiling
struct LoudnessTarget { const char* name; float lufs, truePeak; };

class MasteringProcessor : public juce::AudioProcessor
{
public:
    MasteringProcessor();
    ~MasteringProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "STIDO Empaya Mastering"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Peak meters (linear), read and reset by the editor
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };

    // EQ settings shared with the editor (EQ curve drawing)
    static constexpr float lowShelfFreq = 100.0f, highShelfFreq = 8000.0f, shelfQ = 0.707f, midQ = 0.8f;

    // Spectrum analyser: the audio thread fills analyserBlock (mono sum of the output)
    // when analyserReady is false; the editor reads it, then sets the flag back to false.
    static constexpr int fftOrder = 11, fftSize = 1 << fftOrder;
    std::array<float, fftSize> analyserBlock {};
    std::atomic<bool> analyserReady { false };

    // Loudness (ITU-R BS.1770 / EBU R128), measured on the output. "No value" = below -70.
    static constexpr LoudnessTarget loudnessTargets[] = {
        { "Apple Music", -16.0f, -1.0f }, { "Spotify", -14.0f, -1.0f }, { "YouTube", -14.0f, -1.0f },
        { "Deezer", -15.0f, -1.0f }, { "Tidal", -14.0f, -1.0f }, { "Amazon Music", -14.0f, -2.0f } };
    static constexpr float noLoudness = -100.0f;

    std::atomic<float> momentaryLufs { noLoudness }, shortTermLufs { noLoudness },
                       integratedLufs { noLoudness }, truePeakDb { noLoudness };
    std::atomic<bool> loudnessResetRequested { false };

    // Styles and intensities: one click sets several parameters (message thread only).
    // The last choice is kept in apvts.state ("style" / "intensity", -1 = none) for display.
    static constexpr const char* styleNames[]     = { "Chaud", "Équilibré", "Ouvert" };
    static constexpr const char* intensityNames[] = { "Faible", "Moyenne", "Forte" };
    void applyStyle (int index);
    void applyIntensity (int index);

    // "Analyser": EQ correction computed from the average input spectrum (power per FFT bin).
    // Once stored, the styles are applied on top of it.
    struct AnalysisEq { float low = 0.0f, high = 0.0f, mid = 0.0f, midFreq = 1000.0f; };
    static AnalysisEq computeAnalysisEq (const std::vector<double>& binPower, double sampleRate);
    void setAnalysisEq (const AnalysisEq&);
    void setParameter (const char* id, float value) { setParameters ({ { id, value } }); }
    float getParameterValue (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    // While true, the spectrum analyser receives the input (before processing) instead of the output
    std::atomic<bool> analyseInput { false };

    // De-esser gain reduction (dB, positive) and gain match correction (dB), for display
    std::atomic<float> deessReductionDb { 0.0f }, gainMatchDb { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void updateDSP();
    void pushToAnalyser (const juce::AudioBuffer<float>&);
    void measureLoudness (juce::AudioBuffer<float>&);
    void endLoudnessSubBlock();
    void resetLoudness();
    void processDeEsser (juce::AudioBuffer<float>&);
    void setParameters (std::initializer_list<std::pair<const char*, float>>);

    using Filter  = juce::dsp::IIR::Filter<float>;
    using Coeffs  = juce::dsp::IIR::Coefficients<float>;
    using Duplex  = juce::dsp::ProcessorDuplicator<Filter, Coeffs>;

    // Chain: input -> EQ -> compressor -> makeup -> width -> drive -> limiter -> output
    juce::dsp::Gain<float> inGain, makeup, drive, outGain, normGain;
    Duplex lowShelf, midPeak, highShelf;
    juce::dsp::Compressor<float> compressor;
    juce::dsp::Limiter<float> limiter;

    struct Params
    {
        std::atomic<float> *inGain, *lowGain, *midGain, *midFreq, *highGain,
                           *compThresh, *compRatio, *compAttack, *compRelease, *compMakeup,
                           *width, *drive, *ceiling, *limRelease, *outGain, *bypass,
                           *deessAmount, *deessFreq, *gainMatch, *normGain;
    } prm {};

    struct Cache { float low = 1e9f, mid = 1e9f, midFreq = 1e9f, high = 1e9f; } cache;

    std::array<float, fftSize> fifo {};
    int fifoIndex = 0;

    // K-weighting: high shelf (+4 dB) then high-pass (~38 Hz), one pair per channel
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

        double process (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };
    std::array<Biquad, 2> kShelf, kHighPass;
    Biquad kShelfProto, kHighPassProto;

    // 100 ms sub-blocks: 4 make a 400 ms (momentary) block, 30 make 3 s (short-term)
    static constexpr int numSubBlocks = 30;
    std::array<double, numSubBlocks> subBlockEnergy {};
    int subBlockPos = 0, subBlocksFilled = 0, subBlockLength = 4800, subBlockCount = 0;
    double subBlockSum = 0.0;

    // Gated integrated loudness: histogram of 400 ms blocks, 0.1 LU bins from -70 to +30 LUFS
    static constexpr int histBins = 1000;
    std::array<double, histBins> histEnergy {};
    std::array<int, histBins> histCount {};

    juce::dsp::Oversampling<float> truePeakOversampler { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, false };
    int maxBlockSize = 0;

    // De-esser: the band above deessFreq is turned down when it gets too close to the full-band level
    juce::dsp::LinkwitzRileyFilter<float> deessLow, deessHigh;
    float deessEnvHigh = 0.0f, deessEnvFull = 0.0f, deessAttack = 0.0f, deessRelease = 0.0f;
    bool deessActive = false;

    // Gain match: output turned down/up to the input loudness (K-weighted, ~3 s average)
    std::array<Biquad, 2> gmInShelf, gmInHighPass, gmOutShelf, gmOutHighPass;
    double gmInMs = 0.0, gmOutMs = 0.0, gmCoef = 0.0;
    juce::SmoothedValue<float> gainMatchGain { 1.0f };
    double averageKWeighted (const juce::AudioBuffer<float>&, std::array<Biquad, 2>& shelf,
                             std::array<Biquad, 2>& highPass, double meanSquare) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasteringProcessor)
};
