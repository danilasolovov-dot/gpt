#pragma once

// JUCE-independent audio engine. This file is intentionally portable so the
// deterministic scratch/recovery behavior can be unit tested without a DAW.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vinylpilot {

struct Settings {
    float surface = 0.18f;       // 0..1: continuous surface noise
    float crackle = 0.16f;       // 0..1: discrete dust/click impulses
    float wear = 0.12f;          // 0..1: tone roll-off and wow/flutter
    float mix = 1.0f;            // 0..1: dry/wet, both paths time aligned
    float scratchRate = 1.0f;    // -3..3; 1=normal, 0=still, <0=reverse
    bool forceScratch = false;   // GUI hand still touching a moving platter
    float returnMs = 450.0f;    // release-to-live synchronization time
    bool playing = true;
};

class VinylEngine {
public:
    static constexpr double kLookaheadSeconds = 0.125;
    static constexpr double kHistorySeconds = 12.0;

    void prepare(double newSampleRate, int numChannels) {
        sampleRate = std::max(8000.0, newSampleRate);
        channels = std::clamp(numChannels, 1, 2);
        latency = static_cast<int>(std::lround(sampleRate * kLookaheadSeconds));
        capacity = static_cast<int>(std::ceil(sampleRate * kHistorySeconds)) + latency + 8;
        history.assign(static_cast<size_t>(channels), std::vector<float>(static_cast<size_t>(capacity), 0.0f));
        reset();
    }

    void reset() noexcept {
        for (auto& channel : history)
            std::fill(channel.begin(), channel.end(), 0.0f);
        written = 0;
        cursor = -static_cast<double>(latency);
        previousCursor = cursor;
        wasScratching = false;
        recovering = false;
        returnRemaining = 0;
        returnTotal = 1;
        returnStartOffset = 0.0;
        noiseLow = 0.0f;
        crackleTail = 0.0f;
        toneLow[0] = toneLow[1] = 0.0f;
        wowPhase = flutterPhase = 0.0;
        rng = 0x7cda8f31u;
        measuredRate = 1.0f;
    }

    int latencySamples() const noexcept { return latency; }
    float currentRate() const noexcept { return measuredRate; }
    bool isRecovering() const noexcept { return recovering; }
    double readSamplePosition() const noexcept { return cursor; }
    double liveSamplePosition() const noexcept { return static_cast<double>(written) - latency; }

    // Processing is allocation-free. input and output may refer to the same
    // sample storage; callers save the input samples before invoking this.
    void processSample(float inputL, float inputR, const Settings& settings,
                       float& outputL, float& outputR) noexcept {
        if (history.empty() || capacity == 0) {
            outputL = inputL;
            outputR = inputR;
            return;
        }

        const int slot = static_cast<int>(written % capacity);
        history[0][static_cast<size_t>(slot)] = inputL;
        if (channels > 1)
            history[1][static_cast<size_t>(slot)] = inputR;

        const double base = static_cast<double>(written) - latency;
        const float requestedRate = std::clamp(settings.scratchRate, -3.0f, 3.0f);
        const bool scratching = settings.forceScratch ||
                                std::abs(requestedRate - 1.0f) > 0.008f;

        previousCursor = cursor;
        if (scratching) {
            if (!wasScratching) {
                if (!recovering)
                    cursor = base;
                recovering = false;
            }
            // The read head's position is integrated independently of the host.
            // Its velocity can be negative, zero, or faster than normal.
            if (wasScratching)
                cursor += requestedRate;
            wasScratching = true;
        } else if (wasScratching) {
            // Returning to 1.0 triggers an exact, timed catch-up to the latest
            // host-aligned sample. The offset follows a smoothstep trajectory.
            wasScratching = false;
            recovering = true;
            returnStartOffset = base - cursor;
            returnTotal = std::max(1, static_cast<int>(std::lround(
                                    sampleRate * std::clamp(settings.returnMs, 30.0f, 4000.0f) / 1000.0)));
            returnRemaining = returnTotal;
        }

        if (!scratching) {
            if (recovering) {
                const double t = 1.0 - static_cast<double>(returnRemaining) / returnTotal;
                const double smooth = t * t * (3.0 - 2.0 * t);
                cursor = base - returnStartOffset * (1.0 - smooth);
                if (--returnRemaining <= 0) {
                    cursor = base;
                    recovering = false;
                }
            } else {
                cursor = base;
            }
        }

        // Cannot read future input or samples that have been overwritten.
        // A 125ms compensated input delay gives fast forward-scratches a small
        // amount of headroom without pretending future audio is available.
        const double oldest = std::max(0.0, static_cast<double>(written - capacity + 3));
        cursor = std::clamp(cursor, oldest, static_cast<double>(written));
        measuredRate = static_cast<float>(cursor - previousCursor);

        const float wear = std::clamp(settings.wear, 0.0f, 1.0f);
        wowPhase += 6.283185307179586 * 0.53 / sampleRate;
        flutterPhase += 6.283185307179586 * 7.2 / sampleRate;
        if (wowPhase > 6.283185307179586) wowPhase -= 6.283185307179586;
        if (flutterPhase > 6.283185307179586) flutterPhase -= 6.283185307179586;
        const double modulation = wear * sampleRate *
              (0.0009 * std::sin(wowPhase) + 0.000075 * std::sin(flutterPhase));
        const double playbackPosition = cursor - modulation;

        const float dryL = read(0, base);
        const float dryR = channels == 1 ? dryL : read(1, base);
        const float scratchedL = read(0, playbackPosition);
        const float scratchedR = channels == 1 ? scratchedL : read(1, playbackPosition);

        const float cutoff = 18500.0f - 11500.0f * wear;
        const float decay = std::exp(-6.2831853f * cutoff / static_cast<float>(sampleRate));
        toneLow[0] = scratchedL * (1.0f - decay) + toneLow[0] * decay;
        toneLow[1] = scratchedR * (1.0f - decay) + toneLow[1] * decay;

        const float surface = std::clamp(settings.surface, 0.0f, 1.0f);
        const float random = whiteNoise();
        noiseLow = 0.994f * noiseLow + 0.006f * random;
        const float noise = settings.playing
            ? surface * surface * (0.025f * random + 0.17f * noiseLow) : 0.0f;

        const float dust = std::clamp(settings.crackle, 0.0f, 1.0f);
        crackleTail *= 0.75f;
        if (settings.playing && uniform() < (0.000004f + 0.00019f * dust * dust))
            crackleTail += whiteNoise() * 0.27f * dust;

        const float mix = std::clamp(settings.mix, 0.0f, 1.0f);
        const float effectL = toneLow[0] + noise + crackleTail;
        const float effectR = toneLow[1] + noise * 0.9f + crackleTail * 0.85f;
        outputL = dryL + (effectL - dryL) * mix;
        outputR = dryR + (effectR - dryR) * mix;
        ++written;
    }

private:
    float read(int channel, double position) const noexcept {
        if (position < 0.0 || position > static_cast<double>(written))
            return 0.0f;
        const auto first = static_cast<int64_t>(std::floor(position));
        if (first <= written - capacity)
            return 0.0f;
        const int a = static_cast<int>(first % capacity);
        const int b = static_cast<int>((first + 1) % capacity);
        const auto& samples = history[static_cast<size_t>(channel)];
        const float valueA = samples[static_cast<size_t>(a)];
        const float valueB = first < written ? samples[static_cast<size_t>(b)] : valueA;
        return valueA + (valueB - valueA) * static_cast<float>(position - first);
    }

    uint32_t nextRandom() noexcept {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    }
    float uniform() noexcept { return static_cast<float>(nextRandom() >> 8) / 16777216.0f; }
    float whiteNoise() noexcept { return uniform() * 2.0f - 1.0f; }

    double sampleRate = 44100.0;
    int channels = 2;
    int latency = 0;
    int capacity = 0;
    std::vector<std::vector<float>> history;
    int64_t written = 0;
    double cursor = 0.0;
    double previousCursor = 0.0;
    bool wasScratching = false;
    bool recovering = false;
    int returnTotal = 1;
    int returnRemaining = 0;
    double returnStartOffset = 0.0;
    double wowPhase = 0.0;
    double flutterPhase = 0.0;
    float noiseLow = 0.0f;
    float crackleTail = 0.0f;
    float toneLow[2] = {};
    uint32_t rng = 0x7cda8f31u;
    float measuredRate = 1.0f;
};

} // namespace vinylpilot
