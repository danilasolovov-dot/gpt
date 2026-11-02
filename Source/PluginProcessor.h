#pragma once

#include <JuceHeader.h>
#include <atomic>
#include "VinylEngine.h"

class VinylPilotProcessor final : public juce::AudioProcessor {
public:
    VinylPilotProcessor();
    ~VinylPilotProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    bool isTransportPlaying() const noexcept { return transportPlaying.load(std::memory_order_relaxed); }
    float displayRate() const noexcept { return renderedRate.load(std::memory_order_relaxed); }
    bool isReturning() const noexcept { return returning.load(std::memory_order_relaxed); }
    void setManualTouch(bool touch) noexcept {
        manualTouch.store(touch, std::memory_order_relaxed);
    }

    juce::AudioProcessorValueTreeState parameters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    vinylpilot::VinylEngine engine;
    std::atomic<float>* surface = nullptr;
    std::atomic<float>* crackle = nullptr;
    std::atomic<float>* wear = nullptr;
    std::atomic<float>* mix = nullptr;
    std::atomic<float>* scratch = nullptr;
    std::atomic<float>* returnMs = nullptr;
    std::atomic<float>* bendDepth = nullptr;
    std::atomic<float>* pitchInput = nullptr;

    float lastPitchWheel = 0.0f;
    bool lastTransportState = false;
    int64_t nextExpectedHostSample = -1;
    std::atomic<bool> transportPlaying { false };
    std::atomic<float> renderedRate { 1.0f };
    std::atomic<bool> returning { false };
    std::atomic<bool> manualTouch { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VinylPilotProcessor)
};
