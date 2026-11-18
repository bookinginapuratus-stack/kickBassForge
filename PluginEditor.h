#pragma once
#include "PluginProcessor.h"

class KickBassEditor : public juce::AudioProcessorEditor
{
public:
    explicit KickBassEditor(KickBassProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider s; juce::Label l;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> a;
    };
    KickBassProcessor& proc;
    std::vector<std::unique_ptr<Knob>> knobs;
    juce::TextButton loadBtn{ "Carregar analise (.json)" }, varBtn{ "Gerar variacao" };
    std::unique_ptr<juce::FileChooser> chooser;
};
