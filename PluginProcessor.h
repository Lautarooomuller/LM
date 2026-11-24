#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Dsp.h"

class VocalChainProcessor : public juce::AudioProcessor
{
public:
    VocalChainProcessor();
    ~VocalChainProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "LM"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    float get (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    bool  on  (const char* id) const { return get (id) > 0.5f; }

    vc::Gate         gate;
    vc::Preamp       pre;
    vc::MicMod       mic;
    vc::DeEsser      des;
    vc::CorrectiveEq eq;
    vc::Compressor   comp;
    vc::AdditiveEq   add;

    float lastOutGain = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalChainProcessor)
};
