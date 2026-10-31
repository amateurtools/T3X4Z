// Yamaha TX81Z machine: HD6303X + YM2414 (OPZ via ymfm) + HD44780 LCD + 8K NVRAM + banked 64K ROM.
// Memory map and port usage follow MAME's ymtx81z.cpp (BSD-3-Clause, AJR).
#pragma once
#include "hd6303x.h"
#include "ymfm_opz.h"
#include <array>
#include <string>
#include <vector>
#include <cstdio>

class Hd44780 {
public:
    void write_cmd(uint8_t v);
    void write_data(uint8_t v);
    uint8_t read_status() const { return addr & 0x7f; } // never busy
    uint8_t read_data();
    std::string line(int n) const;   // 16 chars
    bool display_on = true;
    uint8_t cgram[64] = {};
    uint8_t ddram[128] = {};
    uint64_t writes = 0;
private:
    void inc();
    uint8_t addr = 0;
    bool in_cgram = false, entry_inc = true;
};

class Tx81z : public Hd6303xHost, public ymfm::ymfm_interface {
public:
    static constexpr double MASTER_HZ = 7159090.0;
    static constexpr double E_HZ = MASTER_HZ / 4.0;
    static constexpr double OPZ_HZ = MASTER_HZ / 2.0;       // 3.579545 MHz
    static constexpr double SAMPLE_RATE = OPZ_HZ / 64.0;    // ~55.93 kHz native

    Tx81z();
    bool load_rom(const std::string &path);
    bool load_rom_data(const void *data, size_t size); // exactly 65536 bytes
    bool rom_loaded() const { return rom_ok; }
    void reset(bool clear_nvram = true);
    // Run until `n` OPZ samples have been appended to `out` (interleaved L/R floats)
    void run_samples(int n, std::vector<float> &out);
    void midi_in(uint8_t b) { cpu.queue_serial_rx(b); }
    std::vector<uint8_t> midi_out;

    Hd6303x cpu;
    Hd44780 lcd;
    bool trace_opz = false;
    uint64_t opz_writes = 0;
    int bank = 0;
    uint8_t nvram_fill = 0;
    const uint8_t *nvram_data() const { return nvram.data(); }
    uint8_t *nvram_mut() { return nvram.data(); }

    // Hd6303xHost
    uint8_t read(uint16_t a) override;
    void write(uint16_t a, uint8_t v) override;
    // Front panel: active-low, idle levels per MAME input defs.
    // P2: b5 Cursor, b6 Vol->, b7 Vol<-   P5: b1 Store, b4 Inc, b5 Dec, b6 Param->, b7 Param<-   P6: b0 Play, b1 Edit, b2 Utility
    uint8_t kp2 = 0xfa, kp5 = 0xff, kp6 = 0x0f;
    uint8_t in_p2() override { return kp2; }
    uint8_t in_p5() override { return kp5; }
    uint8_t in_p6() override { return kp6; }
    void out_p6(uint8_t v, uint8_t ddr) override { bank = (v >> 3) & 1; p6_last = v; }
    void out_serial(uint8_t b) override { midi_out.push_back(b); }
    void elapsed(int ecycles) override;

    // ymfm_interface
    void ymfm_set_timer(uint32_t tnum, int32_t duration) override { timer_left[tnum] = duration; }
    void ymfm_update_irq(bool asserted) override { cpu.set_irq1(asserted); }

    uint8_t p6_last = 0xff;

private:
    std::array<uint8_t, 0x10000> rom{};
    bool rom_ok = false;
    std::array<uint8_t, 0x2000> nvram{};
    ymfm::ym2414 opz;
    int64_t timer_left[2] = {-1, -1};
    int chip_acc = 0;
    std::vector<float> *sink = nullptr;
};
