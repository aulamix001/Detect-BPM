#pragma once
#include "Engine.h"
#include <array>
#include <atomic>
#include <memory>
#include <thread>

namespace chi {
// Exactly one producer (audio callback) and one consumer (analysis thread).
// Storage is allocated in the constructor, never in push/pop.
template<class T,std::size_t N> class SpscQueue {
public:
    bool push(const T& value) noexcept {
        const auto w=write.load(std::memory_order_relaxed), next=(w+1)%N;
        if(next==read.load(std::memory_order_acquire))return false;
        data[w]=value;write.store(next,std::memory_order_release);return true;
    }
    bool pop(T& value) noexcept {
        const auto r=read.load(std::memory_order_relaxed);
        if(r==write.load(std::memory_order_acquire))return false;
        value=data[r];read.store((r+1)%N,std::memory_order_release);return true;
    }
private:
    std::array<T,N> data;
    alignas(64) std::atomic<std::size_t> write{0};
    alignas(64) std::atomic<std::size_t> read{0};
};
struct HostTiming {
    double sampleRate=48000,bpm=0,ppq=0,stepBeats=.25,swing=0,offsetSeconds=0;
    bool gridValid=false,reset=false;
};
struct LiveResult {
    double bpm=0,confidence=0,hostBpm=0,hostPpq=0,seconds=0;
    std::array<double,16> deviationsMs{};
    std::array<std::uint64_t,16> attackFrames{};
    unsigned count=0,totalAttacks=0,dropped=0;
    bool gridValid=false,error=false;
};
class RealtimeAnalyser {
public:
    RealtimeAnalyser();
    ~RealtimeAnalyser();
    RealtimeAnalyser(const RealtimeAnalyser&)=delete;
    RealtimeAnalyser& operator=(const RealtimeAnalyser&)=delete;
    void setEnabled(bool) noexcept;
    bool isEnabled() const noexcept {return enabled.load(std::memory_order_relaxed);}
    // Host calls prepare only while its audio callback is suspended.
    void reset() noexcept;
    void push(const float* const*,int channels,int frames,const HostTiming&) noexcept;
    bool read(LiveResult&) const noexcept;
private:
    static constexpr std::size_t packetSize=256;
    struct Packet {
        std::array<std::array<float,packetSize>,2> audio;
        HostTiming host;
        std::uint64_t start=0;
        unsigned generation=0,count=0,channels=0;
    };
    struct AtomicResult {
        std::atomic<unsigned> sequence{0},count{0},attacks{0},dropped{0};
        std::atomic<double> bpm{0},confidence{0},hostBpm{0},hostPpq{0},seconds{0};
        std::atomic<bool> grid{false},error{false};
        std::array<std::atomic<double>,16> deviations{};
        std::array<std::atomic<std::uint64_t>,16> frames{};
    } result;
    void publish(const LiveResult&) noexcept;
    void run() noexcept;
    std::unique_ptr<SpscQueue<Packet,1024>> queue;
    std::atomic<bool> enabled{false},stopping{false};
    std::atomic<unsigned> generation{1},dropped{0};
    Cancellation cancellation;
    std::thread thread;
    std::uint64_t audioFrame=0; // audio-thread-only, except suspended prepare
};
}
