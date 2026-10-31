#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class LcdComponent : public juce::Component
{
public:
    void setSnapshot(const TX81ZProcessor::LcdSnapshot &s) { snap = s; repaint(); }
    void paint(juce::Graphics &g) override;
private:
    TX81ZProcessor::LcdSnapshot snap;
};

class TX81ZEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit TX81ZEditor(TX81ZProcessor &p);
    ~TX81ZEditor() override;
    void paint(juce::Graphics &) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateKeys();
    void chooseRom();

    TX81ZProcessor &proc;
    LcdComponent lcd;
    juce::Label status;
    juce::TextButton loadRom { "Load ROM..." }, powerCycleBtn { "Power cycle" }, factoryBtn { "Factory init" };
    juce::TextButton keyBtns[Engine::NumKeys];
    juce::Slider gain;
    juce::Label gainLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttach;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TX81ZEditor)
};
