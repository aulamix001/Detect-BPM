#include "Engine.h"
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

namespace chi {
namespace {
std::uint32_t readLE(std::istream& f,unsigned bytes) {
    std::uint32_t v=0;for(unsigned j=0;j<bytes;++j){const int b=f.get();if(b<0)throw std::runtime_error("WAV truncado");v|=static_cast<std::uint32_t>(b)<<(8*j);}return v;
}
void putLE(std::ostream& f,std::uint32_t v,unsigned bytes) {for(unsigned j=0;j<bytes;++j)f.put(static_cast<char>((v>>(8*j))&255));}
}
AudioClip readWav(const std::filesystem::path& file,const Cancellation& cancel) {
    cancel.check();std::ifstream f(file,std::ios::binary);if(!f)throw std::runtime_error("Nao foi possivel abrir WAV");
    const auto size=std::filesystem::file_size(file);
    if(size<44||size>512ull*1024*1024)throw std::runtime_error("Tamanho WAV invalido (limite 512 MiB)");
    if(readLE(f,4)!=0x46464952u)throw std::runtime_error("Esperado RIFF WAV");
    const auto riffEnd=static_cast<std::uint64_t>(readLE(f,4))+8;
    if(riffEnd>size||riffEnd<12||readLE(f,4)!=0x45564157u)throw std::runtime_error("RIFF invalido");
    unsigned format=0,channels=0,bits=0,rate=0,align=0;std::uint32_t dataBytes=0;std::streampos dataAt{};
    std::uint64_t pos=12;
    while(pos+8<=riffEnd) {
        cancel.check();f.seekg(static_cast<std::streamoff>(pos));const auto id=readLE(f,4),len=readLE(f,4);
        if(pos+8+len>riffEnd)throw std::runtime_error("Chunk WAV truncado");
        if(id==0x20746d66u) {
            if(len<16)throw std::runtime_error("fmt WAV invalido");
            format=readLE(f,2);channels=readLE(f,2);rate=readLE(f,4);readLE(f,4);align=readLE(f,2);bits=readLE(f,2);
        } else if(id==0x61746164u) {dataAt=f.tellg();dataBytes=len;}
        pos+=8+len+(len&1);
    }
    if(channels<1||channels>2||rate<8000||rate>192000||dataBytes==0
       ||!((format==1&&(bits==16||bits==24||bits==32))||(format==3&&bits==32))
       ||align!=channels*(bits/8)||dataBytes%align!=0)
        throw std::runtime_error("WAV suportado: PCM16/24/32 ou float32 mono/estereo");
    const auto frames=dataBytes/align;
    if(frames>rate*600ull||static_cast<std::uint64_t>(frames)*channels>64ull*1024*1024)throw std::runtime_error("WAV excede limite PCM");
    AudioClip clip{static_cast<double>(rate),std::vector<std::vector<float>>(channels,std::vector<float>(frames))};
    f.seekg(dataAt);
    for(std::size_t i=0;i<frames;++i) {
        if(i%1024==0)cancel.check();
        for(unsigned c=0;c<channels;++c) {
            auto raw=readLE(f,bits/8);float value;
            if(format==3)std::memcpy(&value,&raw,sizeof(value));
            else {
                const auto sign=std::uint64_t{1}<<(bits-1);
                const auto signedValue=(static_cast<std::uint64_t>(raw)&sign)?static_cast<std::int64_t>(raw)-static_cast<std::int64_t>(std::uint64_t{1}<<bits):static_cast<std::int64_t>(raw);
                value=static_cast<float>(static_cast<double>(signedValue)/static_cast<double>(sign));
            }
            if(!std::isfinite(value))throw std::runtime_error("PCM nao finito");
            clip.channels[c][i]=value;
        }
    }
    clip.validate();return clip;
}
void writeWav(const std::filesystem::path& file,const AudioClip& clip,const Cancellation& cancel) {
    cancel.check();clip.validate();
    // Caller chooses a temporary path; this function never replaces an existing file.
    if(std::filesystem::exists(file))throw std::runtime_error("Destino ja existe");
    try {
        std::ofstream f(file,std::ios::binary);if(!f)throw std::runtime_error("Nao foi possivel criar WAV");
        const auto bytes=static_cast<std::uint32_t>(clip.frames()*clip.channels.size()*4);
        f.write("RIFF",4);putLE(f,bytes+48,4);f.write("WAVEfmt ",8);putLE(f,16,4);
        putLE(f,3,2);putLE(f,static_cast<std::uint32_t>(clip.channels.size()),2);putLE(f,static_cast<std::uint32_t>(clip.sampleRate),4);
        putLE(f,static_cast<std::uint32_t>(clip.sampleRate)*static_cast<std::uint32_t>(clip.channels.size())*4,4);
        putLE(f,static_cast<std::uint32_t>(clip.channels.size())*4,2);putLE(f,32,2);
        f.write("fact",4);putLE(f,4,4);putLE(f,static_cast<std::uint32_t>(clip.frames()),4);f.write("data",4);putLE(f,bytes,4);
        for(std::size_t i=0;i<clip.frames();++i) {
            if(i%1024==0)cancel.check();
            for(const auto& c:clip.channels) {std::uint32_t raw;std::memcpy(&raw,&c[i],4);putLE(f,raw,4);}
        }
        f.flush();if(!f)throw std::runtime_error("Falha ao gravar WAV");f.close();if(!f)throw std::runtime_error("Falha ao fechar WAV");
    } catch(...) {std::error_code ec;std::filesystem::remove(file,ec);throw;}
}
}
