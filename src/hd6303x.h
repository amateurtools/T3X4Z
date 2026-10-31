// Minimal HD6303X CPU core (6800/6801/6303 instruction set + on-chip timer, SCI, ports)
// Written for the TX81Z standalone port. Behaviour cross-checked against MAME's m6801 core
// (BSD-3-Clause, Aaron Giles / AJR) for register layout, vectors and cycle counts.
#pragma once
#include <cstdint>
#include <deque>

struct Hd6303xHost {
    virtual ~Hd6303xHost() = default;
    virtual uint8_t read(uint16_t addr) = 0;           // external bus
    virtual void    write(uint16_t addr, uint8_t v) = 0;
    virtual uint8_t in_p2() { return 0xff; }
    virtual uint8_t in_p5() { return 0xff; }
    virtual uint8_t in_p6() { return 0xff; }
    virtual void    out_p6(uint8_t v, uint8_t ddr) {}
    virtual void    out_p2(uint8_t v, uint8_t ddr) {}
    virtual void    out_serial(uint8_t byte) {}        // SCI transmitted byte
    virtual void    elapsed(int ecycles) {}            // called after every instruction
};

class Hd6303x {
public:
    explicit Hd6303x(Hd6303xHost &h) : host(h) {}
    void reset();
    int  step();                       // execute one instruction (or idle), returns E-cycles
    void set_irq1(bool asserted) { irq1 = asserted; }
    bool irq1_line() const { return irq1; }
    void queue_serial_rx(uint8_t b) { rx_queue.push_back(b); }
    bool halted() const { return trapped; }
    uint8_t ram(uint8_t a) const { return iram[a]; }
    uint64_t rx_delivered = 0, sci_irqs = 0;
    uint32_t *pc_hits = nullptr; // optional 65536-entry hit counters

    // registers
    uint8_t A = 0, B = 0, cc = 0xd0;
    uint16_t X = 0, S = 0, PC = 0;
    uint64_t total_cycles = 0;
    bool trapped = false;
    uint8_t last_trap_op = 0;

    // on-chip state (public for debugging)
    uint8_t tcsr = 0, tcsr2 = 0, tcsr3 = 0, rmcr = 0, trcsr = 0x20, rcr = 0, tconr = 0xff, t2cnt = 0;
    uint16_t counter = 0xfff8, ocr1 = 0xffff, ocr2 = 0xffff, icr = 0;
    uint8_t p2ddr = 0, p2dat = 0, p6ddr = 0, p6dat = 0;

private:
    Hd6303xHost &host;
    bool irq1 = false, waiting = false, is_wai = false;
    uint8_t latch_ch = 0;
    uint8_t seen_tcsr = 0, seen_tcsr2 = 0;
    uint8_t iram[0x100];

    // SCI
    uint8_t rdr = 0, tdr = 0, tsr_byte = 0;
    bool tdr_full = false, tx_busy = false;
    int64_t tx_left = 0, rx_left = 0; // in 1/1000 E-cycles
    bool rx_busy = false; uint8_t rx_byte = 0;
    std::deque<uint8_t> rx_queue;
    bool sci_status_read = false;

    uint8_t rd(uint16_t a);
    void    wr(uint16_t a, uint8_t v);
    uint8_t rd_io(uint8_t a);
    void    wr_io(uint8_t a, uint8_t v);
    uint16_t rd16(uint16_t a) { return (rd(a) << 8) | rd((uint16_t)(a + 1)); }
    void    wr16(uint16_t a, uint16_t v) { wr(a, v >> 8); wr((uint16_t)(a + 1), (uint8_t)v); }
    uint8_t fetch() { return rd(PC++); }
    uint16_t fetch16() { uint16_t h = fetch(); return (h << 8) | fetch(); }
    void push8(uint8_t v) { wr(S--, v); }
    void push16(uint16_t v) { push8((uint8_t)v); push8(v >> 8); }
    uint8_t pull8() { return rd(++S); }
    uint16_t pull16() { uint16_t h = pull8(); return (h << 8) | pull8(); }

    void tick(int n);
    bool service_interrupts();
    void enter_irq(uint16_t vec, int extra = 0);
    int  exec(uint8_t op);

    int64_t bit_period_milli() const;
};
