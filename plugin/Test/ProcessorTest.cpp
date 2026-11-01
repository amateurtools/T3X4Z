// Headless test: drives TX81ZProcessor like a host would (no audio device, no GUI).
//   ProcessorTest <rom.ic15> [out.wav]
#include "../Source/PluginProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <cstdio>
#include <chrono>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } \
                              else { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

// Goertzel magnitude at a given frequency (windowed)
static double energyAt(const std::vector<float> &x, double sr, double f)
{
    const double w = 2.0 * 3.14159265358979323846 * f / sr, c = 2.0 * std::cos(w);
    double s1 = 0, s2 = 0; const size_t n = x.size();
    for (size_t i = 0; i < n; i++) {
        double win = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * (double)i / (double)(n - 1));
        double s0 = win * x[i] + c * s1 - s2; s2 = s1; s1 = s0;
    }
    return std::sqrt(s1 * s1 + s2 * s2 - c * s1 * s2);
}

// Render `seconds` of audio in host-sized blocks, injecting `events` (time-sorted) as MIDI.
struct Ev { double t; std::vector<uint8_t> bytes; };
static std::vector<float> render(TX81ZProcessor &p, double sr, int block, double seconds, const std::vector<Ev> &evs)
{
    std::vector<float> out;
    juce::AudioBuffer<float> buf(2, block);
    size_t ei = 0; long long done = 0; long long total = (long long)(seconds * sr);
    while (done < total) {
        juce::MidiBuffer mb;
        for (; ei < evs.size() && (long long)(evs[ei].t * sr) < done + block; ei++) {
            int pos = juce::jmax(0, (int)((long long)(evs[ei].t * sr) - done));
            mb.addEvent(evs[ei].bytes.data(), (int)evs[ei].bytes.size(), pos);
        }
        buf.clear();
        p.processBlock(buf, mb);
        for (int i = 0; i < block; i++) out.push_back(buf.getSample(0, i));
        done += block;
    }
    return out;
}

static float peakOf(const std::vector<float> &v, size_t a = 0, size_t b = (size_t)-1)
{
    float pk = 0; b = std::min(b, v.size());
    for (size_t i = a; i < b; i++) pk = std::max(pk, std::abs(v[i]));
    return pk;
}

static void writeWav(const char *path, const std::vector<float> &v, int sr)
{
    juce::File f(path); f.deleteFile();
    juce::WavAudioFormat wav; std::unique_ptr<juce::FileOutputStream> os(f.createOutputStream());
    if (!os) return;
    std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(os.get(), sr, 1, 16, {}, 0));
    if (!w) return; os.release();
    juce::AudioBuffer<float> b(1, (int)v.size()); b.copyFrom(0, 0, v.data(), (int)v.size());
    w->writeFromAudioSampleBuffer(b, 0, (int)v.size());
}

int main(int argc, char **argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::printf("usage: ProcessorTest rom.ic15 [out.wav]\n"); return 2; }

   #if JUCE_LINUX
    // ROM library: isolate with a throw-away HOME, load a ROM, then check a brand-new instance finds it on its own
    {
        auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("t3x4z_home_test");
        tmp.deleteRecursively(); tmp.createDirectory();
        setenv("HOME", tmp.getFullPathName().toRawUTF8(), 1); unsetenv("TX81Z_ROM");
        std::printf("info: ROM folder on this OS = %s\n", TX81ZProcessor::romsDirectory().getFullPathName().toRawUTF8());
        { TX81ZProcessor a; CHECK(!a.hasRom(), "fresh HOME: no ROM found automatically");
          CHECK(a.loadRomFile(juce::File(argv[1])), "load ROM from an arbitrary location");
          CHECK(TX81ZProcessor::romsDirectory().getChildFile(juce::File(argv[1]).getFileName()).existsAsFile(), "ROM was copied into the ROM folder"); }
        { TX81ZProcessor b; CHECK(b.hasRom(), "new instance loads the ROM from the ROM folder without any clicking"); }
        tmp.deleteRecursively();
    }
   #endif

    const double sr = 48000.0; const int block = 256;
    TX81ZProcessor p;
    p.setPlayConfigDetails(0, 2, sr, block);
    p.prepareToPlay(sr, block);

    std::printf("info: startup ROM search %s a ROM\n", p.hasRom() ? "found" : "found no");
    CHECK(p.loadRomFile(juce::File(argv[1])), "ROM loads (factory init runs automatically)");
    CHECK(p.hasRom(), "hasRom() true after load");

    // LCD should show the play screen after init
    auto lcdText = [&]() {
        auto s = p.getLcd(); juce::String t;
        for (int r = 0; r < 2; r++) { for (int c = 0; c < 16; c++) { uint8_t ch = s.ddram[(r ? 0x40 : 0) + c]; t += (ch >= 32 && ch < 127) ? juce::String::charToString((juce::juce_wchar)ch) : "?"; } t += "|"; }
        return t;
    };
    // run one block so the processor snapshots the LCD
    render(p, sr, block, 0.1, {});
    auto lcd = lcdText();
    std::printf("LCD: %s\n", lcd.toRawUTF8());
    CHECK(lcd.startsWith("PLAY SINGLE"), "LCD shows PLAY SINGLE");

    // note C4 for 1.2 s then off
    std::vector<Ev> evs = { { 0.20, { 0x90, 60, 100 } }, { 1.40, { 0x80, 60, 0 } } };
    auto t0 = std::chrono::steady_clock::now();
    auto audio = render(p, sr, block, 3.0, evs);
    double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("perf: 3.0 s of audio (256-sample blocks, 48 kHz, one voice) took %.1f ms = %.2f%% of one core\n", wall * 1000.0, wall / 3.0 * 100.0);
    float before = peakOf(audio, 0, (size_t)(0.2 * sr));
    float during = peakOf(audio, (size_t)(0.35 * sr), (size_t)(1.4 * sr));
    std::printf("peak before note %.5f, during %.5f\n", before, during);
    CHECK(before < 0.001f, "silent before note-on");
    CHECK(during > 0.02f, "audible during note (peak %.4f, gain +12 dB default)", during);
    CHECK(during < 1.0f, "no clipping at default gain");

    std::vector<float> seg(audio.begin() + (long)(0.4 * sr), audio.begin() + (long)(1.2 * sr));
    // The patch has a strong sub-octave partial, so compare energy at C4 against its neighbouring semitones
    const double eC = energyAt(seg, sr, 261.63), eLow = energyAt(seg, sr, 246.94), eHigh = energyAt(seg, sr, 277.18);
    std::printf("energy @C4 %.1f   @B3 %.1f   @C#4 %.1f\n", eC, eLow, eHigh);
    CHECK(eC > 4.0 * eLow && eC > 4.0 * eHigh, "spectrum peaks at C4 (261.6 Hz), not a neighbouring semitone");

    // state round trip: save -> new instance -> restore -> still plays
    juce::MemoryBlock state; p.getStateInformation(state);
    CHECK(state.getSize() > 8192, "state contains NVRAM (%zu bytes)", state.getSize());
    {
        TX81ZProcessor q; q.setPlayConfigDetails(0, 2, sr, block); q.prepareToPlay(sr, block);
        q.setStateInformation(state.getData(), (int)state.getSize());
        CHECK(q.hasRom(), "restored instance finds the ROM from saved path");
        auto a2 = render(q, sr, block, 2.0, { { 0.2, { 0x90, 64, 100 } } });
        float pk = peakOf(a2, (size_t)(0.4 * sr), (size_t)(2.0 * sr));
        CHECK(pk > 0.02f, "restored instance plays (peak %.4f)", pk);
        render(q, sr, block, 0.1, {});
        auto s2 = q.getLcd(); juce::String t;
        for (int c = 0; c < 16; c++) { uint8_t ch = s2.ddram[c]; t += (ch >= 32 && ch < 127) ? juce::String::charToString((juce::juce_wchar)ch) : "?"; }
        CHECK(t.startsWith("PLAY SINGLE"), "restored instance boots to PLAY SINGLE without re-running factory init");
    }

    // MIDI thru: on (default) leaves incoming MIDI in the buffer, off consumes it
    {
        TX81ZProcessor q; q.setPlayConfigDetails(0, 2, sr, block); q.prepareToPlay(sr, block);
        q.setStateInformation(state.getData(), (int)state.getSize());
        juce::AudioBuffer<float> b(2, block);
        auto run = [&](bool thru) {
            q.apvts.getParameter("midiThru")->setValueNotifyingHost(thru ? 1.0f : 0.0f);
            juce::MidiBuffer mb; mb.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 10);
            q.processBlock(b, mb); for (auto m : mb) std::printf("   out event @%d: %s\n", m.samplePosition, m.getMessage().getDescription().toRawUTF8()); return mb.getNumEvents();
        };
        int nOn = run(true); CHECK(nOn == 1,  "MIDI thru ON: incoming note passes through (events out = %d, want 1)", nOn);
        CHECK(run(false) == 0, "MIDI thru OFF: plugin consumes MIDI (events out = 0)");
    }

    // panic: hold a note, hit panic, sound dies and wheels return to zero
    {
        TX81ZProcessor q; q.setPlayConfigDetails(0, 2, sr, block); q.prepareToPlay(sr, block);
        q.setStateInformation(state.getData(), (int)state.getSize());
        auto a = render(q, sr, block, 1.0, { { 0.1, { 0x90, 60, 100 } }, { 0.3, { 0xE0, 0x7f, 0x7f } }, { 0.35, { 0xB0, 1, 127 } } });
        float held = peakOf(a, (size_t)(0.6 * sr), (size_t)(1.0 * sr));
        q.panic();
        auto a2 = render(q, sr, block, 2.0, {});
        float tail = peakOf(a2, (size_t)(1.5 * sr), (size_t)(2.0 * sr));
        std::printf("panic: held-note peak %.4f, peak 1.5-2.0 s after panic %.5f\n", held, tail);
        CHECK(held > 0.02f, "note is sounding before panic");
        CHECK(tail < 0.001f, "panic silences the held note");
        // a new note afterwards plays at the right pitch (pitch bend reset to centre)
        auto a3 = render(q, sr, block, 1.5, { { 0.1, { 0x90, 60, 100 } } });
        std::vector<float> seg3(a3.begin() + (long)(0.4 * sr), a3.begin() + (long)(1.2 * sr));
        const double c = energyAt(seg3, sr, 261.63), hi = energyAt(seg3, sr, 277.18);
        CHECK(c > 4.0 * hi, "after panic the pitch wheel is back at centre (C4 stays C4)");
    }

    // odd host settings
    for (double rate : { 44100.0, 96000.0 }) {
        TX81ZProcessor q; q.setPlayConfigDetails(0, 2, rate, 64); q.prepareToPlay(rate, 64);
        q.setStateInformation(state.getData(), (int)state.getSize());
        auto a3 = render(q, rate, 64, 1.5, { { 0.1, { 0x90, 60, 100 } } });
        CHECK(peakOf(a3, (size_t)(0.3 * rate), (size_t)(1.5 * rate)) > 0.02f, "plays at %.0f Hz with 64-sample blocks", rate);
    }

    if (argc > 3) {   // optional: render the editor to a PNG (ProcessorTest rom out.wav out.png)
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        ed->setBounds(0, 0, ed->getWidth(), ed->getHeight());
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), false, 1.0f);
        juce::File f(argv[3]); f.deleteFile(); juce::FileOutputStream os(f);
        if (os.openedOk()) juce::PNGImageFormat().writeImageToStream(img, os);
    }
    if (argc > 2) writeWav(argv[2], audio, (int)sr);
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}