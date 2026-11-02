#pragma once

#include "PluginProcessor.h"
#include <array>
#include <memory>

class VinylPilotEditor final : public juce::AudioProcessorEditor,
                               private juce::Timer {
public:
    explicit VinylPilotEditor(VinylPilotProcessor&);
    ~VinylPilotEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void setScratchSpeed(float speed);
    float pointAngle(juce::Point<float> location) const;
    void paintDisc(juce::Graphics&);

    VinylPilotProcessor& processor;
    std::array<juce::Slider, 7> sliders;
    std::array<juce::Label, 7> sliderLabels;
    std::array<std::unique_ptr<SliderAttachment>, 7> sliderAttachments;
    juce::ToggleButton pitchToggle { "KEYLAB PITCH WHEEL INPUT" };
    std::unique_ptr<ButtonAttachment> pitchAttachment;

    juce::Rectangle<float> discBounds;
    juce::RangedAudioParameter* scratchParameter = nullptr;
    bool dragging = false;
    float previousDragAngle = 0.0f;
    float lastScratchSpeed = 1.0f;
    double lastDragMs = 0.0;
    double lastAnimationMs = 0.0;
    double discAngle = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VinylPilotEditor)
};
