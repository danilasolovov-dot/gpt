#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float tau = 6.28318530718f;
constexpr float nominalAngularSpeed = tau * 33.33333f / 60.0f;
const juce::Colour background = juce::Colour::fromRGB(15, 21, 26);
const juce::Colour panel = juce::Colour::fromRGB(28, 35, 41);
const juce::Colour accent = juce::Colour::fromRGB(231, 170, 100);
const juce::Colour muted = juce::Colour::fromRGB(147, 159, 161);
const char* parameterIds[] = {
    "surface", "crackle", "wear", "mix", "return", "bendDepth", "scratch"
};
const char* parameterLabels[] = {
    "SURFACE NOISE", "DUST & POPS", "VINYL WEAR", "DRY / WET",
    "RETURN (MS)", "WHEEL DEPTH", "SCRATCH SPEED"
};
}

VinylPilotEditor::VinylPilotEditor(VinylPilotProcessor& p)
    : AudioProcessorEditor(&p), processor(p) {
    scratchParameter = processor.parameters.getParameter("scratch");

    for (size_t i = 0; i < sliders.size(); ++i) {
        auto& slider = sliders[i];
        const bool isScrubSlider = i == 6;
        slider.setSliderStyle(isScrubSlider
            ? juce::Slider::LinearHorizontal
            : juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 100, 22);
        slider.setColour(juce::Slider::rotarySliderFillColourId, accent);
        slider.setColour(juce::Slider::rotarySliderOutlineColourId,
                         juce::Colour::fromRGB(65, 73, 76));
        slider.setColour(juce::Slider::thumbColourId, accent);
        slider.setColour(juce::Slider::trackColourId, accent);
        slider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
        slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setColour(juce::Slider::textBoxBackgroundColourId, panel);
        addAndMakeVisible(slider);

        sliderLabels[i].setText(parameterLabels[i], juce::dontSendNotification);
        sliderLabels[i].setJustificationType(juce::Justification::centred);
        sliderLabels[i].setColour(juce::Label::textColourId, muted);
        sliderLabels[i].setFont(juce::Font(12.0f, juce::Font::bold));
        addAndMakeVisible(sliderLabels[i]);

        sliderAttachments[i] = std::make_unique<SliderAttachment>(
            processor.parameters, parameterIds[i], slider);
    }

    pitchToggle.setColour(juce::ToggleButton::tickColourId, accent);
    pitchToggle.setColour(juce::ToggleButton::textColourId, muted);
    addAndMakeVisible(pitchToggle);
    pitchAttachment = std::make_unique<ButtonAttachment>(
        processor.parameters, "pitchInput", pitchToggle);

    setSize(1030, 650);
    lastAnimationMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz(60);
}

VinylPilotEditor::~VinylPilotEditor() {
    stopTimer();
    if (dragging && scratchParameter != nullptr) {
        processor.setManualTouch(false);
        setScratchSpeed(1.0f);
        scratchParameter->endChangeGesture();
    }
}

void VinylPilotEditor::resized() {
    discBounds = { 57.0f, 133.0f, 430.0f, 430.0f };

    const int leftX = 536;
    const int rightX = 771;
    const int topY = 130;
    for (int i = 0; i < 6; ++i) {
        const int x = i % 2 == 0 ? leftX : rightX;
        const int y = topY + (i / 2) * 125;
        sliderLabels[static_cast<size_t>(i)].setBounds(x, y, 185, 22);
        sliders[static_cast<size_t>(i)].setBounds(x + 15, y + 24, 155, 101);
    }

    sliderLabels[6].setBounds(leftX + 2, 509, 185, 22);
    sliders[6].setBounds(leftX + 8, 532, 402, 64);
    pitchToggle.setBounds(leftX + 2, 605, 260, 28);
}

void VinylPilotEditor::paint(juce::Graphics& g) {
    g.fillAll(background);
    g.setColour(panel);
    g.fillRoundedRectangle(22.0f, 22.0f, 986.0f, 606.0f, 17.0f);

    g.setColour(juce::Colour::fromRGB(41, 50, 56));
    g.drawRoundedRectangle(22.5f, 22.5f, 985.0f, 605.0f, 17.0f, 1.0f);

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(29.0f, juce::Font::bold));
    g.drawText("VINYL PILOT", 57, 45, 390, 48, juce::Justification::centredLeft);

    g.setColour(accent);
    g.setFont(juce::Font(11.5f, juce::Font::bold));
    g.drawText("SOLOVOV  /  AUDIO LAB  •  01", 547, 57, 400, 22,
               juce::Justification::centredRight);
    g.setColour(muted);
    g.setFont(juce::Font(13.0f));
    g.drawText("PLAY  •  GRAB THE RECORD  •  SCRATCH", 57, 91, 480, 21,
               juce::Justification::centredLeft);

    paintDisc(g);

    g.setColour(juce::Colour::fromRGB(52, 60, 65));
    g.drawLine(505.0f, 130.0f, 505.0f, 600.0f, 1.2f);
    g.setColour(muted);
    g.setFont(juce::Font(12.0f));
    g.drawText("-3× REVERSE                       1× NORMAL                        +3×",
               540, 587, 408, 18, juce::Justification::centred);

    if (dragging) {
        g.setColour(accent);
        g.setFont(juce::Font(12.5f, juce::Font::bold));
        g.drawText("●  HAND ON RECORD", 74, 581, 310, 25,
                   juce::Justification::centredLeft);
    } else if (processor.isReturning()) {
        g.setColour(accent);
        g.setFont(juce::Font(12.5f, juce::Font::bold));
        g.drawText("●  CATCHING UP TO LIVE", 74, 581, 340, 25,
                   juce::Justification::centredLeft);
    } else {
        g.setColour(muted);
        g.setFont(juce::Font(12.5f));
        g.drawText(processor.isTransportPlaying() ? "●  LIVE / 33⅓ RPM" : "○  TRANSPORT STOPPED",
                   74, 581, 310, 25, juce::Justification::centredLeft);
    }
}

void VinylPilotEditor::paintDisc(juce::Graphics& g) {
    const auto center = discBounds.getCentre();
    const float cx = center.x;
    const float cy = center.y;
    const float radius = discBounds.getWidth() * 0.5f;

    g.setColour(juce::Colour::fromRGB(9, 12, 16));
    g.fillEllipse(discBounds.expanded(10.0f));
    g.setColour(juce::Colour::fromRGB(79, 86, 87));
    g.drawEllipse(discBounds.expanded(5.5f), 5.0f);
    g.setColour(juce::Colour::fromRGB(22, 25, 28));
    g.fillEllipse(discBounds);

    juce::ColourGradient vinylShade(
        juce::Colour::fromRGB(61, 66, 68), cx - radius, cy - radius,
        juce::Colour::fromRGB(10, 12, 15), cx + radius, cy + radius, false);
    g.setGradientFill(vinylShade);
    g.fillEllipse(discBounds.reduced(4.0f));

    for (int groove = 0; groove < 42; ++groove) {
        const float r = 89.0f + 2.67f * groove;
        const int alpha = (groove % 4 == 0 ? 70 : (groove % 3 == 0 ? 32 : 14));
        g.setColour(juce::Colours::white.withAlpha(static_cast<juce::uint8>(alpha)));
        g.drawEllipse(cx - r, cy - r, 2.0f * r, 2.0f * r, 0.8f);
    }
    g.setColour(juce::Colours::black.withAlpha(0.25f));
    g.fillEllipse(cx - 88.0f, cy - 88.0f, 176.0f, 176.0f);

    {
        juce::Graphics::ScopedSaveState saved(g);
        g.addTransform(juce::AffineTransform::rotation(static_cast<float>(discAngle), cx, cy));
        g.setColour(juce::Colour::fromRGB(172, 93, 62));
        g.fillEllipse(cx - 77.0f, cy - 77.0f, 154.0f, 154.0f);
        g.setColour(juce::Colour::fromRGB(220, 150, 95));
        g.drawEllipse(cx - 72.0f, cy - 72.0f, 144.0f, 144.0f, 1.5f);
        g.setColour(juce::Colour::fromRGB(244, 212, 168));
        g.setFont(juce::Font(18.0f, juce::Font::bold));
        g.drawText("VINYL", static_cast<int>(cx - 63), static_cast<int>(cy - 39),
                   126, 27, juce::Justification::centred);
        g.setFont(juce::Font(9.5f, juce::Font::bold));
        g.drawText("P I L O T", static_cast<int>(cx - 63), static_cast<int>(cy - 6),
                   126, 21, juce::Justification::centred);
        g.setColour(juce::Colour::fromRGB(110, 52, 43));
        g.fillRect(cx - 59.0f, cy + 29.0f, 118.0f, 3.0f);
        g.setColour(juce::Colour::fromRGB(244, 212, 168));
        g.setFont(juce::Font(9.0f));
        g.drawText("33⅓ RPM  ·  SIDE A", static_cast<int>(cx - 63),
                   static_cast<int>(cy + 38), 126, 19, juce::Justification::centred);
    }

    g.setColour(juce::Colour::fromRGB(213, 218, 207));
    g.fillEllipse(cx - 6.0f, cy - 6.0f, 12.0f, 12.0f);
    g.setColour(juce::Colour::fromRGB(56, 62, 61));
    g.fillEllipse(cx - 2.0f, cy - 2.0f, 4.0f, 4.0f);

    // A simple stationary tonearm hints at the hardware metaphor while the
    // record label rotates underneath the user's cursor.
    const float pivotX = 456.0f, pivotY = 148.0f;
    g.setColour(juce::Colour::fromRGB(91, 100, 104));
    g.fillEllipse(pivotX - 14, pivotY - 14, 28, 28);
    g.setColour(juce::Colour::fromRGB(183, 190, 186));
    g.drawLine(pivotX, pivotY, 395.0f, 252.0f, 7.0f);
    g.setColour(juce::Colour::fromRGB(25, 29, 31));
    g.fillRoundedRectangle(385.0f, 246.0f, 21.0f, 18.0f, 3.0f);
    g.setColour(accent);
    g.fillRect(389.0f, 259.0f, 9.0f, 5.0f);
}

float VinylPilotEditor::pointAngle(juce::Point<float> location) const {
    const auto delta = location - discBounds.getCentre();
    return std::atan2(delta.y, delta.x);
}

void VinylPilotEditor::setScratchSpeed(float speed) {
    if (scratchParameter == nullptr) return;
    lastScratchSpeed = std::clamp(speed, -3.0f, 3.0f);
    scratchParameter->setValueNotifyingHost(
        scratchParameter->convertTo0to1(lastScratchSpeed));
}

void VinylPilotEditor::mouseDown(const juce::MouseEvent& event) {
    if (!discBounds.contains(event.position) || scratchParameter == nullptr)
        return;
    dragging = true;
    processor.setManualTouch(true);
    previousDragAngle = pointAngle(event.position);
    lastDragMs = juce::Time::getMillisecondCounterHiRes();
    scratchParameter->beginChangeGesture();
    setScratchSpeed(0.0f);
    repaint();
}

void VinylPilotEditor::mouseDrag(const juce::MouseEvent& event) {
    if (!dragging) return;
    const float angle = pointAngle(event.position);
    float delta = angle - previousDragAngle;
    if (delta > juce::MathConstants<float>::pi) delta -= tau;
    if (delta < -juce::MathConstants<float>::pi) delta += tau;
    const auto time = juce::Time::getMillisecondCounterHiRes();
    const double seconds = std::max(0.008, (time - lastDragMs) / 1000.0);
    setScratchSpeed(delta / (static_cast<float>(seconds) * nominalAngularSpeed));
    discAngle += delta;
    previousDragAngle = angle;
    lastDragMs = time;
    repaint();
}

void VinylPilotEditor::mouseUp(const juce::MouseEvent&) {
    if (!dragging) return;
    dragging = false;
    processor.setManualTouch(false);
    setScratchSpeed(1.0f);
    scratchParameter->endChangeGesture();
    repaint();
}

void VinylPilotEditor::timerCallback() {
    const auto time = juce::Time::getMillisecondCounterHiRes();
    const double dt = std::clamp((time - lastAnimationMs) / 1000.0, 0.0, 0.075);
    lastAnimationMs = time;
    if (dragging) {
        // Stationary finger = stationary platter. No need for mouse-up.
        if (time - lastDragMs > 65.0 && std::abs(lastScratchSpeed) > 0.002f)
            setScratchSpeed(0.0f);
    } else if (processor.isTransportPlaying()) {
        discAngle += dt * nominalAngularSpeed * processor.displayRate();
        if (std::abs(discAngle) > 1000.0)
            discAngle = std::fmod(discAngle, static_cast<double>(tau));
    }
    repaint();
}
