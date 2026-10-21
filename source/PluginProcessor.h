#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

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

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void updateDSP();

    using Filter  = juce::dsp::IIR::Filter<float>;
    using Coeffs  = juce::dsp::IIR::Coefficients<float>;
    using Duplex  = juce::dsp::ProcessorDuplicator<Filter, Coeffs>;

    // Chain: input -> EQ -> compressor -> makeup -> width -> drive -> limiter -> output
    juce::dsp::Gain<float> inGain, makeup, drive, outGain;
    Duplex lowShelf, midPeak, highShelf;
    juce::dsp::Compressor<float> compressor;
    juce::dsp::Limiter<float> limiter;

    struct Params
    {
        std::atomic<float> *inGain, *lowGain, *midGain, *midFreq, *highGain,
                           *compThresh, *compRatio, *compAttack, *compRelease, *compMakeup,
                           *width, *drive, *ceiling, *limRelease, *outGain, *bypass;
    } prm {};

    struct Cache { float low = 1e9f, mid = 1e9f, midFreq = 1e9f, high = 1e9f; } cache;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasteringProcessor)
};
