#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>

struct Pattern
{
    std::array<float, 16> kick{};      // 0 = sem hit
    std::array<int, 16>   bassNote{};  // nota MIDI, 0 = sem nota
    std::array<float, 16> bassVel{};
    std::array<float, 16> kickProb{};  // probabilidades aprendidas (modelo)
    std::array<float, 16> bassProb{};
    std::vector<std::pair<int, float>> pool; // notas do baixo e pesos
};

class KickBassProcessor : public juce::AudioProcessor
{
public:
    KickBassProcessor();
    ~KickBassProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "KickBassForge"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    bool loadAnalysis(const juce::File& jsonFile);
    void generateVariation();

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    static juce::var patternToVar(const Pattern&);
    static void varToPattern(const juce::var&, Pattern&);
    void initDefaultPattern();
    void setParam(const char* id, double value);
    float getParam(const char* id) const { return apvts.getRawParameterValue(id)->load(); }

    Pattern pattern;
    juce::SpinLock lock;

    double sr = 44100.0, internalPpq = 0.0;
    int lastStep = -1000000;

    // kick
    bool kActive = false; double kT = 0.0, kPhase = 0.0; float kVel = 1.f, clickLp = 0.f;
    // sidechain
    float duckEnv = 0.f, duckSm = 0.f;
    // baixo
    double bPhase = 0.0; float bFreq = 55.f, bEnv = 0.f; bool bGate = false; int bGateSamples = 0;
    juce::dsp::StateVariableTPTFilter<float> bFilter;
    juce::Random rng;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KickBassProcessor)
};
