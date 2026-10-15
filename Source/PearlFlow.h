#pragma once

#include <juce_opengl/juce_opengl.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>

// Persistent, palette-independent fluid for Pearl Cloud. All methods, including
// release(), must run with the owning context current, on its rendering thread.
// No allocation or simulation work belongs in the audio callback. Explicitly
// release before the context closes; the destructor deliberately makes no GL calls.
//
// The finite-difference solver is informed by Pavel Dobryakov's MIT-licensed
// WebGL-Fluid-Simulation. Aurora's forcing, ribbon sources, lifecycle and material
// representation are original. The upstream notice is retained at the file end.
class PearlFlow final {
public:
    struct Input {
        float dt = 1.0f / 30.0f, phase = 0, low = 0, mid = 0, high = 0;
        float gain = 1, correlation = 0, onset = 0;
        int width = 750, height = 400;
        bool frozen = false, active = true;
        std::uint64_t resetGeneration = 0;
        std::uint32_t seed = 1;
    };

    bool initialise(juce::OpenGLContext& context) {
        using namespace juce::gl;
        release();
        State saved;
        owner = &context;
        error.clear();
        glGenVertexArrays(1, &vao);
        const char* bodies[] = { seedBody, advectBody, curlBody, forceBody,
                                divergenceBody, pressureBody, projectionBody, dyeBody, dyeTransportBody };
        for (std::size_t i = 0; i < programs.size(); ++i) {
            auto& p = programs[i];
            p.shader = std::make_unique<juce::OpenGLShaderProgram>(context);
            const auto fragment = juce::String(fragmentPrefix) + ribbonFunctions + bodies[i];
            if (!p.shader->addVertexShader(vertexSource)
                || !p.shader->addFragmentShader(fragment) || !p.shader->link()) {
                error = p.shader->getLastError();
                release();
                return false;
            }
            const auto id = p.shader->getProgramID();
            p.source = glGetUniformLocation(id, "sourceTexture");
            p.other = glGetUniformLocation(id, "otherTexture");
            p.transport = glGetUniformLocation(id, "transportTexture");
            p.ribbon = glGetUniformLocation(id, "ribbonTexture");
            p.texel = glGetUniformLocation(id, "texel");
            p.dyeTexel = glGetUniformLocation(id, "dyeTexel");
            p.step = glGetUniformLocation(id, "stepSize");
            p.phase = glGetUniformLocation(id, "phase");
            p.energy = glGetUniformLocation(id, "energy");
            p.impulse = glGetUniformLocation(id, "impulse");
            p.aspect = glGetUniformLocation(id, "aspect");
            p.correlation = glGetUniformLocation(id, "correlation");
            p.seed = glGetUniformLocation(id, "seed");
        }
        initialised = true;
        return true;
    }

    void update(const Input& input) {
        if (!initialised) return;
        using namespace juce::gl;
        State saved;
        glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
        glDisable(GL_SCISSOR_TEST); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glBindVertexArray(vao);
        current = input;
        current.phase = finite(current.phase, 0);
        current.gain = std::clamp(finite(current.gain, 1), 0.0f, 4.0f);
        current.correlation = std::clamp(finite(current.correlation, 0), -1.0f, 1.0f);
        current.low = std::clamp(finite(current.low, 0) * current.gain, 0.0f, 1.5f);
        current.mid = std::clamp(finite(current.mid, 0) * current.gain, 0.0f, 1.5f);
        current.high = std::clamp(finite(current.high, 0) * current.gain, 0.0f, 1.5f);
        const float ratio = std::clamp(float(std::max(1, input.width))
                                      / float(std::max(1, input.height)), 0.25f, 8.0f);
        const auto sw = ratio >= 1 ? 256 : std::max(32, int(std::lround(256 * ratio)));
        const auto sh = ratio >= 1 ? std::max(32, int(std::lround(256 / ratio))) : 256;
        const auto dw = ratio >= 1 ? 768 : std::max(96, int(std::lround(768 * ratio)));
        const auto dh = ratio >= 1 ? std::max(96, int(std::lround(768 / ratio))) : 768;
        const bool resize = velocity[0].width != sw || velocity[0].height != sh
                         || dye[0].width != dw || dye[0].height != dh;
        if (resize) {
            destroyTargets();
            bool ok = true;
            for (auto& t : velocity) ok = makeTarget(t, sw, sh, GL_RG16F, GL_RG, GL_LINEAR) && ok;
            for (auto& t : dye) ok = makeTarget(t, dw, dh, GL_RGBA16F, GL_RGBA, GL_LINEAR) && ok;
            ok = makeTarget(transportDye, dw, dh, GL_RGBA16F, GL_RGBA, GL_LINEAR) && ok;
            ok = makeTarget(ribbonDye, dw, dh, GL_RGBA16F, GL_RGBA, GL_LINEAR) && ok;
            for (auto& t : pressure) ok = makeTarget(t, sw, sh, GL_R16F, GL_RED, GL_NEAREST) && ok;
            ok = makeTarget(curl, sw, sh, GL_R16F, GL_RED, GL_NEAREST) && ok;
            ok = makeTarget(divergence, sw, sh, GL_R16F, GL_RED, GL_NEAREST) && ok;
            if (!ok) {
                error = "Pearl Flow floating-point framebuffer is incomplete";
                destroyTargets();
                initialised = false;
                return;
            }
        }
        if (resize || !seeded || generation != input.resetGeneration || seedValue != input.seed) {
            generation = input.resetGeneration; seedValue = input.seed;
            velocityRead = dyeRead = pressureRead = 0;
            for (auto& t : velocity) clear(t);
            for (auto& t : pressure) clear(t);
            clear(curl); clear(divergence);
            draw(0, ribbonDye, 0, 0, 0);
            draw(0, dye[0], ribbonDye.texture, 0, -1);
            draw(0, dye[1], ribbonDye.texture, 0, -1);
            accumulator = 0;
            previousEnergy = {current.low, current.mid, current.high};
            seeded = true;
        }
        // A frozen frame or silence performs no shader pass, including decay.
        // Clear accumulated time so resuming never catches up hidden simulation.
        if (input.frozen || !input.active
            || std::max({current.low, current.mid, current.high}) < 0.0001f) {
            accumulator = 0;
            previousEnergy = {current.low, current.mid, current.high};
            return;
        }
        constexpr float fixedStep = 1.0f / 60.0f;
        accumulator = std::min(2.0f * fixedStep,
            accumulator + std::clamp(finite(input.dt, 0), 0.0f, 2.0f * fixedStep));
        const std::array<float, 3> next {current.low, current.mid, current.high};
        for (std::size_t i = 0; i < 3; ++i)
            impulses[i] = std::clamp((next[i] - previousEnergy[i]) * 5.0f, 0.0f, 1.0f);
        // A time-based transient envelope remains visible even between FFT snapshots.
        const float attack=std::clamp(finite(input.onset,0),0.f,1.f);
        impulses[0]=std::max(impulses[0],attack);
        impulses[1]=std::max(impulses[1],attack*.65f);
        impulses[2]=std::max(impulses[2],attack*.35f);
        const int steps = std::min(2, int((accumulator + 0.000001f) / fixedStep));
        if (steps == 0) return; // Retain previous energy until the next real step.
        for (int i = 0; i < steps; ++i) {
            step(fixedStep);
            accumulator = std::max(0.0f, accumulator - fixedStep);
        }
        previousEnergy = next;
    }

    unsigned int getDyeTexture() const noexcept { return dye[dyeRead].texture; }
    unsigned int getVelocityTexture() const noexcept { return velocity[velocityRead].texture; }
    int getDyeWidth() const noexcept { return dye[0].width; }
    int getDyeHeight() const noexcept { return dye[0].height; }
    int getSimulationWidth() const noexcept { return velocity[0].width; }
    int getSimulationHeight() const noexcept { return velocity[0].height; }
    bool isReady() const noexcept { return initialised && seeded; }
    juce::String getError() const { return error; }

    void release() {
        using namespace juce::gl;
        destroyTargets();
        for (auto& p : programs) p.shader.reset();
        if (vao) glDeleteVertexArrays(1, &vao);
        vao = 0; owner = nullptr; initialised = seeded = false; accumulator = 0;
    }

private:
    struct Target { unsigned int texture = 0, fbo = 0; int width = 0, height = 0; };
    struct Program {
        std::unique_ptr<juce::OpenGLShaderProgram> shader;
        int source = -1, other = -1, transport = -1, ribbon = -1,
            texel = -1, dyeTexel = -1, step = -1, phase = -1,
            energy = -1, impulse = -1, aspect = -1, correlation = -1, seed = -1;
    };
    // Restore the caller's GL target, vertex state, texture units and enables.
    struct State {
        int drawFbo = 0, readFbo = 0, viewport[4]{}, program = 0, vao = 0, active = 0;
        int texture[4]{}; unsigned char mask[4]{};
        bool blend = false, depth = false, cull = false, scissor = false;
        State() {
            using namespace juce::gl;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
            glGetIntegerv(GL_VIEWPORT, viewport); glGetIntegerv(GL_CURRENT_PROGRAM, &program);
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao); glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
            for (int i = 0; i < 4; ++i) {
                glActiveTexture(GL_TEXTURE0 + unsigned(i)); glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture[i]);
            }
            glActiveTexture(unsigned(active));
            glGetBooleanv(GL_COLOR_WRITEMASK, mask);
            blend = glIsEnabled(GL_BLEND); depth = glIsEnabled(GL_DEPTH_TEST);
            cull = glIsEnabled(GL_CULL_FACE); scissor = glIsEnabled(GL_SCISSOR_TEST);
        }
        ~State() {
            using namespace juce::gl;
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, unsigned(drawFbo));
            glBindFramebuffer(GL_READ_FRAMEBUFFER, unsigned(readFbo));
            glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
            glUseProgram(unsigned(program)); glBindVertexArray(unsigned(vao));
            for (int i = 0; i < 4; ++i) {
                glActiveTexture(GL_TEXTURE0 + unsigned(i)); glBindTexture(GL_TEXTURE_2D, unsigned(texture[i]));
            }
            glActiveTexture(unsigned(active)); glColorMask(mask[0], mask[1], mask[2], mask[3]);
            if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
            if (depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
            if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
            if (scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
        }
    };
    static float finite(float value, float fallback) { return std::isfinite(value) ? value : fallback; }
    static bool makeTarget(Target& t, int w, int h, int internalFormat, unsigned format, unsigned filter) {
        using namespace juce::gl;
        t.width = w; t.height = h;
        glActiveTexture(GL_TEXTURE0);
        glGenTextures(1, &t.texture); glBindTexture(GL_TEXTURE_2D, t.texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, int(filter));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, int(filter));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, w, h, 0, format, GL_HALF_FLOAT, nullptr);
        glGenFramebuffers(1, &t.fbo); glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.texture, 0);
        return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }
    static void destroy(Target& t) {
        using namespace juce::gl;
        if (t.fbo) glDeleteFramebuffers(1, &t.fbo);
        if (t.texture) glDeleteTextures(1, &t.texture);
        t = {};
    }
    void destroyTargets() {
        for (auto& t : velocity) destroy(t);
        for (auto& t : dye) destroy(t);
        for (auto& t : pressure) destroy(t);
        destroy(curl); destroy(divergence); destroy(transportDye); destroy(ribbonDye);
    }
    static void clear(const Target& t) {
        using namespace juce::gl;
        static constexpr float zero[4]{};
        glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
        glClearBufferfv(GL_COLOR, 0, zero);
    }
    void draw(int index, const Target& target, unsigned source, unsigned other, float dt) {
        using namespace juce::gl;
        auto& p = programs[std::size_t(index)]; p.shader->use();
        glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
        glViewport(0, 0, target.width, target.height);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, source); glUniform1i(p.source, 0);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, other); glUniform1i(p.other, 1);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, index == 7 ? transportDye.texture : 0); glUniform1i(p.transport, 2);
        glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, index == 7 ? ribbonDye.texture : 0); glUniform1i(p.ribbon, 3);
        glUniform2f(p.texel, 1.0f / float(velocity[0].width), 1.0f / float(velocity[0].height));
        glUniform2f(p.dyeTexel, 1.0f / float(dye[0].width), 1.0f / float(dye[0].height));
        glUniform1f(p.step, dt); glUniform1f(p.phase, current.phase);
        glUniform3f(p.energy, current.low, current.mid, current.high);
        glUniform3f(p.impulse, impulses[0], impulses[1], impulses[2]);
        glUniform1f(p.aspect, float(target.width) / float(target.height));
        glUniform1f(p.correlation, current.correlation);
        glUniform1f(p.seed, float(current.seed % 65521u) * 0.03125f);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    void step(float dt) {
        // Semi-Lagrangian velocity -> curl/confinement and audio force ->
        // pressure projection -> high-resolution pigment advection/injection.
        draw(1, velocity[1 - velocityRead], velocity[velocityRead].texture, 0, dt);
        velocityRead = 1 - velocityRead;
        draw(2, curl, velocity[velocityRead].texture, 0, dt);
        draw(3, velocity[1 - velocityRead], velocity[velocityRead].texture, curl.texture, dt);
        velocityRead = 1 - velocityRead;
        draw(4, divergence, velocity[velocityRead].texture, 0, dt);
        clear(pressure[pressureRead]);
        for (int i = 0; i < 12; ++i) {
            draw(5, pressure[1 - pressureRead], pressure[pressureRead].texture, divergence.texture, dt);
            pressureRead = 1 - pressureRead;
        }
        draw(6, velocity[1 - velocityRead], velocity[velocityRead].texture, pressure[pressureRead].texture, dt);
        velocityRead = 1 - velocityRead;
        // Forward transport followed by limited MacCormack error correction.
        // The correction is clamped to the four source texels surrounding the
        // backtrace, preserving thin pigment ribbons without new extrema.
        draw(8, transportDye, dye[dyeRead].texture, velocity[velocityRead].texture, dt);
        draw(7, dye[1 - dyeRead], dye[dyeRead].texture, velocity[velocityRead].texture, dt);
        dyeRead = 1 - dyeRead;
    }

    juce::OpenGLContext* owner = nullptr;
    std::array<Program, 9> programs;
    std::array<Target, 2> velocity{}, dye{}, pressure{};
    Target curl, divergence, transportDye, ribbonDye;
    unsigned int vao = 0;
    int velocityRead = 0, dyeRead = 0, pressureRead = 0;
    float accumulator = 0;
    std::array<float, 3> previousEnergy{}, impulses{};
    std::uint64_t generation = 0;
    std::uint32_t seedValue = 0;
    Input current;
    juce::String error;
    bool initialised = false, seeded = false;

    inline static constexpr const char* vertexSource = R"GLSL(#version 150
out vec2 uv;
void main() {
    vec2 p = gl_VertexID == 0 ? vec2(-1.0,-1.0)
           : gl_VertexID == 1 ? vec2(3.0,-1.0) : vec2(-1.0,3.0);
    uv = p * 0.5 + 0.5;
    gl_Position = vec4(p,0.0,1.0);
}
)GLSL";
    inline static constexpr const char* fragmentPrefix = R"GLSL(#version 150
in vec2 uv;
out vec4 result;
uniform sampler2D sourceTexture, otherTexture, transportTexture, ribbonTexture;
uniform vec2 texel, dyeTexel;
uniform float stepSize, phase, aspect, correlation, seed;
uniform vec3 energy, impulse;
vec2 safeUV(vec2 p) { return clamp(p,texel*0.5,vec2(1.0)-texel*0.5); }
float border(vec2 p) {
    vec2 d = min(p,vec2(1.0)-p);
    return smoothstep(0.0,0.055,min(d.x,d.y));
}
)GLSL";
    inline static constexpr const char* ribbonFunctions = R"GLSL(
// Independent pigment weights retain arbitrary user palettes, including black.
// Original open ribbon sources are drawn once, then transported by the fluid.
vec4 ribbons(vec2 at, float motion) {
    float fit = 0.90 * min(aspect/1.10,1.0/0.67);
    vec2 p = (at-0.5)*vec2(2.0*aspect,2.0)/fit;
    p.y *= 0.72;
    vec3 pigment=vec3(0.0);
    for (int j=0;j<6;j++) {
        float k=float(j), side=mod(k,2.0)*2.0-1.0;
        float centre=side*(0.11+0.105*sin(p.x*3.15+seed*0.03))
            +0.075*sin(p.x*5.1+k*0.61+motion*0.22)
            +0.045*(k-2.5)*cos(p.x*2.3+0.6);
        float spread=0.045+0.013*sin(p.x*4.7+k*0.92);
        float transverse=(p.y-centre)/spread;
        float body=exp(-transverse*transverse*0.48);
        float ends=1.0-smoothstep(0.61,1.03,abs(p.x+0.036*(k-2.5)));
        float fibre=pow(0.5+0.5*sin(transverse*19.0+p.x*7.0+k),8.0);
        float folds=0.48+0.52*pow(0.5+0.5*sin(transverse*3.4+p.x*4.8+k*0.7),2.0);
        float density=body*ends*(0.08+0.23*folds+0.27*fibre);
        // Each ribbon carries a dominant pigment with a slow longitudinal shift.
        // A peaked mixture avoids six overlapping ribbons averaging to grey.
        vec3 weights=vec3(0.008)+pow(0.5+0.5*cos(vec3(0.0,2.0944,4.1888)
            +k*2.0944+p.x*0.58),vec3(5.0));
        pigment+=density*weights/(weights.x+weights.y+weights.z);
    }
    float total=dot(pigment,vec3(1.0));
    pigment/=max(1.0,total);
    return vec4(pigment,min(total,1.0));
}
)GLSL";
    inline static constexpr const char* seedBody = R"GLSL(
void main() { result=stepSize<0.0 ? texture(sourceTexture,uv) : ribbons(uv,0.0); }
)GLSL";
    inline static constexpr const char* advectBody = R"GLSL(
void main() {
    vec2 velocity=texture(sourceTexture,uv).xy;
    vec2 back=safeUV(uv-stepSize*velocity*texel);
    result=vec4(texture(sourceTexture,back).xy*exp(-0.32*stepSize),0.0,1.0);
}
)GLSL";
    inline static constexpr const char* curlBody = R"GLSL(
void main() {
    float l=texture(sourceTexture,uv-vec2(texel.x,0.0)).y;
    float r=texture(sourceTexture,uv+vec2(texel.x,0.0)).y;
    float b=texture(sourceTexture,uv-vec2(0.0,texel.y)).x;
    float t=texture(sourceTexture,uv+vec2(0.0,texel.y)).x;
    result=vec4(0.5*(r-l-t+b),0.0,0.0,1.0);
}
)GLSL";
    inline static constexpr const char* forceBody = R"GLSL(
void main() {
    float l=texture(otherTexture,uv-vec2(texel.x,0.0)).r;
    float r=texture(otherTexture,uv+vec2(texel.x,0.0)).r;
    float b=texture(otherTexture,uv-vec2(0.0,texel.y)).r;
    float t=texture(otherTexture,uv+vec2(0.0,texel.y)).r;
    float c=texture(otherTexture,uv).r;
    vec2 n=vec2(abs(r)-abs(l),abs(t)-abs(b));
    n/=max(length(n),0.0001);
    vec2 force=vec2(n.y,-n.x)*c*(8.0+energy.y*9.0+energy.z*5.0);
    vec2 p=(uv-0.5)*vec2(aspect,1.0);
    float size=min(aspect,1.0);
    vec2 artistic=vec2(0.0);
    for (int j=0;j<4;j++) {
        float k=float(j), signValue=mod(k,2.0)*2.0-1.0;
        vec2 centre=size*vec2((k-1.5)*0.20,
            signValue*0.105+0.035*sin(phase*0.63+k*1.3+seed));
        vec2 d=(p-centre)/size;
        float radius=0.019+0.014*energy.x;
        float falloff=exp(-dot(d,d)/radius);
        float strength=(0.24+0.62*energy.x+0.32*energy.y+0.85*impulse.x);
        artistic+=vec2(-d.y,d.x)*falloff*signValue*strength;
    }
    float narrow=exp(-p.y*p.y/max(0.005,size*size*0.037));
    artistic.x+=narrow*0.037*(energy.y+impulse.y)*cos(p.x*7.0/size+phase*0.4);
    artistic.y+=0.022*(energy.z+impulse.z)*sin(p.x*19.0/size-phase*0.7)
        *exp(-dot(p,p)/max(0.01,size*size*0.23));
    artistic.y*=1.0-0.24*correlation;
    force+=artistic*vec2(1.0/aspect,1.0)/texel;
    vec2 velocity=texture(sourceTexture,uv).xy+stepSize*force;
    velocity*=border(uv);
    result=vec4(clamp(velocity,vec2(-140.0),vec2(140.0)),0.0,1.0);
}
)GLSL";
    inline static constexpr const char* divergenceBody = R"GLSL(
void main() {
    vec2 c=texture(sourceTexture,uv).xy;
    float l=uv.x<texel.x ? -c.x : texture(sourceTexture,uv-vec2(texel.x,0.0)).x;
    float r=uv.x>1.0-texel.x ? -c.x : texture(sourceTexture,uv+vec2(texel.x,0.0)).x;
    float b=uv.y<texel.y ? -c.y : texture(sourceTexture,uv-vec2(0.0,texel.y)).y;
    float t=uv.y>1.0-texel.y ? -c.y : texture(sourceTexture,uv+vec2(0.0,texel.y)).y;
    result=vec4(0.5*(r-l+t-b),0.0,0.0,1.0);
}
)GLSL";
    inline static constexpr const char* pressureBody = R"GLSL(
void main() {
    float l=texture(sourceTexture,uv-vec2(texel.x,0.0)).r;
    float r=texture(sourceTexture,uv+vec2(texel.x,0.0)).r;
    float b=texture(sourceTexture,uv-vec2(0.0,texel.y)).r;
    float t=texture(sourceTexture,uv+vec2(0.0,texel.y)).r;
    result=vec4((l+r+b+t-texture(otherTexture,uv).r)*0.25,0.0,0.0,1.0);
}
)GLSL";
    inline static constexpr const char* projectionBody = R"GLSL(
void main() {
    float l=texture(otherTexture,uv-vec2(texel.x,0.0)).r;
    float r=texture(otherTexture,uv+vec2(texel.x,0.0)).r;
    float b=texture(otherTexture,uv-vec2(0.0,texel.y)).r;
    float t=texture(otherTexture,uv+vec2(0.0,texel.y)).r;
    vec2 v=texture(sourceTexture,uv).xy-0.5*vec2(r-l,t-b);
    if(uv.x<texel.x || uv.x>1.0-texel.x) v.x=0.0;
    if(uv.y<texel.y || uv.y>1.0-texel.y) v.y=0.0;
    result=vec4(clamp(v,vec2(-140.0),vec2(140.0)),0.0,1.0);
}
)GLSL";
    inline static constexpr const char* dyeBody = R"GLSL(
void main() {
    vec2 v=texture(otherTexture,uv).xy;
    vec2 back=safeUV(uv-stepSize*v*texel);
    vec2 forward=safeUV(uv+stepSize*v*texel);
    vec3 advected=texture(transportTexture,uv).rgb;
    vec3 reversed=texture(transportTexture,forward).rgb;
    vec3 corrected=advected+0.5*(texture(sourceTexture,uv).rgb-reversed);
    vec2 cell=(floor(back/dyeTexel-0.5)+0.5)*dyeTexel;
    vec3 a=texture(sourceTexture,cell).rgb;
    vec3 b=texture(sourceTexture,cell+vec2(dyeTexel.x,0.0)).rgb;
    vec3 c=texture(sourceTexture,cell+vec2(0.0,dyeTexel.y)).rgb;
    vec3 d=texture(sourceTexture,cell+dyeTexel).rgb;
    vec3 lower=min(min(a,b),min(c,d)), upper=max(max(a,b),max(c,d));
    vec3 pigment=clamp(corrected,lower,upper)*exp(-stepSize*0.38);
    // Cache the intricate source instead of evaluating its six ribbon loops for
    // every dye pixel on every step. Slow source parallax follows supplied phase.
    vec2 sourceUV=uv+vec2(0.006*sin(phase*0.31+uv.y*5.0),
                               0.009*sin(phase*0.24+uv.x*4.0));
    vec4 fresh=texture(ribbonTexture,sourceUV);
    float source=0.65*energy.x+0.45*energy.y+0.35*impulse.y;
    pigment+=fresh.rgb*stepSize*source;
    pigment*=mix(0.97,1.0,border(uv));
    float total=dot(pigment,vec3(1.0));
    pigment/=max(1.0,total);
    result=vec4(pigment,min(total,1.0));
}
)GLSL";
    inline static constexpr const char* dyeTransportBody = R"GLSL(
void main() {
    vec2 v=texture(otherTexture,uv).xy;
    result=texture(sourceTexture,safeUV(uv-stepSize*v*texel));
}
)GLSL";
};

/*
MIT License

Copyright (c) 2017 Pavel Dobryakov

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
