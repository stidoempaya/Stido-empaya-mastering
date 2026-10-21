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
    juce::Rectangle<int> gridArea, meterArea;
    int rowH = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasteringEditor)
};
