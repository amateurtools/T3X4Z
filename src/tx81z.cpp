#include "tx81z.h"
#include <fstream>
#include <cstring>

// ---------------------------------------------------------------- HD44780 (just enough for the TX81Z)
void Hd44780::inc()
{
    if (in_cgram) { addr = (addr + (entry_inc ? 1 : -1)) & 0x3f; return; }
    addr = (addr + (entry_inc ? 1 : -1)) & 0x7f;
}

void Hd44780::write_cmd(uint8_t v)
{
    writes++;
    if (v & 0x80) { in_cgram = false; addr = v & 0x7f; }
    else if (v & 0x40) { in_cgram = true; addr = v & 0x3f; }
    else if (v & 0x20) { /* function set */ }
    else if (v & 0x10) { /* shift: move cursor only */ addr = (addr + ((v & 4) ? 1 : -1)) & 0x7f; }
    else if (v & 0x08) { display_on = v & 4; }
    else if (v & 0x04) { entry_inc = v & 2; }
    else if (v & 0x02) { in_cgram = false; addr = 0; }
    else if (v & 0x01) { std::memset(ddram, ' ', sizeof ddram); in_cgram = false; addr = 0; entry_inc = true; }
}

void Hd44780::write_data(uint8_t v)
{
    writes++;
    if (in_cgram) cgram[addr & 0x3f] = v; else ddram[addr & 0x7f] = v;
    inc();
}

uint8_t Hd44780::read_data()
{
    uint8_t v = in_cgram ? cgram[addr & 0x3f] : ddram[addr & 0x7f];
    inc();
    return v;
}

std::string Hd44780::line(int n) const
{
    std::string s;
    int base = n ? 0x40 : 0;
    for (int i = 0; i < 16; i++) {
        uint8_t c = ddram[base + i];
        s += (c >= 0x20 && c < 0x7f) ? (char)c : (c < 8 ? '#' : '?');
    }
    return s;
}

// ---------------------------------------------------------------- machine
Tx81z::Tx81z() : cpu(*this), opz(*this) { std::memset(lcd.ddram, ' ', sizeof lcd.ddram); }

bool Tx81z::load_rom(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.read(reinterpret_cast<char *>(rom.data()), rom.size());
    rom_ok = f.gcount() == (std::streamsize)rom.size();
    return rom_ok;
}

bool Tx81z::load_rom_data(const void *data, size_t size)
{
    if (size != rom.size()) return false;
    std::memcpy(rom.data(), data, size);
    rom_ok = true;
    return true;
}

void Tx81z::reset(bool clear_nvram)
{
    if (clear_nvram) nvram.fill(nvram_fill);
    opz.reset();
    timer_left[0] = timer_left[1] = -1;
    chip_acc = 0; bank = 0;
    cpu.reset();
}

uint8_t Tx81z::read(uint16_t a)
{
    if (a >= 0x8000) return rom[(size_t)bank * 0x8000 + (a & 0x7fff)];
    if (a >= 0x6000) return nvram[a & 0x1fff];
    if (a >= 0x4000) return (a & 1) ? lcd.read_data() : lcd.read_status();
    if (a >= 0x2000) return opz.read(a & 1);
    return 0xff;
}

void Tx81z::write(uint16_t a, uint8_t v)
{
    if (a >= 0x8000) return;
    if (a >= 0x6000) { nvram[a & 0x1fff] = v; return; }
    if (a >= 0x4000) { if (a & 1) lcd.write_data(v); else lcd.write_cmd(v); return; }
    if (a >= 0x2000) {
        opz_writes++;
        if (trace_opz) std::printf("OPZ %s %02x\n", (a & 1) ? "data" : "addr", v);
        opz.write(a & 1, v);
    }
}

void Tx81z::elapsed(int ecycles)
{
    int clocks = ecycles * 2; // chip clocks (master/2) per E-cycle (master/4)
    // OPZ timers count in chip input clocks
    for (int t = 0; t < 2; t++) {
        if (timer_left[t] >= 0) {
            timer_left[t] -= clocks;
            if (timer_left[t] <= 0) { timer_left[t] = -1; m_engine->engine_timer_expired(t); }
        }
    }
    chip_acc += clocks;
    while (chip_acc >= 64) {
        chip_acc -= 64;
        if (sink) {
            ymfm::ym2414::output_data o;
            opz.generate(&o, 1);
            sink->push_back(o.data[0] / 32768.0f);
            sink->push_back(o.data[1] / 32768.0f);
        }
    }
}

void Tx81z::run_samples(int n, std::vector<float> &out)
{
    sink = &out;
    size_t target = out.size() + (size_t)n * 2;
    while (out.size() < target) cpu.step();
    sink = nullptr;
}
