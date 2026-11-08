#include "Engine.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <chrono>

namespace {
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
template<class F> void mustFail(F f) {bool failed=false;try{f();}catch(const std::exception&){failed=true;}require(failed,"Expected exception");}
chi::AudioClip pulses(bool irregular=false) {
    chi::AudioClip c{48000,{std::vector<float>(48000*6,0)}};
    for(int i=0;i<10;++i) {
        const auto start=static_cast<std::size_t>(24000+i*24000+(irregular?(i%2?1900:-1700):0));
        for(std::size_t j=0;j<960;++j)c.channels[0][start+j]=static_cast<float>(.8*std::exp(-static_cast<double>(j)/180.0)*std::cos(j*.42));
    }
    return c;
}
double pitch(const chi::AudioClip& c,std::size_t begin,std::size_t end) {
    std::size_t crossings=0;
    for(auto i=begin+1;i<end;++i)if(c.channels[0][i-1]<=0&&c.channels[0][i]>0)++crossings;
    return crossings*c.sampleRate/static_cast<double>(end-begin);
}
double timingError(const chi::Analysis& a) {
    double sum=0;for(auto p:a.attacks)sum+=std::abs(static_cast<double>(p.frame)-std::round(p.frame/6000.0)*6000);
    return a.attacks.empty()?1e9:sum/a.attacks.size();
}
}
int main(int argc,char** argv) {
    try {
        require(argc==2,"Specify test case");const std::string test=argv[1];chi::Cancellation token;chi::Settings s;
        if(test=="wav") {
            auto c=pulses();c.channels.push_back(c.channels[0]);
            const auto path=std::filesystem::temp_directory_path()/"chi-smart-time-roundtrip.wav";
            std::filesystem::remove(path);chi::writeWav(path,c,token);auto restored=chi::readWav(path,token);
            require(restored.sampleRate==c.sampleRate&&restored.channels==c.channels,"WAV roundtrip differs");
            mustFail([&]{chi::writeWav(path,c,token);});std::filesystem::remove(path);
        } else if(test=="invalid") {
            chi::AudioClip c;mustFail([&]{c.validate();});c=pulses();c.channels[0][0]=NAN;mustFail([&]{chi::analyse(c,token);});
            const auto path=std::filesystem::temp_directory_path()/"chi-smart-time-invalid.wav";
            {std::ofstream f(path);f<<"RIFF invalid";}mustFail([&]{chi::readWav(path,token);});std::filesystem::remove(path);
            s.bpm=0;mustFail([&]{s.validate();});
        } else if(test=="silence") {
            chi::AudioClip c{48000,{std::vector<float>(48000)}};auto a=chi::analyse(c,token);
            require(a.attacks.empty()&&a.confidence==0&&a.bpm==0,"Silence returned tempo");
        } else if(test=="attacks"||test=="tempo") {
            const auto a=chi::analyse(pulses(),token);require(a.attacks.size()==10,"Wrong attack count");
            for(std::size_t i=0;i<a.attacks.size();++i)require(std::abs(static_cast<double>(a.attacks[i].frame)-(24000+i*24000))<240,"Attack timing > 5ms");
            require(std::abs(a.bpm-120)<1&&a.confidence>.9,"Wrong BPM/confidence");
        } else if(test=="map") {
            auto c=pulses(true);auto a=chi::analyse(c,token);std::mt19937 rng(42);
            for(int trial=0;trial<250;++trial) {
                s.swing=(static_cast<int>(rng()%101)-50)/100.0;s.strength=(rng()%101)/100.0;
                auto map=chi::makeMap(c,a,s,token);std::size_t in=0,out=0;bool first=true;
                for(auto p:map){require(first||(p.first>in&&p.second>out),"Map not monotonic");first=false;in=p.first;out=p.second;}
                require(map.begin()->first==0&&map.begin()->second==0&&map.rbegin()->second==c.frames()-1,"Wrong endpoints");
            }
            s.strength=0;for(auto p:chi::makeMap(c,a,s,token))require(p.first==p.second,"Zero intensity moved attacks");
        } else if(test=="render") {
            const auto dir=std::filesystem::absolute(argv[0]).parent_path()/"test-audio";
            std::filesystem::create_directories(dir);
            const auto original=dir/"original.wav",corrected=dir/"corrected.wav";
            std::filesystem::remove(original);std::filesystem::remove(corrected);
            chi::writeWav(original,pulses(true),token);
            auto c=chi::readWav(original,token);auto a=chi::analyse(c,token);auto map=chi::makeMap(c,a,s,token);auto r=chi::render(c,map,s,token);
            chi::writeWav(corrected,r,token);require(chi::readWav(corrected,token).channels==r.channels,"Corrected export differs");
            require(r.frames()==c.frames(),"Wrong render length");const auto after=chi::analyse(r,token);
            require(after.attacks.size()>=8,"Render lost attacks");
            std::cout<<"Timing frames before="<<timingError(a)<<" after="<<timingError(after)<<'\n';
            require(timingError(after)<timingError(a)*.5,"Correction did not reduce timing error");
            chi::AudioClip tone{48000,{std::vector<float>(48000*6)}};
            for(std::size_t i=0;i<tone.frames();++i)tone.channels[0][i]=static_cast<float>(.3*std::sin(2*3.141592653589793*440*i/48000));
            chi::TimeMap m{{0,0},{48000,60000},{192000,180000},{tone.frames()-1,tone.frames()-1}};
            const auto t=chi::render(tone,m,s,token);const double hz=pitch(t,70000,170000),cents=1200*std::log2(hz/440);
            std::cout<<"Pitch Hz="<<hz<<" cents="<<cents<<'\n';require(std::abs(cents)<5,"Pitch drift > 5 cents");
            s.preservePitch=false;auto resampled=chi::render(tone,m,s,token);require(resampled.frames()==tone.frames(),"Resample length");
            require(std::abs(pitch(resampled,70000,170000)-440)>20,"Pitch-off path did not resample");
        } else if(test=="cancel") {
            token.requested=true;auto c=pulses();mustFail([&]{chi::analyse(c,token);});mustFail([&]{chi::render(c,{{0,0},{c.frames()-1,c.frames()-1}},s,token);});
            token.requested=false;
            std::thread canceller([&]{std::this_thread::sleep_for(std::chrono::milliseconds(5));token.requested=true;});
            bool cancelled=false;
            try {chi::render(c,{{0,0},{48000,60000},{c.frames()-1,c.frames()-1}},s,token);}catch(const chi::Cancelled&){cancelled=true;}
            catch(...){canceller.join();throw;}
            canceller.join();require(cancelled,"Mid-render cancellation was not observed");
        } else if(test=="stereo") {
            auto c=pulses(true);c.channels.push_back(c.channels[0]);auto a=chi::analyse(c,token);auto r=chi::render(c,chi::makeMap(c,a,s,token),s,token);
            double error=0;for(std::size_t i=0;i<r.frames();++i)error=std::max(error,static_cast<double>(std::abs(r.channels[0][i]-r.channels[1][i])));
            require(error<1e-5,"Identical stereo channels diverged");
            for(auto& v:c.channels[1])v=-v;require(chi::analyse(c,token).attacks.size()==a.attacks.size(),"Antiphase cancelled detection");
        } else throw std::runtime_error("Unknown test");
        std::cout<<"PASS "<<test<<'\n';return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
