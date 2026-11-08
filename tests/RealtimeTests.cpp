#include "RealtimeAnalyser.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <thread>
#include <string>
#include <filesystem>
#include <ctime>

static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static chi::AudioClip loop(bool bass) {
    chi::AudioClip clip{48000,{std::vector<float>(48000*8,0),std::vector<float>(48000*8,0)}};
    std::mt19937 rng(123);std::uniform_real_distribution<float> noise(-1,1);
    for(int beat=0;beat<14;++beat) {
        const int jitter=bass?(beat%3-1)*100:0;
        const auto start=static_cast<std::size_t>(24000+beat*24000+(bass?1440+jitter:960));
        const auto duration=static_cast<std::size_t>(bass?17000:6000);
        for(std::size_t j=0;j<duration&&start+j<clip.frames();++j) {
            const double t=j/48000.0;
            const double v=bass?.7*(1-std::exp(-t/.002))*std::exp(-t/ .11)*(std::sin(2*3.141592653589793*82.4069*t)+.2*std::sin(2*3.141592653589793*164.8138*t)):
                .7*std::exp(-t/.025)*(beat%2?noise(rng):std::cos(2*3.141592653589793*(70*t-100*t*t)));
            clip.channels[0][start+j]=static_cast<float>(v);clip.channels[1][start+j]=static_cast<float>(v*.8);
        }
    }
    return clip;
}
int main(int argc,char** argv) {
    try {
        require(argc==2,"Specify drum or bass");const bool bass=std::string(argv[1])=="bass";
        auto clip=loop(bass);const auto dir=std::filesystem::absolute(argv[0]).parent_path()/"live-fixtures";std::filesystem::create_directories(dir);
        const auto file=dir/(bass?"bass-offset.wav":"drums-offset.wav");std::filesystem::remove(file);chi::Cancellation token;chi::writeWav(file,clip,token);
        chi::RealtimeAnalyser live;live.setEnabled(true);
        const auto start=std::chrono::steady_clock::now();
        for(std::size_t pos=0;pos<clip.frames();pos+=512) {
            const auto n=static_cast<int>(std::min<std::size_t>(512,clip.frames()-pos));
            const float* ptr[2]{clip.channels[0].data()+pos,clip.channels[1].data()+pos};
            chi::HostTiming host;host.bpm=120;host.ppq=8+(bass?pos/24000.0:std::fmod(pos/24000.0,4.0));host.gridValid=true;
            live.push(ptr,2,n,host);
            std::this_thread::sleep_until(start+std::chrono::microseconds(static_cast<long long>((pos+n)*1000000.0/48000)));
        }
        chi::LiveResult result;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(14);
        do {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));live.read(result);
        }while(result.seconds<7.4&&std::chrono::steady_clock::now()<deadline);
        require(result.seconds>=7.4,"Worker did not finish queued fixture before deadline");
        std::cout<<(bass?"bass":"drums")<<": BPM="<<result.bpm<<" confidence="<<result.confidence<<" attacks="<<result.totalAttacks<<" drops="<<result.dropped<<'\n';
        require(!result.error&&result.dropped==0,"Analysis error or overflow at realtime rate");
        require(std::abs(result.bpm-120)<2&&result.confidence>=.65,"Tempo/confidence failed");
        require(result.totalAttacks==14&&result.count==14&&result.gridValid,"Missing onset/grid results");
        if(!bass)require(result.hostPpq<12,"Host loop PPQ wrap missing");
        double mean=0;for(unsigned i=0;i<result.count;++i){std::cout<<result.deviationsMs[i]<<" ms ";mean+=result.deviationsMs[i];}mean/=result.count;std::cout<<"\nMean offset="<<mean<<" ms\n";
        require(std::abs(mean-(bass?30:20))<(bass?15:8),"Host grid timing deviation inaccurate");
        live.setEnabled(false);std::this_thread::sleep_for(std::chrono::milliseconds(100));chi::LiveResult frozen;require(live.read(frozen),"Paused snapshot unavailable");
        const float* ptr[2]{clip.channels[0].data(),clip.channels[1].data()};chi::HostTiming host;
        const auto idleClock=std::clock();
        for(int i=0;i<500;++i)live.push(ptr,2,512,host);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));chi::LiveResult after;live.read(after);
        std::cout<<"Disabled process CPU time over 100ms sleep: "<<1000.0*(std::clock()-idleClock)/CLOCKS_PER_SEC<<" ms\n";
        require(after.totalAttacks==frozen.totalAttacks&&after.bpm==frozen.bpm&&after.seconds==frozen.seconds,"Disabled result changed");
        live.setEnabled(true);host.bpm=120;host.gridValid=false;host.reset=true;
        for(int i=0;i<50;++i){live.push(ptr,2,512,host);host.reset=false;}
        // Destroy while input is pending: destructor cancels and joins its worker.
        std::cout<<"PASS realtime tempo/grid (drums with host PPQ loop), paused freeze, pending destruction\n";return 0;
    } catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
