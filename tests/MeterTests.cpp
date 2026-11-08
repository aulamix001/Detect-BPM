#include "VuMeter.h"
#include <iostream>
#include <stdexcept>
#include <vector>
int main() {
    try {
        for(int block:{1,64,511,1024}) {
            chi::VuMeter meter;meter.prepare(48000);
            std::vector<float> input(1024,std::pow(10.0f,-18.0f/20));const float* ptr=input.data();
            int remaining=14400;while(remaining>0){int n=std::min(remaining,block);meter.process(&ptr,1,n);remaining-=n;}
            const double vu=20*std::log10(meter.rms.load())+18;
            if(std::abs(vu)>.06)throw std::runtime_error("300ms RMS calibration failed");
            input[0]=1.1f;meter.process(&ptr,1,1);if(!meter.clip||meter.peak<1.1f)throw std::runtime_error("Clip not latched");
            std::fill(input.begin(),input.end(),0);
            for(int i=0;i<100;++i)meter.process(&ptr,1,1024);
            if(meter.rms>1e-6||meter.clip)throw std::runtime_error("Meter did not release");
        }
        std::cout<<"PASS RMS -18dBFS, 300ms settling, clip/release, block sizes\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
