#include "WaveformRibbon.h"

#include <functional>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int checks = 0;
void require(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

struct View {
    double head = 960, span = 960;
    std::uint64_t grid = 48;
    float scale = 1.f, gain = 1.f;
    int style = 0, theme = 3, mode = 0;
    juce::Rectangle<float> plot {8, 7, 300, 130};
    std::array<std::uint32_t, 3> palette {0xef9477, 0x71d9c4, 0xa99bed};
    const Customization* settings=nullptr;
};

WaveformProjectionGroup group(std::uint64_t start, std::uint64_t end, int variant = 0) {
    WaveformProjectionGroup g;
    g.sampleStart=start;g.sampleEnd=end;g.sampleCount=end-start;
    for(std::size_t channel=0;channel<2;++channel) {
        for(auto sample=start;sample<end;++sample) {
            // Deliberately asymmetric signed PCM, with actual additive density
            // and energies. Varying intervals must never synthesize or lose it.
            const int integer=int((sample*std::uint64_t(5+channel*2)+std::uint64_t(variant*11))%43)-21;
            const float audio=float(integer)/32.f+(channel==0?.0625f:-.09375f);
            if(sample==start || audio<g.minimum[channel]){g.minimum[channel]=audio;g.minimumSample[channel]=sample;}
            if(sample==start || audio>g.maximum[channel]){g.maximum[channel]=audio;g.maximumSample[channel]=sample;}
            g.sumSquares[channel]+=double(audio)*audio;
            const auto bin=std::size_t(std::llround((std::clamp(double(audio),-1.,1.)+1.)*32.));
            ++g.amplitudeCounts[channel][bin];
            for(std::size_t band=0;band<3;++band) {
                const double filtered=double(audio)*(.1+.15*double((sample+band+channel+variant)%5));
                g.bandEnergy[channel][band]+=filtered*filtered;
            }
        }
        g.rms[channel]=std::sqrt(g.sumSquares[channel]/double(g.sampleCount));
        const auto& e=g.bandEnergy[channel];const double total=e[0]+e[1]+e[2];
        for(std::size_t band=0;band<3;++band)g.bandPower[channel][band]=e[band]/total;
    }
    return g;
}

std::vector<WaveformProjectionGroup> wholeGroups(std::uint64_t first, int count, std::uint64_t grid=48) {
    std::vector<WaveformProjectionGroup> groups;
    for(int i=0;i<count;++i)groups.push_back(group(first+std::uint64_t(i)*grid,first+std::uint64_t(i+1)*grid,int((first/grid+std::uint64_t(i))%4)));
    return groups;
}

juce::Image render(WaveformRibbon& renderer,const std::vector<WaveformProjectionGroup>& groups,const View& v) {
    const int width=int(std::ceil((v.plot.getRight()+9)*v.scale));
    const int height=int(std::ceil((v.plot.getBottom()+9)*v.scale));
    juce::Image image(juce::Image::ARGB,width,height,true,juce::SoftwareImageType());
    juce::LowLevelGraphicsSoftwareRenderer context(image);
    juce::Graphics graphics(context);
    graphics.addTransform(juce::AffineTransform::scale(v.scale));
    require(std::abs(graphics.getInternalContext().getPhysicalPixelScaleFactor()-v.scale)<.001f,
            "fixture exercises the requested physical pixel scale");
    renderer.draw(graphics,v.plot,groups,v.head,v.span,v.grid,v.palette,v.mode,v.style,v.theme,v.gain,v.settings);
    return image;
}

std::size_t differentPixels(const juce::Image& a,const juce::Image& b) {
    require(a.getBounds()==b.getBounds(),"reference and cached image dimensions match");
    std::size_t different=0;
    juce::Image::BitmapData pa(a,juce::Image::BitmapData::readOnly),pb(b,juce::Image::BitmapData::readOnly);
    for(int y=0;y<a.getHeight();++y)for(int x=0;x<a.getWidth();++x)
        if(pa.getPixelColour(x,y)!=pb.getPixelColour(x,y))++different;
    return different;
}

void matchesFresh(WaveformRibbon& cached,const std::vector<WaveformProjectionGroup>& groups,const View& v,const std::string& context) {
    const auto image=render(cached,groups,v);
    WaveformRibbon fresh;const auto reference=render(fresh,groups,v);
    const auto differences=differentPixels(image,reference);
    require(differences==0,context+": cached image differs from fresh raster at "+std::to_string(differences)+" pixels");
}

void unchangedAndFractionalMotion() {
    for(float scale:{1.f,2.f})for(int style:{0,1,2}) {
        View v;v.scale=scale;v.style=style;
        auto groups=wholeGroups(0,20);
        WaveformRibbon renderer;
        auto first=render(renderer,groups,v);
        require(renderer.redrawnColumns()==groups.size(),"first draw shades each measured column once");
        const auto repeated=render(renderer,groups,v);
        require(renderer.redrawnColumns()==0,"unchanged drawing reuses all cached pigment");
        require(differentPixels(first,repeated)==0,"unchanged frames are pixel-identical");
        v.head+=11.375;
        matchesFresh(renderer,groups,v,"fractional movement at "+std::to_string(scale)+"x");
        require(renderer.redrawnColumns()==0,"fractional scrolling transforms the cached image without re-shading");
    }
}

void growingTailsAndInvalidation() {
    View v;v.head=935;v.scale=2;
    auto groups=wholeGroups(0,19);groups.push_back(group(912,935));
    WaveformRibbon renderer;
    render(renderer,groups,v);
    groups.back()=group(912,941);v.head=941;
    matchesFresh(renderer,groups,v,"growing provisional tail");
    require(renderer.redrawnColumns()==1,"only a growing partial group is re-shaded");
    groups.back()=group(912,960);v.head=960;
    matchesFresh(renderer,groups,v,"tail promoted to completed interval");
    require(renderer.redrawnColumns()==1,"promoting a tail changes one atlas column");
    // Groups are immutable once measured. Reset is the public stream boundary;
    // a new generation may legitimately reuse old sample positions with new PCM.
    for(auto& g:groups)g=group(g.sampleStart,g.sampleEnd,6);
    renderer.reset();
    matchesFresh(renderer,groups,v,"new stream generation after reset");
    require(renderer.redrawnColumns()==groups.size(),"reset invalidates identical positions from the preceding stream");
    for(int change=0;change<6;++change) {
        if(change==0)v.palette={0xff6234,0x3377ff,0xeeeeff};
        if(change==1)v.mode=1;
        if(change==2)v.style=2;
        if(change==3)v.theme=0;
        if(change==4)v.gain=1.6f;
        if(change==5)v.plot.setHeight(137.5f);
        matchesFresh(renderer,groups,v,"material or physical-height change");
        require(renderer.redrawnColumns()==groups.size(),"material changes invalidate all visible pigment columns");
    }
}

void gapsFragmentsAndWraps() {
    for(float scale:{1.f,2.f}) {
        View v;v.scale=scale;
        WaveformRibbon renderer;
        auto groups=wholeGroups(0,20);
        render(renderer,groups,v);
        groups.erase(groups.begin()+8);groups.erase(groups.begin()+12);
        v.head=971.375;
        matchesFresh(renderer,groups,v,"removed cells at fractional positions");
        groups=wholeGroups(48*7,20);v.head=48*27+9.25;
        matchesFresh(renderer,groups,v,"circular atlas wrap");
        groups=wholeGroups(48*34,20);v.head=48*54+15.5;
        matchesFresh(renderer,groups,v,"reused slots after a full atlas turn");
        groups=wholeGroups(48*34,20);
        groups.erase(groups.begin()+6);
        const auto cell=48*40;
        groups.insert(groups.begin()+6,group(cell,cell+11));
        groups.insert(groups.begin()+7,group(cell+27,cell+48));
        matchesFresh(renderer,groups,v,"two disjoint measured fragments sharing one grid cell");
        const auto image=render(renderer,groups,v);
        const double start=v.head-v.span;
        const float gapCentre=v.plot.getX()+float((double(cell+19)-start)/v.span)*v.plot.getWidth();
        const int gapX=int(std::round(gapCentre*v.scale));
        for(int y=int((v.plot.getY()+3)*v.scale);y<int((v.plot.getBottom()-3)*v.scale);++y)
            require(image.getPixelAt(gapX,y).isTransparent(),"a source-data gap remains transparent in both stereo lanes");
    }
}

void silenceDCAndBounds() {
    View v;v.scale=2;v.plot.setHeight(131.5f);
    auto groups=wholeGroups(0,20);
    for(auto& g:groups)for(std::size_t ch=0;ch<2;++ch) {
        g.minimum[ch]=g.maximum[ch]=g.rms[ch]=0;g.sumSquares[ch]=0;
        g.bandEnergy[ch].fill(0);g.bandPower[ch].fill(0);g.amplitudeCounts[ch].fill(0);
        g.amplitudeCounts[ch][32]=g.sampleCount;
    }
    WaveformRibbon renderer;auto image=render(renderer,groups,v);
    for(int y=0;y<image.getHeight();++y)for(int x=0;x<image.getWidth();++x)
        require(image.getPixelAt(x,y).isTransparent(),"digital silence never creates a decorative signal");
    for(auto& g:groups)for(std::size_t ch=0;ch<2;++ch) {
        const float value=ch==0?.375f:-.25f;
        g.minimum[ch]=g.maximum[ch]=value;g.rms[ch]=std::abs(value);
        g.sumSquares[ch]=double(value)*value*g.sampleCount;
        g.bandEnergy[ch][0]=g.sumSquares[ch];g.bandPower[ch][0]=1;
        g.amplitudeCounts[ch].fill(0);g.amplitudeCounts[ch][ch==0?44:24]=g.sampleCount;
    }
    renderer.reset();image=render(renderer,groups,v);
    int colouredRows=0;
    for(int y=0;y<image.getHeight();++y) {
        bool row=false;
        for(int x=0;x<image.getWidth();++x)if(!image.getPixelAt(x,y).isTransparent()) {
            row=true;
            require(x>=int(v.plot.getX()*v.scale)&&x<int(v.plot.getRight()*v.scale),"DC crest stays inside horizontal plot bounds");
        }
        if(row)++colouredRows;
    }
    require(colouredRows>0&&colouredRows<=6,"constant signed DC remains two thin positioned lines, never filled spikes");
}

void scaledFragmentsStayWithinAmplitudeBounds() {
    for(float scale:{1.f,2.f})for(float gain:{1.f,2.f,4.f}) {
        View v;v.scale=scale;v.gain=gain;v.head=96;v.span=96;v.plot.setHeight(131.5f);
        const std::vector<WaveformProjectionGroup> groups{group(0,17),group(30,48),group(48,96)};
        WaveformRibbon renderer;const auto image=render(renderer,groups,v);
        const float lane=v.plot.getHeight()*.5f,half=lane*.4f;
        for(int y=0;y<image.getHeight();++y) {
            const float logicalY=(float(y)+.5f)/scale;
            bool allowed=false;
            for(int channel=0;channel<2;++channel) {
                const float centre=v.plot.getY()+lane*(float(channel)+.5f);
                allowed|=std::abs(logicalY-centre)<=half+1.0f/scale;
            }
            if(allowed)continue;
            for(int x=0;x<image.getWidth();++x)
                require(image.getPixelAt(x,y).isTransparent(),
                        "fragments and full cells respect full-scale amplitude guides at "+std::to_string(gain)+"x gain (pixel "+std::to_string(x)+","+std::to_string(y)+")");
        }
    }
}

void cacheTimings() {
    using Clock=std::chrono::steady_clock;
    for(float scale:{1.f,2.f}) {
        View v;v.scale=scale;v.grid=144;v.span=2000*144;v.head=v.span;
        v.plot={8,7,1280,340};
        auto groups=wholeGroups(0,2000,v.grid);
        WaveformRibbon renderer;
        auto begin=Clock::now();
        render(renderer,groups,v);
        const auto first=std::chrono::duration<double,std::milli>(Clock::now()-begin).count();
        require(renderer.redrawnColumns()==2000,"timing fixture initializes every real source group");
        std::vector<double> timings;
        for(int frame=1;frame<=32;++frame) {
            // One new actual interval per paint; retained summaries are exactly
            // unchanged. Fixture generation happens outside the timed section.
            groups.erase(groups.begin());
            const auto start=std::uint64_t(1999+frame)*v.grid;
            groups.push_back(group(start,start+v.grid,int((start/v.grid)%4)));
            v.head=v.span+double(frame)*double(v.grid)+.375;
            begin=Clock::now();render(renderer,groups,v);
            timings.push_back(std::chrono::duration<double,std::milli>(Clock::now()-begin).count());
            require(renderer.redrawnColumns()==1,"steady scrolling shades only the newly arrived measured interval");
        }
        std::sort(timings.begin(),timings.end());
        // Report timings rather than making machine-dependent frame-time
        // promises in a unit test. This uses software, not native CoreGraphics.
        std::cout<<"Ribbon software timing "<<scale<<"x: first="<<first
                 <<" ms, steady median="<<timings[timings.size()/2]
                 <<" ms, p95="<<timings[30]<<" ms\n";
    }
}

void customizationInvalidatesAndFreezes() {
    Customization settings;
    View v;v.style=1;
    const auto groups=wholeGroups(0,20);
    WaveformRibbon renderer;
    const auto legacy=render(renderer,groups,v);
    v.settings=&settings;
    const auto defaults=render(renderer,groups,v);
    require(renderer.redrawnColumns()==0,"default customization preserves the existing cache key");
    require(differentPixels(legacy,defaults)==0,"explicit defaults retain the original rendered pixels");
    constexpr const char* controls[]{"waveDensity","waveCore","waveCrest","waveBands","waveFill","waveGradient",
                                  "waveLowColour","waveMidColour","waveHighColour","paletteMiddle"};
    for(const auto* key:controls) {
        settings.resetAll();v.mode=std::string(key)=="paletteMiddle"?1:0;
        const auto baseline=render(renderer,groups,v);
        require(settings.set(key,std::string(key)=="paletteMiddle"?.2f:0.f),"render setting is registered");
        const auto changed=render(renderer,groups,v);
        require(renderer.redrawnColumns()==groups.size(),std::string(key)+" invalidates all cached material");
        require(differentPixels(baseline,changed)>0,std::string(key)+" visibly changes measured material");
        const auto frozen=render(renderer,groups,v);
        require(renderer.redrawnColumns()==0,"frozen settings and PCM do not shade again");
        require(differentPixels(changed,frozen)==0,"customized frozen frames are pixel-identical");
        matchesFresh(renderer,groups,v,std::string(key)+" cached/fresh equivalence");
    }
    settings.resetAll();settings.set("waveFill",0);settings.set("waveCrest",0);
    const auto hidden=render(renderer,groups,v);
    for(int y=0;y<hidden.getHeight();++y)for(int x=0;x<hidden.getWidth();++x)
        require(hidden.getPixelAt(x,y).isTransparent(),"zero fill and crest leave no phantom waveform");
}

void channelSelectionAndOverlay() {
    for(float scale:{1.f,2.f})for(float gain:{1.f,4.f}) {
        Customization settings;View v;v.settings=&settings;v.scale=scale;v.gain=gain;v.plot.setHeight(131.5f);
        auto groups=wholeGroups(0,20);
        for(auto& g:groups)for(std::size_t ch=0;ch<2;++ch) {
            const float value=ch==0?.125f:-.09375f;
            g.minimum[ch]=g.maximum[ch]=value;g.rms[ch]=std::abs(value);
            g.sumSquares[ch]=double(value)*value*g.sampleCount;
            g.bandEnergy[ch].fill(0);g.bandEnergy[ch][0]=g.sumSquares[ch];
            g.bandPower[ch].fill(0);g.bandPower[ch][0]=1;
            g.amplitudeCounts[ch].fill(0);g.amplitudeCounts[ch][ch==0?36:29]=g.sampleCount;
        }
        WaveformRibbon renderer;
        for(int view:{1,2,3}) {
            settings.set("waveChannels",float(view));
            const auto image=render(renderer,groups,v);
            require(renderer.redrawnColumns()==groups.size(),"channel selection invalidates every raster column");
            const auto repeated=render(renderer,groups,v);
            require(renderer.redrawnColumns()==0,"unchanged single/overlay view reuses pigment");
            require(differentPixels(image,repeated)==0,"single/overlay frozen output is stable");
            bool foundLeft=false,foundRight=false;
            const float centre=v.plot.getCentreY(),half=v.plot.getHeight()*.4f;
            const float leftY=centre-.125f*gain*half,rightY=centre+.09375f*gain*half;
            for(int y=0;y<image.getHeight();++y)for(int x=0;x<image.getWidth();++x) {
                if(image.getPixelAt(x,y).isTransparent())continue;
                const float logicalY=(float(y)+.5f)/scale;
                const bool isLeft=std::abs(logicalY-leftY)<1.5f/scale;
                const bool isRight=std::abs(logicalY-rightY)<1.5f/scale;
                require((view!=2 && isLeft)||(view!=1 && isRight),"channel view contains only true signed DC positions on its common full-height axis");
                foundLeft|=isLeft;foundRight|=isRight;
                require(logicalY>=centre-half-1.f/scale && logicalY<=centre+half+1.f/scale,"single/overlay view stays within its amplitude guides");
            }
            require(foundLeft==(view!=2)&&foundRight==(view!=1),"Left/Right suppress the excluded channel; Overlay retains both");
            matchesFresh(renderer,groups,v,"selected channels cached/fresh equivalence");
        }
        // A gap must stay empty with both channels sharing the same centre.
        groups.erase(groups.begin()+10);v.head+=11.375;
        const auto image=render(renderer,groups,v);
        const int gapX=int((v.plot.getX()+float((504.-(v.head-v.span))/v.span)*v.plot.getWidth())*scale);
        for(int y=0;y<image.getHeight();++y)
            require(image.getPixelAt(gapX,y).isTransparent(),"overlay never bridges a missing PCM interval");
    }
}
}

int main() {
    int failed=0;
    const std::array<std::pair<const char*,std::function<void()>>,8> tests{{
        {"unchanged/fractional",unchangedAndFractionalMotion},
        {"tails/invalidation",growingTailsAndInvalidation},
        {"gaps/fragments/wraps",gapsFragmentsAndWraps},
        {"silence/DC/bounds",silenceDCAndBounds},
        {"scaled fragment bounds",scaledFragmentsStayWithinAmplitudeBounds},
        {"cache timings",cacheTimings},
        {"customization/freeze",customizationInvalidatesAndFreezes},
        {"channel views",channelSelectionAndOverlay}}};
    for(const auto& test:tests)try{test.second();std::cout<<"PASS "<<test.first<<'\n';}
        catch(const std::exception& error){++failed;std::cerr<<"FAIL "<<test.first<<": "<<error.what()<<'\n';}
    std::cout<<"Waveform ribbon tests: "<<checks<<" checks, "<<failed<<" failed suites\n";
    return failed==0?0:1;
}
