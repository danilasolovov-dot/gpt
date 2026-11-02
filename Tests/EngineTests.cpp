#include "../Source/VinylEngine.h"
#include <cassert>
#include <cmath>
#include <iostream>

using vinylpilot::Settings;
using vinylpilot::VinylEngine;

static void expectNear(float actual, float target, float tolerance, const char* why) {
    if (std::abs(actual - target) > tolerance) {
        std::cerr << why << ": expected " << target << ", got " << actual << '\n';
        std::abort();
    }
}

int main() {
    constexpr double sr = 8000.0;
    VinylEngine engine;
    engine.prepare(sr, 2);
    Settings s;
    s.surface = 0;
    s.crackle = 0;
    s.wear = 0;
    s.mix = 0;
    float l = 0, r = 0;

    // A 1-sample impulse must emerge at precisely the declared host latency.
    for (int i = 0; i < 1500; ++i) {
        engine.processSample(i == 0 ? 1.0f : 0.0f,
                             i == 0 ? 0.5f : 0.0f, s, l, r);
        if (i == engine.latencySamples()) {
            expectNear(l, 1.0f, 1e-6f, "declared delay left");
            expectNear(r, 0.5f, 1e-6f, "declared delay right");
        } else {
            expectNear(l, 0.0f, 1e-6f, "no pre/post echo");
        }
    }

    engine.reset();
    s.mix = 1;
    for (int i = 0; i < 4000; ++i)
        engine.processSample(static_cast<float>(i) / 20000.0f,
                             static_cast<float>(i) / 20000.0f, s, l, r);

    // Touch/stop: reading one sample repeatedly must hold the audio still.
    s.scratchRate = 0.0f;
    for (int i = 0; i < 30; ++i)
        engine.processSample(0.3f, 0.3f, s, l, r);
    const float held = l;
    for (int i = 0; i < 200; ++i)
        engine.processSample(0.3f, 0.3f, s, l, r);
    expectNear(l, held, 0.00001f, "stopped platter holds read head");

    // Reverse must move backwards through the recorded stream.
    s.scratchRate = -1.0f;
    engine.processSample(0.3f, 0.3f, s, l, r);
    const float reverseStart = l;
    for (int i = 0; i < 100; ++i)
        engine.processSample(0.3f, 0.3f, s, l, r);
    assert(l < reverseStart);

    // Returning to rate 1.0 must catch up in the user-defined amount of time.
    s.scratchRate = 1.0f;
    s.returnMs = 300.0f;
    const auto framesToReturn = static_cast<int>(sr * s.returnMs / 1000.0f);
    for (int i = 0; i < framesToReturn + 10; ++i)
        engine.processSample(0.5f, 0.5f, s, l, r);
    assert(!engine.isRecovering());
    assert(std::abs(engine.readSamplePosition() -
                    (engine.liveSamplePosition() - 1.0)) < 0.001);

    // Zero surface amount must add no noise to a silent input.
    engine.reset();
    s.scratchRate = 1.0f;
    s.surface = 0;
    s.crackle = 0;
    for (int i = 0; i < 2000; ++i) {
        engine.processSample(0, 0, s, l, r);
        expectNear(l, 0, 1e-7f, "zero texture is silent");
    }
    s.surface = 1;
    float absoluteSum = 0;
    for (int i = 0; i < 2000; ++i) {
        engine.processSample(0, 0, s, l, r);
        absoluteSum += std::abs(l);
    }
    assert(absoluteSum > 0.01f);

    std::cout << "VinylEngine tests passed (latency, stop, reverse, "
                 "timed sync and noise).\n";
    return 0;
}
