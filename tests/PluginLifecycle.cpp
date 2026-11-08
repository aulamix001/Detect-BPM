#include <juce_audio_utils/juce_audio_utils.h>
#include <memory>
#include <stdexcept>
#include <iostream>
#include "Engine.h"
#include <thread>
#include <chrono>
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
int main() {
    try {
        juce::ScopedJuceInitialiser_GUI init;
        for(int cycle=0;cycle<100;++cycle) {
            std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
            {std::unique_ptr<juce::AudioProcessorEditor> e(p->createEditor());e->setSize(1000,700);}
            juce::MemoryBlock state;p->getStateInformation(state);p->setStateInformation(state.getData(),static_cast<int>(state.getSize()));
            for(double rate:{44100.0,48000.0,96000.0})for(int block:{32,64,256,1024}) {
                p->setRateAndBufferSizeDetails(rate,block);p->prepareToPlay(rate,block);
                juce::AudioBuffer<float> audio(2,block);audio.clear();audio.setSample(0,0,.5f);juce::MidiBuffer midi;
                p->processBlock(audio,midi);
                if(audio.getSample(0,0)!=.5f)throw std::runtime_error("Idle pass-through failed");
                p->releaseResources();
            }
        }
        const auto wav=std::filesystem::temp_directory_path()/"chi-smart-time-lifecycle.wav";
        std::filesystem::remove(wav);
        chi::AudioClip clip{48000,{std::vector<float>(48000*6)}};
        for(std::size_t i=24000;i<clip.frames();i+=24000)clip.channels[0][i]=.8f;
        chi::Cancellation token;chi::writeWav(wav,clip,token);
        auto pump=[] {std::this_thread::sleep_for(std::chrono::milliseconds(60));juce::Timer::callPendingTimersSynchronously();};
        for(int cycle=0;cycle<10;++cycle) {
            std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
            p->setRateAndBufferSizeDetails(48000,256);p->prepareToPlay(48000,256);
            juce::MemoryBlock state;p->getStateInformation(state);
            auto xml=juce::AudioProcessor::getXmlFromBinary(state.getData(),static_cast<int>(state.getSize()));
            if(!xml)throw std::runtime_error("State XML absent");
            xml->setAttribute("audioPath",juce::String(wav.string()));
            juce::AudioProcessor::copyXmlToBinary(*xml,state);p->setStateInformation(state.getData(),static_cast<int>(state.getSize()));
            std::unique_ptr<juce::AudioProcessorEditor> e(p->createEditor());
            pump();bool imported=false;
            for(int wait=0;wait<40&&!imported;++wait){pump();for(int i=0;i<e->getNumChildComponents();++i)
                if(auto* label=dynamic_cast<juce::Label*>(e->getChildComponent(i)))imported|=label->getText().contains("WAV importado");}
            if(!imported)throw std::runtime_error("WAV restoration failed");
            if(cycle==0) {
                const auto image=e->createComponentSnapshot(e->getLocalBounds());
                const auto png=juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("editor.png");
                if(png.existsAsFile()&&!png.deleteFile())throw std::runtime_error("Cannot replace editor snapshot");
                auto stream=png.createOutputStream();
                if(!stream||!juce::PNGImageFormat().writeImageToStream(image,*stream))throw std::runtime_error("Editor snapshot failed");
            }
            auto click=[&](const char* text){for(int i=0;i<e->getNumChildComponents();++i)
                if(auto* button=dynamic_cast<juce::TextButton*>(e->getChildComponent(i)))
                    if(button->getButtonText()==text){button->onClick();return;}
                throw std::runtime_error("Control absent");};
            click("Corrigir");std::this_thread::sleep_for(std::chrono::milliseconds(5));
            if(cycle%2==0)click("Cancelar");
            e.reset();p.reset();
        }
        std::filesystem::remove(wav);
        std::cout<<"PASS 100 processor/editor/state cycles; 1200 rate/buffer combinations; 10 pending render/cancel destructions\n";return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
