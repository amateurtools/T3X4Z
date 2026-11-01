// Headless boot test: runs a TX81Z ROM, prints LCD changes, counts traps, writes a WAV of the output.
#include "tx81z.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void wav(const char *path, const std::vector<float> &s, int rate)
{
    FILE *f = std::fopen(path, "wb"); if (!f) return;
    uint32_t n = (uint32_t)(s.size() * 2), r = rate, br = rate * 4; uint16_t ch = 2, bits = 16, ba = 4, fmt = 1;
    uint32_t sz = 36 + n, f16 = 16;
    std::fwrite("RIFF", 1, 4, f); std::fwrite(&sz, 4, 1, f); std::fwrite("WAVEfmt ", 1, 8, f);
    std::fwrite(&f16, 4, 1, f); std::fwrite(&fmt, 2, 1, f); std::fwrite(&ch, 2, 1, f);
    std::fwrite(&r, 4, 1, f); std::fwrite(&br, 4, 1, f); std::fwrite(&ba, 2, 1, f); std::fwrite(&bits, 2, 1, f);
    std::fwrite("data", 1, 4, f); std::fwrite(&n, 4, 1, f);
    for (float v : s) { int x = (int)(v * 32767.f); if (x > 32767) x = 32767; if (x < -32768) x = -32768; int16_t q = (int16_t)x; std::fwrite(&q, 2, 1, f); }
    std::fclose(f);
}

static uint32_t *pc_hits;
struct Dbg : Tx81z {
    int n[3] = {0,0,0};
    void log(int i, uint8_t v) { if (!getenv("PORTLOG")) return; if (cpu.total_cycles > 12000000ull && n[i]++ < 6) std::printf("  p%s read pc=%04x t=%.2fs v=%02x ddr6=%02x\n", i==0?"2":i==1?"5":"6", cpu.PC, cpu.total_cycles/Tx81z::E_HZ, v, cpu.p6ddr); }
};
int main(int argc, char **argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: boot_test rom.ic15 [seconds] [out.wav] [-t]\n"); return 1; }
    double secs = argc > 2 ? std::atof(argv[2]) : 3.0;
    Dbg m;
    if (!m.load_rom(argv[1])) { std::fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }
    for (int i = 3; i < argc; i++) if (!std::strcmp(argv[i], "-t")) m.trace_opz = true;
    if (getenv("NVFILL")) m.nvram_fill = (uint8_t)strtol(getenv("NVFILL"), 0, 16);
    static uint32_t pc_hits_arr[65536]; pc_hits = pc_hits_arr; m.cpu.pc_hits = pc_hits_arr;
    m.reset();
    std::printf("reset PC=%04x\n", m.cpu.PC);

    std::vector<float> audio;
    std::string last0, last1;
    int total = (int)(secs * Tx81z::SAMPLE_RATE);
    int chunk = (int)(Tx81z::SAMPLE_RATE / 20); // 50 ms
    bool midi = false; for (int i = 3; i < argc; i++) if (!std::strcmp(argv[i], "-m")) midi = true;
    bool cycled = false; double cyc_t = 0; bool sent = false; unsigned long long opz_at_midi = 0;
    for (int done = 0; done < total; done += chunk) {
        { double t = done / Tx81z::SAMPLE_RATE; if (cycled) { t -= cyc_t; } if (getenv("INIT")) m.kp6 = (t < atof(getenv("INIT"))) ? 0x0c : 0x0f;
          if (getenv("TESTENTRY") && !cycled) { double te = atof(getenv("TESTENTRY")); m.kp2 = (t < te) ? 0x3a : 0xfa; double ip = te + 1.0; m.kp5 = (t >= ip && t < ip + 0.3) ? 0xef : 0xff; }
          if (getenv("PRESS")) { double pt = atof(getenv("PRESS")); m.kp6 = (t >= pt && t < pt + 0.3) ? 0x0e : m.kp6; } }
        if (getenv("POWERCYCLE") && !cycled && done / Tx81z::SAMPLE_RATE >= atof(getenv("POWERCYCLE"))) { cycled = true; cyc_t = done / Tx81z::SAMPLE_RATE; m.kp2 = 0xfa; m.kp5 = 0xff; m.kp6 = 0x0f; m.reset(false); std::printf("[power cycle, NVRAM kept]\n"); }
        if (getenv("MELODY") && cycled) {
            static const struct { double t; uint8_t b[3]; } ev[] = {
                {0.0,{0x90,60,100}},{0.5,{0x80,60,0}},{0.55,{0x90,64,100}},{1.05,{0x80,64,0}},{1.1,{0x90,67,100}},{1.6,{0x80,67,0}},
                {1.7,{0x90,48,110}},{1.7,{0x90,60,100}},{1.7,{0x90,64,100}},{1.7,{0x90,67,100}},{3.6,{0x80,48,0}},{3.6,{0x80,60,0}},{3.6,{0x80,64,0}},{3.6,{0x80,67,0}} };
            static size_t next = 0; double rt = done / Tx81z::SAMPLE_RATE - cyc_t - (getenv("NOTEAT") ? atof(getenv("NOTEAT")) : 8.0);
            while (next < sizeof ev / sizeof ev[0] && rt >= ev[next].t) { for (int k = 0; k < 3; k++) m.midi_in(ev[next].b[k]); next++; }
        }
        if (midi && !sent && (done / Tx81z::SAMPLE_RATE - cyc_t) >= (getenv("NOTEAT") ? atof(getenv("NOTEAT")) : 8.0)) { m.midi_in(0x90); m.midi_in(0x3c); m.midi_in(0x64); sent = true; opz_at_midi = m.opz_writes; std::printf("[midi] note on sent\n"); }
        m.run_samples(chunk, audio);
        std::string a = m.lcd.line(0), b = m.lcd.line(1);
        if (a != last0 || b != last1) {
            std::printf("[%5.2fs] |%s|\n         |%s|\n", done / Tx81z::SAMPLE_RATE, a.c_str(), b.c_str());
            last0 = a; last1 = b;
        }
    }
    std::printf("PC=%04x S=%04x cycles=%llu bank=%d opz_writes=%llu lcd_writes=%llu trapped=%d(op %02x) rcr=%02x tcsr=%02x p6=%02x\n",
                m.cpu.PC, m.cpu.S, (unsigned long long)m.cpu.total_cycles, m.bank,
                (unsigned long long)m.opz_writes, (unsigned long long)m.lcd.writes, m.cpu.trapped, m.cpu.last_trap_op,
                m.cpu.rcr, m.cpu.tcsr, m.p6_last);
    std::printf("sci: rx_delivered=%llu sci_irqs=%llu opz_writes_after_midi=%llu ", (unsigned long long)m.cpu.rx_delivered, (unsigned long long)m.cpu.sci_irqs, (unsigned long long)(m.opz_writes - opz_at_midi));
    std::printf("sci: rmcr=%02x trcsr=%02x p2ddr=%02x p6ddr=%02x tcsr2=%02x tcsr3=%02x midi_out=%zu\n", m.cpu.rmcr, m.cpu.trcsr, m.cpu.p2ddr, m.cpu.p6ddr, m.cpu.tcsr2, m.cpu.tcsr3, m.midi_out.size());
    if (getenv("NVDUMP")) { const uint8_t *n = m.nvram_data(); int nz = 0; for (int i = 0; i < 0x2000; i++) nz += n[i] != 0;
        std::printf("nvram nonzero=%d\n", nz); for (int i = 0; i < 0x2000; i += 16) { bool any = false; for (int j = 0; j < 16; j++) any |= n[i + j] != 0; if (!any) continue; std::printf("%04x:", 0x6000 + i); for (int j = 0; j < 16; j++) std::printf(" %02x", n[i + j]); std::printf("\n"); } }
    if (getenv("RAMDUMP")) { for (int a = 0x40; a < 0x100; a += 16) { std::printf("iram %02x:", a); for (int j = 0; j < 16; j++) std::printf(" %02x", m.cpu.ram(a + j)); std::printf("\n"); }
        const uint8_t *n = m.nvram_data(); std::printf("midi buf 7dcc..: "); for (int i = 0x1dcc; i < 0x1dcc + 16; i++) std::printf("%02x ", n[i]); std::printf("\n"); }
    if (getenv("PCHITS")) { for (const char *t = getenv("PCHITS"); *t; ) { unsigned a = strtoul(t, (char**)&t, 16); while (*t == ',') t++; std::printf("pc %04x hits=%u\n", a, pc_hits[a]); } }
    float peak = 0; for (float v : audio) peak = std::max(peak, std::abs(v));
    std::printf("audio samples=%zu peak=%.4f\n", audio.size() / 2, peak);
    if (argc > 3 && argv[3][0] != '-') wav(argv[3], audio, (int)Tx81z::SAMPLE_RATE);
    return 0;
}
