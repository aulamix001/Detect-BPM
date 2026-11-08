#include "Engine.h"
#include <rubberband/RubberBandStretcher.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>

namespace chi {
namespace {
constexpr double pi = 3.14159265358979323846;
void fft(std::vector<std::complex<double>>& a) {
    const auto n = a.size();
    for (std::size_t i=1,j=0;i<n;++i) {
        auto bit=n>>1;
        for (; j&bit; bit>>=1) j^=bit;
        j^=bit; if(i<j) std::swap(a[i],a[j]);
    }
    for(std::size_t len=2;len<=n;len<<=1) {
        const auto wlen=std::polar(1.0,-2*pi/static_cast<double>(len));
        for(std::size_t i=0;i<n;i+=len) {
            std::complex<double> w=1;
            for(std::size_t j=0;j<len/2;++j) {
                const auto u=a[i+j],v=a[i+j+len/2]*w;
                a[i+j]=u+v; a[i+j+len/2]=u-v; w*=wlen;
            }
        }
    }
}
}
void AudioClip::validate() const {
    if(!std::isfinite(sampleRate)||sampleRate<8000||sampleRate>192000||channels.empty()
       ||channels.size()>2||frames()==0||frames()>static_cast<std::size_t>(sampleRate*600))
        throw std::runtime_error("Audio invalido: mono/estereo, 8-192 kHz, ate 10 minutos");
    if(frames()*channels.size()>64u*1024u*1024u)
        throw std::runtime_error("Audio excede o limite de memoria PCM (256 MiB)");
    for(const auto& c:channels) {
        if(c.size()!=frames()) throw std::runtime_error("Canais desalinhados");
        for(float v:c) if(!std::isfinite(v)) throw std::runtime_error("PCM nao finito");
    }
}
void Settings::validate() const {
    if(!std::isfinite(bpm)||bpm<30||bpm>300||!std::isfinite(strength)||strength<0||strength>1
       ||!std::isfinite(naturalness)||naturalness<0||naturalness>1||!std::isfinite(swing)
       ||swing<-.5||swing>.5||!std::isfinite(offsetSeconds)||std::abs(offsetSeconds)>10
       ||(subdivision!=4&&subdivision!=8&&subdivision!=16&&subdivision!=32)
       ||beatsPerBar<1||beatsPerBar>12) throw std::runtime_error("Parametros invalidos");
}
Analysis analyse(const AudioClip& clip,const Cancellation& cancel) {
    cancel.check(); clip.validate();
    const std::size_t n=1024,hop=128, count=(clip.frames()+hop-1)/hop;
    std::vector<double> novelty(count), energy(count), prev(n/2+1,0);
    std::vector<std::complex<double>> bins(n);
    for(std::size_t k=0;k<count;++k) {
        cancel.check(); double e=0;
        std::vector<double> mag(n/2+1,0);
        for(const auto& channel:clip.channels) {
            for(std::size_t j=0;j<n;++j) {
                const auto pos=static_cast<std::int64_t>(k*hop+j)-static_cast<std::int64_t>(n/2);
                const double v=pos>=0&&static_cast<std::size_t>(pos)<clip.frames()?channel[static_cast<std::size_t>(pos)]:0;
                bins[j]=v*(.5-.5*std::cos(2*pi*j/(n-1))); e+=v*v;
            }
            fft(bins);
            for(std::size_t j=0;j<mag.size();++j) mag[j]+=std::abs(bins[j]);
        }
        for(std::size_t j=0;j<mag.size();++j) {novelty[k]+=std::max(0.0,mag[j]-prev[j]);prev[j]=mag[j];}
        energy[k]=e;
    }
    Analysis result;
    const double maxFlux=*std::max_element(novelty.begin(),novelty.end());
    const double maxEnergy=*std::max_element(energy.begin(),energy.end());
    if(maxEnergy<1e-10||maxFlux<1e-10) return result;
    const auto refractory=static_cast<std::size_t>(clip.sampleRate*.05);
    for(std::size_t k=1;k+1<count;++k) {
        cancel.check();
        const auto lo=k>16?k-16:0, hi=std::min(count,k+17);
        double mean=0; for(auto j=lo;j<hi;++j) mean+=novelty[j]; mean/=static_cast<double>(hi-lo);
        const double rise=std::max(0.0,energy[k]-energy[k-1])/maxEnergy;
        if(novelty[k]<std::max(maxFlux*.07,mean*1.45)||novelty[k]<novelty[k-1]
           ||novelty[k]<=novelty[k+1]||rise<.00001) continue;
        const auto center=k*hop;
        const auto start=center>n/2?center-n/2:0, end=std::min(clip.frames(),center+n/2);
        double peak=0; for(auto j=start;j<end;++j) for(const auto& c:clip.channels) peak=std::max(peak,static_cast<double>(std::abs(c[j])));
        auto frame=start;
        for(;frame<end;++frame) {
            double v=0;for(const auto& c:clip.channels)v=std::max(v,static_cast<double>(std::abs(c[frame])));
            if(v>=peak*.18) break;
        }
        if(frame<clip.frames()&&(result.attacks.empty()||frame>result.attacks.back().frame+refractory))
            result.attacks.push_back({frame,novelty[k]/maxFlux});
    }
    if(result.attacks.size()<3) return result;
    std::vector<double> intervals;
    for(std::size_t j=1;j<result.attacks.size();++j) {
        double t=static_cast<double>(result.attacks[j].frame-result.attacks[j-1].frame)/clip.sampleRate;
        if(t>0) {double bpm=60/t; while(bpm<70)bpm*=2;while(bpm>180)bpm/=2;intervals.push_back(bpm);}
    }
    std::sort(intervals.begin(),intervals.end());
    if(!intervals.empty()) {
        result.bpm=intervals[intervals.size()/2];
        const auto good=std::count_if(intervals.begin(),intervals.end(),[&](double v){return std::abs(v-result.bpm)<result.bpm*.06;});
        result.confidence=static_cast<double>(good)/intervals.size()*std::min(1.0,intervals.size()/8.0);
    }
    return result;
}
TimeMap makeMap(const AudioClip& clip,const Analysis& analysis,const Settings& s,const Cancellation& cancel) {
    cancel.check();clip.validate();s.validate();
    const auto last=clip.frames()-1;
    TimeMap map{{0,0},{last,last}};
    const double step=clip.sampleRate*60/s.bpm*4/s.subdivision, offset=s.offsetSeconds*clip.sampleRate;
    std::size_t prevIn=0,prevOut=0;
    const auto guard=static_cast<std::size_t>(clip.sampleRate*.025);
    for(const auto& attack:analysis.attacks) {
        cancel.check(); const auto input=attack.frame;
        if(input<=prevIn||input>=last)continue;
        const auto index=static_cast<long long>(std::llround((input-offset)/step));
        double nearest=static_cast<double>(input),best=step*3;
        for(long long i=index-2;i<=index+2;++i) {
            const double point=offset+(i+((i%2)!=0?s.swing:0))*step;
            if(std::abs(point-input)<best){best=std::abs(point-input);nearest=point;}
        }
        double shift=nearest-input;
        if(std::abs(shift)<s.naturalness*clip.sampleRate*.012)shift=0;
        const double delta=static_cast<double>(input-prevIn),remaining=static_cast<double>(last-input);
        const double low=std::max(prevOut+delta*.5,static_cast<double>(last)-remaining*2);
        const double high=std::min(prevOut+delta*2,static_cast<double>(last)-remaining*.5);
        if(low>high)continue;
        const auto output=static_cast<std::size_t>(std::llround(std::clamp(input+shift*s.strength,low,high)));
        if(output<=prevOut||output>=last)continue;
        // Guard intervals keep the immediate onset neighbourhood at unit slope.
        if(s.protectTransients&&input>prevIn+guard&&output>prevOut+guard)
            map.emplace(input-guard,output-guard);
        map.emplace(input,output);
        if(s.protectTransients&&input+guard<last&&output+guard<last) {
            map.emplace(input+guard,output+guard);prevIn=input+guard;prevOut=output+guard;
        } else {prevIn=input;prevOut=output;}
    }
    std::size_t in=0,out=0;bool first=true;
    for(const auto& p:map) {if(!first&&(p.first<=in||p.second<=out))throw std::runtime_error("Mapa nao monotonico");first=false;in=p.first;out=p.second;}
    return map;
}
AudioClip render(const AudioClip& clip,const TimeMap& map,const Settings& s,const Cancellation& cancel) {
    cancel.check();clip.validate();s.validate();
    if(map.empty()||map.begin()->first!=0||map.begin()->second!=0||map.rbegin()->first!=clip.frames()-1
       ||map.rbegin()->second!=clip.frames()-1)throw std::runtime_error("Extremos do mapa invalidos");
    bool identity=true; std::size_t prev=0;bool first=true;
    for(const auto& p:map){if(p.first>=clip.frames()||p.second>=clip.frames()||(!first&&p.second<=prev))throw std::runtime_error("Mapa invalido");first=false;prev=p.second;identity&=p.first==p.second;}
    if(identity)return clip;
    AudioClip output{clip.sampleRate,std::vector<std::vector<float>>(clip.channels.size())};
    if(!s.preservePitch) {
        for(auto& c:output.channels)c.resize(clip.frames());
        auto a=map.begin(),b=std::next(a);
        for(std::size_t i=0;i<clip.frames();++i) {
            if(i%1024==0)cancel.check();
            while(b!=map.end()&&i>b->second){++a;++b;}
            const double pos=b==map.end()?static_cast<double>(clip.frames()-1):a->first+static_cast<double>(i-a->second)*(b->first-a->first)/(b->second-a->second);
            const auto j=static_cast<std::size_t>(pos),k=std::min(j+1,clip.frames()-1);const float f=static_cast<float>(pos-j);
            for(std::size_t c=0;c<clip.channels.size();++c)output.channels[c][i]=clip.channels[c][j]*(1-f)+clip.channels[c][k]*f;
        }
        return output;
    }
    using RB=RubberBand::RubberBandStretcher;
    RB stretcher(static_cast<std::size_t>(clip.sampleRate),clip.channels.size(),
        RB::OptionProcessOffline|RB::OptionChannelsTogether|RB::OptionThreadingNever|RB::OptionEngineFiner|
        (s.protectTransients?RB::OptionTransientsCrisp:RB::OptionTransientsSmooth),1.0,1.0);
    constexpr std::size_t block=1024;
    stretcher.setMaxProcessSize(block);stretcher.setExpectedInputDuration(clip.frames());
    // R3 derives its first ratio from target/source; its implicit origin must not
    // be submitted as 0/0. Keep the explicit origin in our own validated map.
    auto libraryMap=map;libraryMap.erase(0);stretcher.setKeyFrameMap(libraryMap);
    std::vector<const float*> input(clip.channels.size());
    for(std::size_t pos=0;pos<clip.frames();pos+=block) {
        cancel.check();const auto count=std::min(block,clip.frames()-pos);
        for(std::size_t c=0;c<input.size();++c)input[c]=clip.channels[c].data()+pos;
        stretcher.study(input.data(),count,pos+count==clip.frames());
    }
    std::vector<std::vector<float>> scratch(clip.channels.size(),std::vector<float>(block));
    std::vector<float*> pointers(clip.channels.size());for(std::size_t c=0;c<pointers.size();++c)pointers[c]=scratch[c].data();
    auto drain=[&] {
        while(stretcher.available()>0) {
            cancel.check();const auto got=stretcher.retrieve(pointers.data(),std::min(block,static_cast<std::size_t>(stretcher.available())));
            if(!got)throw std::runtime_error("Rubber Band nao retornou dados");
            if(output.frames()+got>clip.frames()+clip.sampleRate)throw std::runtime_error("Render excedeu o tamanho esperado");
            for(std::size_t c=0;c<pointers.size();++c)output.channels[c].insert(output.channels[c].end(),scratch[c].begin(),scratch[c].begin()+static_cast<std::ptrdiff_t>(got));
        }
    };
    for(std::size_t pos=0;pos<clip.frames();pos+=block) {
        cancel.check();const auto count=std::min(block,clip.frames()-pos);
        for(std::size_t c=0;c<input.size();++c)input[c]=clip.channels[c].data()+pos;
        stretcher.process(input.data(),count,pos+count==clip.frames());drain();
    }
    drain();
    if(output.frames()!=clip.frames())throw std::runtime_error("Duracao inesperada do render");
    output.validate();return output;
}
}
