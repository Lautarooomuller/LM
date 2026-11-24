#pragma once

#include <algorithm>
#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>

// Módulos DSP de la vocal chain. Todos son procesamiento "inspirado en" el equipo
// original (no es un modelado de componentes circuito por circuito).

namespace vc
{
constexpr int kMaxCh = 2;
constexpr double kPi = 3.14159265358979323846;

inline float dbToLin (float db) { return std::pow (10.0f, db * 0.05f); }
inline float linToDb (float x)  { return 20.0f * std::log10 (std::max (x, 1.0e-9f)); }

// coeficiente de un filtro de un polo para un tiempo en ms
inline float coefMs (float ms, double sr)
{
    return std::exp (-1.0f / (std::max (ms, 0.01f) * 0.001f * (float) sr));
}

// Saturación asimétrica (armónicos pares e impares), ganancia unitaria para señales chicas.
// drive: 0..1
inline float asymSat (float x, float drive)
{
    if (drive <= 0.0f)
        return x;

    const float k  = 1.0f + drive * 6.0f;
    const float b  = 0.15f * drive;
    const float tb = std::tanh (b);
    return (std::tanh (k * x + b) - tb) / (k * (1.0f - tb * tb));
}

// Bloqueo de continua (la saturación asimétrica genera algo de DC)
struct DcBlock
{
    void prepare (double sr)
    {
        r = 1.0f - (float) (2.0 * kPi * 10.0 / sr);
        x1[0] = x1[1] = y1[0] = y1[1] = 0.0f;
    }

    float process (int ch, float x)
    {
        const float y = x - x1[ch] + r * y1[ch];
        x1[ch] = x;
        y1[ch] = y;
        return y;
    }

    float r = 0.999f;
    float x1[kMaxCh] {}, y1[kMaxCh] {};
};

// Biquad (RBJ cookbook), estado por canal, sin alocaciones
struct Biquad
{
    enum Type { LowPass, HighPass, Peak, LowShelf, HighShelf };

    void invalidate()
    {
        lastType = -1;
        z1[0] = z1[1] = z2[0] = z2[1] = 0.0;
    }

    void set (Type t, double sr, double f, double q, double gainDb)
    {
        if ((int) t == lastType && f == lf && q == lq && gainDb == lg && sr == lsr)
            return;

        lastType = (int) t; lf = f; lq = q; lg = gainDb; lsr = sr;

        const double w0 = 2.0 * kPi * std::min (f, sr * 0.49) / sr;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * q);
        const double A = std::pow (10.0, gainDb / 40.0);
        double nb0 = 1, nb1 = 0, nb2 = 0, na0 = 1, na1 = 0, na2 = 0;

        switch (t)
        {
            case LowPass:
                nb0 = (1.0 - cw) * 0.5; nb1 = 1.0 - cw; nb2 = nb0;
                na0 = 1.0 + alpha; na1 = -2.0 * cw; na2 = 1.0 - alpha;
                break;
            case HighPass:
                nb0 = (1.0 + cw) * 0.5; nb1 = -(1.0 + cw); nb2 = nb0;
                na0 = 1.0 + alpha; na1 = -2.0 * cw; na2 = 1.0 - alpha;
                break;
            case Peak:
                nb0 = 1.0 + alpha * A; nb1 = -2.0 * cw; nb2 = 1.0 - alpha * A;
                na0 = 1.0 + alpha / A; na1 = -2.0 * cw; na2 = 1.0 - alpha / A;
                break;
            case LowShelf:
            {
                const double sa = 2.0 * std::sqrt (A) * (sw * 0.70710678118);
                nb0 = A * ((A + 1.0) - (A - 1.0) * cw + sa);
                nb1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cw);
                nb2 = A * ((A + 1.0) - (A - 1.0) * cw - sa);
                na0 = (A + 1.0) + (A - 1.0) * cw + sa;
                na1 = -2.0 * ((A - 1.0) + (A + 1.0) * cw);
                na2 = (A + 1.0) + (A - 1.0) * cw - sa;
                break;
            }
            case HighShelf:
            {
                const double sa = 2.0 * std::sqrt (A) * (sw * 0.70710678118);
                nb0 = A * ((A + 1.0) + (A - 1.0) * cw + sa);
                nb1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw);
                nb2 = A * ((A + 1.0) + (A - 1.0) * cw - sa);
                na0 = (A + 1.0) - (A - 1.0) * cw + sa;
                na1 = 2.0 * ((A - 1.0) - (A + 1.0) * cw);
                na2 = (A + 1.0) - (A - 1.0) * cw - sa;
                break;
            }
        }

        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0;
        a1 = na1 / na0; a2 = na2 / na0;
    }

    inline float process (int ch, float x)
    {
        const double in  = x;
        const double out = b0 * in + z1[ch];
        z1[ch] = b1 * in - a1 * out + z2[ch];
        z2[ch] = b2 * in - a2 * out;
        return (float) out;
    }

    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1[kMaxCh] {}, z2[kMaxCh] {};
    int lastType = -1;
    double lf = 0, lq = 0, lg = 0, lsr = 0;
};

// ---------------------------------------------------------------- NOISE GATE
struct GateParams { bool on; float thr, range, att, hold, rel; };

class Gate
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        env = 0.0f; gain = 1.0f; holdCnt = 0; open = false;
    }

    void process (juce::AudioBuffer<float>& b, const GateParams& p)
    {
        if (! p.on) return;

        const int n = b.getNumSamples(), nc = std::min (b.getNumChannels(), kMaxCh);
        auto d = b.getArrayOfWritePointers();

        const float thrLin = dbToLin (p.thr), closeLin = dbToLin (p.thr - 4.0f);
        const float rangeLin = dbToLin (p.range);
        const float envRel = coefMs (20.0f, sr);
        const float attC = coefMs (p.att, sr), relC = coefMs (p.rel, sr);
        const int hold = (int) (p.hold * 0.001 * sr);

        for (int i = 0; i < n; ++i)
        {
            float x = 0.0f;
            for (int ch = 0; ch < nc; ++ch)
                x = std::max (x, std::abs (d[ch][i]));

            env = x > env ? x : x + (env - x) * envRel;

            if (env > thrLin)              { open = true; holdCnt = hold; }
            else if (open)
            {
                if (env > closeLin)        holdCnt = hold;
                else if (holdCnt > 0)      --holdCnt;
                else                       open = false;
            }

            const float target = open ? 1.0f : rangeLin;
            const float c = target > gain ? attC : relC;
            gain = target + (gain - target) * c;

            for (int ch = 0; ch < nc; ++ch)
                d[ch][i] *= gain;
        }
    }

private:
    double sr = 44100.0;
    float env = 0, gain = 1;
    int holdCnt = 0;
    bool open = false;
};

// ------------------------------------------------------- PREAMP (estilo 1073)
struct PreParams { bool on; float gainDb, drive /*0..1*/; int hpfSel; float lowDb, highDb; };

class Preamp
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        hpf.invalidate(); low.invalidate(); high.invalidate();
        dc.prepare (sr);
    }

    void process (juce::AudioBuffer<float>& b, const PreParams& p)
    {
        if (! p.on) return;

        static const double hpfFreqs[] = { 0.0, 50.0, 80.0, 160.0, 300.0 };
        const int sel = juce::jlimit (0, 4, p.hpfSel);
        const bool useHpf = sel > 0;
        const bool useLow = std::abs (p.lowDb) > 0.01f;
        const bool useHigh = std::abs (p.highDb) > 0.01f;

        if (useHpf)  hpf.set (Biquad::HighPass, sr, hpfFreqs[sel], 0.7071, 0.0);
        if (useLow)  low.set (Biquad::LowShelf, sr, 110.0, 0.7071, p.lowDb);
        if (useHigh) high.set (Biquad::HighShelf, sr, 12000.0, 0.7071, p.highDb);

        const float g = dbToLin (p.gainDb);
        const int n = b.getNumSamples(), nc = std::min (b.getNumChannels(), kMaxCh);
        auto d = b.getArrayOfWritePointers();

        for (int i = 0; i < n; ++i)
            for (int ch = 0; ch < nc; ++ch)
            {
                float x = d[ch][i] * g;
                if (useHpf) x = hpf.process (ch, x);
                if (useLow) x = low.process (ch, x);
                if (p.drive > 0.0f) x = dc.process (ch, asymSat (x, p.drive)); // "hierro" del transformador
                if (useHigh) x = high.process (ch, x);
                d[ch][i] = x;
            }
    }

private:
    double sr = 44100.0;
    Biquad hpf, low, high;
    DcBlock dc;
};

// ------------------------------------------- MICMOD (P120 -> "tipo Manley Ref")
struct MicParams { bool on; float amt /*0..1*/, tube /*0..1*/; };

class MicMod
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        low.invalidate(); dip.invalidate(); pres.invalidate(); air.invalidate();
        dc.prepare (sr);
    }

    void process (juce::AudioBuffer<float>& b, const MicParams& p)
    {
        if (! p.on) return;

        // Curva aproximada (punto de partida, ajustar de oído):
        // más cuerpo abajo, suaviza el medio-agudo del P120, presencia sedosa y aire.
        low.set  (Biquad::LowShelf,  sr, 120.0,   0.7071, 1.5 * p.amt);
        dip.set  (Biquad::Peak,      sr, 2800.0,  0.8,   -1.0 * p.amt);
        pres.set (Biquad::Peak,      sr, 7000.0,  0.7,    1.5 * p.amt);
        air.set  (Biquad::HighShelf, sr, 12000.0, 0.7071, 2.0 * p.amt);

        const int n = b.getNumSamples(), nc = std::min (b.getNumChannels(), kMaxCh);
        auto d = b.getArrayOfWritePointers();

        for (int i = 0; i < n; ++i)
            for (int ch = 0; ch < nc; ++ch)
            {
                float x = d[ch][i];
                x = low.process (ch, x);
                x = dip.process (ch, x);
                x = pres.process (ch, x);
                x = air.process (ch, x);
                if (p.tube > 0.0f) x = dc.process (ch, asymSat (x, p.tube * 0.6f)); // calidez "de válvula"
                d[ch][i] = x;
            }
    }

private:
    double sr = 44100.0;
    Biquad low, dip, pres, air;
    DcBlock dc;
};

// ------------------------------------------------------------------ DE-ESSER
struct DesParams { bool on; float freq, thr, ratio, range; };

class DeEsser
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        lp1.invalidate(); lp2.invalidate(); hp1.invalidate(); hp2.invalidate();
        env = 0.0f;
    }

    void process (juce::AudioBuffer<float>& b, const DesParams& p)
    {
        if (! p.on) return;

        // Crossover Linkwitz-Riley de 4º orden: la banda alta se comprime, la baja pasa intacta
        lp1.set (Biquad::LowPass,  sr, p.freq, 0.7071, 0.0);
        lp2.set (Biquad::LowPass,  sr, p.freq, 0.7071, 0.0);
        hp1.set (Biquad::HighPass, sr, p.freq, 0.7071, 0.0);
        hp2.set (Biquad::HighPass, sr, p.freq, 0.7071, 0.0);

        const int n = b.getNumSamples(), nc = std::min (b.getNumChannels(), kMaxCh);
        auto d = b.getArrayOfWritePointers();
        const float attC = coefMs (0.5f, sr), relC = coefMs (40.0f, sr);

        for (int i = 0; i < n; ++i)
        {
            float lo[kMaxCh] {}, hi[kMaxCh] {};
            float det = 0.0f;

            for (int ch = 0; ch < nc; ++ch)
            {
                const float x = d[ch][i];
                lo[ch] = lp2.process (ch, lp1.process (ch, x));
                hi[ch] = hp2.process (ch, hp1.process (ch, x));
                det = std::max (det, std::abs (hi[ch]));
            }

            env = det > env ? det + (env - det) * attC : det + (env - det) * relC;

            const float over = linToDb (env) - p.thr;
            float grDb = over > 0.0f ? over * (1.0f - 1.0f / p.ratio) : 0.0f;
            grDb = std::min (grDb, p.range);
            const float g = dbToLin (-grDb);

            for (int ch = 0; ch < nc; ++ch)
                d[ch][i] = lo[ch] + hi[ch] * g;
        }
    }

private:
    double sr = 44100.0;
    Biquad lp1, lp2, hp1, hp2;
    float env = 0.0f;
};

// ------------------------------------------------------------- EQ CORRECTIVA
struct EqParams { bool on; float hpf, boomF, boomG, boomQ, nasF, nasG, nasQ; };

class CorrectiveEq
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        hp1.invalidate(); hp2.invalidate(); boom.invalidate(); nas.invalidate();
    }

    void process (juce::AudioBuffer<float>& b, const EqParams& p)
    {
        if (! p.on) return;

        // HPF Butterworth de 4º orden (24 dB/oct)
        hp1.set (Biquad::HighPass, sr, p.hpf, 0.5412, 0.0);
        hp2.set (Biquad::HighPass, sr, p.hpf, 1.3066, 0.0);

        const bool useBoom = std::abs (p.boomG) > 0.05f;
        const bool useNas  = std::abs (p.nasG)  > 0.05f;
        if (useBoom) boom.set (Biquad::Peak, sr, p.boomF, p.boomQ, p.boomG);
        if (useNas)  nas.set  (Biquad::Peak, sr, p.nasF,  p.nasQ,  p.nasG);

        const int n = b.getNumSamples(), nc = std::min (b.getNumChannels(), kMaxCh);
        auto d = b.getArrayOfWritePointers();

        for (int i = 0; i < n; ++i)
            for (int ch = 0; ch < nc; ++ch)
            {
                float x = hp2.process (ch, hp1.process (ch, d[ch][i]));
                if (useBoom) x = boom.process (ch, x);
                if (useNas)  x = nas.process (ch, x);
                d[ch][i] = x;
            }
    }

private:
    double sr = 44100.0;
    Biquad hp1, hp2, boom, nas;
};

// ------------------------------------------------- COMPRESOR (estilo CL1B, opto)
struct CompParams { bool on; float thr, ratio, att, rel, makeup, tube /*0..1*/, mix /*0..1*/; };

class Compressor
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        grA = grB = ms = 0.0f;
        dc.prepare (sr);
    }

    void process (juce::AudioBuffer<float>& b, const CompParams& p)
    {
        if (! p.on) return;

        const int n = b.getNumSamples(), nc = std::min (b.getNumChannels(), kMaxCh);
        auto d = b.getArrayOfWritePointers();

        const float rmsC  = coefMs (10.0f, sr);
        const float attC  = coefMs (p.att, sr);
        const float relFC = coefMs (p.rel * 0.3f, sr);  // liberación rápida
        const float relSC = coefMs (p.rel * 2.0f, sr);  // liberación lenta (dependiente del programa)
        const float slope = 1.0f - 1.0f / p.ratio;
        const float knee = 6.0f;
        const float makeup = dbToLin (p.makeup);

        for (int i = 0; i < n; ++i)
        {
            float lv = 0.0f;
            for (int ch = 0; ch < nc; ++ch)
                lv = std::max (lv, std::abs (d[ch][i]));

            const float sq = lv * lv;
            ms = sq + (ms - sq) * rmsC;
            const float lvDb = 10.0f * std::log10 (ms + 1.0e-12f);

            const float over = lvDb - p.thr;
            float target;
            if (2.0f * over < -knee)              target = 0.0f;
            else if (2.0f * std::abs (over) <= knee)
            {
                const float t = over + knee * 0.5f;
                target = slope * t * t / (2.0f * knee);
            }
            else                                  target = slope * over;

            grA += (target - grA) * (1.0f - (target > grA ? attC : relFC));
            grB += (target - grB) * (1.0f - (target > grB ? attC : relSC));
            const float gr = 0.6f * grA + 0.4f * grB;
            const float g = dbToLin (-gr) * makeup;

            for (int ch = 0; ch < nc; ++ch)
            {
                const float dry = d[ch][i];
                float wet = dry * g;
                if (p.tube > 0.0f) wet = dc.process (ch, asymSat (wet, p.tube * 0.5f));
                d[ch][i] = dry + (wet - dry) * p.mix;
            }
        }
    }

private:
    double sr = 44100.0;
    float grA = 0, grB = 0, ms = 0;
    DcBlock dc;
};

// ------------------------------------------------------------------ EQ ADITIVA
struct AddParams { bool on; float bodyDb, presF, presDb, airDb, harm /*0..1*/, harmF; };

class AdditiveEq
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        body.invalidate(); pres.invalidate(); air.invalidate();
        hx.invalidate(); hy.invalidate();
    }

    void process (juce::AudioBuffer<float>& b, const AddParams& p)
    {
        if (! p.on) return;

        const bool useBody = p.bodyDb > 0.05f, usePres = p.presDb > 0.05f, useAir = p.airDb > 0.05f;
        const bool useHarm = p.harm > 0.0f;

        if (useBody) body.set (Biquad::Peak, sr, 180.0, 0.8, p.bodyDb);
        if (usePres) pres.set (Biquad::Peak, sr, p.presF, 0.9, p.presDb);
        if (useAir)  air.set  (Biquad::HighShelf, sr, 12000.0, 0.7071, p.airDb);
        if (useHarm)
        {
            hx.set (Biquad::HighPass, sr, p.harmF, 0.7071, 0.0);
            hy.set (Biquad::HighPass, sr, p.harmF * 2.0, 0.7071, 0.0);
        }

        const int n = b.getNumSamples(), nc = std::min (b.getNumChannels(), kMaxCh);
        auto d = b.getArrayOfWritePointers();

        for (int i = 0; i < n; ++i)
            for (int ch = 0; ch < nc; ++ch)
            {
                float x = d[ch][i];
                float harmonics = 0.0f;

                if (useHarm)
                {
                    // exciter: satura la banda alta y se queda solo con lo nuevo que aparece arriba
                    const float h = hx.process (ch, x);
                    harmonics = hy.process (ch, std::tanh (4.0f * h) * 0.25f);
                }

                if (useBody) x = body.process (ch, x);
                if (usePres) x = pres.process (ch, x);
                if (useAir)  x = air.process (ch, x);

                d[ch][i] = x + harmonics * p.harm * 0.5f;
            }
    }

private:
    double sr = 44100.0;
    Biquad body, pres, air, hx, hy;
};

} // namespace vc
