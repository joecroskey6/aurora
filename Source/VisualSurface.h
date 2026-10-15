#pragma once
#include <juce_opengl/juce_opengl.h>
#include "Analysis.h"
#include "PearlFlow.h"
#include "VisualPostProcess.h"
#include "VisualReactivity.h"
#include "Customization.h"
#include <mutex>

// A GPU surface fed only by copied analysis snapshots. No audio callback work.
class VisualSurface final : public juce::Component, public juce::SettableTooltipClient, private juce::OpenGLRenderer {
public:
    VisualSurface();
    ~VisualSurface() override;
    void update(const AnalysisFrame&,int visual,std::array<std::uint32_t,3> colours,int theme,float gain,bool frozen);
    void paint(juce::Graphics&) override;
    void setCamera(float yaw,float pitch,float zoom);
    // Called on the message thread; only the copied Scene values cross into GL.
    void setCustomization(const Customization&);
    std::function<void(float,float,float)> onCameraChanged;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&,const juce::MouseWheelDetails&) override;
    void mouseMagnify(const juce::MouseEvent&,float scale) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
private:
    friend class VisualSurfacePerformanceProbe;
    struct Scene {std::array<float,128> bands{},wave{},right{};float low=0,mid=0,high=0,correlation=0,phase=0,gain=1,onset=0,activity=0,yaw=0,pitch=0,zoom=1;
        float speed=1,sensitivity=1,bloom=1,exposure=1,saturation=1;
        std::array<std::uint32_t,3> colours{};int visual=2,theme=0;bool frozen=false;std::uint64_t generation=0;};
    std::mutex mutex;
    Scene scene;
    VisualReactivity reactivity;
    float dragYaw=0,dragPitch=0;
    void moveCamera(float yaw,float pitch,float zoom);
    double lastTick=0;
    std::uint64_t lastGeneration=0;
    juce::OpenGLContext context;
    std::unique_ptr<juce::OpenGLShaderProgram> shader;
    PearlFlow pearlFlow;
    struct Locations {
        int aspect=-1, bands=-1, cameraOrbit=-1, correlation=-1, energy=-1, flowDye=-1, flowReady=-1, flowTexel=-1, flowVelocity=-1, framing=-1, gain=-1, mode=-1, onset=-1, pass=-1, pearlViewFit=-1, phase=-1, resolution=-1, rightWave=-1, theme=-1, wave=-1, zBound=-1, position=-1;
        std::array<int,3> colours{{-1,-1,-1}};
    } locations;
    bool flowInitialisationAttempted=false;
    bool sceneDirty=true; // Protected by mutex; avoids redrawing identical frozen frames.

    VisualPostProcess postProcess;
    double lastRenderTime=0;
    juce::String error;
    unsigned int vertexBuffer=0,indexBuffer=0,vertexArray=0;
    int indexCount=0;
    std::atomic<bool> ready{false};
    std::atomic<bool> failed{false}, fallbackDirty{true};
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;
};
