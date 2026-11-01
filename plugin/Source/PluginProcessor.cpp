#include "PluginProcessor.h"
#include "PluginEditor.h"

static const char *kStateMagic = "TX81ZN01";

juce::AudioProcessorValueTreeState::ParameterLayout TX81ZProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "gain", 1 }, "Output Gain",
          juce::NormalisableRange<float>(-24.0f, 36.0f, 0.1f), 12.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "midiThru", 1 }, "MIDI Thru", true));
    return l;
}

TX81ZProcessor::TX81ZProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "STATE", createLayout())
{
    engine.setHostRate(hostRate);
    startupRomSearch();
}

juce::File TX81ZProcessor::romsDirectory()
{
    auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile("Application Support");   // JUCE returns ~/Library on macOS
   #endif
    return base.getChildFile("AmateurToolsDSP").getChildFile("T3X4Z").getChildFile("roms");
}

static juce::File lastRomMarker() { return TX81ZProcessor::romsDirectory().getParentDirectory().getChildFile("last_rom.txt"); }

void TX81ZProcessor::rememberRom(const juce::File &f)
{
    const auto dir = romsDirectory();
    if (!dir.createDirectory().wasOk()) return;
    juce::File stored = dir.getChildFile(f.getFileName());
    if (f.getParentDirectory() != dir) {
        if (stored.existsAsFile() && !stored.hasIdenticalContentTo(f)) stored = stored.getNonexistentSibling();   // different ROM, same name
        if (!stored.existsAsFile() && !f.copyFileTo(stored)) return;
    }
    lastRomMarker().replaceWithText(stored.getFileName());
}

void TX81ZProcessor::startupRomSearch()
{
    // Order: TX81Z_ROM env var, the last ROM used, any ROM in the user's ROM folder, then the old pre-library location.
    juce::Array<juce::File> candidates;
    if (auto e = juce::SystemStats::getEnvironmentVariable("TX81Z_ROM", {}); e.isNotEmpty()) candidates.add(juce::File(e));
    if (auto last = lastRomMarker().loadFileAsString().trim(); last.isNotEmpty()) candidates.add(romsDirectory().getChildFile(last));
    for (auto &f : romsDirectory().findChildFiles(juce::File::findFiles, false, "*")) candidates.add(f);
    candidates.add(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("TX81ZNative/rom.ic15"));
    for (auto &f : candidates)
        if (f.existsAsFile() && f.getSize() == 65536) { loadRomFile(f); if (hasRom()) break; }
}

void TX81ZProcessor::snapshotLcd()
{
    const juce::SpinLock::ScopedLockType l(lcdLock);
    std::memcpy(lcd.ddram, engine.tx.lcd.ddram, sizeof lcd.ddram);
    std::memcpy(lcd.cgram, engine.tx.lcd.cgram, sizeof lcd.cgram);
    lcd.displayOn = engine.tx.lcd.display_on;
}

bool TX81ZProcessor::installRom(const juce::MemoryBlock &mb, const juce::String &path, bool initIfNoNvram)
{
    const juce::ScopedLock sl(engineLock);
    if (!engine.loadRom(mb.getData(), mb.getSize())) return false;
    romPath = path;
    romLoaded = true;
    if (pendingNvram.getSize() == Engine::NVRAM_SIZE) {
        std::memcpy(engine.nvramMut(), pendingNvram.getData(), Engine::NVRAM_SIZE);
        nvramValid = pendingNvramValid; pendingNvram.reset();
    }
    if (initIfNoNvram && !nvramValid) { engine.factoryInit(); nvramValid = true; }
    else engine.powerCycle();
    snapshotLcd();
    return true;
}

bool TX81ZProcessor::loadRomFile(const juce::File &f)
{
    juce::MemoryBlock mb;
    if (!f.loadFileAsData(mb) || mb.getSize() != 65536) return false;
    if (!installRom(mb, f.getFullPathName(), true)) return false;
    rememberRom(f);
    return true;
}

void TX81ZProcessor::powerCycle()
{
    const juce::ScopedLock sl(engineLock);
    if (romLoaded) { engine.powerCycle(); snapshotLcd(); }
}

void TX81ZProcessor::factoryInit()
{
    const juce::ScopedLock sl(engineLock);
    if (romLoaded) { engine.factoryInit(); nvramValid = true; snapshotLcd(); }
}

void TX81ZProcessor::prepareToPlay(double sampleRate, int)
{
    const juce::ScopedLock sl(engineLock);
    hostRate = sampleRate;
    engine.setHostRate(sampleRate);
    gainSmooth.reset(sampleRate, 0.03);
    gainSmooth.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(apvts.getRawParameterValue("gain")->load()));
}

bool TX81ZProcessor::isBusesLayoutSupported(const BusesLayout &layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}

void TX81ZProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();

    // MIDI thru on: incoming MIDI continues to the next plugin untouched. Off: this plugin consumes it.
    const bool thru = apvts.getRawParameterValue("midiThru")->load() > 0.5f;

    const juce::ScopedTryLock sl(engineLock);
    if (!sl.isLocked() || !romLoaded.load()) { if (!thru) midi.clear(); return; }

    if (panicRequest.exchange(false)) engine.panic();

    engine.applyKeys(keyMask.load());

    float *L = buffer.getWritePointer(0);
    float *R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    static thread_local std::vector<float> tmpR;
    if (!R) { tmpR.resize((size_t)n); R = tmpR.data(); }

    int pos = 0;
    for (const auto meta : midi) {
        int at = juce::jlimit(pos, n, meta.samplePosition);
        if (at > pos) { engine.render(L + pos, R + pos, at - pos); pos = at; }
        auto m = meta.getMessage();
        engine.feedMidi(m.getRawData(), m.getRawDataSize());
    }
    if (pos < n) engine.render(L + pos, R + pos, n - pos);

    // output gain
    gainSmooth.setTargetValue(juce::Decibels::decibelsToGain(apvts.getRawParameterValue("gain")->load()));
    for (int i = 0; i < n; i++) { float g = gainSmooth.getNextValue(); L[i] *= g; R[i] *= g; }
    if (buffer.getNumChannels() == 1) { /* mono: L already holds left channel */ }

    // snapshot LCD for the editor
    {
        const juce::SpinLock::ScopedTryLockType l(lcdLock);
        if (l.isLocked()) {
            std::memcpy(lcd.ddram, engine.tx.lcd.ddram, sizeof lcd.ddram);
            std::memcpy(lcd.cgram, engine.tx.lcd.cgram, sizeof lcd.cgram);
            lcd.displayOn = engine.tx.lcd.display_on;
        }
    }

    // MIDI out: with thru on, the incoming MIDI stays in the buffer (so other synths can be chained after this one);
    // with thru off it is consumed. Complete SysEx messages produced by the firmware (bulk dumps) are added either way.
    if (!thru) midi.clear();
    for (uint8_t b : engine.tx.midi_out) {
        if (b == 0xf0) { inSysex = true; sysexOut.clear(); }
        if (inSysex) {
            sysexOut.push_back(b);
            if (b == 0xf7) { inSysex = false; midi.addEvent(juce::MidiMessage::createSysExMessage(sysexOut.data() + 1, (int)sysexOut.size() - 2), n - 1); }
        }
    }
    engine.tx.midi_out.clear();
}

juce::AudioProcessorEditor *TX81ZProcessor::createEditor() { return new TX81ZEditor(*this); }

void TX81ZProcessor::getStateInformation(juce::MemoryBlock &dest)
{
    juce::MemoryOutputStream out(dest, false);
    out.write(kStateMagic, 8);
    out.writeString(romPath);
    out.writeBool(nvramValid);
    {
        const juce::ScopedLock sl(engineLock);
        out.writeInt((int)Engine::NVRAM_SIZE);
        out.write(engine.nvram(), Engine::NVRAM_SIZE);
    }
    if (auto xml = apvts.copyState().createXml()) out.writeString(xml->toString());
}

void TX81ZProcessor::setStateInformation(const void *data, int size)
{
    juce::MemoryInputStream in(data, (size_t)size, false);
    char magic[8] {};
    if (in.read(magic, 8) != 8 || std::memcmp(magic, kStateMagic, 8) != 0) return;
    auto path = in.readString();
    bool valid = in.readBool();
    int nv = in.readInt();
    juce::MemoryBlock nvram;
    if (nv == (int)Engine::NVRAM_SIZE) { nvram.setSize((size_t)nv); in.read(nvram.getData(), nv); } else return;
    auto xmlText = in.readString();

    if (auto xml = juce::XmlDocument::parse(xmlText)) apvts.replaceState(juce::ValueTree::fromXml(*xml));

    juce::File f(path);
    if (!f.existsAsFile()) {   // project opened on another machine or OS: look for the same file name in our ROM folder
        const auto name = path.replaceCharacter('\\', '/').fromLastOccurrenceOf("/", false, false);
        if (name.isNotEmpty()) f = romsDirectory().getChildFile(name);
    }
    juce::MemoryBlock rom;
    const juce::ScopedLock sl(engineLock);
    if (f.existsAsFile() && f.loadFileAsData(rom) && rom.getSize() == 65536) {
        engine.loadRom(rom.getData(), rom.getSize());
        romPath = f.getFullPathName(); romLoaded = true;
    }
    if (romLoaded) {
        std::memcpy(engine.nvramMut(), nvram.getData(), Engine::NVRAM_SIZE);
        nvramValid = valid;
        engine.powerCycle();               // boot the firmware on the restored memory
        snapshotLcd();
    } else {
        pendingNvram = nvram; pendingNvramValid = valid;   // applied when a ROM is loaded
    }
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new TX81ZProcessor(); }