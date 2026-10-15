#include "VisualSurface.h"
#include "SpectralProjection.h"
#include "VisualFraming.h"
#include "PearlCloudShader.h"
#include "PearlFlowMaterial.h"
#include "SceneMaterial.h"
#include <cmath>
using namespace juce;
using namespace juce::gl;
namespace {
const char* vertexSource=R"GLSL(
attribute vec2 position;
uniform int mode;
uniform int pass;
uniform float bands[128];
uniform float wave[128];
uniform float rightWave[128];
uniform vec3 energy;
uniform float correlation;
uniform float phase;
uniform float gain;
uniform float aspect;
uniform vec4 framing;
uniform float zBound;
uniform vec3 cameraOrbit;
varying vec3 world;
varying vec2 uv;
varying float frequency;
const float PI=3.14159265359;
float band(float p){float k=clamp(p,0.0,1.0)*127.0;int i=int(k);return mix(bands[i],bands[min(i+1,127)],fract(k));}
float signal(float p){float k=clamp(p,0.0,1.0)*127.0;int i=int(k);return mix(wave[i],wave[min(i+1,127)],fract(k));}
float rightSignal(float p){float k=clamp(p,0.0,1.0)*127.0;int i=int(k);return mix(rightWave[i],rightWave[min(i+1,127)],fract(k));}
vec3 rotX(vec3 p,float a){return vec3(p.x,cos(a)*p.y-sin(a)*p.z,sin(a)*p.y+cos(a)*p.z);}
vec3 rotY(vec3 p,float a){return vec3(cos(a)*p.x+sin(a)*p.z,p.y,-sin(a)*p.x+cos(a)*p.z);}
vec3 rotZ(vec3 p,float a){return vec3(cos(a)*p.x-sin(a)*p.y,sin(a)*p.x+cos(a)*p.y,p.z);}
void main(){
 if(pass==0){
   uv=gl_VertexID==0?vec2(0.,0.):gl_VertexID==1?vec2(2.,0.):vec2(0.,2.);
   world=vec3(0.);frequency=0.;gl_Position=vec4(uv*2.-1.,.99,1.);return;
 }
 uv=position;
 bool closed=mode==1||mode==2||mode==5;
 float spectralCoordinate=closed?abs(2.0*uv.x-1.0):uv.x;
 frequency=band(spectralCoordinate);
 float u=uv.x*2.0*PI,v=uv.y*2.0*PI;
 float b=frequency*gain, w=signal(closed?.5+.5*cos(u):uv.x)*gain;
 float strength=min(gain,2.6);vec3 p;
 if(mode==0){
   float x=(uv.x-.5)*3.35,y=(uv.y-.5)*1.95;
   float z=.23*sin(x*2.0+phase*.24)+.17*cos(y*3.2-phase*.17);
   z+=b*.36*sin(y*3.1+x*.7)+w*.09;
   y+=.18*sin(x*1.5+phase*.12);
   p=rotX(vec3(x,y,z),.63);p=rotZ(p,-.22)*.90;
 }else if(mode==1){
   float r=.92+.19*cos(v)+.13*b+.065*sin(7.0*u+phase*.4)*energy.y*strength;
   p=vec3(r*cos(u)*(1.0+.12*(1.0-correlation)),r*sin(u),.27*sin(v)+w*.14);
   p=rotX(p,.54);p=rotZ(p,.2);
 }else if(mode==2){
   float theta=uv.y*PI;
   float r=.95+.10*sin(5.0*u+phase*.3)*sin(theta*6.0)+b*.19*sin(theta);
   p=vec3(r*sin(theta)*cos(u),r*cos(theta),r*sin(theta)*sin(u));
   p.x+=.16*sin(theta*3.0+phase*.13)*energy.x*strength;
   p=rotY(p,phase*.09+.6);
 }else if(mode==3){
   float x=(uv.x-.5)*3.2,y=(uv.y-.5)*2.8;
   float cell=floor(uv.x*40.0)/40.0;
   float towers=band(cell)*(.5+.5*sin(floor(uv.y*34.0)*1.8+phase*.3));
   float z=pow(abs(sin(uv.x*40.0*PI)),.3)*towers*.85*strength;
   p=rotX(vec3(x,y,z-.3),-.90);p=rotZ(p,-.26)*.80;p.y-=.08;
 }else if(mode==4){
   float x=(uv.x-.5)*3.2,y=(uv.y-.5)*2.1;
   float z=.19*sin(x*2.2+y*1.1+phase*.2)+.16*cos(y*3.1-x);
   z+=b*.26*sin(x*3.0-y*2.0)+w*.10+rightSignal(uv.y)*gain*.15;
   p=rotX(vec3(x,y,z),.34);p=rotZ(p,.20)*.82;
 }else{
   float radius=.78+.23*cos(3.0*u+phase*.16);
   vec3 centre=vec3(radius*cos(2.0*u),radius*sin(2.0*u),.32*sin(3.0*u+phase*.16));
   float dr=-.69*sin(3.0*u+phase*.16);
   vec3 tangent=normalize(vec3(dr*cos(2.0*u)-2.0*radius*sin(2.0*u),dr*sin(2.0*u)+2.0*radius*cos(2.0*u),.96*cos(3.0*u+phase*.16)));
   vec3 binormal=normalize(cross(tangent,vec3(cos(2.0*u),sin(2.0*u),0.0)));
   vec3 normal=normalize(cross(binormal,tangent));
   float tube=.12+.065*b+.025*energy.z*strength;
   p=centre+(normal*cos(v)+binormal*sin(v))*tube;
   p.x*=1.0+.10*(1.0-correlation);
   p=rotX(p,.38);p=rotY(p,.3+phase*.04);
 }
 p*=1.0/(1.0+max(0.0,gain-1.0)*.08);
 p=rotX(rotY(p,cameraOrbit.x),cameraOrbit.y);
 world=p;
 float perspectiveW=framing.x-p.z;
 vec2 projected=framing.y*(vec2(p.x/aspect,p.y)-framing.zw*perspectiveW);
 float nearPlane=.1,farPlane=framing.x+zBound+1.0;
 float depth=((farPlane+nearPlane)/(farPlane-nearPlane))*perspectiveW-(2.0*farPlane*nearPlane)/(farPlane-nearPlane);
 gl_Position=vec4(projected*cameraOrbit.z,depth,perspectiveW);
}
)GLSL";
const char* fragmentSource=R"GLSL(
uniform int pass;
uniform int mode;
uniform int theme;
uniform vec3 colourA;
uniform vec3 colourB;
uniform vec3 colourC;
uniform vec4 framing;
uniform float gain;
uniform float aspect;
uniform float phase;
uniform vec3 energy;
uniform vec2 resolution;
varying vec3 world;
varying vec2 uv;
varying float frequency;
vec3 gradientColour(float t){return t<.5?mix(colourA,colourB,smoothstep(0.0,.5,t)):mix(colourB,colourC,smoothstep(.5,1.0,t));}
float hash(vec2 p){return fract(sin(dot(p,vec2(12.9898,78.233)))*43758.5453);}
void main(){
 vec3 a=colourA,b=colourC;
 if(pass==0){
   vec2 p=uv-.5;
   vec3 base=theme==3?vec3(.063,.098,.098):theme==1?vec3(.020,.032,.065):theme==2?vec3(.82,.84,.86):vec3(.86,.92,.93);
   float haze=exp(-dot(p*vec2(1.1,1.8),p*vec2(1.1,1.8))*3.0);
   vec3 colour=mix(base,theme==3?a*.24:theme==1?a*.28:b*.96,haze*(theme==3?.18:theme==1?.55:.46));
   float stripe=(p.y+.20)*9.0;
   colour+=b*(theme==3?.009:.032)*exp(-stripe*stripe);
   float grid=(1.0-smoothstep(.0,.015,abs(fract(uv.x*22.0)-.5)))*(1.0-smoothstep(.0,.025,abs(fract(uv.y*15.0)-.5)));
   colour+=vec3(grid*(theme==3?.012:.035));colour+=(hash(gl_FragCoord.xy)-.5)*(theme==3?.003:.006);
   if(mode==2)colour=flowReady==1?renderLivingPearl(uv,colour,colourA,colourB,colourC,energy,phase,gain,aspect,theme):renderPearlCloud(uv,colour,colourA,colourB,colourC,energy,phase,gain,aspect,theme,resolution);
   gl_FragColor=vec4(colour,1.0);return;
 }
 vec3 n=normalize(cross(dFdx(world),dFdy(world)));if(!gl_FrontFacing)n=-n;
 vec3 view=normalize(vec3(0.0,0.0,framing.x)-world);
 vec3 col=shadeAuroraSurface(world,uv,frequency,n,view,colourA,colourB,colourC,energy,onset,phase,mode,theme);
 gl_FragColor=vec4(col,1.0);
}
)GLSL";
}
VisualSurface::VisualSurface(){setOpaque(false);setWantsKeyboardFocus(true);setMouseCursor(MouseCursor::DraggingHandCursor);context.setOpenGLVersionRequired(OpenGLContext::openGL3_2);context.setMultisamplingEnabled(true);context.setRenderer(this);context.setComponentPaintingEnabled(true);context.setContinuousRepainting(false);context.attachTo(*this);}
VisualSurface::~VisualSurface(){context.detach();}
void VisualSurface::paint(Graphics& g){if(!ready.load()){g.fillAll(Colour(0xffdae6e8));g.setColour(Colour(0xff315765));g.drawText(failed.load()?"GPU view unavailable - meters remain active":"Preparing visual surface",getLocalBounds(),Justification::centred);}}
void VisualSurface::setCamera(float yaw,float pitch,float zoom){
    std::lock_guard<std::mutex> lock(mutex);
    const auto wrap=[](float v){return std::isfinite(v)?std::remainder(v,MathConstants<float>::twoPi):0.f;};
    const float nextYaw=wrap(yaw),nextPitch=wrap(pitch),nextZoom=std::isfinite(zoom)?jlimit(.55f,2.4f,zoom):1.f;
    sceneDirty=sceneDirty || scene.yaw!=nextYaw || scene.pitch!=nextPitch || scene.zoom!=nextZoom;
    scene.yaw=nextYaw;scene.pitch=nextPitch;scene.zoom=nextZoom;
}
void VisualSurface::setCustomization(const Customization& settings){
    const auto value=[&](const char* key,float low,float high){
        const float input=settings.get(key);return std::isfinite(input)?jlimit(low,high,input):1.f;
    };
    const float speed=value("sceneSpeed",0,3),sensitivity=value("sceneSensitivity",0,3);
    const float bloom=value("sceneBloom",0,3),exposure=value("sceneExposure",.25f,2),saturation=value("sceneSaturation",0,2);
    std::lock_guard<std::mutex> lock(mutex);
    sceneDirty=sceneDirty || scene.speed!=speed || scene.sensitivity!=sensitivity
        || scene.bloom!=bloom || scene.exposure!=exposure || scene.saturation!=saturation;
    scene.speed=speed;scene.sensitivity=sensitivity;scene.bloom=bloom;
    scene.exposure=exposure;scene.saturation=saturation;
}
void VisualSurface::moveCamera(float yaw,float pitch,float zoom){
    setCamera(yaw,pitch,zoom);
    Scene s;{std::lock_guard<std::mutex> lock(mutex);s=scene;}
    if(onCameraChanged)onCameraChanged(s.yaw,s.pitch,s.zoom);
    context.triggerRepaint();
}
void VisualSurface::mouseDown(const MouseEvent&){grabKeyboardFocus();std::lock_guard<std::mutex> lock(mutex);dragYaw=scene.yaw;dragPitch=scene.pitch;}
void VisualSurface::mouseDrag(const MouseEvent& e){
    float zoom;{std::lock_guard<std::mutex> lock(mutex);zoom=scene.zoom;}
    moveCamera(dragYaw+e.getDistanceFromDragStartX()*.008f,dragPitch+e.getDistanceFromDragStartY()*.008f,zoom);
}
void VisualSurface::mouseUp(const MouseEvent&){}
void VisualSurface::mouseWheelMove(const MouseEvent&,const MouseWheelDetails& wheel){
    Scene s;{std::lock_guard<std::mutex> lock(mutex);s=scene;}
    moveCamera(s.yaw,s.pitch,s.zoom*std::exp(wheel.deltaY*1.5f));
}
void VisualSurface::mouseMagnify(const MouseEvent&,float factor){
    Scene s;{std::lock_guard<std::mutex> lock(mutex);s=scene;}
    moveCamera(s.yaw,s.pitch,s.zoom*factor);
}
void VisualSurface::mouseDoubleClick(const MouseEvent&){moveCamera(0,0,1);}
bool VisualSurface::keyPressed(const KeyPress& key){
    Scene s;{std::lock_guard<std::mutex> lock(mutex);s=scene;}
    if(key==KeyPress::leftKey)s.yaw-=.12f;else if(key==KeyPress::rightKey)s.yaw+=.12f;
    else if(key==KeyPress::upKey)s.pitch-=.12f;else if(key==KeyPress::downKey)s.pitch+=.12f;
    else if(key.getTextCharacter()=='+'||key.getTextCharacter()=='=')s.zoom*=1.1f;
    else if(key.getTextCharacter()=='-')s.zoom/=1.1f;
    else if(key.getTextCharacter()=='0'){s.yaw=s.pitch=0;s.zoom=1;}
    else return false;
    moveCamera(s.yaw,s.pitch,s.zoom);return true;
}
void VisualSurface::update(const AnalysisFrame& frame,int mode,std::array<std::uint32_t,3> colours,int theme,float gain,bool frozen){
    const double now=Time::getMillisecondCounterHiRes()*.001;const float dt=float(jlimit(0.0,.10,now-lastTick));lastTick=now;
    std::lock_guard<std::mutex> lock(mutex);
    if(frame.resetGeneration!=lastGeneration){
        const auto previous=scene;
        scene=Scene{};scene.yaw=previous.yaw;scene.pitch=previous.pitch;scene.zoom=previous.zoom;
        scene.speed=previous.speed;scene.sensitivity=previous.sensitivity;scene.bloom=previous.bloom;
        scene.exposure=previous.exposure;scene.saturation=previous.saturation;
        reactivity.reset();lastGeneration=frame.resetGeneration;sceneDirty=true;
    }
    sceneDirty=sceneDirty || scene.visual!=mode || scene.colours!=colours || scene.theme!=theme
        || scene.gain!=gain || scene.frozen!=frozen;
    scene.visual=mode;scene.colours=colours;scene.theme=theme;scene.gain=gain;scene.frozen=frozen;scene.generation=frame.resetGeneration;
    if(!frozen){
        const float smooth=1-std::exp(-dt*12);
        const auto response=reactivity.update(frame,dt);
        // Hold the unscaled artistic response, so sensitivity can be edited on
        // a frozen image without changing its captured PCM or phase.
        scene.low=response.low;scene.mid=response.mid;scene.high=response.high;
        scene.onset=response.onset;scene.activity=response.activity;
        scene.phase+=dt*scene.speed*(jlimit(0.f,1.f,scene.low*scene.sensitivity)*.48f
            +jlimit(0.f,1.f,scene.mid*scene.sensitivity)*.38f+jlimit(0.f,1.f,scene.high*scene.sensitivity)*.20f);
        scene.correlation=frame.correlation;
        for(int i=0;i<128;i++){
            const float peak=spectralPeak(frame.spectrum,frame.sampleRate,(float(i)+.5f)/128,1.f/128);
            float value=jlimit(0.f,1.f,(peak+85)/85);scene.bands[size_t(i)]+=(value-scene.bands[size_t(i)])*smooth;
            scene.wave[size_t(i)]=frame.left.empty()?0:frame.left[size_t(i)*frame.left.size()/128];
            scene.right[size_t(i)]=frame.right.empty()?0:frame.right[size_t(i)*frame.right.size()/128];
        }
    }
    if(fallbackDirty.exchange(false)){repaint();sceneDirty=true;}
    if(!frozen || sceneDirty)context.triggerRepaint();
    sceneDirty=false;
}
void VisualSurface::newOpenGLContextCreated(){
    // The Windows driver may fall back to a legacy context even when 3.2 was
    // requested. Check before JUCE calls shader functions that may be null.
    if(!context.areShadersAvailable() || OpenGLShaderProgram::getLanguageVersion()<1.5
        || glCreateShader==nullptr || glCreateProgram==nullptr
        || glGenVertexArrays==nullptr || glBindVertexArray==nullptr
        || glGenFramebuffers==nullptr || glBindFramebuffer==nullptr){
        error="Aurora visuals require OpenGL 3.2 / GLSL 1.50 and a compatible graphics driver";
        Logger::writeToLog(error);ready.store(false);failed.store(true);fallbackDirty.store(true);return;
    }
    shader=std::make_unique<OpenGLShaderProgram>(context);
    const String fragment=String(pearlCloudFunctions)+pearlFlowMaterial+sceneMaterialFunctions+fragmentSource;
    if(!(shader->addVertexShader(OpenGLHelpers::translateVertexShaderToV3(vertexSource))&&shader->addFragmentShader(OpenGLHelpers::translateFragmentShaderToV3(fragment))&&shader->link())){error=shader->getLastError();Logger::writeToLog(error);shader.reset();failed.store(true);fallbackDirty.store(true);return;}
    const auto id=shader->getProgramID();
    locations.aspect=glGetUniformLocation(id,"aspect");
    locations.bands=glGetUniformLocation(id,"bands");
    locations.cameraOrbit=glGetUniformLocation(id,"cameraOrbit");
    locations.correlation=glGetUniformLocation(id,"correlation");
    locations.energy=glGetUniformLocation(id,"energy");
    locations.flowDye=glGetUniformLocation(id,"flowDye");
    locations.flowReady=glGetUniformLocation(id,"flowReady");
    locations.flowTexel=glGetUniformLocation(id,"flowTexel");
    locations.flowVelocity=glGetUniformLocation(id,"flowVelocity");
    locations.framing=glGetUniformLocation(id,"framing");
    locations.gain=glGetUniformLocation(id,"gain");
    locations.mode=glGetUniformLocation(id,"mode");
    locations.onset=glGetUniformLocation(id,"onset");
    locations.pass=glGetUniformLocation(id,"pass");
    locations.pearlViewFit=glGetUniformLocation(id,"pearlViewFit");
    locations.phase=glGetUniformLocation(id,"phase");
    locations.resolution=glGetUniformLocation(id,"resolution");
    locations.rightWave=glGetUniformLocation(id,"rightWave");
    locations.theme=glGetUniformLocation(id,"theme");
    locations.wave=glGetUniformLocation(id,"wave");
    locations.zBound=glGetUniformLocation(id,"zBound");
    locations.position=glGetAttribLocation(id,"position");
    const char* colourNames[]={"colourA","colourB","colourC"};
    for(int i=0;i<3;++i)locations.colours[size_t(i)]=glGetUniformLocation(id,colourNames[i]);
    std::vector<float> points;std::vector<unsigned int> indices;
    constexpr int nx=256,ny=128;
    for(int y=0;y<=ny;y++)for(int x=0;x<=nx;x++){points.push_back(float(x)/nx);points.push_back(float(y)/ny);}
    for(int y=0;y<ny;y++)for(int x=0;x<nx;x++){unsigned int a=static_cast<unsigned int>(y*(nx+1)+x),b=a+1,c=a+nx+1,d=c+1;indices.insert(indices.end(),{a,b,c,b,d,c});}
    indexCount=int(indices.size());glGenVertexArrays(1,&vertexArray);glBindVertexArray(vertexArray);
    glGenBuffers(1,&vertexBuffer);glBindBuffer(GL_ARRAY_BUFFER,vertexBuffer);glBufferData(GL_ARRAY_BUFFER,GLsizeiptr(points.size()*sizeof(float)),points.data(),GL_STATIC_DRAW);
    glGenBuffers(1,&indexBuffer);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,indexBuffer);glBufferData(GL_ELEMENT_ARRAY_BUFFER,GLsizeiptr(indices.size()*sizeof(unsigned int)),indices.data(),GL_STATIC_DRAW);
    flowInitialisationAttempted=false;
    if(!postProcess.initialise(context))Logger::writeToLog("Visual finishing: "+postProcess.getError());
    lastRenderTime=0;
    failed.store(false);ready.store(true);fallbackDirty.store(true);
}
void VisualSurface::renderOpenGL(){
    if(!shader){OpenGLHelpers::clear(Colour(0xffd6e4e8));return;}
    Scene s;{std::lock_guard<std::mutex> lock(mutex);s=scene;}
    s.low=jlimit(0.f,1.f,s.low*s.sensitivity);s.mid=jlimit(0.f,1.f,s.mid*s.sensitivity);
    s.high=jlimit(0.f,1.f,s.high*s.sensitivity);s.onset=jlimit(0.f,1.f,s.onset*s.sensitivity);
    s.activity=jlimit(0.f,1.f,s.activity*s.sensitivity);
    // The surfaces use (1-correlation) for stereo width. Fade that creative
    // deviation toward its neutral value too; never alter measured correlation.
    // Preserve the original float exactly at the default sensitivity or above.
    if(s.sensitivity<1.f)s.correlation=jlimit(-1.f,1.f,1.f+(s.correlation-1.f)*jlimit(0.f,1.f,s.sensitivity));
    for(int i=0;i<128;++i){s.bands[size_t(i)]*=s.sensitivity;s.wave[size_t(i)]*=s.sensitivity;s.right[size_t(i)]*=s.sensitivity;}
    const float scale=float(context.getRenderingScale());const int outputW=std::max(1,int(getWidth()*scale)),outputH=std::max(1,int(getHeight()*scale));
    GLint targetFramebuffer=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&targetFramebuffer);
    const double now=Time::getMillisecondCounterHiRes()*.001;
    const float dt=lastRenderTime>0?float(jlimit(0.0,.067,now-lastRenderTime)):1.f/30;lastRenderTime=now;
    // Only Pearl Cloud consumes the fluid textures. Leave its persistent field
    // untouched while another scene is selected, with no hidden catch-up work.
    if(s.visual==2){
    if(!flowInitialisationAttempted){
        flowInitialisationAttempted=true;
        if(!pearlFlow.initialise(context))Logger::writeToLog("Pearl flow: "+pearlFlow.getError());
    }
    PearlFlow::Input input;input.phase=s.phase;input.low=s.low;input.mid=s.mid;input.high=s.high;input.gain=s.gain;input.correlation=s.correlation;input.onset=s.onset;
    // The pigment canvas uses the material's fixed aspect, independent of the
    // surrounding panel. Resizing cannot stretch or restart its fluid motion.
    input.width=848;input.height=512;input.frozen=s.frozen || s.speed==0.f;input.active=s.activity>.001f;input.resetGeneration=s.generation;
    // Split accelerated time into bounded solver updates instead of increasing
    // its integration step or silently capping accelerated speed at 30 Hz.
    const int advances=input.frozen || !input.active?1:std::max(1,int(std::ceil(s.speed)));
    input.dt=input.frozen?0.f:dt*s.speed/float(advances);
    for(int advance=0;advance<advances;++advance)pearlFlow.update(input);
    }
    glBindFramebuffer(GL_FRAMEBUFFER,static_cast<GLuint>(targetFramebuffer));
    const bool finishing=postProcess.begin(outputW,outputH);
    const int w=finishing?postProcess.getSceneWidth():outputW,h=finishing?postProcess.getSceneHeight():outputH;
    glViewport(0,0,w,h);glDisable(GL_SCISSOR_TEST);glDepthMask(GL_TRUE);glDepthFunc(GL_LESS);OpenGLHelpers::clear(Colour(0xffd6e4e8));
    shader->use();
    glUniform1i(locations.mode,s.visual);glUniform1i(locations.theme,s.theme);
    glUniform1i(locations.flowReady,pearlFlow.isReady()?1:0);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,pearlFlow.getDyeTexture());glUniform1i(locations.flowDye,0);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,pearlFlow.getVelocityTexture());glUniform1i(locations.flowVelocity,1);
    glUniform2f(locations.flowTexel,1.f/float(std::max(1,pearlFlow.getDyeWidth())),1.f/float(std::max(1,pearlFlow.getDyeHeight())));
    for(int i=0;i<3;i++) {
        auto colour=Colour(0xff000000|s.colours[size_t(i)]);
        glUniform3f(locations.colours[size_t(i)],colour.getFloatRed(),colour.getFloatGreen(),colour.getFloatBlue());
    }
    float maxBand=0,maxLeft=0,maxRight=0;
    for(int i=0;i<128;i++){maxBand=std::max(maxBand,std::abs(s.bands[size_t(i)]));maxLeft=std::max(maxLeft,std::abs(s.wave[size_t(i)]));maxRight=std::max(maxRight,std::abs(s.right[size_t(i)]));}
    const auto fit=computeVisualFraming(s.visual,float(w)/float(std::max(1,h)),s.phase,s.gain,s.low,s.mid,s.high,s.correlation,maxBand,maxLeft,maxRight,s.yaw,s.pitch);
    glUniform4f(locations.framing,fit.distance,fit.fit,fit.centerX,fit.centerY);glUniform1f(locations.zBound,fit.zBound);
    glUniform3f(locations.cameraOrbit,s.yaw,s.pitch,s.zoom);glUniform1f(locations.onset,s.onset);
    // Rotated support bounds keep the volume within the panel at reset zoom.
    float pearlX=0,pearlY=0;
    for(int k=0;k<8;k++){
        visual_framing_detail::Point p{(k&1)?1.06:-1.06,(k&2)?.64:-.64,(k&4)?.68:-.68};
        p=visual_framing_detail::rotateX(visual_framing_detail::rotateY(p,s.yaw),s.pitch);
        pearlX=std::max(pearlX,float(std::abs(p.x)));pearlY=std::max(pearlY,float(std::abs(p.y)));
    }
    glUniform1f(locations.pearlViewFit,.78f*std::min(float(w)/float(h)/pearlX,1.f/pearlY));
    glUniform1f(locations.phase,s.phase);glUniform1f(locations.gain,s.gain);glUniform1f(locations.aspect,float(w)/float(std::max(1,h)));
    glUniform1f(locations.correlation,s.correlation);glUniform3f(locations.energy,s.low,s.mid,s.high);glUniform2f(locations.resolution,float(w),float(h));
    glUniform1fv(locations.bands,128,s.bands.data());glUniform1fv(locations.wave,128,s.wave.data());
    glUniform1fv(locations.rightWave,128,s.right.data());
    glBindVertexArray(vertexArray);glBindBuffer(GL_ARRAY_BUFFER,vertexBuffer);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,indexBuffer);
    auto attr=static_cast<unsigned int>(locations.position);glEnableVertexAttribArray(attr);glVertexAttribPointer(attr,2,GL_FLOAT,GL_FALSE,2*sizeof(float),nullptr);
    glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_DEPTH_TEST);
    // The full-screen material needs one triangle, independent of mesh density.
    glUniform1i(locations.pass,0);glDrawArrays(GL_TRIANGLES,0,3);
    if(s.visual!=2){glEnable(GL_DEPTH_TEST);glClear(GL_DEPTH_BUFFER_BIT);glUniform1i(locations.pass,1);glDrawElements(GL_TRIANGLES,indexCount,GL_UNSIGNED_INT,nullptr);}
    glDisableVertexAttribArray(attr);glBindVertexArray(0);glDisable(GL_DEPTH_TEST);
    if(finishing)postProcess.end(static_cast<unsigned int>(targetFramebuffer),outputW,outputH,s.theme,s.bloom,s.exposure,s.saturation);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,0);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,0);
}
void VisualSurface::openGLContextClosing(){ready.store(false);fallbackDirty.store(true);pearlFlow.release();flowInitialisationAttempted=false;postProcess.release();shader.reset();if(vertexBuffer)glDeleteBuffers(1,&vertexBuffer);if(indexBuffer)glDeleteBuffers(1,&indexBuffer);if(vertexArray)glDeleteVertexArrays(1,&vertexArray);vertexBuffer=indexBuffer=vertexArray=0;}
