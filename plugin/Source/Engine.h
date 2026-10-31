// Wraps the standalone TX81Z core for use inside a plugin: boot/init scripting, resampling, key mapping.
#pragma once
#include "tx81z.h"
#include "Resampler.h"
#include <array>
#include <vector>
#include <cstdint>

class Engine {
public:
    enum Key { Play = 0, Edit, Utility, Store, Inc, Dec, ParamRight, ParamLeft, Cursor, VolRight, VolLeft, NumKeys };

    Tx81z tx;
    bool romLoaded = false;

    bool loadRom(const void *data, size_t size)
    {
        romLoaded = tx.load_rom_data(data, size);
        return romLoaded;
    }

    void setHostRate(double hostRate)
    {
        rs.setRatio(Tx81z::SAMPLE_RATE / hostRate);
        scratch.reserve(8192);
        scratch.clear();
    }

    // ---- headless helpers (message thread) ------------------------------------------------
    void runHeadless(double seconds)
    {
        int n = (int)(seconds * Tx81z::SAMPLE_RATE);
        while (n > 0) { int c = std::min(n, 2048); scratch.clear(); tx.run_samples(c, scratch); n -= c; }
        scratch.clear();
    }
    void applyKeys(uint32_t mask)
    {
        auto k = [&](Key key) { return (mask >> key) & 1u; };
        uint8_t p6 = 0x0f, p5 = 0xff, p2 = 0xfa;
        if (k(Play)) p6 &= ~0x01;
        if (k(Edit)) p6 &= ~0x02;
        if (k(Utility)) p6 &= ~0x04;
        if (k(Store)) p5 &= ~0x02;
        if (k(Inc)) p5 &= ~0x10;
        if (k(Dec)) p5 &= ~0x20;
        if (k(ParamRight)) p5 &= ~0x40;
        if (k(ParamLeft)) p5 &= ~0x80;
        if (k(Cursor)) p2 &= ~0x20;
        if (k(VolRight)) p2 &= ~0x40;
        if (k(VolLeft)) p2 &= ~0x80;
        tx.kp2 = p2; tx.kp5 = p5; tx.kp6 = p6;
    }
    // Warm boot: keeps NVRAM, runs the firmware until it is ready to play.
    void powerCycle()
    {
        applyKeys(0);
        tx.reset(false);
        runHeadless(9.0);
        rs.reset();
        tx.midi_out.clear();   // drop anything the firmware transmitted while booting
    }
    // Wipes NVRAM and runs the firmware's own factory reset (power-on with both volume buttons, press INC
    // at "Test entry?"), then power-cycles with the new memory.
    void factoryInit()
    {
        tx.nvram_fill = 0xff;
        tx.reset(true);
        const double step = 0.02;
        for (double t = 0; t < 6.0; t += step) {
            uint32_t m = 0;
            if (t < 3.0) m |= (1u << VolLeft) | (1u << VolRight);
            if (t >= 4.0 && t < 4.3) m |= (1u << Inc);
            applyKeys(m);
            runHeadless(step);
        }
        powerCycle();
    }

    const uint8_t *nvram() const { return tx.nvram_data(); }
    uint8_t *nvramMut() { return tx.nvram_mut(); }
    static constexpr size_t NVRAM_SIZE = 0x2000;

    // ---- realtime ------------------------------------------------------------------------------
    void feedMidi(const uint8_t *d, int n) { for (int i = 0; i < n; i++) tx.midi_in(d[i]); }

    // Panic: release every voice and zero pitch bend / mod wheel / sustain on all 16 channels.
    void panic()
    {
        tx.key_off_all();
        for (uint8_t c = 0; c < 16; c++) {
            const uint8_t msgs[] = { uint8_t(0xB0 | c), 123, 0,      // all notes off
                                     uint8_t(0xB0 | c), 120, 0,      // all sound off
                                     uint8_t(0xB0 | c), 64, 0,       // sustain off
                                     uint8_t(0xB0 | c), 1, 0,        // mod wheel to zero
                                     uint8_t(0xE0 | c), 0, 0x40 };  // pitch bend to centre
            feedMidi(msgs, (int)sizeof msgs);
        }
    }

    void render(float *l, float *r, int n)
    {
        for (int i = 0; i < n; i++) {
            while (rs.needsInput()) {
                scratch.clear();
                tx.run_samples(64, scratch);
                rs.pushInput(scratch.data(), 64);
            }
            rs.pull(l[i], r[i]);
        }
    }

private:
    SincResampler rs;
    std::vector<float> scratch;
};
