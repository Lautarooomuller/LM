#include "PluginProcessor.h"

namespace
{
using Params = std::vector<std::unique_ptr<juce::RangedAudioParameter>>;

void addF (Params& v, const juce::String& id, const juce::String& name,
           float lo, float hi, float def, const juce::String& unit = {}, float skew = 1.0f)
{
    v.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, name,
        juce::NormalisableRange<float> (lo, hi, 0.01f, skew), def,
        juce::AudioParameterFloatAttributes().withLabel (unit)));
}

void addB (Params& v, const juce::String& id, const juce::String& name, bool def)
{
    v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def));
}

void addC (Params& v, const juce::String& id, const juce::String& name,
           const juce::StringArray& choices, int def)
{
    v.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, choices, def));
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout VocalChainProcessor::createLayout()
{
    Params p;

    // ---- Global
    addF (p, "out_gain", "Salida", -24.0f, 24.0f, 0.0f, "dB");
    addC (p, "add_pos", "EQ aditiva: posicion",
          { "Antes del compresor", "Despues del compresor" }, 1);

    // ---- 1. Noise gate
    addB (p, "gate_on",    "Gate ON", true);
    addF (p, "gate_thr",   "Gate Umbral",    -80.0f, -10.0f, -55.0f, "dB");
    addF (p, "gate_range", "Gate Reduccion", -80.0f,   0.0f, -25.0f, "dB");
    addF (p, "gate_att",   "Gate Ataque",      0.05f, 20.0f,  0.5f, "ms", 0.5f);
    addF (p, "gate_hold",  "Gate Hold",        0.0f, 300.0f, 40.0f, "ms");
    addF (p, "gate_rel",   "Gate Release",    10.0f, 800.0f, 150.0f, "ms", 0.5f);

    // ---- 2. Preamp (estilo Neve 1073)
    addB (p, "pre_on",    "Preamp ON", true);
    addF (p, "pre_gain",  "Preamp Ganancia",   -12.0f, 30.0f, 0.0f, "dB");
    addF (p, "pre_drive", "Preamp Saturacion",   0.0f, 100.0f, 20.0f, "%");
    addC (p, "pre_hpf",   "Preamp HPF", { "Off", "50 Hz", "80 Hz", "160 Hz", "300 Hz" }, 0);
    addF (p, "pre_low",   "Preamp Low shelf 110 Hz",  -16.0f, 16.0f, 0.0f, "dB");
    addF (p, "pre_high",  "Preamp High shelf 12 kHz", -16.0f, 16.0f, 0.0f, "dB");

    // ---- 3. Micmod (AKG P120 -> tipo Manley Reference)
    addB (p, "mic_on",   "MicMod ON", true);
    addF (p, "mic_amt",  "MicMod Cantidad", 0.0f, 100.0f, 60.0f, "%");
    addF (p, "mic_tube", "MicMod Valvula",  0.0f, 100.0f, 25.0f, "%");

    // ---- 4. De-esser
    addB (p, "des_on",    "De-esser ON", true);
    addF (p, "des_freq",  "De-esser Frecuencia", 3000.0f, 10000.0f, 6500.0f, "Hz", 0.6f);
    addF (p, "des_thr",   "De-esser Umbral",  -60.0f,  0.0f, -30.0f, "dB");
    addF (p, "des_ratio", "De-esser Ratio",     1.5f, 10.0f,   4.0f);
    addF (p, "des_range", "De-esser Max reduccion", 0.0f, 20.0f, 8.0f, "dB");

    // ---- 5. EQ correctiva
    addB (p, "eq_on",     "EQ correctiva ON", true);
    addF (p, "eq_hpf",    "EQ Corte graves",  20.0f, 200.0f, 80.0f, "Hz", 0.5f);
    addF (p, "eq_boom_f", "EQ Boomy Freq",   120.0f, 500.0f, 250.0f, "Hz", 0.6f);
    addF (p, "eq_boom_g", "EQ Boomy Gain",    -9.0f,   0.0f,  -2.5f, "dB");
    addF (p, "eq_boom_q", "EQ Boomy Q",        0.4f,   4.0f,   1.1f);
    addF (p, "eq_nas_f",  "EQ Nasal Freq",   500.0f, 2500.0f, 1000.0f, "Hz", 0.6f);
    addF (p, "eq_nas_g",  "EQ Nasal Gain",    -9.0f,   0.0f,  -2.0f, "dB");
    addF (p, "eq_nas_q",  "EQ Nasal Q",        0.5f,   8.0f,   2.0f);

    // ---- 6. Compresor (estilo Tube-Tech CL1B)
    addB (p, "cmp_on",     "Comp ON", true);
    addF (p, "cmp_thr",    "Comp Umbral",  -50.0f,   0.0f, -20.0f, "dB");
    addF (p, "cmp_ratio",  "Comp Ratio",     1.5f,  20.0f,   3.5f);
    addF (p, "cmp_att",    "Comp Ataque",    0.3f, 150.0f,  12.0f, "ms", 0.5f);
    addF (p, "cmp_rel",    "Comp Release",  30.0f, 2000.0f, 250.0f, "ms", 0.5f);
    addF (p, "cmp_makeup", "Comp Makeup",    0.0f,  24.0f,   4.0f, "dB");
    addF (p, "cmp_tube",   "Comp Valvula",   0.0f, 100.0f,  20.0f, "%");
    addF (p, "cmp_mix",    "Comp Mezcla (paralelo)", 0.0f, 100.0f, 100.0f, "%");

    // ---- 7. EQ aditiva
    addB (p, "add_on",     "EQ aditiva ON", true);
    addF (p, "add_body",   "Aditiva Cuerpo 180 Hz", 0.0f,  8.0f, 1.0f, "dB");
    addF (p, "add_pres_f", "Aditiva Presencia Freq", 1500.0f, 7000.0f, 3500.0f, "Hz", 0.6f);
    addF (p, "add_pres_g", "Aditiva Presencia Gain", 0.0f,  8.0f, 1.5f, "dB");
    addF (p, "add_air",    "Aditiva Aire 12 kHz",    0.0f, 10.0f, 2.5f, "dB");
    addF (p, "add_harm",   "Aditiva Armonicos",      0.0f, 100.0f, 15.0f, "%");
    addF (p, "add_harm_f", "Aditiva Armonicos Freq", 2000.0f, 9000.0f, 4500.0f, "Hz", 0.6f);

    return { p.begin(), p.end() };
}

VocalChainProcessor::VocalChainProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

bool VocalChainProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == l.getMainInputChannelSet();
}

void VocalChainProcessor::prepareToPlay (double sr, int)
{
    gate.prepare (sr); pre.prepare (sr); mic.prepare (sr); des.prepare (sr);
    eq.prepare (sr);   comp.prepare (sr); add.prepare (sr);
    lastOutGain = vc::dbToLin (get ("out_gain"));
}

void VocalChainProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // ---- leer parametros
    const vc::GateParams gp { on ("gate_on"), get ("gate_thr"), get ("gate_range"),
                              get ("gate_att"), get ("gate_hold"), get ("gate_rel") };

    const vc::PreParams pp { on ("pre_on"), get ("pre_gain"), get ("pre_drive") * 0.01f,
                             (int) get ("pre_hpf"), get ("pre_low"), get ("pre_high") };

    const vc::MicParams mp { on ("mic_on"), get ("mic_amt") * 0.01f, get ("mic_tube") * 0.01f };

    const vc::DesParams dp { on ("des_on"), get ("des_freq"), get ("des_thr"),
                             get ("des_ratio"), get ("des_range") };

    const vc::EqParams ep { on ("eq_on"), get ("eq_hpf"),
                            get ("eq_boom_f"), get ("eq_boom_g"), get ("eq_boom_q"),
                            get ("eq_nas_f"),  get ("eq_nas_g"),  get ("eq_nas_q") };

    const vc::CompParams cp { on ("cmp_on"), get ("cmp_thr"), get ("cmp_ratio"), get ("cmp_att"),
                              get ("cmp_rel"), get ("cmp_makeup"),
                              get ("cmp_tube") * 0.01f, get ("cmp_mix") * 0.01f };

    const vc::AddParams ap { on ("add_on"), get ("add_body"), get ("add_pres_f"), get ("add_pres_g"),
                             get ("add_air"), get ("add_harm") * 0.01f, get ("add_harm_f") };

    const bool addBeforeComp = (int) get ("add_pos") == 0;

    // ---- cadena
    gate.process (buffer, gp);
    pre.process  (buffer, pp);
    mic.process  (buffer, mp);
    des.process  (buffer, dp);
    eq.process   (buffer, ep);
    if (addBeforeComp) add.process (buffer, ap);
    comp.process (buffer, cp);
    if (! addBeforeComp) add.process (buffer, ap);

    // ---- salida (con rampa para evitar clicks)
    const float outGain = vc::dbToLin (get ("out_gain"));
    buffer.applyGainRamp (0, buffer.getNumSamples(), lastOutGain, outGain);
    lastOutGain = outGain;
}

juce::AudioProcessorEditor* VocalChainProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

void VocalChainProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void VocalChainProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocalChainProcessor();
}
