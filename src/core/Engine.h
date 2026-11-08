#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <vector>

namespace chi {
struct Cancelled : std::runtime_error { Cancelled() : std::runtime_error("Cancelado") {} };
struct Cancellation {
    std::atomic<bool> requested{false};
    void check() const { if (requested.load(std::memory_order_relaxed)) throw Cancelled(); }
};
struct AudioClip {
    double sampleRate = 48000;
    std::vector<std::vector<float>> channels;
    std::size_t frames() const { return channels.empty() ? 0 : channels.front().size(); }
    void validate() const;
};
struct Attack { std::size_t frame; double strength; };
struct Analysis { std::vector<Attack> attacks; double bpm = 0, confidence = 0; };
struct Settings {
    double bpm = 120, strength = 1, naturalness = .35, swing = 0, offsetSeconds = 0;
    int subdivision = 16, beatsPerBar = 4;
    bool preservePitch = true, protectTransients = true;
    void validate() const;
};
using TimeMap = std::map<std::size_t, std::size_t>;
Analysis analyse(const AudioClip&, const Cancellation&);
TimeMap makeMap(const AudioClip&, const Analysis&, const Settings&, const Cancellation&);
AudioClip render(const AudioClip&, const TimeMap&, const Settings&, const Cancellation&);
AudioClip readWav(const std::filesystem::path&, const Cancellation&);
void writeWav(const std::filesystem::path&, const AudioClip&, const Cancellation&);
}
