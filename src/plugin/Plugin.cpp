#include "Engine.h"
#include "../ui/Theme.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace {
using namespace juce;
static_assert(std::atomic<double>::is_always_lock_free && std::atomic<float>::is_always_lock_free
              && std::atomic<unsigned>::is_always_lock_free && std::atomic<int>::is_always_lock_free
              && std::atomic<bool>::is_always_lock_free,"Audio-thread atomics must be lock-free");
using Task = std::function<void(const chi::Cancellation&)>;
struct Snapshot {
    std::shared_ptr<const chi::AudioClip> original, corrected;
    std::shared_ptr<const std::vector<float>> originalPeaks, correctedPeaks;
    chi::Analysis analysis;
    chi::TimeMap map;
    String message = "Importe um WAV mono ou estereo";
    String path;
    unsigned renderVersion=0;
};
struct Preview {
    enum {free,writing,ready,reading};
    std::atomic<int> state{free};
    chi::AudioClip original, corrected;
    unsigned version=0,transport=0;
};
class Processor final : public AudioProcessor, private AudioProcessorValueTreeState::Listener {
public:
    Processor() : AudioProcessor(BusesProperties().withInput("Input",AudioChannelSet::stereo(),true)
                  .withOutput("Output",AudioChannelSet::stereo(),true)), params(*this,nullptr,"SMART_TIME",layout()) {
        for(const char* id:ids)params.addParameterListener(id,this);
        worker=std::thread([this]{run();});
    }
    ~Processor() override {
        for(const char* id:ids)params.removeParameterListener(id,this);
        {std::lock_guard<std::mutex> lock(jobMutex);stopping=true;if(token)token->requested=true;pending={};}
        changed.notify_one();if(worker.joinable())worker.join();
    }
    AudioProcessorValueTreeState params;
    std::atomic<unsigned> version{1};
    std::atomic<bool> busy{false}, playing{false}, useB{false};
    std::atomic<float> inputPeak{0},outputPeak{0};
    std::atomic<double> rate{48000}, hostBpm{0};
    void stopPreview() noexcept {++transportVersion;playing=false;}
    Snapshot snapshot() {std::lock_guard<std::mutex> lock(sessionMutex);return session;}
    chi::Settings settings() const {
        auto val=[&](const char* id){return static_cast<double>(params.getRawParameterValue(id)->load());};
        chi::Settings s;s.bpm=val("bpm");s.strength=val("strength")/100;s.naturalness=val("natural")/100;
        s.swing=val("swing")/100;s.subdivision=4<<static_cast<int>(val("grid"));s.beatsPerBar=static_cast<int>(val("meter"));
        s.preservePitch=val("pitch")>.5;s.protectTransients=val("protect")>.5;s.offsetSeconds=val("offset")/1000;
        const auto source=static_cast<int>(val("source"));
        if(source==2) {s.bpm=hostBpm.load();if(s.bpm==0)throw std::runtime_error("DAW nao fornece BPM");}
        return s;
    }
    void importFile(const File& file) {
        stopPreview();const auto v=++version;
        submit([this,file,v](const chi::Cancellation& cancel){
            auto clip=std::make_shared<chi::AudioClip>(chi::readWav(std::filesystem::u8path(file.getFullPathName().toRawUTF8()),cancel));
            auto peaks=overview(*clip,cancel);
            cancel.check();if(version.load()!=v)return;
            std::lock_guard<std::mutex> lock(sessionMutex);session={};session.original=std::move(clip);session.originalPeaks=std::move(peaks);
            session.path=file.getFullPathName();session.message="WAV importado. Detecte BPM ou renderize a correcao.";
        });
    }
    void analyseFile() {
        stopPreview();
        auto snap=snapshot();if(!snap.original)return;
        submit([this,clip=snap.original](const chi::Cancellation& cancel){
            auto a=chi::analyse(*clip,cancel);cancel.check();std::lock_guard<std::mutex> lock(sessionMutex);
            if(session.original!=clip)return;
            session.analysis=std::move(a);session.message="Analise concluida; BPM pode ter ambiguidade metade/dobro.";
        });
    }
    void renderFile() {
        stopPreview();
        auto snap=snapshot();if(!snap.original)return;
        const auto v=version.load();
        chi::Settings s;
        try{s=settings();}catch(const std::exception& e){message(e.what());return;}
        const bool detected=params.getRawParameterValue("source")->load()==1;
        submit([this,clip=snap.original,s,v,detected](const chi::Cancellation& cancel) mutable {
            auto a=chi::analyse(*clip,cancel);
            if(detected){if(a.bpm==0)throw std::runtime_error("BPM indeterminado; selecione BPM fixo");s.bpm=a.bpm;}
            auto map=chi::makeMap(*clip,a,s,cancel);
            auto corrected=std::make_shared<chi::AudioClip>(chi::render(*clip,map,s,cancel));
            auto peaks=overview(*corrected,cancel);
            cancel.check();if(version.load()!=v)return;
            std::lock_guard<std::mutex> lock(sessionMutex);if(session.original!=clip)return;
            session.analysis=std::move(a);session.map=std::move(map);session.corrected=std::move(corrected);session.renderVersion=v;
            session.correctedPeaks=std::move(peaks);
            session.message="Correcao renderizada. Compare A/B ou exporte WAV.";
        });
    }
    void preview() {
        stopPreview();const auto transport=transportVersion.load();
        auto snap=snapshot();if(!snap.original)return;
        const auto v=version.load();if(useB&&(!snap.corrected||snap.renderVersion!=v)){message("Renderize a correcao atual antes de tocar B");return;}
        const auto sr=rate.load();
        submit([this,snap,v,sr,transport](const chi::Cancellation& cancel){
            auto a=resample(*snap.original,sr,cancel);
            auto b=snap.corrected&&snap.renderVersion==v?resample(*snap.corrected,sr,cancel):a;
            cancel.check();if(v!=version.load()||sr!=rate.load()||transport!=transportVersion.load())return;
            for(auto& slot:slots) {
                int expected=Preview::ready;
                if(slot.state.compare_exchange_strong(expected,Preview::writing))slot.state.store(Preview::free);
            }
            for(auto& slot:slots) {
                int expected=Preview::free;
                if(!slot.state.compare_exchange_strong(expected,Preview::writing)) {
                    expected=Preview::ready;
                    if(!slot.state.compare_exchange_strong(expected,Preview::writing))continue;
                }
                slot.original=std::move(a);slot.corrected=std::move(b);slot.version=v;slot.transport=transport;
                slot.state.store(Preview::ready,std::memory_order_release);playing=true;return;
            }
            throw std::runtime_error("Preview ocupado; pare e tente novamente");
        });
    }
    void exportFile(const File& file) {
        stopPreview();
        auto snap=snapshot();if(!snap.corrected||snap.renderVersion!=version.load())return;
        submit([this,file,clip=snap.corrected](const chi::Cancellation& cancel){
            TemporaryFile temp(file);
            chi::writeWav(std::filesystem::u8path(temp.getFile().getFullPathName().toRawUTF8()),*clip,cancel);
            cancel.check();if(!temp.overwriteTargetFileWithTemporary())throw std::runtime_error("Falha ao substituir WAV de destino");
            message("WAV exportado: "+file.getFileName());
        });
    }
    void cancel() {stopPreview();std::lock_guard<std::mutex> lock(jobMutex);if(token)token->requested=true;}
    void prepareToPlay(double sr,int) override {rate=sr;stopPreview();}
    void releaseResources() override {stopPreview();}
    bool isBusesLayoutSupported(const BusesLayout& b) const override {
        return b.getMainInputChannelSet()==b.getMainOutputChannelSet()
            &&(b.getMainOutputChannelSet()==AudioChannelSet::mono()||b.getMainOutputChannelSet()==AudioChannelSet::stereo());
    }
    void processBlock(AudioBuffer<float>& buffer,MidiBuffer&) override {
        ScopedNoDenormals noDenormals;
        float in=0;for(int c=0;c<buffer.getNumChannels();++c)in=std::max(in,buffer.getMagnitude(c,0,buffer.getNumSamples()));inputPeak=in;
        double bpm=0;
        if(auto* ph=getPlayHead())if(auto pos=ph->getPosition())if(auto tempo=pos->getBpm())if(*tempo>=30&&*tempo<=300)bpm=*tempo;
        hostBpm=bpm;
        for(auto& slot:slots) {
            int expected=Preview::ready;
            if(slot.state.compare_exchange_strong(expected,Preview::reading,std::memory_order_acquire)) {
                if(active)active->state.store(Preview::free,std::memory_order_release);
                active=&slot;position=0;blend=useB?1.0f:0.0f;
            }
        }
        if(playing&&active&&active->version==version.load()&&active->transport==transportVersion.load()&&active->original.sampleRate==rate.load()) {
            const float target=useB?1.0f:0.0f,delta=1.0f/240.0f;
            for(int i=0;i<buffer.getNumSamples();++i) {
                blend+=std::clamp(target-blend,-delta,delta);
                for(int c=0;c<buffer.getNumChannels();++c) {
                    const auto channel=std::min(static_cast<std::size_t>(c),active->original.channels.size()-1);
                    buffer.setSample(c,i,position<active->original.frames()?active->original.channels[channel][position]*(1-blend)+active->corrected.channels[channel][position]*blend:0);
                }
                if(position<active->original.frames())++position;else playing=false;
            }
        } else if(playing) playing=false;
        float out=0;for(int c=0;c<buffer.getNumChannels();++c)out=std::max(out,buffer.getMagnitude(c,0,buffer.getNumSamples()));outputPeak=out;
    }
    const String getName() const override {return "SMART TIME";}
    bool acceptsMidi() const override{return false;} bool producesMidi() const override{return false;}
    double getTailLengthSeconds() const override{return 0;}
    int getNumPrograms() override{return 1;} int getCurrentProgram() override{return 0;}
    void setCurrentProgram(int) override{} const String getProgramName(int) override{return {};}
    void changeProgramName(int,const String&) override{}
    bool hasEditor() const override{return true;} AudioProcessorEditor* createEditor() override;
    void getStateInformation(MemoryBlock& dest) override {
        auto tree=params.copyState();tree.setProperty("schema",1,nullptr);
        tree.setProperty("audioPath",snapshot().path,nullptr);if(auto xml=tree.createXml())copyXmlToBinary(*xml,dest);
    }
    void setStateInformation(const void* data,int size) override {
        if(auto xml=getXmlFromBinary(data,size)) {
            auto tree=ValueTree::fromXml(*xml);
            if(tree.hasType("SMART_TIME")&&static_cast<int>(tree.getProperty("schema",1))==1) {
                params.replaceState(tree);
                const String path=tree.getProperty("audioPath");
                // File reopening is queued on the message thread, never done in state restoration.
                {std::lock_guard<std::mutex> lock(restoreMutex);restorePath=path;}
                ++version;stopPreview();
            }
        }
    }
    void restorePending() {
        String path;{std::lock_guard<std::mutex> lock(restoreMutex);std::swap(path,restorePath);}
        if(path.isNotEmpty())importFile(File(path));
    }
private:
    static std::shared_ptr<const std::vector<float>> overview(const chi::AudioClip& clip,const chi::Cancellation& cancel) {
        const auto bins=std::min<std::size_t>(4096,clip.frames());
        auto peaks=std::make_shared<std::vector<float>>(bins,0);
        for(std::size_t bin=0;bin<bins;++bin) {
            cancel.check();const auto begin=bin*clip.frames()/bins,end=(bin+1)*clip.frames()/bins;
            for(const auto& channel:clip.channels)for(auto i=begin;i<end;++i)(*peaks)[bin]=std::max((*peaks)[bin],std::abs(channel[i]));
        }
        return peaks;
    }
    inline static constexpr std::array<const char*,10> ids{"bpm","strength","natural","swing","grid","meter","pitch","protect","source","offset"};
    static AudioProcessorValueTreeState::ParameterLayout layout() {
        AudioProcessorValueTreeState::ParameterLayout p;
        p.add(std::make_unique<AudioParameterFloat>(ParameterID{"bpm",1},"BPM fixo",NormalisableRange<float>(30,300,.01f),120));
        p.add(std::make_unique<AudioParameterFloat>(ParameterID{"strength",1},"Intensidade",NormalisableRange<float>(0,100,.1f),100));
        p.add(std::make_unique<AudioParameterFloat>(ParameterID{"natural",1},"Naturalidade",NormalisableRange<float>(0,100,.1f),35));
        p.add(std::make_unique<AudioParameterFloat>(ParameterID{"swing",1},"Swing",NormalisableRange<float>(-50,50,.1f),0));
        p.add(std::make_unique<AudioParameterFloat>(ParameterID{"offset",1},"Origem da grade (ms)",NormalisableRange<float>(-1000,1000,.1f),0));
        p.add(std::make_unique<AudioParameterChoice>(ParameterID{"grid",1},"Quantizacao",StringArray{"1/4","1/8","1/16","1/32"},2));
        p.add(std::make_unique<AudioParameterInt>(ParameterID{"meter",1},"Compasso (n/4)",1,12,4));
        p.add(std::make_unique<AudioParameterBool>(ParameterID{"pitch",1},"Preservar afinacao",true));
        p.add(std::make_unique<AudioParameterBool>(ParameterID{"protect",1},"Proteger transientes",true));
        p.add(std::make_unique<AudioParameterChoice>(ParameterID{"source",1},"Referencia BPM",StringArray{"BPM fixo","BPM detectado","BPM da DAW"},0));
        return p;
    }
    void parameterChanged(const String&,float) override {++version;stopPreview();}
    void message(String s) {std::lock_guard<std::mutex> lock(sessionMutex);session.message=std::move(s);}
    static chi::AudioClip resample(const chi::AudioClip& source,double sr,const chi::Cancellation& cancel) {
        if(sr==source.sampleRate)return source;
        chi::AudioClip out{sr,std::vector<std::vector<float>>(source.channels.size())};
        const auto frames=static_cast<std::size_t>(std::llround(source.frames()*sr/source.sampleRate));
        if(frames*source.channels.size()>64u*1024u*1024u)throw std::runtime_error("Preview excede limite de memoria");
        for(std::size_t c=0;c<out.channels.size();++c){out.channels[c].resize(frames);for(std::size_t i=0;i<frames;++i){
            if(i%1024==0)cancel.check();const double pos=i*source.sampleRate/sr;
            const auto j=std::min(static_cast<std::size_t>(pos),source.frames()-1),k=std::min(j+1,source.frames()-1);
            const auto f=static_cast<float>(pos-j);out.channels[c][i]=source.channels[c][j]*(1-f)+source.channels[c][k]*f;
        }}return out;
    }
    void submit(Task task) {
        playing=false;{std::lock_guard<std::mutex> lock(jobMutex);if(token)token->requested=true;
            token=std::make_shared<chi::Cancellation>();pending=std::move(task);busy=true;}
        changed.notify_one();
    }
    void run() {
        for(;;) {
            Task task;std::shared_ptr<chi::Cancellation> cancelToken;
            {std::unique_lock<std::mutex> lock(jobMutex);changed.wait(lock,[this]{return stopping||static_cast<bool>(pending);});
                if(stopping)return;task=std::move(pending);pending={};cancelToken=token;}
            try {task(*cancelToken);}
            catch(const chi::Cancelled&){try{message("Operacao cancelada");}catch(...){workerError=true;}}
            catch(const std::exception& e){try{message(String("Erro: ")+e.what());}catch(...){workerError=true;}}
            catch(...){try{message("Erro inesperado no processamento");}catch(...){workerError=true;}}
            {std::lock_guard<std::mutex> lock(jobMutex);if(!pending)busy=false;}
        }
    }
    std::atomic<bool> workerError{false};std::atomic<unsigned> transportVersion{0};
    Snapshot session;std::mutex sessionMutex,jobMutex,restoreMutex;
    std::condition_variable changed;std::thread worker;bool stopping=false;Task pending;
    std::shared_ptr<chi::Cancellation> token;String restorePath;
    std::array<Preview,3> slots;Preview* active=nullptr;std::size_t position=0;float blend=0;
};

class Editor final : public AudioProcessorEditor,private Timer {
public:
    explicit Editor(Processor& processor) : AudioProcessorEditor(processor),p(processor) {
        setLookAndFeel(&look);
        for(auto* b:{&open,&detect,&render,&play,&stop,&exportWav,&cancel,&future})addAndMakeVisible(b);
        render.getProperties().set("primary",true);exportWav.getProperties().set("primary",true);
        future.setEnabled(false);
        open.onClick=[this]{choose(false);};detect.onClick=[this]{p.analyseFile();};render.onClick=[this]{p.renderFile();};
        play.onClick=[this]{p.preview();};stop.onClick=[this]{p.stopPreview();};cancel.onClick=[this]{p.cancel();};exportWav.onClick=[this]{choose(true);};
        addAndMakeVisible(ab);ab.setButtonText("B corrigido (A original desligado)");ab.onClick=[this]{p.useB=ab.getToggleState();};
        addAndMakeVisible(pitch);addAndMakeVisible(protect);
        pitch.setButtonText("Preservar afinacao");protect.setButtonText("Proteger transientes");
        pitchAttach=std::make_unique<ButtonAttachment>(p.params,"pitch",pitch);protectAttach=std::make_unique<ButtonAttachment>(p.params,"protect",protect);
        const std::array<const char*,6> ids{"bpm","strength","natural","swing","offset","meter"};
        const std::array<const char*,6> labels{"BPM fixo","Intensidade %","Naturalidade %","Swing %","Origem ms","Compasso n/4"};
        for(std::size_t i=0;i<knobs.size();++i) {
            addAndMakeVisible(knobs[i]);knobs[i].setSliderStyle(Slider::RotaryHorizontalVerticalDrag);knobs[i].setTextBoxStyle(Slider::TextBoxBelow,false,80,20);
            
            addAndMakeVisible(names[i]);names[i].setText(labels[i],dontSendNotification);names[i].setJustificationType(Justification::centred);names[i].setColour(Label::textColourId,chi::Theme::muted);
            attachments[i]=std::make_unique<SliderAttachment>(p.params,ids[i],knobs[i]);
        }
        addAndMakeVisible(grid);grid.addItemList({"1/4","1/8","1/16","1/32"},1);
        addAndMakeVisible(source);source.addItemList({"BPM fixo","BPM detectado","BPM da DAW"},1);
        gridAttach=std::make_unique<ComboAttachment>(p.params,"grid",grid);sourceAttach=std::make_unique<ComboAttachment>(p.params,"source",source);
        addAndMakeVisible(status);status.setColour(Label::textColourId,chi::Theme::muted);
        setResizable(true,true);setResizeLimits(900,680,1500,1000);setSize(1050,700);startTimerHz(20);
    }
    ~Editor() override {stopTimer();setLookAndFeel(nullptr);}
    void resized() override {
        auto r=getLocalBounds().reduced(30);r.removeFromTop(110);
        auto buttons=r.removeFromTop(34);
        for(auto* b:{&open,&detect,&render,&play,&stop,&exportWav,&cancel})b->setBounds(buttons.removeFromLeft(buttons.getWidth()/(b==&cancel?1:b==&exportWav?2:b==&stop?3:b==&play?4:b==&render?5:b==&detect?6:7)).reduced(2));
        r.removeFromTop(10);wave=r.removeFromTop(std::max(150,getHeight()/3));r.removeFromTop(10);
        auto selectors=r.removeFromTop(30);source.setBounds(selectors.removeFromLeft(180).reduced(2));grid.setBounds(selectors.removeFromLeft(100).reduced(2));ab.setBounds(selectors.removeFromLeft(300));
        auto controls=r.removeFromTop(125);const auto width=controls.getWidth()/6;
        for(std::size_t i=0;i<knobs.size();++i){auto c=controls.removeFromLeft(width);names[i].setBounds(c.removeFromTop(24));knobs[i].setBounds(c);}
        auto toggles=r.removeFromTop(28);pitch.setBounds(toggles.removeFromLeft(200));protect.setBounds(toggles.removeFromLeft(200));future.setBounds(toggles);
        status.setBounds(r);
    }
    void paint(Graphics& g) override {
        g.fillAll(chi::Theme::background);g.setColour(chi::Theme::panel);g.fillRoundedRectangle(getLocalBounds().reduced(18).toFloat(),12);
        g.setColour(chi::Theme::border);g.drawRoundedRectangle(getLocalBounds().reduced(18).toFloat(),chi::Theme::radius,1);
        g.setColour(chi::Theme::text);g.setFont(FontOptions(32,Font::bold));g.drawText("CHi",0,30,getWidth(),36,Justification::centred);
        g.setFont(FontOptions(25,Font::italic));g.drawText("SMART TIME",0,66,getWidth(),30,Justification::centred);
        g.setFont(FontOptions(12));g.drawText("INTELLIGENT TIME CORRECTION  |  0.1.0 - DEVELOPMENT",0,98,getWidth(),20,Justification::centred);
        meter(g,{40,45,160,60},inPeak,"IN");meter(g,{getWidth()-200,45,160,60},outPeak,"OUT");
        g.setColour(chi::Theme::well);g.fillRect(wave);g.setColour(chi::Theme::accent);g.drawRect(wave);
        const auto clip=p.useB&&snap.corrected&&snap.renderVersion==p.version.load()?snap.corrected:snap.original;
        if(!clip)return;
        const double frames=static_cast<double>(clip->frames());
        double bpm=120;chi::Settings s;try{s=p.settings();bpm=s.bpm;}catch(...){}
        if(p.params.getRawParameterValue("source")->load()==1&&snap.analysis.bpm>0)bpm=snap.analysis.bpm;
        const double step=clip->sampleRate*60/bpm*4/s.subdivision;
        const auto maxLines=static_cast<std::size_t>(frames/step)+3;
        for(std::size_t i=0;i<std::min<std::size_t>(maxLines,5000);++i){const double frame=s.offsetSeconds*clip->sampleRate+(i+(i%2?s.swing:0))*step;
            const int x=wave.getX()+static_cast<int>(frame/frames*wave.getWidth());if(x<wave.getX()||x>wave.getRight())continue;
            g.setColour(chi::Theme::accent.withAlpha(i%(s.subdivision*s.beatsPerBar/4)==0?.65f:.2f));g.drawVerticalLine(x,static_cast<float>(wave.getY()),static_cast<float>(wave.getBottom()));}
        g.setColour(chi::Theme::accent);
        const auto peaks=p.useB&&snap.correctedPeaks&&snap.renderVersion==p.version.load()?snap.correctedPeaks:snap.originalPeaks;
        if(!peaks)return;
        for(int x=0;x<wave.getWidth();++x){const auto begin=static_cast<std::size_t>(x)*peaks->size()/wave.getWidth(),end=std::min(peaks->size(),static_cast<std::size_t>(x+1)*peaks->size()/wave.getWidth()+1);
            float peak=0;for(auto j=begin;j<end;++j)peak=std::max(peak,(*peaks)[j]);
            const float y=static_cast<float>(wave.getCentreY()),h=std::min(1.0f,peak)*wave.getHeight()*.43f;g.drawVerticalLine(wave.getX()+x,y-h,y+h);}
        g.setColour(chi::Theme::clip);for(const auto& a:snap.analysis.attacks){auto frame=a.frame;if(p.useB){const auto it=snap.map.find(frame);if(it!=snap.map.end())frame=it->second;}
            const int x=wave.getX()+static_cast<int>(frame/frames*wave.getWidth());g.fillRect(x,wave.getY()+2,2,12);}
    }
private:
    using SliderAttachment=AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment=AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment=AudioProcessorValueTreeState::ComboBoxAttachment;
    void timerCallback() override {
        p.restorePending();snap=p.snapshot();const bool idle=!p.busy.load(),has=static_cast<bool>(snap.original),corrected=snap.corrected&&snap.renderVersion==p.version.load();
        open.setEnabled(idle);detect.setEnabled(idle&&has);render.setEnabled(idle&&has);play.setEnabled(idle&&has);exportWav.setEnabled(idle&&corrected);cancel.setEnabled(!idle);ab.setEnabled(corrected);if(!corrected){ab.setToggleState(false,dontSendNotification);p.useB=false;}
        source.setItemEnabled(3,p.hostBpm.load()>0);
        String text=p.busy?"Processando... Cancelar disponivel":snap.message;
        if(snap.analysis.bpm>0)text+=" | BPM "+String(snap.analysis.bpm,2)+" / confianca "+String(snap.analysis.confidence*100,0)+"%";
        text+=" | Ataques: "+String(static_cast<int>(snap.analysis.attacks.size()));status.setText(text,dontSendNotification);
        inPeak=std::max(p.inputPeak.load(),inPeak*.86f);outPeak=std::max(p.outputPeak.load(),outPeak*.86f);repaint();
    }
    void choose(bool save) {
        chooser=std::make_unique<FileChooser>(save?"Exportar WAV":"Importar WAV",File{},"*.wav");
        Component::SafePointer<Editor> safe(this);
        chooser->launchAsync(save?(FileBrowserComponent::saveMode|FileBrowserComponent::canSelectFiles|FileBrowserComponent::warnAboutOverwriting):(FileBrowserComponent::openMode|FileBrowserComponent::canSelectFiles),[safe,save](const FileChooser& fc){if(safe){const auto file=fc.getResult();if(file!=File{}){if(save)safe->p.exportFile(file);else safe->p.importFile(file);}}});
    }
    static void meter(Graphics& g,Rectangle<int> box,float peak,const char* name) {
        g.setColour(chi::Theme::well);g.fillRoundedRectangle(box.toFloat(),5);g.setColour(chi::Theme::text);g.setFont(FontOptions(12));g.drawText(name,box.removeFromBottom(18),Justification::centred);
        const auto center=box.getBottomLeft().toFloat()+Point<float>(box.getWidth()*.5f,0);
        const float angle=-2.8f+std::clamp((Decibels::gainToDecibels(peak,-60.0f)+60)/60,0.0f,1.0f)*2.45f;
        g.setColour(chi::Theme::needle);g.drawLine(center.x,center.y,center.x+std::cos(angle)*box.getWidth()*.4f,center.y+std::sin(angle)*box.getHeight()*.9f,2);
    }
    Processor& p;Snapshot snap;chi::SmartLookAndFeel look;Rectangle<int> wave;float inPeak=0,outPeak=0;
    TextButton open{"Abrir WAV"},detect{"Detectar BPM"},render{"Corrigir"},play{"Play"},stop{"Stop"},exportWav{"Exportar WAV"},cancel{"Cancelar"},future{"Tempo real - futuro"};
    ToggleButton ab,pitch,protect;ComboBox grid,source;Label status;
    std::array<Slider,6> knobs;std::array<Label,6> names;
    std::array<std::unique_ptr<SliderAttachment>,6> attachments;
    std::unique_ptr<ButtonAttachment> pitchAttach,protectAttach;
    std::unique_ptr<ComboAttachment> gridAttach,sourceAttach;
    std::unique_ptr<FileChooser> chooser;
};
AudioProcessorEditor* Processor::createEditor(){return new Editor(*this);}
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new Processor();}
