#include "PluginProcessor.h"
#include <cmath>

// Developer-only visual QA host. Synthetic PCM is sent to the processor;
// no audio device is opened and no sound is played or recorded.
class PreviewApp final : public juce::JUCEApplication, private juce::Timer {
public:
    const juce::String getApplicationName() override { return "Aurora Preview"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    void initialise(const juce::String&) override {
        processor=std::make_unique<AuroraAudioProcessor>();
        processor->prepareToPlay(48000,1600);
        window=std::make_unique<Window>();
        window->setContentOwned(processor->createEditor(),true);
        window->centreWithSize(1280,850);window->setVisible(true);
        started=juce::Time::getMillisecondCounterHiRes()*.001;
        startTimerHz(60);
    }
    void shutdown() override {stopTimer();window.reset();processor->releaseResources();processor.reset();}
    void systemRequestedQuit() override {quit();}
private:
    struct Window : juce::DocumentWindow {
        Window():DocumentWindow("Aurora Preview — synthetic audio",juce::Colour(0xffdbe7e9),allButtons) {setUsingNativeTitleBar(true);setResizable(true,true);}
        void closeButtonPressed() override {juce::JUCEApplication::getInstance()->systemRequestedQuit();}
    };
    std::unique_ptr<AuroraAudioProcessor> processor;
    std::unique_ptr<Window> window;
    juce::AudioBuffer<float> buffer{2,1600};
    juce::MidiBuffer midi;
    uint64_t sample=0;
    double started=0;
    void timerCallback() override {
        constexpr double pi=juce::MathConstants<double>::pi;
        // Keep the demonstration sample clock tied to elapsed time, not GUI
        // callback count. A delayed paint must not slow down its synthetic audio.
        const auto target=uint64_t(std::max(0.0,juce::Time::getMillisecondCounterHiRes()*.001-started)*48000.0);
        while(sample<target){
        const int count=int(std::min<uint64_t>(1600,target-sample));
        for(int i=0;i<count;i++,sample++) {
            double t=double(sample)/48000.0,beat=std::fmod(t,0.5),env=std::exp(-beat*13);
            double kick=0.29*env*std::sin(2*pi*(54*t-1.8*std::exp(-beat*30)));
            double chord=0.08*std::sin(2*pi*220*t)+0.04*std::sin(2*pi*329.63*t)+0.04*std::sin(2*pi*523.25*t);
            double high=0.025*std::exp(-std::fmod(t,0.125)*60)*std::sin(2*pi*6400*t);
            buffer.setSample(0,i,float(kick+chord+high));
            buffer.setSample(1,i,float(kick+chord*0.83+0.045*std::sin(2*pi*440*t+0.8)+high));
        }
        auto* channels=buffer.getArrayOfWritePointers();
        juce::AudioBuffer<float> view(channels,2,count);
        processor->processBlock(view,midi);
        }
    }
};
START_JUCE_APPLICATION(PreviewApp)
