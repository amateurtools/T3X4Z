#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// Look-and-feel that draws all GUI text in the embedded Comic Neue typeface (JUCE 8 resolves plain fonts through the
// global default look-and-feel, so we pass the typeface explicitly instead of touching global state).
class ComicLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ComicLookAndFeel();

    juce::Font make(float height) const { return juce::Font(juce::FontOptions(typeface).withHeight(height)); }
    juce::Font getTextButtonFont(juce::TextButton &, int buttonHeight) override { return make(juce::jmin(16.0f, (float) buttonHeight * 0.6f)); }
    juce::Font getLabelFont(juce::Label &l) override { return make(l.getFont().getHeight()); }

    void drawToggleButton(juce::Graphics &g, juce::ToggleButton &button, bool highlighted, bool down) override
    {
        const float fontSize = juce::jmin(15.0f, (float) button.getHeight() * 0.75f);
        const float tickWidth = fontSize * 1.1f;
        drawTickBox(g, button, 4.0f, ((float) button.getHeight() - tickWidth) * 0.5f, tickWidth, tickWidth,
                    button.getToggleState(), button.isEnabled(), highlighted, down);
        g.setColour(button.findColour(juce::ToggleButton::textColourId));
        g.setFont(make(fontSize));
        if (!button.isEnabled()) g.setOpacity(0.5f);
        g.drawFittedText(button.getButtonText(),
                         button.getLocalBounds().withTrimmedLeft(juce::roundToInt(tickWidth) + 10).withTrimmedRight(2),
                         juce::Justification::centredLeft, 10);
    }

private:
    juce::Typeface::Ptr typeface;
};

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
    ComicLookAndFeel lnf;              // draws all GUI text in the embedded Comic Neue
    LcdComponent lcd;
    juce::Label brand;                 // red "T3X4Z" in the empty corner of the key grid
    juce::TextButton loadRom { "Load ROM..." }, powerCycleBtn { "Power cycle" }, factoryBtn { "Factory init" };
    juce::TextButton keyBtns[Engine::NumKeys];
    juce::TextButton panicBtn { "Panic" };
    juce::ToggleButton midiThruBtn { "MIDI thru" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> thruAttach;
    juce::Slider gain;
    juce::Label gainLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttach;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TX81ZEditor)
};