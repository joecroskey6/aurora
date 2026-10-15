#pragma once

#include <JuceHeader.h>
#include <optional>
#include "Customization.h"
#include "WaveformProjection.h"

// Original raster material for a measured waveform. Geometry is always the
// signed min/max envelope. Interior light comes from PCM occupancy, RMS and
// filtered band energy, with no animated noise or fabricated signal paths.
// A circular, sample-indexed image caches immutable columns; fractional motion
// is a transform of that image, not a regeneration of the waveform texture.
class WaveformRibbon final {
public:
    // Start a new stream generation before reusing sample coordinates with
    // different PCM. Completed summaries are immutable within one generation.
    void reset() { atlas = {}; entries.clear(); grid = 0; redrawn = 0; }
    std::size_t redrawnColumns() const noexcept { return redrawn; }

    void draw(juce::Graphics& g, juce::Rectangle<float> plot,
              const std::vector<WaveformProjectionGroup>& groups,
              double head, double span, std::uint64_t gridSamples,
              std::array<std::uint32_t,3> colours, int colourMode,
              int style, int theme, float gain, const Customization* settings=nullptr) {
        using namespace juce;
        redrawn=0;
        if(groups.empty() || !std::isfinite(head) || head<0 || !std::isfinite(span)
            || span<=0 || gridSamples==0 || plot.isEmpty() || !std::isfinite(gain) || gain<=0)return;
        const double scale=std::max(1.0,double(g.getInternalContext().getPhysicalPixelScaleFactor()));
        const int h=std::max(4,int(std::ceil(plot.getHeight()*scale)));
        const int capacity=std::max(8,int(std::ceil(span/double(gridSamples)))+8);
        const auto nextMaterial=readMaterial(settings);
        if(atlas.isNull() || atlas.getWidth()!=capacity || atlas.getHeight()!=h
            || grid!=gridSamples || palette!=colours || mode!=colourMode
            || treatment!=style || surface!=theme || zoom!=gain || materialSettings!=nextMaterial) {
            atlas=Image(Image::ARGB,capacity,h,true);
            entries.assign(std::size_t(capacity),Entry{});
            grid=gridSamples;palette=colours;mode=colourMode;treatment=style;surface=theme;zoom=gain;
            materialSettings=nextMaterial;
        }
        const double start=head-span;
        const auto xAt=[&](double sample){return plot.getX()+float((sample-start)/span)*plot.getWidth();};
        // Two fixed lane rectangles also constrain the resampling footprint at
        // odd physical heights. This clip has constant complexity regardless of
        // how many measured time bins are visible.
        Graphics::ScopedSaveState laneClip(g);
        Path amplitudeBounds;
        const int lanes=channelView()==0?2:1;
        const float lane=plot.getHeight()/float(lanes),half=lane*.4f;
        for(int channel=0;channel<lanes;++channel)
            amplitudeBounds.addRectangle(plot.getX(),plot.getY()+lane*(float(channel)+.5f)-half,
                                         plot.getWidth(),half*2.f);
        g.reduceClipRegion(amplitudeBounds);
        // Rare non-contiguous fragments in the same grid cell must be shaded
        // separately; their distinct envelopes must never overwrite each other.
        std::vector<std::size_t> fragments;
        {
            // A write mapping invalidates JUCE's cached native CGImage even if
            // no bytes change, so keep it absent for completely unchanged data.
            std::optional<Image::BitmapData> pixels;
            for(std::size_t i=0;i<groups.size();++i) {
                const auto& group=groups[i];
                const auto cell=group.sampleStart/grid;
                const bool shared=(i>0 && groups[i-1].sampleStart/grid==cell)
                    || (i+1<groups.size() && groups[i+1].sampleStart/grid==cell);
                if(shared){fragments.push_back(i);continue;}
                const auto slot=std::size_t(cell%std::uint64_t(capacity));
                auto& entry=entries[slot];
                if(!entry.valid || entry.start!=group.sampleStart || entry.end!=group.sampleEnd) {
                    if(!pixels)pixels.emplace(atlas,Image::BitmapData::writeOnly);
                    shadeColumn(*pixels,int(slot),h,group);
                    entry={group.sampleStart,group.sampleEnd,true};++redrawn;
                }
            }
        }
        {
            Graphics::ScopedSaveState saved(g);
            g.reduceClipRegion(plot.toNearestInt());
            g.setOpacity(1.f);g.setImageResamplingQuality(Graphics::mediumResamplingQuality);
            // Only populated, contiguous full cells share one image draw. Crops
            // at gaps and circular-wrap boundaries prevent old atlas neighbours
            // from bleeding into current data during subpixel resampling.
            // Per-pixel alpha already encodes amplitude bounds, avoiding an
            // expensive clip path containing thousands of tiny rectangles.
            for(std::size_t i=0;i<groups.size();) {
                const auto& group=groups[i];
                const auto cell=group.sampleStart/grid;
                const bool shared=(i>0 && groups[i-1].sampleStart/grid==cell)
                    || (i+1<groups.size() && groups[i+1].sampleStart/grid==cell);
                if(shared){++i;continue;}
                const int slot=int(cell%std::uint64_t(capacity));
                const bool full=group.sampleStart==cell*grid && group.sampleEnd==(cell+1)*grid;
                int count=1;
                std::uint64_t end=group.sampleEnd;
                if(full)while(i+std::size_t(count)<groups.size() && slot+count<capacity) {
                    const auto& next=groups[i+std::size_t(count)];
                    const auto nextCell=cell+std::uint64_t(count);
                    if(next.sampleStart!=nextCell*grid || next.sampleEnd!=(nextCell+1)*grid)break;
                    const auto after=i+std::size_t(count)+1;
                    if(after<groups.size() && groups[after].sampleStart/grid==nextCell)break;
                    end=next.sampleEnd;++count;
                }
                const float x=xAt(double(group.sampleStart));
                const float width=xAt(double(end))-x;
                g.drawImage(atlas.getClippedImage({slot,0,count,h}),{x,plot.getY(),width,plot.getHeight()});
                i+=std::size_t(count);
            }
        }
        for(auto index:fragments){
            const auto& group=groups[index];Image fragment(Image::ARGB,1,h,true);
            {Image::BitmapData pixels(fragment,Image::BitmapData::writeOnly);shadeColumn(pixels,0,h,group);}
            Graphics::ScopedSaveState saved(g);g.reduceClipRegion(plot.toNearestInt());
            const float x=xAt(double(group.sampleStart)),w=xAt(double(group.sampleEnd))-x;
            g.setOpacity(1.f);g.drawImage(fragment,{x,plot.getY(),w,plot.getHeight()});++redrawn;
        }
        // Crest highlights are baked into the same bounded pixels. Steady
        // scrolling therefore needs no per-group fillRect calls on the native
        // graphics context, only a few cached image draws.

    }

private:
    struct Entry {std::uint64_t start=0,end=0;bool valid=false;};
    juce::Image atlas;
    std::vector<Entry> entries;
    std::array<std::uint32_t,3> palette{};
    std::uint64_t grid=0;
    int mode=-1,treatment=-1,surface=-1;
    float zoom=1.f;
    std::size_t redrawn=0;
    // A plain value snapshot also serves as the complete material cache key.
    // No Customization pointer survives draw(), and completed PCM stays immutable.
    enum MaterialIndex {LowColour,MidColour,HighColour,Density,Core,Crest,Bands,Fill,Gradient,Middle,Channels,SettingCount};
    std::array<float,SettingCount> materialSettings{float(0xffb77b),float(0x62ebd0),float(0xa897ff),1,1,1,1,1,1,.5f,0};
    static std::array<float,SettingCount> readMaterial(const Customization* settings) {
        std::array<float,SettingCount> values{float(0xffb77b),float(0x62ebd0),float(0xa897ff),1,1,1,1,1,1,.5f,0};
        if(!settings)return values;
        constexpr const char* keys[]{"waveLowColour","waveMidColour","waveHighColour","waveDensity","waveCore","waveCrest","waveBands","waveFill","waveGradient","paletteMiddle","waveChannels"};
        for(std::size_t i=0;i<values.size();++i){
            const float value=settings->get(keys[i]);
            if(std::isfinite(value))values[i]=std::clamp(value,0.f,i<=HighColour?float(0xffffff):i==Channels?3.f:i==Middle?1.f:4.f);
        }
        values[Middle]=std::clamp(values[Middle],.01f,.99f);
        return values;
    }
    int channelView() const noexcept{return int(materialSettings[Channels]);}

    static juce::Colour rgb(std::uint32_t value){return juce::Colour(value|0xff000000u);}
    std::array<juce::Colour,3> pigments() const {
        if(mode==0)return {rgb(std::uint32_t(materialSettings[LowColour])),rgb(std::uint32_t(materialSettings[MidColour])),rgb(std::uint32_t(materialSettings[HighColour]))};
        return {rgb(palette[0]),rgb(palette[1]),rgb(palette[2])};
    }
    juce::Colour columnColour(const WaveformProjectionGroup& group,std::size_t ch) const {
        const auto p=pigments();
        if(mode!=0){
            const float t=juce::jlimit(0.f,1.f,float((20.*std::log10(std::max(group.rms[ch],1.e-9))+60.)/60.));
            const float middle=materialSettings[Middle];
            return t<middle?p[0].interpolatedWith(p[1],t/middle):p[1].interpolatedWith(p[2],(t-middle)/(1.f-middle));
        }
        float r=0,g=0,b=0;
        for(std::size_t i=0;i<3;++i){const float w=float(group.bandPower[ch][i]);r+=p[i].getFloatRed()*w;g+=p[i].getFloatGreen()*w;b+=p[i].getFloatBlue()*w;}
        const float maximum=std::max({r,g,b,.001f});
        return juce::Colour::fromFloatRGBA(r/maximum,g/maximum,b/maximum,1.f).withMultipliedSaturation(1.12f);
    }
    static float lineLight(float distance,float width) {
        const float x=distance/std::max(width,.000001f);return 1.f/(1.f+x*x*3.f);
    }
    void shadeColumn(juce::Image::BitmapData& pixels,int x,int height,const WaveformProjectionGroup& group) const {
        using namespace juce;
        const bool dark=surface==1 || surface==3;
        const auto p=pigments();
        const int view=channelView();
        const float lane=view==0?float(height)*.5f:float(height),half=lane*.4f;
        const float samplePerPixel=1.f/(half*zoom);
        for(int y=0;y<height;++y)pixels.setPixelColour(x,y,Colours::transparentBlack);
        for(std::size_t ch=0;ch<2;++ch){
            if((view==1 && ch!=0)||(view==2 && ch!=1))continue;
            const float low=group.minimum[ch],high=group.maximum[ch];
            const float cy=lane*(view==0?float(ch)+.5f:.5f),rms=float(group.rms[ch]);
            const auto writePixel=[&](int y,Colour colour){
                if(view==3)colour=colour.withMultipliedAlpha(ch==0?.78f:.52f);
                pixels.setPixelColour(x,y,view==3?pixels.getPixelColour(x,y).overlaidWith(colour):colour);
            };
            const float visibleLow=std::max(low,-1.f/zoom),visibleHigh=std::min(high,1.f/zoom);
            std::array<float,65> density{};
            float maxDensity=0;
            for(std::size_t b=0;b<density.size();++b){
                // Sub-bin interpolation of measured occupancy, for pigment only.
                density[b]=float(group.amplitudeCounts[ch][b])*.6f;
                if(b>0)density[b]+=float(group.amplitudeCounts[ch][b-1])*.2f;
                if(b+1<density.size())density[b]+=float(group.amplitudeCounts[ch][b+1])*.2f;
                maxDensity=std::max(maxDensity,density[b]);
            }
            if(maxDensity>0)for(auto& value:density)value=std::sqrt(value/maxDensity);
            const auto hue=columnColour(group,ch);
            const auto crest=dark?hue.interpolatedWith(Colour(0xfffff7e8),.48f):hue.darker(.55f);
            const std::array<float,3> bandRms{
                float(std::sqrt(group.bandEnergy[ch][0]/double(group.sampleCount))),
                float(std::sqrt(group.bandEnergy[ch][1]/double(group.sampleCount))),
                float(std::sqrt(group.bandEnergy[ch][2]/double(group.sampleCount)))};
            const int firstRow=view==0?int(ch)*height/2:0;
            const int lastRow=view==0?(int(ch)+1)*height/2:height;
            for(int y=firstRow;y<lastRow;++y){
                const float value=(cy-float(y)-.5f)/(half*zoom);
                if(low==high) {
                    // A DC value is one positioned physical hairline. Zero
                    // remains entirely transparent; no filled body is invented.
                    const float distance=std::abs((low-value)/samplePerPixel);
                    const float alpha=low!=0.f && std::abs(low*zoom)<=1.f
                        ?std::clamp(.875f-distance,0.f,1.f)*std::clamp((treatment==2?.94f:.70f)*materialSettings[Crest],0.f,1.f):0.f;
                    writePixel(y,crest.withAlpha(alpha));continue;
                }
                const float coverage=visibleHigh>visibleLow
                    ?std::clamp((visibleHigh-value)/samplePerPixel+.5f,0.f,1.f)
                        *std::clamp((value-visibleLow)/samplePerPixel+.5f,0.f,1.f):0.f;
                if(coverage<=0 || (low==0 && high==0))continue;
                const float coordinate=std::clamp((value+1.f)*32.f,0.f,64.f);
                const int bin=std::min(63,int(coordinate));const float fraction=coordinate-float(bin);
                const float occupancy=(density[std::size_t(bin)]*(1-fraction)+density[std::size_t(bin+1)]*fraction)*materialSettings[Density];
                const float magnitude=std::abs(value);
                const float body=std::clamp((rms-magnitude)/samplePerPixel+.5f,0.f,1.f)*materialSettings[Core];
                const float rim=lineLight(std::min(std::abs(high-value),std::abs(value-low)),samplePerPixel*1.1f)*materialSettings[Crest];
                const float rmsEdge=lineLight(std::abs(magnitude-rms),samplePerPixel*.7f)*materialSettings[Core];
                const float vertical=std::clamp((value-low)/std::max(high-low,.00001f),0.f,1.f);
                // Depth follows the spectral hue. Broad cross-hue/white blends
                // would turn warm transients grey and flatten measured density.
                auto material=hue.interpolatedWith(hue.darker(.65f),std::clamp((.12f+.24f*(1.f-vertical))*materialSettings[Gradient],0.f,1.f));
                if(mode!=0)material=material.interpolatedWith(p[0],std::clamp(.08f*(1.f-vertical)*materialSettings[Gradient],0.f,1.f));
                material=material.interpolatedWith(hue.brighter(.18f),std::clamp(.23f*body,0.f,1.f));
                float glow=0;Colour thread=Colours::black;
                for(std::size_t band=0;band<3;++band){
                    const float weight=std::sqrt(float(group.bandPower[ch][band]));
                    const float light=lineLight(std::abs(magnitude-bandRms[band]),samplePerPixel*(treatment==1?1.25f:.72f))*weight*materialSettings[Bands];
                    if(light>glow){glow=light;thread=p[band];}
                }
                float luminosity=.16f+.27f*body+.32f*occupancy+.12f*rim;
                if(treatment==1){luminosity=.10f+.24f*occupancy+.17f*body+.35f*glow;material=material.interpolatedWith(thread,std::clamp(glow*.74f,0.f,1.f));}
                if(treatment==2){luminosity=.025f+.065f*occupancy+.48f*glow+.32f*rmsEdge;material=material.interpolatedWith(thread,std::clamp(glow*.8f,0.f,1.f));}
                material=material.interpolatedWith(dark?Colour(0xff080f19):Colour(0xffc6d8d7),std::clamp(.48f-luminosity*.38f,0.f,.6f));
                const float pearl=std::clamp(occupancy*(treatment==0?.075f:.035f)+rmsEdge*.23f+glow*.15f,0.f,.65f);
                material=material.interpolatedWith(dark?Colour(0xfff0ffef):Colour(0xff203c4a),pearl);
                if(!dark)material=material.interpolatedWith(Colour(0xff203545),.28f);
                const float opacity=std::clamp((treatment==2?std::clamp(luminosity*1.8f,0.f,.98f):std::clamp(.44f+luminosity*.70f,0.f,1.f))*materialSettings[Fill],0.f,1.f)*coverage;
                const float crestWidth=samplePerPixel*.8f;
                const float upper=std::clamp((visibleHigh-value)/samplePerPixel+.5f,0.f,1.f)
                    *std::clamp((value-(visibleHigh-crestWidth))/samplePerPixel+.5f,0.f,1.f);
                const float lower=std::clamp(((visibleLow+crestWidth)-value)/samplePerPixel+.5f,0.f,1.f)
                    *std::clamp((value-visibleLow)/samplePerPixel+.5f,0.f,1.f);
                const float crestAlpha=std::min(coverage,std::max(upper,lower))*std::clamp((treatment==2?.94f:.70f)*materialSettings[Crest],0.f,1.f);
                writePixel(y,material.withAlpha(opacity).overlaidWith(crest.withAlpha(crestAlpha)));
            }
        }
        // Odd physical heights must not leave a stale last row in the atlas.
        if(view==0 && height%2)pixels.setPixelColour(x,height-1,juce::Colours::transparentBlack);
    }
};
