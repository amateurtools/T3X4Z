// Windowed-sinc streaming resampler (native OPZ rate -> host rate), stereo.
#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

class SincResampler {
public:
    static constexpr int HALF = 12;      // kernel half-width in (cutoff-scaled) zero crossings
    static constexpr int RES = 512;      // table points per zero crossing

    SincResampler() { buildTable(); }

    // ratio = input rate / output rate
    void setRatio(double r)
    {
        ratio = r;
        fc = std::min(1.0, 1.0 / r) * 0.96;           // anti-alias cutoff (relative to input Nyquist)
        halfTaps = (int)std::ceil(HALF / fc);
        reset();
    }
    void reset()
    {
        inL.assign((size_t)halfTaps + 2, 0.f); inR.assign((size_t)halfTaps + 2, 0.f);
        inL.reserve(1 << 16); inR.reserve(1 << 16);
        pos = (double)halfTaps;                          // absolute index in in-buffers (relative to base)
    }
    double getRatio() const { return ratio; }

    // How many more input frames are needed before one more output can be produced
    bool needsInput() const { return (int)pos + halfTaps + 1 >= (int)inL.size(); }
    void pushInput(const float *interleaved, int frames)
    {
        for (int i = 0; i < frames; i++) { inL.push_back(interleaved[2 * i]); inR.push_back(interleaved[2 * i + 1]); }
    }
    // produce one stereo output frame (call only when !needsInput())
    void pull(float &l, float &r)
    {
        int c = (int)pos; double frac = pos - c;
        double sl = 0, sr = 0;
        for (int n = c - halfTaps + 1; n <= c + halfTaps; n++) {
            double d = (double)n - (c + frac);            // distance in input samples
            double w = kernel(d * fc) * fc;
            sl += w * inL[(size_t)n]; sr += w * inR[(size_t)n];
        }
        l = (float)sl; r = (float)sr;
        pos += ratio;
        // discard consumed history now and then
        int drop = (int)pos - halfTaps - 2;
        if (drop > 4096) { inL.erase(inL.begin(), inL.begin() + drop); inR.erase(inR.begin(), inR.begin() + drop); pos -= drop; }
    }

private:
    double ratio = 1.0, fc = 1.0, pos = 0;
    int halfTaps = HALF;
    std::vector<float> inL, inR;
    std::vector<float> table;

    void buildTable()
    {
        const double PI = 3.14159265358979323846;
        table.assign((size_t)(HALF * RES + 2), 0.f);
        auto i0 = [](double x) { double s = 1, t = 1; for (int k = 1; k < 30; k++) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; };
        const double beta = 9.0, norm = i0(beta);
        for (size_t i = 0; i < table.size(); i++) {
            double u = (double)i / RES;                  // 0..HALF
            double sinc = (u == 0) ? 1.0 : std::sin(PI * u) / (PI * u);
            double x = u / HALF; double win = (x >= 1) ? 0 : i0(beta * std::sqrt(1 - x * x)) / norm;
            table[i] = (float)(sinc * win);
        }
    }
    double kernel(double u) const
    {
        u = std::fabs(u);
        if (u >= HALF) return 0.0;
        double p = u * RES; size_t i = (size_t)p; double f = p - (double)i;
        return table[i] * (1 - f) + table[i + 1] * f;
    }
};
