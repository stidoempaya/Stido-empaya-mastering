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
    void drawLoudness (juce::Graphics&);

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attach;
    };

    MasteringProcessor& proc;
    std::vector<std::unique_ptr<Knob>> knobs;

    juce::ToggleButton bypass { "Bypass" }, gainMatch { "Gain Match" };
    std::unique_ptr<ButtonAttachment> bypassAttach, gainMatchAttach;
    float deessGr = 0.0f, matchDb = 0.0f;
    juce::TooltipWindow tooltips { this };

    juce::ComboBox targetBox;
    std::unique_ptr<ComboAttachment> targetAttach;
    juce::TextButton resetLoudness;

    // Style / intensity shortcuts
    std::array<juce::TextButton, 3> styleButtons, intensityButtons;
    juce::Rectangle<int> presetArea;
    void refreshPresetButtons();
    float lufsM = MasteringProcessor::noLoudness, lufsS = MasteringProcessor::noLoudness,
          lufsI = MasteringProcessor::noLoudness, truePeak = MasteringProcessor::noLoudness;

    float dbL = -100.f, dbR = -100.f;
    juce::Rectangle<int> gridArea, meterArea, spectrumArea, loudnessArea, gainMatchInfo;

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
