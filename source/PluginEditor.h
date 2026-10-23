#pragma once

#include "PluginProcessor.h"

class MasteringEditor : public juce::AudioProcessorEditor,
                        private juce::Timer
{
public:
    explicit MasteringEditor (MasteringProcessor&);
    ~MasteringEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void addKnob (const juce::String& paramId, const juce::String& name);
    void updateSpectrum();
    void drawSpectrum (juce::Graphics&);

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attach;
    };

    MasteringProcessor& proc;
    std::vector<std::unique_ptr<Knob>> knobs;

    juce::ToggleButton bypass { "Bypass" };
    std::unique_ptr<ButtonAttachment> bypassAttach;

    float dbL = -100.f, dbR = -100.f;
    juce::Rectangle<int> gridArea, meterArea, spectrumArea;

    // Spectrum analyser
    static constexpr int fftSize = MasteringProcessor::fftSize;
    static constexpr int numPoints = 240;                 // log-spaced points, 20 Hz .. 20 kHz
    juce::dsp::FFT fft { MasteringProcessor::fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, 2 * fftSize> fftData {};
    std::array<float, numPoints> targetDb {}, spectrumDb {};
    int rowH = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasteringEditor)
};
