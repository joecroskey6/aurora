#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_opengl/juce_opengl.h>
#include "PearlFlow.h"
#include "PearlFlowMaterial.h"
#include "VisualPostProcess.h"
#include "Palette.h"
#include "Customization.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

// Real GPU integration test, not an audio host. The temporary drawable is large
// enough to read a 1200x720 default framebuffer on both 1x and Retina displays.
// Run the executable directly in a graphical desktop session. No audio device
// is opened. glFinish/readback/timing belong to this harness only.
namespace {
using namespace juce;
using namespace juce::gl;
constexpr int outputWidth=1200, outputHeight=720;
const File outputDirectory("/tmp/aurora-flow-qa");
using Palette=std::array<std::array<float,3>,3>;
constexpr Palette defaultPalette {{{.24f,.44f,.58f},{.55f,.76f,.84f},{.82f,.91f,.96f}}};

struct Result {
    std::atomic<bool> done{false};
    int failures=0;
    var report{new DynamicObject};
    Array<var> checks;

    void check(const String& name,bool passed,const String& detail={}) {
        auto* item=new DynamicObject;
        item->setProperty("name",name); item->setProperty("passed",passed);
        if(detail.isNotEmpty()) item->setProperty("detail",detail);
        checks.add(var(item));
        if(!passed) ++failures;
        std::cout<<(passed?"PASS ":"FAIL ")<<name.toStdString();
        if(detail.isNotEmpty()) std::cout<<": "<<detail.toStdString();
        std::cout<<std::endl;
    }
    void require(const String& name,bool passed,const String& detail={}) {
        check(name,passed,detail);
        if(!passed) throw std::runtime_error(name.toStdString());
    }
    void finish() {
        report.getDynamicObject()->setProperty("checks",var(checks));
        report.getDynamicObject()->setProperty("failures",failures);
        report.getDynamicObject()->setProperty("passed",failures==0);
        report.getDynamicObject()->setProperty("outputWidth",outputWidth);
        report.getDynamicObject()->setProperty("outputHeight",outputHeight);
        report.getDynamicObject()->setProperty("audioDeviceOpened",false);
        if(!outputDirectory.createDirectory().wasOk()
           || !outputDirectory.getChildFile("report.json").replaceWithText(JSON::toString(report,true))) {
            ++failures;
            std::cerr<<"FAIL writing /tmp/aurora-flow-qa/report.json"<<std::endl;
        }
        done.store(true,std::memory_order_release);
    }
};

class RenderHarness final : public Component, private OpenGLRenderer {
public:
    explicit RenderHarness(Result& r,bool captureClip,bool onlyCustomization=false):result(r),clipRequested(captureClip),customOnly(onlyCustomization) {
        setOpaque(true);
        setSize(outputWidth,outputHeight);
        context.setOpenGLVersionRequired(OpenGLContext::openGL3_2);
        context.setMultisamplingEnabled(false);
        context.setComponentPaintingEnabled(false);
        context.setContinuousRepainting(false);
        context.setRenderer(this);
        context.attachTo(*this);
    }
    ~RenderHarness() override {context.detach();}
    void paint(Graphics& g) override {g.fillAll(Colours::black);}

private:
    Result& result;
    const bool clipRequested;
    const bool customOnly;
    OpenGLContext context;
    PearlFlow flow;
    VisualPostProcess finishing;
    std::unique_ptr<OpenGLShaderProgram> material;
    GLuint triangle=0,readFramebuffer=0;
    bool attempted=false,initialised=false,targetVerified=false;
    float testYaw=0,testPitch=0,testZoom=1;

    static String glString(GLenum name) {
        const auto* text=glGetString(name);
        return text?String::fromUTF8(reinterpret_cast<const char*>(text)):String("unavailable");
    }
    void checkGL(const String& where) {
        String errors;
        for(int i=0;i<16;++i) {
            const auto error=glGetError(); if(error==GL_NO_ERROR) break;
            errors+=String::toHexString(int(error))+" ";
        }
        result.require(where+" has no GL errors",errors.isEmpty(),errors.trim());
    }
    void newOpenGLContextCreated() override {
        try {
            auto* report=result.report.getDynamicObject();
            report->setProperty("glVendor",glString(GL_VENDOR));
            report->setProperty("glRenderer",glString(GL_RENDERER));
            report->setProperty("glVersion",glString(GL_VERSION));
            GLint major=0,minor=0;
            glGetIntegerv(GL_MAJOR_VERSION,&major);glGetIntegerv(GL_MINOR_VERSION,&minor);
            result.require("OpenGL 3.2 or newer",major>3 || (major==3 && minor>=2),glString(GL_VERSION));
            const bool flowReady=flow.initialise(context);
            result.require("PearlFlow shaders compile",flowReady,flow.getError());
            const bool finishReady=finishing.initialise(context);
            result.require("Post-process shaders compile",finishReady,finishing.getError());
            material=std::make_unique<OpenGLShaderProgram>(context);
            const String fragment=String(pearlFlowMaterial)+fragmentSource;
            const bool linked=material->addVertexShader(vertexSource)
                && material->addFragmentShader(OpenGLHelpers::translateFragmentShaderToV3(fragment))
                && material->link();
            result.require("PearlFlow material compiles",linked,material->getLastError());
            glGenVertexArrays(1,&triangle);glGenFramebuffers(1,&readFramebuffer);
            result.require("Readback and fullscreen resources exist",triangle!=0 && readFramebuffer!=0);
            checkGL("Initialization");initialised=true;
        } catch(const std::exception& e) {
            if(result.failures==0)result.check("GPU initialization exception",false,e.what());
            result.report.getDynamicObject()->setProperty("exception",e.what());
            result.finish();
        }
    }
    std::vector<float> readDye() {
        GLint oldRead=0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&oldRead);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,readFramebuffer);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,flow.getDyeTexture(),0);
        const auto complete=glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
        if(!complete) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(oldRead));
            result.require("Floating point dye readback framebuffer is complete",false);
        }
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        std::vector<float> pixels(size_t(flow.getDyeWidth())*size_t(flow.getDyeHeight())*4);
        glPixelStorei(GL_PACK_ALIGNMENT,1);
        glReadPixels(0,0,flow.getDyeWidth(),flow.getDyeHeight(),GL_RGBA,GL_FLOAT,pixels.data());
        glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(oldRead));
        return pixels;
    }
    static bool finite(const std::vector<float>& data) {
        return !data.empty() && std::all_of(data.begin(),data.end(),[](float value){return std::isfinite(value);});
    }
    static bool exact(const std::vector<float>& a,const std::vector<float>& b) {
        return a.size()==b.size() && !a.empty() && std::memcmp(a.data(),b.data(),a.size()*sizeof(float))==0;
    }
    void checkPigment(const std::vector<float>& data,const String& label) {
        bool bounded=true,consistent=true;
        float maximumSum=0,maximumAlphaError=0;
        for(size_t i=0;i+3<data.size();i+=4) {
            const float sum=data[i]+data[i+1]+data[i+2];
            maximumSum=std::max(maximumSum,sum);
            maximumAlphaError=std::max(maximumAlphaError,std::abs(sum-data[i+3]));
            for(size_t c=0;c<4;++c) bounded=bounded && data[i+c]>=-0.00001f && data[i+c]<=1.001f;
            bounded=bounded && sum<=1.002f;
            // Independent RGBA16F rounding permits about 0.001 here.
            consistent=consistent && std::abs(sum-data[i+3])<=0.002f;
        }
        result.require(label+" pigment is nonnegative and bounded",bounded,"max RGB sum "+String(maximumSum,6));
        result.require(label+" alpha matches RGB concentration",consistent,"max error "+String(maximumAlphaError,6));
    }
    static double difference(const std::vector<float>& a,const std::vector<float>& b) {
        if(a.size()!=b.size() || a.empty()) return -1;
        double total=0;
        for(size_t i=0;i<a.size();++i) total+=std::abs(double(a[i])-double(b[i]));
        return total/double(a.size());
    }
    void renderMaterial(const PearlFlow::Input& input,unsigned int target,
                        int theme=0,const Palette& palette=defaultPalette,const Customization* controls=nullptr) {
        const bool ready=finishing.begin(outputWidth,outputHeight);
        if(!ready || !targetVerified)
            result.require("Post-process floating point target ready",ready,finishing.getError());
        targetVerified=true;
        glViewport(0,0,finishing.getSceneWidth(),finishing.getSceneHeight());
        glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        glClearColor(.86f,.92f,.93f,1);glClear(GL_COLOR_BUFFER_BIT);
        material->use();
        const auto uniform=[this](const char* name){return glGetUniformLocation(material->getProgramID(),name);};
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,flow.getDyeTexture());glUniform1i(uniform("flowDye"),0);
        glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,flow.getVelocityTexture());glUniform1i(uniform("flowVelocity"),1);
        glUniform2f(uniform("flowTexel"),1.f/float(flow.getDyeWidth()),1.f/float(flow.getDyeHeight()));
        glUniform3f(uniform("energy"),input.low,input.mid,input.high);
        glUniform3f(uniform("cameraOrbit"),testYaw,testPitch,testZoom);
        glUniform1f(uniform("onset"),input.onset);
        glUniform1f(uniform("phase"),input.phase);glUniform1f(uniform("gain"),input.gain);
        glUniform1f(uniform("aspect"),float(outputWidth)/float(outputHeight));
        glUniform1i(uniform("theme"),theme);
        const char* colourNames[]={"colourA","colourB","colourC"};
        for(size_t i=0;i<palette.size();++i)
            glUniform3f(uniform(colourNames[i]),palette[i][0],palette[i][1],palette[i][2]);
        glBindVertexArray(triangle);glDrawArrays(GL_TRIANGLES,0,3);
        finishing.end(target,outputWidth,outputHeight,theme,
                      controls?controls->get("sceneBloom"):1.f,
                      controls?controls->get("sceneExposure"):1.f,
                      controls?controls->get("sceneSaturation"):1.f);
    }
    std::vector<unsigned char> readPixels(unsigned int target) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER,target);
        glReadBuffer(target==0?GL_BACK:GL_COLOR_ATTACHMENT0);
        std::vector<unsigned char> pixels(size_t(outputWidth)*size_t(outputHeight)*4);
        glPixelStorei(GL_PACK_ALIGNMENT,1);
        glReadPixels(0,0,outputWidth,outputHeight,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        return pixels;
    }
    std::vector<unsigned char> readOutput(unsigned int target,const String& name,
                                         const File& directory=outputDirectory,bool announce=true) {
        auto pixels=readPixels(target);
        Image image(Image::RGB,outputWidth,outputHeight,false);
        {
            Image::BitmapData bitmap(image,Image::BitmapData::writeOnly);
            for(int y=0;y<outputHeight;++y) for(int x=0;x<outputWidth;++x) {
                const auto offset=(size_t(outputHeight-1-y)*size_t(outputWidth)+size_t(x))*4;
                bitmap.setPixelColour(x,y,Colour(pixels[offset],pixels[offset+1],pixels[offset+2]));
            }
        }
        const bool directoryReady=directory.createDirectory().wasOk();
        if(announce || !directoryReady)result.require("QA output directory",directoryReady);
        const auto file=directory.getChildFile(name+".png");
        FileOutputStream stream(file);
        if(announce || !stream.openedOk())result.require(name+" output stream opens",stream.openedOk(),file.getFullPathName());
        stream.setPosition(0);stream.truncate();
        const bool written=PNGImageFormat().writeImageToStream(image,stream);
        if(announce || !written)result.require(name+" PNG written",written,file.getFullPathName());
        return pixels;
    }
    static std::size_t changedPixels(const std::vector<unsigned char>& a,const std::vector<unsigned char>& b) {
        if(a.size()!=b.size())return std::max(a.size(),b.size())/4;
        std::size_t count=0;
        for(std::size_t i=0;i+3<a.size();i+=4)
            if(a[i]!=b[i] || a[i+1]!=b[i+1] || a[i+2]!=b[i+2])++count;
        return count;
    }
    void customizationChecks(unsigned int target,const PearlFlow::Input& snapshot) {
        // Exercise the production post-process shader and fluid/material inputs
        // directly. This harness does not instantiate VisualSurface or test its
        // message-thread/mutex bridge; editor integration is a separate check.
        Customization controls;
        auto held=snapshot;held.frozen=true;
        const auto initialDye=readDye();
        renderMaterial(held,target,1);glFinish();
        const auto baseline=readOutput(target,"custom-default");
        renderMaterial(held,target,1,defaultPalette,&controls);glFinish();
        result.require("Explicit custom defaults preserve GPU pixels",baseline==readPixels(target));
        const std::array<std::pair<const char*,float>,5> edits{{
            {"sceneBloom",0.f},{"sceneBloom",3.f},{"sceneExposure",.55f},
            {"sceneSaturation",0.f},{"sceneSaturation",2.f}}};
        int editIndex=0;
        for(const auto& edit:edits) {
            controls.resetAll();
            result.require("GPU customization key is registered",controls.set(edit.first,edit.second),edit.first);
            renderMaterial(held,target,1,defaultPalette,&controls);glFinish();
            const String label=String(edit.first)+"="+String(edit.second,2);
            const auto changed=readOutput(target,"custom-finish-"+String(++editIndex));
            const auto count=changedPixels(baseline,changed);
            result.require(label+" changes actual shader pixels",count>25,String(int(count))+" changed RGB pixels");
            renderMaterial(held,target,1,defaultPalette,&controls);glFinish();
            result.require(label+" frozen redraw is bit exact",changed==readPixels(target));
            result.require(label+" leaves measured pigment unchanged",exact(initialDye,readDye()));
        }
        controls.resetAll();controls.set("sceneSpeed",0.f);
        auto stopped=held;
        stopped.dt=(1.f/30)*controls.get("sceneSpeed");
        stopped.frozen=controls.get("sceneSpeed")==0.f;
        for(int i=0;i<8;++i)flow.update(stopped);
        renderMaterial(stopped,target,1,defaultPalette,&controls);glFinish();
        result.require("Zero-speed flow input keeps dye bit exact",exact(initialDye,readDye()));
        result.require("Zero-speed held phase keeps composited pixels bit exact",baseline==readPixels(target));

        controls.resetAll();controls.set("sceneSensitivity",0.f);
        auto unreactive=held;unreactive.frozen=false;unreactive.dt=1.f/30;
        const float sensitivity=controls.get("sceneSensitivity");
        unreactive.low*=sensitivity;unreactive.mid*=sensitivity;unreactive.high*=sensitivity;
        unreactive.onset*=sensitivity;unreactive.active=sensitivity>0.f;
        // Match the neutral stereo-width input at zero sensitivity. The Pearl
        // material below does not exercise the other five scenes' mesh widths.
        unreactive.correlation=1.f;
        for(int i=0;i<8;++i)flow.update(unreactive);
        renderMaterial(unreactive,target,1,defaultPalette,&controls);glFinish();
        const auto unreactivePixels=readOutput(target,"custom-sensitivity-zero");
        result.require("Zero-sensitivity energy uniforms change material pixels",changedPixels(baseline,unreactivePixels)>25);
        result.require("Zero-sensitivity inactive flow preserves dye",exact(initialDye,readDye()));
        renderMaterial(unreactive,target,1,defaultPalette,&controls);glFinish();
        result.require("Zero-sensitivity held material redraw is stable",unreactivePixels==readPixels(target));

        controls.resetAll();auto resumed=held;resumed.frozen=false;resumed.active=true;resumed.dt=1.f/60;
        for(int i=0;i<4;++i){resumed.phase+=resumed.dt;flow.update(resumed);}
        result.require("Restoring active flow resumes pigment evolution",!exact(initialDye,readDye()));
        result.require("Customized flow remains finite",finite(readDye()));
        checkGL("GPU customization");
        result.report.getDynamicObject()->setProperty("customizationCoverage",
            "Native post-process uniforms and direct fluid/material input gating; frozen RGB/dye readbacks. VisualSurface UI/mutex wiring and other scenes' correlation-driven mesh widths are not instantiated by this harness.");
    }
    void captureClip(unsigned int target,const PearlFlow::Input& initial) {
        constexpr int warmupSteps=120,frames=240;
        const File directory("/tmp/aurora-flow-clip");
        result.require("Clip output directory",directory.createDirectory().wasOk(),directory.getFullPathName());
        auto input=initial;input.resetGeneration=1001;input.dt=0;flow.update(input);
        const auto setEnergy=[&](float time,float dt) {
            input.dt=dt;
            input.low=.16f+.62f*std::exp(-std::fmod(time,.5f)*11.f);
            input.mid=.30f+.12f*std::sin(time*1.6f)+.05f*std::sin(time*3.7f);
            input.high=.12f+.31f*std::exp(-std::fmod(time,.25f)*25.f);
            input.correlation=.30f+.35f*std::sin(time*.6f);
            input.phase+=dt*(input.low*.48f+input.mid*.38f+input.high*.20f);
        };
        for(int i=0;i<warmupSteps;++i) {
            setEnergy(float(i)/60,1.f/60);flow.update(input);
        }
        Palette glacier{};
        const auto colours=auroraPalette(0,0,0,0);
        for(size_t i=0;i<colours.size();++i) {
            const Colour c(0xff000000u|colours[i]);
            glacier[i]={{c.getFloatRed(),c.getFloatGreen(),c.getFloatBlue()}};
        }
        for(int i=0;i<frames;++i) {
            setEnergy(float(warmupSteps)/60+float(i)/30,1.f/30);
            flow.update(input);renderMaterial(input,target,0,glacier);glFinish();
            const auto name="frame-"+String(i).paddedLeft('0',4);
            readOutput(target,name,directory,false);
            if((i+1)%60==0)std::cout<<"Native clip: "<<i+1<<" / "<<frames<<" PNG frames"<<std::endl;
        }
        result.require("Clip final dye remains finite",finite(readDye()));
        checkGL("Native clip capture");
        auto* info=new DynamicObject;
        info->setProperty("directory",directory.getFullPathName());info->setProperty("frameCount",frames);
        info->setProperty("fps",30);info->setProperty("durationSeconds",8);info->setProperty("warmupSteps",warmupSteps);
        info->setProperty("inputDt",1.0/30.0);info->setProperty("theme","Soft Club");info->setProperty("palette","Glacier");
        info->setProperty("source","Native PearlFlow + PearlFlowMaterial + VisualPostProcess; deterministic synthetic energy; no audio");
        result.report.getDynamicObject()->setProperty("clip",var(info));
    }
    void renderOpenGL() override {
        if(attempted || !initialised || result.done.load()) return;
        attempted=true;
        try {
            GLint target=0,viewport[4]{};
            glGetIntegerv(GL_FRAMEBUFFER_BINDING,&target);glGetIntegerv(GL_VIEWPORT,viewport);
            result.require("Drawable covers representative readback",viewport[2]>=outputWidth && viewport[3]>=outputHeight,
                           String(viewport[2])+" x "+String(viewport[3]));
            result.report.getDynamicObject()->setProperty("contextDefaultFramebuffer",int(context.getFrameBufferID()));
            result.report.getDynamicObject()->setProperty("renderTargetFramebuffer",int(target));

            PearlFlow::Input initial;
            initial.width=848;initial.height=512;initial.dt=0;
            initial.low=.7f;initial.mid=.4f;initial.high=.3f;initial.correlation=.25f;
            initial.phase=0;initial.seed=20261012;initial.resetGeneration=1;
            flow.update(initial);
            result.require("Flow targets allocated",flow.isReady(),flow.getError());
            const auto seed=readDye();result.require("Seed dye is finite",finite(seed));checkPigment(seed,"Seed");
            result.report.getDynamicObject()->setProperty("mode",customOnly?"customization-only":"full");
            if(customOnly) {
                auto customInput=initial;customInput.dt=1.f/60;
                for(int i=0;i<120;++i){customInput.phase=float(i+1)/60;flow.update(customInput);}
                customizationChecks(GLuint(target),customInput);
                result.finish();return;
            }
            renderMaterial(initial,GLuint(target));glFinish();
            const auto before=readOutput(GLuint(target),"before");
            checkGL("Seed frame and readback");

            auto input=initial;input.dt=1.f/60;
            for(int i=0;i<180;++i) {input.phase=float(i+1)/60;flow.update(input);}
            const auto evolved=readDye();
            result.require("180-step dye is finite",finite(evolved));
            checkPigment(evolved,"Evolved");
            const double change=difference(seed,evolved);
            result.require("Dye evolves over 180 fixed steps",!exact(seed,evolved) && change>1.e-6,String(change,8));
            result.report.getDynamicObject()->setProperty("dyeMeanAbsoluteChange",change);
            renderMaterial(input,GLuint(target));glFinish();
            const auto after=readOutput(GLuint(target),"after");
            result.require("Composited pixels change",before!=after);
            size_t varyingPixels=0;
            for(size_t i=4;i<after.size();i+=4)
                if(after[i]!=after[0] || after[i+1]!=after[1] || after[i+2]!=after[2]) ++varyingPixels;
            result.require("Composited frame has spatial detail",varyingPixels>100,String(int(varyingPixels))+" pixels differ from the corner");
            checkGL("Evolution and material composition");

            const auto heldMaterialInput=input;
            input.frozen=true;
            for(int i=0;i<30;++i) {input.phase+=1.f/60;flow.update(input);}
            result.require("Frozen dye remains bit exact",exact(evolved,readDye()));
            // The product holds scene phase/energy as well as the simulation.
            // Redraw the held scene to verify the entire finish stays unchanged.
            renderMaterial(heldMaterialInput,GLuint(target));glFinish();
            result.require("Frozen composited pixels remain bit exact",after==readPixels(GLuint(target)));
            testYaw=1.5707963f;testPitch=.24f;
            renderMaterial(heldMaterialInput,GLuint(target));
            const auto sideView=readOutput(GLuint(target),"orbit-side");
            result.require("Side orbit reveals volumetric depth",sideView!=after);
            testYaw=3.1415926f;testPitch=0;
            renderMaterial(heldMaterialInput,GLuint(target));
            const auto backView=readOutput(GLuint(target),"orbit-back");
            result.require("Back orbit differs from front and side",backView!=after && backView!=sideView);
            testYaw=0;testZoom=1.5f;
            renderMaterial(heldMaterialInput,GLuint(target));
            result.require("Zoom changes projection",readPixels(GLuint(target))!=after);
            testZoom=1;
            renderMaterial(heldMaterialInput,GLuint(target));
            result.require("Camera reset returns exact original pixels",readPixels(GLuint(target))==after);
            result.require("Camera controls leave fluid unchanged",exact(evolved,readDye()));
            input.frozen=false;input.active=false;
            for(int i=0;i<30;++i) {input.phase+=1.f/60;flow.update(input);}
            result.require("Inactive dye remains bit exact",exact(evolved,readDye()));
            input.active=true;input.low=input.mid=input.high=0;
            for(int i=0;i<30;++i) {input.phase+=1.f/60;flow.update(input);}
            result.require("Silent active dye remains bit exact",exact(evolved,readDye()));

            input=initial;input.resetGeneration=2;flow.update(input);
            result.require("Reset restores deterministic seed bit exact",exact(seed,readDye()));
            input.dt=1.f/60;
            for(int i=0;i<180;++i) {input.phase=float(i+1)/60;flow.update(input);}
            result.require("Repeated 180-step run is deterministic",exact(evolved,readDye()));
            checkGL("Freeze silence and deterministic reset");

            input=initial;input.resetGeneration=3;flow.update(input);input.dt=1.f/60;
            for(int i=0;i<900;++i) {input.phase=float(i+1)/60;flow.update(input);}
            const auto longRun=readDye();
            result.require("900-step dye is finite",finite(longRun));checkPigment(longRun,"Long-run");
            float minDensity=1,maxDensity=0;
            size_t occupied=0;double densitySum=0,densitySquares=0;
            for(size_t i=3;i<longRun.size();i+=4) {
                const float density=longRun[i];minDensity=std::min(minDensity,density);maxDensity=std::max(maxDensity,density);
                occupied+=density>0.001f?1u:0u;densitySum+=density;densitySquares+=double(density)*density;
            }
            const auto pixelCount=longRun.size()/4;
            const double mean=densitySum/double(pixelCount);
            const double variance=densitySquares/double(pixelCount)-mean*mean;
            result.require("900-step pigment is nonzero",occupied>100 && maxDensity>0.01f,String(int(occupied))+" occupied pixels");
            result.require("900-step pigment retains spatial variation",maxDensity-minDensity>0.01f && variance>1.e-6,
                           "range "+String(maxDensity-minDensity,6)+", variance "+String(variance,8));
            auto* longStats=new DynamicObject;
            longStats->setProperty("steps",900);longStats->setProperty("dt",1.0/60.0);
            longStats->setProperty("minDensity",minDensity);longStats->setProperty("maxDensity",maxDensity);
            longStats->setProperty("meanDensity",mean);longStats->setProperty("densityVariance",variance);
            longStats->setProperty("occupiedPixels",int(occupied));
            result.report.getDynamicObject()->setProperty("longRun",var(longStats));
            renderMaterial(input,GLuint(target));glFinish();
            const auto longPixels=readOutput(GLuint(target),"long-run");

            // Change each pigment colour independently on the same simulation
            // state, so evolution cannot accidentally satisfy the palette tests.
            const Palette editColours {{{.86f,.20f,.29f},{.14f,.82f,.34f},{.53f,.21f,.91f}}};
            for(size_t stop=0;stop<3;++stop) {
                auto palette=defaultPalette;palette[stop]=editColours[stop];
                renderMaterial(input,GLuint(target),0,palette);glFinish();
                result.require("Palette stop "+String(int(stop+1))+" independently changes pixels",
                               longPixels!=readPixels(GLuint(target)));
            }
            result.require("Palette edits do not change dye",exact(longRun,readDye()));
            renderMaterial(input,GLuint(target),1);glFinish();
            const auto afterhours=readOutput(GLuint(target),"afterhours");
            result.require("Afterhours changes composited pixels",afterhours!=longPixels);
            const Palette custom {{{.20f,.05f,.39f},{.93f,.34f,.38f},{.97f,.80f,.60f}}};
            renderMaterial(input,GLuint(target),1,custom);glFinish();
            result.require("Custom palette output differs",readOutput(GLuint(target),"custom")!=afterhours);
            const auto colourMap=auroraPalette(auroraDefaultPalette,0,0,0);
            Palette phosphor{};
            for(size_t i=0;i<3;++i){const auto c=Colour(0xff000000|colourMap[i]);phosphor[i]={c.getFloatRed(),c.getFloatGreen(),c.getFloatBlue()};}
            renderMaterial(input,GLuint(target),auroraFlightDeckTheme,phosphor);glFinish();
            const auto flightDeck=readOutput(GLuint(target),"flight-deck");
            result.require("Flight Deck phosphor changes composed pixels",flightDeck!=afterhours);
            renderMaterial(input,GLuint(target),auroraFlightDeckTheme,phosphor);glFinish();
            result.require("Flight Deck frozen output is stable",flightDeck==readPixels(GLuint(target)));
            result.require("Flight Deck recolouring preserves pigment",exact(longRun,readDye()));
            checkGL("Long run and independent palette edits");

            // Synchronization removes queued GPU work from adjacent samples.
            // These wall timings include submission + GPU completion; they are
            // one machine/context observation, not a cross-platform FPS claim.
            input.dt=1.f/30; // Match the product's 30 Hz / two-substep update.
            std::vector<double> timings;timings.reserve(30);
            for(int i=0;i<30;++i) {
                glFinish();const double start=Time::getMillisecondCounterHiRes();
                input.phase+=input.dt;flow.update(input);renderMaterial(input,GLuint(target));glFinish();
                timings.push_back(Time::getMillisecondCounterHiRes()-start);
            }
            Array<var> timingValues;for(double value:timings)timingValues.add(value);
            auto ordered=timings;std::sort(ordered.begin(),ordered.end());
            auto* report=result.report.getDynamicObject();
            report->setProperty("timingMethod","30 full fluid/material/bloom frames; dt=1/30 (two solver substeps); glFinish wall time; milliseconds");
            report->setProperty("timedInputDt",1.0/30.0);
            report->setProperty("frameMilliseconds",var(timingValues));
            report->setProperty("meanMilliseconds",std::accumulate(timings.begin(),timings.end(),0.0)/double(timings.size()));
            report->setProperty("medianMilliseconds",(ordered[14]+ordered[15])*.5);
            report->setProperty("p95Milliseconds",ordered[28]);report->setProperty("maxMilliseconds",ordered.back());
            result.require("Timed final dye remains finite",finite(readDye()));
            checkGL("Timed rendering");
            customizationChecks(GLuint(target),input);
            if(clipRequested)captureClip(GLuint(target),initial);
        } catch(const std::exception& e) {
            if(result.failures==0)result.check("GPU test exception",false,e.what());
            result.report.getDynamicObject()->setProperty("exception",e.what());
        }
        result.finish();
    }
    void openGLContextClosing() override {
        flow.release();finishing.release();material.reset();
        if(triangle)glDeleteVertexArrays(1,&triangle);
        if(readFramebuffer)glDeleteFramebuffers(1,&readFramebuffer);
        triangle=readFramebuffer=0;initialised=false;
    }
    inline static const char* vertexSource=R"GLSL(#version 150
out vec2 uv;
void main(){
 vec2 p=gl_VertexID==0?vec2(-1.,-1.):gl_VertexID==1?vec2(3.,-1.):vec2(-1.,3.);
 uv=p*.5+.5;gl_Position=vec4(p,0.,1.);
}
)GLSL";
    inline static const char* fragmentSource=R"GLSL(
varying vec2 uv;
uniform vec3 energy;
uniform vec3 colourA,colourB,colourC;
uniform float phase,gain,aspect;
uniform int theme;
void main(){
 vec3 background=theme==3?vec3(.063,.098,.098):theme==1?vec3(.020,.032,.065):theme==2?vec3(.82,.84,.86):vec3(.86,.92,.93);
 vec3 colour=renderLivingPearl(uv,background,colourA,colourB,colourC,energy,phase,gain,aspect,theme);
 gl_FragColor=vec4(colour,1.);
}
)GLSL";
};

class RenderingTestApp final : public JUCEApplication,private Timer {
public:
    const String getApplicationName() override {return "Aurora Render Tests";}
    const String getApplicationVersion() override {return "0.1.0";}
    void initialise(const String& commandLine) override {
        const bool captureClip=StringArray::fromTokens(commandLine,true).contains("--clip");
        const bool customOnly=StringArray::fromTokens(commandLine,true).contains("--custom-only");
        window=std::make_unique<DocumentWindow>("Aurora GPU validation",Colours::black,0);
        window->setUsingNativeTitleBar(true);
        window->setContentOwned(new RenderHarness(result,captureClip,customOnly),true);
        window->centreWithSize(window->getWidth(),window->getHeight());
        window->setVisible(true);
        started=Time::getMillisecondCounterHiRes();startTimer(100);
    }
    void shutdown() override {stopTimer();window.reset();}
    void systemRequestedQuit() override {setApplicationReturnValue(1);quit();}
private:
    Result result;
    std::unique_ptr<DocumentWindow> window;
    double started=0;
    void timerCallback() override {
        if(result.done.load(std::memory_order_acquire)) {
            stopTimer();
            setApplicationReturnValue(result.failures==0?0:1);quit();
        } else if(Time::getMillisecondCounterHiRes()-started>90000) {
            // Unsupported/no drawable contexts must fail, never silently skip.
            std::cerr<<"FAIL: no completed GPU test within 90 seconds"<<std::endl;
            stopTimer();
            setApplicationReturnValue(1);quit();
        }
    }
};
}
START_JUCE_APPLICATION(RenderingTestApp)
