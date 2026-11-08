#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace chi {
// Exponentially integrated mean-square, tau=65 ms: a step reaches >99% of
// its RMS amplitude in 300 ms. Independent of host block size and GUI lifetime.
class VuMeter {
public:
    std::atomic<float> rms{0},peak{0};
    std::atomic<bool> clip{false};
    void prepare(double sr) noexcept {
        sampleRate=std::max(1.0,sr);coefficient=-std::expm1(-1.0/(sampleRate*.065));
        power={};peakValue=0;clipRemaining=0;rms=0;peak=0;clip=false;
    }
    void process(const float* const* audio,int channels,int frames) noexcept {
        double maxPower=0;float blockPeak=0;
        for(int c=0;c<std::min(2,channels);++c) {
            auto& p=power[static_cast<std::size_t>(c)];
            for(int i=0;i<frames;++i) {
                const double v=std::isfinite(audio[c][i])?audio[c][i]:0;
                p+=coefficient*(v*v-p);blockPeak=std::max(blockPeak,static_cast<float>(std::abs(v)));
            }
            maxPower=std::max(maxPower,p);
        }
        rms.store(static_cast<float>(std::sqrt(std::max(0.0,maxPower))),std::memory_order_relaxed);
        peakValue=std::max(blockPeak,peakValue*static_cast<float>(std::exp(-frames/(sampleRate*.4))));
        peak.store(peakValue,std::memory_order_relaxed);
        if(blockPeak>=1)clipRemaining=static_cast<int>(sampleRate);
        else clipRemaining=std::max(0,clipRemaining-frames);
        clip.store(clipRemaining>0,std::memory_order_relaxed);
    }
private:
    std::array<double,2> power{};double sampleRate=48000,coefficient=0;
    float peakValue=0;int clipRemaining=0;
};
}
