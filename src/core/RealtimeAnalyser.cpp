#include "RealtimeAnalyser.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>

namespace chi {
static_assert(std::atomic<std::uint64_t>::is_always_lock_free&&std::atomic<double>::is_always_lock_free
              &&std::atomic<std::size_t>::is_always_lock_free,"Realtime handoff must be lock-free");
RealtimeAnalyser::RealtimeAnalyser():queue(std::make_unique<SpscQueue<Packet,1024>>()) {
    for(unsigned i=0;i<16;++i){result.deviations[i]=0;result.frames[i]=0;}
    thread=std::thread([this]{run();});
}
RealtimeAnalyser::~RealtimeAnalyser(){stopping=true;cancellation.requested=true;if(thread.joinable())thread.join();}
void RealtimeAnalyser::setEnabled(bool on) noexcept {
    if(enabled.exchange(on,std::memory_order_acq_rel)!=on){++generation;cancellation.requested=true;}
}
void RealtimeAnalyser::reset() noexcept {++generation;cancellation.requested=true;audioFrame=0;}
void RealtimeAnalyser::push(const float* const* audio,int channels,int frames,const HostTiming& host) noexcept {
    if(!isEnabled()||frames<=0||channels<=0)return;
    for(int offset=0;offset<frames;offset+=static_cast<int>(packetSize)) {
        Packet packet{};packet.host=host;packet.host.reset=host.reset&&offset==0;
        if(host.gridValid)packet.host.ppq+=offset*host.bpm/(host.sampleRate*60);
        packet.start=audioFrame+static_cast<unsigned>(offset);packet.generation=generation.load(std::memory_order_acquire);
        packet.channels=static_cast<unsigned>(std::min(2,channels));
        packet.count=static_cast<unsigned>(std::min(static_cast<int>(packetSize),frames-offset));
        for(unsigned c=0;c<packet.channels;++c)std::copy_n(audio[c]+offset,packet.count,packet.audio[c].data());
        if(!queue->push(packet))dropped.fetch_add(1,std::memory_order_relaxed);
    }
    audioFrame+=static_cast<unsigned>(frames);
}
void RealtimeAnalyser::publish(const LiveResult& r) noexcept {
    result.sequence.fetch_add(1,std::memory_order_acq_rel);
    result.bpm.store(r.bpm,std::memory_order_relaxed);result.confidence.store(r.confidence,std::memory_order_relaxed);
    result.hostBpm.store(r.hostBpm,std::memory_order_relaxed);result.hostPpq.store(r.hostPpq,std::memory_order_relaxed);
    result.seconds.store(r.seconds,std::memory_order_relaxed);result.count.store(r.count,std::memory_order_relaxed);
    result.attacks.store(r.totalAttacks,std::memory_order_relaxed);result.dropped.store(r.dropped,std::memory_order_relaxed);
    result.grid.store(r.gridValid,std::memory_order_relaxed);result.error.store(r.error,std::memory_order_relaxed);
    for(unsigned i=0;i<16;++i){result.deviations[i].store(r.deviationsMs[i],std::memory_order_relaxed);result.frames[i].store(r.attackFrames[i],std::memory_order_relaxed);}
    result.sequence.fetch_add(1,std::memory_order_release);
}
bool RealtimeAnalyser::read(LiveResult& output) const noexcept {
    // All payload members are atomic: bounded retries cannot cause a data race.
    for(int attempt=0;attempt<4;++attempt) {
        const auto before=result.sequence.load(std::memory_order_acquire);if(before&1)continue;
        LiveResult r;r.bpm=result.bpm.load(std::memory_order_relaxed);r.confidence=result.confidence.load(std::memory_order_relaxed);
        r.hostBpm=result.hostBpm.load(std::memory_order_relaxed);r.hostPpq=result.hostPpq.load(std::memory_order_relaxed);
        r.seconds=result.seconds.load(std::memory_order_relaxed);r.count=result.count.load(std::memory_order_relaxed);
        r.totalAttacks=result.attacks.load(std::memory_order_relaxed);r.dropped=dropped.load(std::memory_order_relaxed);
        r.gridValid=result.grid.load(std::memory_order_relaxed);r.error=result.error.load(std::memory_order_relaxed);
        for(unsigned i=0;i<16;++i){r.deviationsMs[i]=result.deviations[i].load(std::memory_order_relaxed);r.attackFrames[i]=result.frames[i].load(std::memory_order_relaxed);}
        std::atomic_thread_fence(std::memory_order_acquire);
        if(before==result.sequence.load(std::memory_order_relaxed)){output=r;return true;}
    }
    return false;
}
void RealtimeAnalyser::run() noexcept {
    Packet packet;AudioClip window;std::deque<Packet> metadata;
    // Keep only timing in the metadata deque; audio fields are unused there.
    unsigned seenGeneration=0;std::uint64_t first=0,last=0,lastAnalysis=0;
    HostTiming latest;bool fresh=true;
    auto clear=[&]{window.channels.clear();metadata.clear();fresh=true;lastAnalysis=0;};
    while(!stopping.load(std::memory_order_acquire)) {
        try {
            if(!isEnabled()) {
                while(queue->pop(packet)) {if(stopping)return;}
                clear();std::this_thread::sleep_for(std::chrono::milliseconds(25));continue;
            }
            const auto currentGeneration=generation.load(std::memory_order_acquire);
            if(currentGeneration!=seenGeneration){clear();seenGeneration=currentGeneration;cancellation.requested=false;if(isEnabled())publish({});}
            bool received=false;
            for(int batch=0;batch<128&&queue->pop(packet);++batch) {
                if(stopping)return;
                if(packet.generation!=seenGeneration)continue;
                if(fresh||packet.host.reset||window.sampleRate!=packet.host.sampleRate||window.channels.size()!=packet.channels||packet.start!=last) {
                    clear();window.sampleRate=packet.host.sampleRate;window.channels.resize(packet.channels);
                    for(auto& c:window.channels)c.reserve(static_cast<std::size_t>(window.sampleRate*9));
                    first=packet.start;lastAnalysis=first;fresh=false;
                }
                for(unsigned c=0;c<packet.channels;++c)window.channels[c].insert(window.channels[c].end(),packet.audio[c].begin(),packet.audio[c].begin()+packet.count);
                metadata.push_back(packet);last=packet.start+packet.count;latest=packet.host;received=true;
            }
            if(received&&window.frames()>=static_cast<std::size_t>(window.sampleRate*2)
               &&last-lastAnalysis>=static_cast<std::uint64_t>(window.sampleRate*.5)) {
                lastAnalysis=last;
                const auto maximum=static_cast<std::size_t>(window.sampleRate*8);
                if(window.frames()>maximum) {
                    const auto trim=window.frames()-maximum;
                    for(auto& c:window.channels)c.erase(c.begin(),c.begin()+static_cast<std::ptrdiff_t>(trim));
                    first+=trim;
                    while(metadata.size()>1&&metadata[1].start<=first)metadata.pop_front();
                }
                cancellation.check();auto analysis=analyse(window,cancellation);LiveResult r;
                r.bpm=analysis.bpm;r.confidence=analysis.confidence;r.hostBpm=latest.bpm;r.hostPpq=latest.ppq;
                r.seconds=window.frames()/window.sampleRate;r.totalAttacks=static_cast<unsigned>(analysis.attacks.size());
                r.count=static_cast<unsigned>(std::min<std::size_t>(16,analysis.attacks.size()));r.gridValid=latest.gridValid;
                r.dropped=dropped.load(std::memory_order_relaxed);
                for(unsigned i=0;i<r.count;++i) {
                    const auto& attack=analysis.attacks[analysis.attacks.size()-r.count+i];const auto frame=first+attack.frame;
                    r.attackFrames[i]=frame;
                    auto point=std::upper_bound(metadata.begin(),metadata.end(),frame,[](auto value,const Packet& p){return value<p.start;});
                    if(point!=metadata.begin())--point;
                    if(point==metadata.end()||!point->host.gridValid) {r.deviationsMs[i]=NAN;continue;}
                    const auto& h=point->host;const double ppq=h.ppq+(frame-point->start)*h.bpm/(60*h.sampleRate);
                    const double origin=h.offsetSeconds*h.bpm/60,step=h.stepBeats;
                    const auto index=static_cast<long long>(std::llround((ppq-origin)/step));
                    double nearest=ppq,distance=step*3;
                    for(auto j=index-2;j<=index+2;++j) {
                        const double grid=origin+(j+(j%2?h.swing:0))*step;
                        if(std::abs(grid-ppq)<distance){distance=std::abs(grid-ppq);nearest=grid;}
                    }
                    r.deviationsMs[i]=(ppq-nearest)*60000/h.bpm;
                }
                if(isEnabled()&&generation.load()==seenGeneration)publish(r);
            }
            if(!received)std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }catch(const Cancelled&) {clear();}
        catch(...) {clear();if(isEnabled()){LiveResult r;r.error=true;publish(r);}std::this_thread::sleep_for(std::chrono::milliseconds(25));}
    }
}
}
