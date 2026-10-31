#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine.h"

class TX81ZProcessor : public juce::AudioProcessor
{
public:
    TX81ZProcessor();
    ~TX81ZProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;

    juce::AudioProcessorEditor *createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "TX81Z Native"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String &) override {}

    void getStateInformation(juce::MemoryBlock &destData) override;
    void setStateInformation(const void *data, int sizeInBytes) override;

    // ---- UI-facing API (message thread) ----
    bool loadRomFile(const juce::File &f);          // also runs factory init if no memory exists yet
    void powerCycle();
    void factoryInit();
    bool hasRom() const { return romLoaded.load(); }
    juce::String getRomPath() const { return romPath; }
    void setKeyMask(uint32_t m) { keyMask.store(m); }
    void panic() { panicRequest.store(true); }   // handled on the audio thread at the next block

    struct LcdSnapshot { uint8_t ddram[128] {}; uint8_t cgram[64] {}; bool displayOn = true; };
    LcdSnapshot getLcd() { const juce::SpinLock::ScopedLockType l(lcdLock); return lcd; }

    juce::AudioProcessorValueTreeState apvts;

    // test hook
    Engine &getEngine() { return engine; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void startupRomSearch();
    void snapshotLcd();               // copy the firmware LCD for the editor (caller holds engineLock)
    bool installRom(const juce::MemoryBlock &mb, const juce::String &path, bool initIfNoNvram);

    Engine engine;
    juce::CriticalSection engineLock;
    std::atomic<bool> romLoaded { false };
    juce::String romPath;
    bool nvramValid = false;
    juce::MemoryBlock pendingNvram;   // restored from session state while no ROM was available
    bool pendingNvramValid = false;
    double hostRate = 44100.0;

    std::atomic<uint32_t> keyMask { 0 };
    std::atomic<bool> panicRequest { false };
    juce::SmoothedValue<float> gainSmooth;
    juce::SpinLock lcdLock;
    LcdSnapshot lcd;

    // pending sysex assembly for MIDI out
    std::vector<uint8_t> sysexOut;
    bool inSysex = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TX81ZProcessor)
};
