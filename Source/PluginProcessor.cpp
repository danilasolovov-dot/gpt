#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <cstdlib>

namespace {
std::unique_ptr<juce::AudioParameterFloat> floatParameter(
    const char* id, const char* label, float minimum, float maximum,
    float step, float initial) {
    return std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, label,
        juce::NormalisableRange<float> { minimum, maximum, step }, initial);
}
}

juce::AudioProcessorValueTreeState::ParameterLayout
VinylPilotProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(floatParameter("surface", "Surface Noise", 0.0f, 1.0f, 0.001f, 0.18f));
    layout.add(floatParameter("crackle", "Dust Crackle", 0.0f, 1.0f, 0.001f, 0.16f));
    layout.add(floatParameter("wear", "Vinyl Wear", 0.0f, 1.0f, 0.001f, 0.12f));
    layout.add(floatParameter("mix", "Effect Mix", 0.0f, 1.0f, 0.001f, 1.0f));
    layout.add(floatParameter("scratch", "Scratch Speed", -3.0f, 3.0f, 0.001f, 1.0f));
    layout.add(floatParameter("return", "Catch-up Time", 30.0f, 4000.0f, 1.0f, 450.0f));
    layout.add(floatParameter("bendDepth", "Pitch Wheel Range", 0.0f, 4.0f, 0.01f, 2.0f));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "pitchInput", 1 }, "Receive Pitch Bend", true));
    return layout;
}

VinylPilotProcessor::VinylPilotProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "VinylPilotState", createParameterLayout()) {
    surface = parameters.getRawParameterValue("surface");
    crackle = parameters.getRawParameterValue("crackle");
    wear = parameters.getRawParameterValue("wear");
    mix = parameters.getRawParameterValue("mix");
    scratch = parameters.getRawParameterValue("scratch");
    returnMs = parameters.getRawParameterValue("return");
    bendDepth = parameters.getRawParameterValue("bendDepth");
    pitchInput = parameters.getRawParameterValue("pitchInput");
}

void VinylPilotProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/) {
    engine.prepare(sampleRate, getTotalNumInputChannels());
    setLatencySamples(engine.latencySamples());
    lastPitchWheel = 0.0f;
    lastTransportState = false;
    nextExpectedHostSample = -1;
}

void VinylPilotProcessor::releaseResources() {
    nextExpectedHostSample = -1;
}

bool VinylPilotProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto output = layouts.getMainOutputChannelSet();
    return (output == juce::AudioChannelSet::mono() ||
            output == juce::AudioChannelSet::stereo()) &&
           layouts.getMainInputChannelSet() == output;
}

void VinylPilotProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                       juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;
    const auto samples = buffer.getNumSamples();
    const auto inputChannels = buffer.getNumChannels();
    if (inputChannels == 0 || samples <= 0) return;

    bool isPlaying = true; // Hosts without transport APIs can still process audio.
    if (auto* playhead = getPlayHead()) {
        if (auto position = playhead->getPosition()) {
            isPlaying = position->getIsPlaying();
            if (auto hostSample = position->getTimeInSamples()) {
                if (isPlaying && nextExpectedHostSample >= 0 &&
                    std::llabs(*hostSample - nextExpectedHostSample) > samples * 2LL) {
                    // Looping, rewinding or jumping: old audio is no longer
                    // synchronized to the source timeline.
                    engine.reset();
                }
                nextExpectedHostSample = isPlaying ? (*hostSample + samples) : -1;
            } else {
                nextExpectedHostSample = -1;
            }
        }
    }

    if (isPlaying && !lastTransportState)
        engine.reset();
    if (!isPlaying)
        nextExpectedHostSample = -1;
    lastTransportState = isPlaying;
    transportPlaying.store(isPlaying, std::memory_order_relaxed);

    vinylpilot::Settings s;
    s.surface = surface->load(std::memory_order_relaxed);
    s.crackle = crackle->load(std::memory_order_relaxed);
    s.wear = wear->load(std::memory_order_relaxed);
    s.mix = mix->load(std::memory_order_relaxed);
    s.returnMs = returnMs->load(std::memory_order_relaxed);
    s.playing = isPlaying;

    auto* left = buffer.getWritePointer(0);
    auto* right = inputChannels == 2 ? buffer.getWritePointer(1) : nullptr;

    auto midiIterator = midiMessages.begin();
    const auto midiEnd = midiMessages.end();
    for (int i = 0; i < samples; ++i) {
        while (midiIterator != midiEnd && (*midiIterator).samplePosition <= i) {
            const auto message = (*midiIterator).getMessage();
            if (message.isPitchWheel()) {
                lastPitchWheel = std::clamp(
                    (static_cast<float>(message.getPitchWheelValue()) - 8192.0f) / 8192.0f,
                    -1.0f, 1.0f);
                if (std::abs(lastPitchWheel) < 0.012f)
                    lastPitchWheel = 0.0f;
            }
            ++midiIterator;
        }

        s.scratchRate = scratch->load(std::memory_order_relaxed);
        s.forceScratch = manualTouch.load(std::memory_order_relaxed);
        if (pitchInput->load(std::memory_order_relaxed) > 0.5f &&
            std::abs(lastPitchWheel) >= 0.012f) {
            s.scratchRate = std::clamp(1.0f + lastPitchWheel *
                                    bendDepth->load(std::memory_order_relaxed),
                                    -3.0f, 3.0f);
        }

        float outL = 0.0f, outR = 0.0f;
        engine.processSample(left[i], right != nullptr ? right[i] : left[i],
                             s, outL, outR);
        left[i] = outL;
        if (right != nullptr) right[i] = outR;
    }

    renderedRate.store(engine.currentRate(), std::memory_order_relaxed);
    returning.store(engine.isRecovering(), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* VinylPilotProcessor::createEditor() {
    return new VinylPilotEditor(*this);
}

void VinylPilotProcessor::getStateInformation(juce::MemoryBlock& destination) {
    auto state = parameters.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destination);
}

void VinylPilotProcessor::setStateInformation(const void* data, int bytes) {
    if (auto xml = getXmlFromBinary(data, bytes)) {
        if (xml->hasTagName(parameters.state.getType().toString()))
            parameters.replaceState(juce::ValueTree::fromXml(*xml));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VinylPilotProcessor();
}
