#include "hd6303x.h"
#include <cstring>

namespace {
constexpr uint8_t XX = 4;
// HD6303 cycle table (taken from MAME m6801 cycles_63701; verified against HD6303 datasheet shape)
const uint8_t CYC[256] = {
 XX, 1,XX,XX, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1,XX,XX,XX,XX, 1, 1, 2, 2, 4, 1,XX,XX,XX,XX,
  3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
  1, 1, 3, 3, 1, 1, 4, 4, 4, 5, 1,10, 5, 7, 9,12,
  1,XX,XX, 1, 1,XX, 1, 1, 1, 1, 1,XX, 1, 1,XX, 1,
  1,XX,XX, 1, 1,XX, 1, 1, 1, 1, 1,XX, 1, 1,XX, 1,
  6, 7, 7, 6, 6, 7, 6, 6, 6, 6, 6, 5, 6, 4, 3, 5,
  6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 4, 6, 4, 3, 5,
  2, 2, 2, 3, 2, 2, 2,XX, 2, 2, 2, 2, 3, 5, 3,XX,
  3, 3, 3, 4, 3, 3, 3, 3, 3, 3, 3, 3, 4, 5, 4, 4,
  4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,
  4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 5, 6, 5, 5,
  2, 2, 2, 3, 2, 2, 2,XX, 2, 2, 2, 2, 3,XX, 3,XX,
  3, 3, 3, 4, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4,
  4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,
  4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5};

constexpr uint8_t H_ = 0x20, I_ = 0x10, N_ = 8, Z_ = 4, V_ = 2, C_ = 1;
constexpr uint8_t TCSR_OLVL = 1, TCSR_IEDG = 2, TCSR_ETOI = 4, TCSR_EOCI = 8, TCSR_EICI = 0x10,
                  TCSR_TOF = 0x20, TCSR_OCF = 0x40, TCSR_ICF = 0x80;
constexpr uint8_t TRCSR_RDRF = 0x80, TRCSR_ORFE = 0x40, TRCSR_TDRE = 0x20, TRCSR_RIE = 0x10,
                  TRCSR_RE = 8, TRCSR_TIE = 4, TRCSR_TE = 2;

// pending-flag tracking (mirrors MAME's m_pending_tcsr logic)
uint8_t pending_tcsr = 0, pending_tcsr2 = 0;
bool trcsr_read_tdre = false, trcsr_read_rdrf = false, trcsr_read_orfe = false;
}

void Hd6303x::reset()
{
    std::memset(iram, 0, sizeof iram);
    cc = 0xd0; // I set
    A = B = 0; X = 0; S = 0;
    tcsr = tcsr2 = tcsr3 = rmcr = 0; trcsr = TRCSR_TDRE; rcr = 0x7c; tconr = 0xff; t2cnt = 0;
    counter = 0xfff8; ocr1 = ocr2 = 0xffff; icr = 0;
    p2ddr = p6ddr = 0; p2dat = p6dat = 0;
    pending_tcsr = pending_tcsr2 = 0;
    trcsr_read_tdre = trcsr_read_rdrf = trcsr_read_orfe = false;
    tx_busy = rx_busy = tdr_full = false; rx_queue.clear();
    waiting = false; is_wai = false; trapped = false; total_cycles = 0;
    PC = rd16(0xfffe);
    host.out_p6(0xff, 0); // ports reset to inputs (pulled high)
}

int64_t Hd6303x::bit_period_milli() const
{
    static const int div[4] = {16, 128, 1024, 4096};
    int ccm = (rmcr & 0x1c) >> 2;
    bool ext = (ccm == 0 || ccm == 3 || ccm == 7);
    if (ext) return 57273; // 500 kHz /16 = 31250 baud at E = 1.7897727 MHz
    return (int64_t)div[rmcr & 3] * 1000;
}

uint8_t Hd6303x::rd(uint16_t a)
{
    if (a < 0x20) return rd_io((uint8_t)a);
    if (a >= 0x40 && a < 0x100 && (rcr & 0x40)) return iram[a];
    return host.read(a);
}

void Hd6303x::wr(uint16_t a, uint8_t v)
{
    if (a < 0x20) { wr_io((uint8_t)a, v); return; }
    if (a >= 0x40 && a < 0x100 && (rcr & 0x40)) { iram[a] = v; return; }
    host.write(a, v);
}

uint8_t Hd6303x::rd_io(uint8_t a)
{
    switch (a) {
    case 0x03: return (uint8_t)((host.in_p2() & ~p2ddr) | (p2dat & p2ddr));
    case 0x08: pending_tcsr = 0; return tcsr;
    case 0x09:
        if (!(pending_tcsr & TCSR_TOF)) tcsr &= ~TCSR_TOF;
        return counter >> 8;
    case 0x0a: return (uint8_t)counter;
    case 0x0b: return ocr1 >> 8;
    case 0x0c: return (uint8_t)ocr1;
    case 0x0d: if (!(pending_tcsr & TCSR_ICF)) tcsr &= ~TCSR_ICF; return icr >> 8;
    case 0x0e: return (uint8_t)icr;
    case 0x0f:
        pending_tcsr &= ~(TCSR_ICF | TCSR_OCF); pending_tcsr2 = 0;
        return tcsr2 | (tcsr & (TCSR_ICF | TCSR_OCF)) | 0x10;
    case 0x10: return rmcr;
    case 0x11:
        if (trcsr & TRCSR_TDRE) trcsr_read_tdre = true;
        if (trcsr & TRCSR_ORFE) trcsr_read_orfe = true;
        if (trcsr & TRCSR_RDRF) trcsr_read_rdrf = true;
        return trcsr;
    case 0x12:
        if (trcsr_read_orfe) { trcsr_read_orfe = false; trcsr &= ~TRCSR_ORFE; }
        if (trcsr_read_rdrf) { trcsr_read_rdrf = false; trcsr &= ~TRCSR_RDRF; }
        return rdr;
    case 0x14: return rcr | 0x30;
    case 0x15: { uint8_t d = host.in_p5(); if (irq1) d &= 0xfe; return d; }
    case 0x17: {
        uint8_t ddr = p6ddr;
        if (ddr == 0xff) return p6dat;
        return (uint8_t)((host.in_p6() & ~ddr) | (p6dat & ddr));
    }
    case 0x19: return ocr2 >> 8;
    case 0x1a: return (uint8_t)ocr2;
    case 0x1b: return tcsr3;
    case 0x1d: return t2cnt;
    default: return 0xff;
    }
}

void Hd6303x::wr_io(uint8_t a, uint8_t v)
{
    switch (a) {
    case 0x01: p2ddr = v & 0x1f; host.out_p2(p2dat, p2ddr); break;
    case 0x03: p2dat = v; host.out_p2(p2dat, p2ddr); break;
    case 0x08:
        v &= 0x1f;
        tcsr = v | (tcsr & 0xe0);
        pending_tcsr &= tcsr;
        break;
    case 0x09: latch_ch = v; counter = 0xfff8; break;
    case 0x0a: counter = (uint16_t)((latch_ch << 8) | v); break;
    case 0x0b: case 0x0c:
        if (!(pending_tcsr & TCSR_OCF)) tcsr &= ~TCSR_OCF;
        if (a == 0x0b) ocr1 = (uint16_t)((ocr1 & 0x00ff) | (v << 8)); else ocr1 = (uint16_t)((ocr1 & 0xff00) | v);
        break;
    case 0x0f:
        v &= 0x0f; // OE1 OE2 OLVL2 EOCI2
        tcsr2 = v | (tcsr2 & 0x20);
        pending_tcsr2 &= tcsr2;
        break;
    case 0x10: rmcr = v; break;
    case 0x11:
        if ((v & TRCSR_TE) && !(trcsr & TRCSR_TE)) { /* transmitter enabled */ }
        trcsr = (trcsr & 0xe0) | (v & 0x1f);
        break;
    case 0x13:
        if (trcsr_read_tdre) { trcsr_read_tdre = false; trcsr &= ~TRCSR_TDRE; }
        tdr = v; tdr_full = true;
        break;
    case 0x14: rcr = (uint8_t)((v & 0xc3) | (rcr & 0x3c)); break;
    case 0x16: p6ddr = v; host.out_p6((uint8_t)((p6dat & p6ddr) | (p6ddr ^ 0xff)), p6ddr); break;
    case 0x17: p6dat = v; host.out_p6((uint8_t)((p6dat & p6ddr) | (p6ddr ^ 0xff)), p6ddr); break;
    case 0x19: case 0x1a:
        if (!(pending_tcsr2 & 0x20)) tcsr2 &= ~0x20;
        if (a == 0x19) ocr2 = (uint16_t)((ocr2 & 0x00ff) | (v << 8)); else ocr2 = (uint16_t)((ocr2 & 0xff00) | v);
        break;
    case 0x1b: tcsr3 = (uint8_t)(v & (0x5f | (tcsr3 & 0x80))); break;
    case 0x1c: tconr = v; break;
    case 0x1d: t2cnt = v; if (tcsr3 & 0x80) tcsr3 &= 0x7f; break;
    default: break;
    }
}

void Hd6303x::tick(int n)
{
    total_cycles += (uint64_t)n;

    // free-running counter and compares
    uint16_t old = counter;
    counter = (uint16_t)(counter + n);
    auto crossed = [&](uint16_t target) {
        uint32_t d = (uint16_t)(target - old);
        return d != 0 && d <= (uint32_t)n;
    };
    if (crossed(ocr1)) { tcsr |= TCSR_OCF; pending_tcsr |= TCSR_OCF; }
    if (crossed(ocr2)) { tcsr2 |= 0x20; pending_tcsr2 |= 0x20; }
    { uint32_t d = (uint32_t)(0x10000 - old); if (d <= (uint32_t)n) { tcsr |= TCSR_TOF; pending_tcsr |= TCSR_TOF; } }

    // timer 2 (8-bit down-compare timer)
    if (tcsr3 & 0x10) {
        static const int sh[3] = {0, 3, 7};
        int s = tcsr3 & 3;
        if (s < 3) {
            uint32_t ticks = (uint32_t)((n + (old & ((1u << sh[s]) - 1))) >> sh[s]);
            while (ticks--) {
                if (++t2cnt == (uint8_t)(tconr + 1)) { t2cnt = 0; tcsr3 |= 0x80; }
            }
        }
    }

    // SCI transmitter
    int64_t dt = (int64_t)n * 1000;
    if ((trcsr & TRCSR_TE)) {
        if (!tx_busy && tdr_full) {
            tsr_byte = tdr; tdr_full = false; trcsr |= TRCSR_TDRE; tx_busy = true;
            tx_left = 10 * bit_period_milli();
        }
        if (tx_busy) {
            tx_left -= dt;
            if (tx_left <= 0) {
                tx_busy = false;
                host.out_serial(tsr_byte);
                if (tdr_full) { // chain next byte
                    tsr_byte = tdr; tdr_full = false; trcsr |= TRCSR_TDRE; tx_busy = true;
                    tx_left = 10 * bit_period_milli() + tx_left;
                }
            }
        }
    }
    // SCI receiver
    if (trcsr & TRCSR_RE) {
        if (!rx_busy && !rx_queue.empty()) {
            rx_byte = rx_queue.front(); rx_queue.pop_front();
            rx_busy = true; rx_left = 10 * bit_period_milli();
        }
        if (rx_busy) {
            rx_left -= dt;
            if (rx_left <= 0) {
                rx_busy = false;
                if (trcsr & TRCSR_RDRF) trcsr |= TRCSR_ORFE;
                else { rdr = rx_byte; trcsr |= TRCSR_RDRF; rx_delivered++; }
            }
        }
    }
    host.elapsed(n);
}

void Hd6303x::enter_irq(uint16_t vec, int extra)
{
    int cycles;
    if (waiting && is_wai) { cycles = 4; waiting = false; is_wai = false; }
    else {
        waiting = false;
        push16(PC); push16(X); push8(A); push8(B); push8(cc);
        cycles = 12;
    }
    cc |= I_;
    PC = rd16(vec);
    tick(cycles + extra);
}

bool Hd6303x::service_interrupts()
{
    bool maskable_ok = !(cc & I_);
    // IRQ1 (external, level) has top priority after NMI (unused here)
    if (irq1 && (rcr & 1)) {
        if (maskable_ok) { enter_irq(0xfff8); return true; }
        if (waiting) { /* masked */ }
    }
    uint16_t vec = 0;
    if ((tcsr & (TCSR_EICI | TCSR_ICF)) == (TCSR_EICI | TCSR_ICF)) vec = 0xfff6;
    else if ((tcsr & (TCSR_EOCI | TCSR_OCF)) == (TCSR_EOCI | TCSR_OCF) || (tcsr2 & 0x28) == 0x28) vec = 0xfff4;
    else if ((tcsr & (TCSR_ETOI | TCSR_TOF)) == (TCSR_ETOI | TCSR_TOF)) vec = 0xfff2;
    else if ((tcsr3 & 0xc0) == 0xc0) vec = 0xffec;
    else if (((trcsr & (TRCSR_RIE | TRCSR_RDRF)) == (TRCSR_RIE | TRCSR_RDRF)) ||
             ((trcsr & (TRCSR_RIE | TRCSR_ORFE)) == (TRCSR_RIE | TRCSR_ORFE)) ||
             ((trcsr & (TRCSR_TIE | TRCSR_TDRE)) == (TRCSR_TIE | TRCSR_TDRE))) vec = 0xfff0;
    if (vec && maskable_ok) { if (vec == 0xfff0) sci_irqs++; enter_irq(vec); return true; }
    return false;
}

int Hd6303x::step()
{
    uint64_t start = total_cycles;
    if (service_interrupts()) return (int)(total_cycles - start);
    if (waiting) { tick(4); return 4; }
    if (pc_hits) pc_hits[PC]++;
    int c = exec(fetch());
    tick(c);
    return (int)(total_cycles - start);
}

// ---------------------------------------------------------------- ALU helpers
#define SETNZ8(r)  do { cc = (cc & ~(N_|Z_)) | (((r) & 0x80) ? N_ : 0) | (((r) & 0xff) == 0 ? Z_ : 0); } while (0)
#define SETNZ16(r) do { cc = (cc & ~(N_|Z_)) | (((r) & 0x8000) ? N_ : 0) | (((r) & 0xffff) == 0 ? Z_ : 0); } while (0)
#define SETV(f)    do { cc = (cc & ~V_) | ((f) ? V_ : 0); } while (0)
#define SETC(f)    do { cc = (cc & ~C_) | ((f) ? C_ : 0); } while (0)
#define SETH(f)    do { cc = (cc & ~H_) | ((f) ? H_ : 0); } while (0)

static inline bool getf(uint8_t cc, uint8_t m) { return cc & m; }

int Hd6303x::exec(uint8_t op)
{
    int cyc = CYC[op];
    auto trap = [&]() {
        trapped = true; last_trap_op = op;
        push16(PC); push16(X); push8(A); push8(B); push8(cc);
        cc |= I_;
        PC = rd16(0xffee);
        cyc = 12;
    };

    auto add8 = [&](uint8_t a, uint8_t m, int c) -> uint8_t {
        unsigned r = a + m + c;
        SETH(((a ^ m ^ r) & 0x10));
        SETNZ8(r);
        SETV(((a ^ r) & (m ^ r) & 0x80));
        SETC(r & 0x100);
        return (uint8_t)r;
    };
    auto sub8 = [&](uint8_t a, uint8_t m, int c) -> uint8_t {
        unsigned r = (unsigned)a - m - c;
        SETNZ8(r);
        SETV(((a ^ m) & (a ^ r) & 0x80));
        SETC(r & 0x100);
        return (uint8_t)r;
    };
    auto sub16 = [&](uint16_t a, uint16_t m) -> uint16_t {
        uint32_t r = (uint32_t)a - m;
        SETNZ16(r);
        SETV(((a ^ m) & (a ^ r) & 0x8000));
        SETC(r & 0x10000);
        return (uint16_t)r;
    };
    auto add16 = [&](uint16_t a, uint16_t m) -> uint16_t {
        uint32_t r = (uint32_t)a + m;
        SETNZ16(r);
        SETV(((a ^ r) & (m ^ r) & 0x8000));
        SETC(r & 0x10000);
        return (uint16_t)r;
    };
    auto logic8 = [&](uint8_t r) { SETNZ8(r); SETV(0); };

    // ---- unary read-modify-write core (value in, value out; returns false if opcode column invalid)
    auto unary = [&](int col, uint8_t x, uint8_t &r, bool &write) -> bool {
        write = true;
        switch (col) {
        case 0x0: r = (uint8_t)-x; SETNZ8(r); SETV(r == 0x80); SETC(r != 0); return true;
        case 0x3: r = ~x; SETNZ8(r); SETV(0); SETC(1); return true;
        case 0x4: r = x >> 1; SETC(x & 1); SETNZ8(r); SETV(getf(cc, C_) != getf(cc, N_)); return true;
        case 0x6: r = (uint8_t)((getf(cc, C_) ? 0x80 : 0) | (x >> 1)); SETC(x & 1); SETNZ8(r); SETV(getf(cc, C_) != getf(cc, N_)); return true;
        case 0x7: r = (uint8_t)((x >> 1) | (x & 0x80)); SETC(x & 1); SETNZ8(r); SETV(getf(cc, C_) != getf(cc, N_)); return true;
        case 0x8: r = (uint8_t)(x << 1); SETC(x & 0x80); SETNZ8(r); SETV(getf(cc, C_) != getf(cc, N_)); return true;
        case 0x9: r = (uint8_t)((x << 1) | (getf(cc, C_) ? 1 : 0)); SETC(x & 0x80); SETNZ8(r); SETV(getf(cc, C_) != getf(cc, N_)); return true;
        case 0xa: r = x - 1; SETNZ8(r); SETV(x == 0x80); return true;
        case 0xc: r = x + 1; SETNZ8(r); SETV(x == 0x7f); return true;
        case 0xd: r = x; SETNZ8(x); SETV(0); SETC(0); write = false; return true;
        case 0xf: r = 0; cc = (cc & ~(N_|V_|C_)) | Z_; return true;
        default: return false;
        }
    };

    uint8_t hi = op >> 4, lo = op & 15;

    if (op < 0x20) {
        switch (op) {
        case 0x01: break;
        case 0x04: { uint16_t d = (A << 8) | B; SETC(d & 1); d >>= 1; SETNZ16(d); cc &= ~N_; SETV(getf(cc, C_)); A = d >> 8; B = (uint8_t)d; } break;
        case 0x05: { uint16_t d = (A << 8) | B; uint32_t r = (uint32_t)d << 1; SETNZ16(r); SETC(r & 0x10000); SETV(getf(cc, C_) != getf(cc, N_)); A = (r >> 8) & 0xff; B = (uint8_t)r; } break;
        case 0x06: cc = A | 0xc0; break;
        case 0x07: A = cc | 0xc0; break;
        case 0x08: X++; cc = (cc & ~Z_) | (X == 0 ? Z_ : 0); break;
        case 0x09: X--; cc = (cc & ~Z_) | (X == 0 ? Z_ : 0); break;
        case 0x0a: cc &= ~V_; break;
        case 0x0b: cc |= V_; break;
        case 0x0c: cc &= ~C_; break;
        case 0x0d: cc |= C_; break;
        case 0x0e: cc &= ~I_; break;
        case 0x0f: cc |= I_; break;
        case 0x10: A = sub8(A, B, 0); break;
        case 0x11: sub8(A, B, 0); break;
        case 0x12: case 0x13: X = (uint16_t)(X + rd((uint16_t)(S + 1))); break; // undocumented
        case 0x16: B = A; SETNZ8(B); SETV(0); break;
        case 0x17: A = B; SETNZ8(A); SETV(0); break;
        case 0x18: { uint16_t t = X; X = (uint16_t)((A << 8) | B); A = t >> 8; B = (uint8_t)t; } break;
        case 0x19: {
            uint8_t msn = A & 0xf0, lsn = A & 0x0f; unsigned cf = 0;
            if (lsn > 9 || (cc & H_)) cf |= 6;
            if (msn > 0x80 && lsn > 9) cf |= 0x60;
            if (msn > 0x90 || (cc & C_)) cf |= 0x60;
            unsigned t = cf + A;
            SETNZ8(t); SETV(0); if (t & 0x100) cc |= C_; // carry kept from previous op unless set
            A = (uint8_t)t;
        } break;
        case 0x1a: waiting = true; is_wai = false; break; // SLP (wakes on any interrupt request, handled in step())
        case 0x1b: A = add8(A, B, 0); break;
        default: trap(); break;
        }
        return cyc;
    }

    if (hi == 2) { // branches
        int8_t off = (int8_t)fetch();
        bool N = cc & N_, Z = cc & Z_, V = cc & V_, C = cc & C_;
        bool t = false;
        switch (lo) {
        case 0: t = true; break; case 1: t = false; break;
        case 2: t = !(C || Z); break; case 3: t = C || Z; break;
        case 4: t = !C; break; case 5: t = C; break;
        case 6: t = !Z; break; case 7: t = Z; break;
        case 8: t = !V; break; case 9: t = V; break;
        case 10: t = !N; break; case 11: t = N; break;
        case 12: t = (N == V); break; case 13: t = (N != V); break;
        case 14: t = !Z && (N == V); break; case 15: t = Z || (N != V); break;
        }
        if (t) PC = (uint16_t)(PC + off);
        return cyc;
    }

    if (hi == 3) {
        switch (lo) {
        case 0: X = (uint16_t)(S + 1); break;
        case 1: S++; break;
        case 2: A = pull8(); break;
        case 3: B = pull8(); break;
        case 4: S--; break;
        case 5: S = (uint16_t)(X - 1); break;
        case 6: push8(A); break;
        case 7: push8(B); break;
        case 8: X = pull16(); break;
        case 9: PC = pull16(); break;
        case 10: X = (uint16_t)(X + B); break;
        case 11: cc = pull8() | 0xc0; B = pull8(); A = pull8(); X = pull16(); PC = pull16(); break;
        case 12: push16(X); break;
        case 13: { uint16_t t = A * B; SETC(t & 0x80); A = t >> 8; B = (uint8_t)t; } break;
        case 14: push16(PC); push16(X); push8(A); push8(B); push8(cc); waiting = true; is_wai = true; break; // WAI
        case 15: push16(PC); push16(X); push8(A); push8(B); push8(cc); cc |= I_; PC = rd16(0xfffa); break;
        }
        return cyc;
    }

    if (hi == 4 || hi == 5) {
        uint8_t &R = (hi == 4) ? A : B;
        uint8_t r; bool w;
        if (!unary(lo, R, r, w)) { trap(); return cyc; }
        if (w) R = r;
        return cyc;
    }

    if (hi == 6 || hi == 7) {
        // HD6301/6303 bit-manipulation immediates
        if (lo == 1 || lo == 2 || lo == 5 || lo == 11) {
            uint8_t imm = fetch();
            uint16_t ea = (hi == 6) ? (uint16_t)(X + fetch()) : (uint16_t)fetch();
            uint8_t m = rd(ea), r = 0;
            switch (lo) {
            case 1: r = m & imm; SETNZ8(r); SETV(0); wr(ea, r); break;   // AIM
            case 2: r = m | imm; SETNZ8(r); SETV(0); wr(ea, r); break;   // OIM
            case 5: r = m ^ imm; SETNZ8(r); SETV(0); wr(ea, r); break;   // EIM
            case 11: r = m & imm; SETNZ8(r); SETV(0); break;             // TIM
            }
            return cyc;
        }
        uint16_t ea = (hi == 6) ? (uint16_t)(X + fetch()) : fetch16();
        if (lo == 14) { PC = ea; return cyc; } // JMP
        uint8_t m = rd(ea), r; bool w;
        if (!unary(lo, m, r, w)) { trap(); return cyc; }
        if (w) wr(ea, r);
        return cyc;
    }

    // ---- 0x80..0xFF : accumulator/index operations
    const bool bgrp = (hi >= 0xc);       // B accumulator / D / X group
    const int mode = hi & 3;             // 0 imm, 1 direct, 2 indexed, 3 extended
    auto ea_of = [&]() -> uint16_t {
        switch (mode) {
        case 1: return fetch();
        case 2: return (uint16_t)(X + fetch());
        default: return fetch16();
        }
    };
    auto op8 = [&]() -> uint8_t { return mode == 0 ? fetch() : rd(ea_of()); };
    auto op16 = [&]() -> uint16_t {
        if (mode == 0) return fetch16();
        uint16_t a = ea_of(); return rd16(a);
    };
    uint8_t &ACC = bgrp ? B : A;

    switch (lo) {
    case 0x0: ACC = sub8(ACC, op8(), 0); break;
    case 0x1: sub8(ACC, op8(), 0); break;
    case 0x2: ACC = sub8(ACC, op8(), getf(cc, C_)); break;
    case 0x3: { uint16_t m = op16(); uint16_t d = (A << 8) | B; d = bgrp ? add16(d, m) : sub16(d, m); A = d >> 8; B = (uint8_t)d; } break;
    case 0x4: ACC &= op8(); logic8(ACC); break;
    case 0x5: logic8(ACC & op8()); break;
    case 0x6: ACC = op8(); logic8(ACC); break;
    case 0x7:
        if (mode == 0) { trap(); break; }
        { uint16_t ea = ea_of(); logic8(ACC); wr(ea, ACC); }
        break;
    case 0x8: ACC ^= op8(); logic8(ACC); break;
    case 0x9: ACC = add8(ACC, op8(), getf(cc, C_)); break;
    case 0xa: ACC |= op8(); logic8(ACC); break;
    case 0xb: ACC = add8(ACC, op8(), 0); break;
    case 0xc:
        if (!bgrp) sub16(X, op16());
        else { uint16_t m = op16(); A = m >> 8; B = (uint8_t)m; SETNZ16(m); SETV(0); }
        break;
    case 0xd:
        if (!bgrp) {
            if (mode == 0) { int8_t off = (int8_t)fetch(); push16(PC); PC = (uint16_t)(PC + off); } // BSR
            else { uint16_t ea = ea_of(); push16(PC); PC = ea; }                                      // JSR
        } else {
            if (mode == 0) { trap(); break; }
            uint16_t ea = ea_of(); uint16_t d = (A << 8) | B; SETNZ16(d); SETV(0); wr16(ea, d);       // STD
        }
        break;
    case 0xe:
        if (!bgrp) { uint16_t m = op16(); S = m; SETNZ16(m); SETV(0); }                                // LDS
        else { uint16_t m = op16(); X = m; SETNZ16(m); SETV(0); }                                      // LDX
        break;
    case 0xf:
        if (mode == 0) { trap(); break; }
        { uint16_t ea = ea_of(); uint16_t v = bgrp ? X : S; SETNZ16(v); SETV(0); wr16(ea, v); }        // STX / STS
        break;
    }
    return cyc;
}
