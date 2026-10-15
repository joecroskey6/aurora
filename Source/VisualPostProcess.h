#pragma once

#include <juce_opengl/juce_opengl.h>
#include <algorithm>
#include <cmath>
#include <memory>

// Call every method on the owning context's OpenGL thread. Call release() before
// that context closes. begin() selects an offscreen scene target; end() selects
// the supplied host target. The caller restores any other host rendering state.
// This is original rendering code: no projectM/Butterchurn implementation is used.
class VisualPostProcess final
{
public:
    VisualPostProcess() = default;
    VisualPostProcess(const VisualPostProcess&) = delete;
    VisualPostProcess& operator=(const VisualPostProcess&) = delete;

    bool initialise(juce::OpenGLContext& context)
    {
        using namespace juce::gl;
        release();
        error.clear();

        program = std::make_unique<juce::OpenGLShaderProgram>(context);
        if (!program->addVertexShader(juce::OpenGLHelpers::translateVertexShaderToV3(vertexSource))
            || !program->addFragmentShader(juce::OpenGLHelpers::translateFragmentShaderToV3(fragmentSource))
            || !program->link())
        {
            error = "Visual bloom shader: " + program->getLastError();
            program.reset();
            return false;
        }

        const auto id = program->getProgramID();
        position = glGetAttribLocation(id, "position");
        operation = glGetUniformLocation(id, "operation");
        source = glGetUniformLocation(id, "sourceImage");
        bloom = glGetUniformLocation(id, "bloomImage");
        texel = glGetUniformLocation(id, "texel");
        direction = glGetUniformLocation(id, "direction");
        threshold = glGetUniformLocation(id, "threshold");
        amount = glGetUniformLocation(id, "amount");
        exposureLocation = glGetUniformLocation(id, "exposure");
        saturationLocation = glGetUniformLocation(id, "saturation");
        if (position < 0)
        {
            error = "Visual bloom shader has no position attribute";
            program.reset();
            return false;
        }

        GLint previousArray = 0, previousBuffer = 0;
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousArray);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousBuffer);
        glGenVertexArrays(1, &vertexArray);
        glGenBuffers(1, &vertexBuffer);
        if (vertexArray != 0 && vertexBuffer != 0)
        {
            constexpr float quad[] = {-1.0f, -1.0f, 1.0f, -1.0f,
                                     -1.0f,  1.0f, 1.0f,  1.0f};
            glBindVertexArray(vertexArray);
            glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
            glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
            glEnableVertexAttribArray(static_cast<GLuint>(position));
            glVertexAttribPointer(static_cast<GLuint>(position), 2, GL_FLOAT,
                                  GL_FALSE, 2 * sizeof(float), nullptr);
            ready = true;
        }
        glBindVertexArray(static_cast<GLuint>(previousArray));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousBuffer));

        if (!ready)
        {
            error = "Visual bloom could not allocate its quad";
            release();
        }
        return ready;
    }

    // A false return leaves the entry framebuffer and viewport untouched, so the
    // caller can draw the ordinary scene directly. Buffers allocate only on resize.
    bool begin(int width, int height)
    {
        using namespace juce::gl;
        if (!ready || width < 1 || height < 1)
            return false;

        GLint previousRead = 0, previousDraw = 0, previousTexture = 0;
        GLint previousRenderbuffer = 0, viewport[4]{};
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousRead);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDraw);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
        glGetIntegerv(GL_RENDERBUFFER_BINDING, &previousRenderbuffer);
        glGetIntegerv(GL_VIEWPORT, viewport);

        GLint maximumTexture = 0, maximumRenderbuffer = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumTexture);
        glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maximumRenderbuffer);
        const int cap = std::min({1440, maximumTexture, maximumRenderbuffer});
        const double scale = std::min(1.0, double(std::max(1, cap)) / double(std::max(width, height)));
        const int nextWidth = std::max(1, int(std::lround(double(width) * scale)));
        const int nextHeight = std::max(1, int(std::lround(double(height) * scale)));
        bool allocated = cap > 0;
        if (allocated && (nextWidth != sceneWidth || nextHeight != sceneHeight))
            allocated = allocateTargets(nextWidth, nextHeight);

        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
        glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(previousRenderbuffer));
        if (!allocated)
        {
            if (error.isEmpty())
                error = "Floating point scene targets are unavailable";
            releaseTargets();
            ready = false;
            glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousRead));
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDraw));
            glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
            return false;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, sceneFramebuffer);
        glViewport(0, 0, sceneWidth, sceneHeight);
        // Existing scene shaders output their intended display colour values.
        // Decode explicitly during extraction/composition; never apply an implicit
        // framebuffer transfer function to these RGBA16F attachments.
        glDisable(GL_FRAMEBUFFER_SRGB);
        inScene = true;
        return true;
    }

    void end(unsigned int targetFramebuffer, int outputWidth, int outputHeight, int theme,
             float bloomMultiplier=1.f, float exposure=1.f, float saturation=1.f)
    {
        using namespace juce::gl;
        if (!ready || !inScene)
            return;
        inScene = false;

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_FRAMEBUFFER_SRGB);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        program->use();
        glBindVertexArray(vertexArray);
        glUniform1i(source, 0);
        glUniform1i(bloom, 1);

        const float intensity = theme == 3 ? 0.17f : theme == 1 ? 0.28f : theme == 2 ? 0.10f : 0.12f;
        const float highlightThreshold = theme == 3 ? 0.46f : theme == 1 ? 0.28f : theme == 2 ? 0.80f : 0.78f;

        // Detect highlights before averaging so narrow glints survive reduction.
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFramebuffers[0]);
        glViewport(0, 0, bloomWidth, bloomHeight);
        bindTexture(0, sceneTexture);
        glUniform1i(operation, 0);
        glUniform2f(texel, 1.0f / float(sceneWidth), 1.0f / float(sceneHeight));
        glUniform1f(threshold, highlightThreshold);
        drawQuad();

        // Nine taps per direction at one quarter of the scene dimensions.
        // All blur colours are linear-light half-float values.
        glUniform1i(operation, 1);
        glUniform2f(texel, 1.0f / float(bloomWidth), 1.0f / float(bloomHeight));
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFramebuffers[1]);
        bindTexture(0, bloomTextures[0]);
        glUniform2f(direction, 1.0f, 0.0f);
        drawQuad();
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFramebuffers[0]);
        bindTexture(0, bloomTextures[1]);
        glUniform2f(direction, 0.0f, 1.0f);
        drawQuad();

        glBindFramebuffer(GL_FRAMEBUFFER, targetFramebuffer);
        glViewport(0, 0, std::max(1, outputWidth), std::max(1, outputHeight));
        bindTexture(0, sceneTexture);
        bindTexture(1, bloomTextures[0]);
        glUniform1i(operation, 2);
        glUniform1f(amount, intensity*(std::isfinite(bloomMultiplier)?std::clamp(bloomMultiplier,0.f,3.f):1.f));
        glUniform1f(exposureLocation,std::isfinite(exposure)?std::clamp(exposure,.25f,2.f):1.f);
        glUniform1f(saturationLocation,std::isfinite(saturation)?std::clamp(saturation,0.f,2.f):1.f);
        drawQuad();

        bindTexture(1, 0);
        bindTexture(0, 0);
        glBindVertexArray(0);
        glUseProgram(0);
    }

    void release()
    {
        using namespace juce::gl;
        releaseTargets();
        if (vertexBuffer != 0)
            glDeleteBuffers(1, &vertexBuffer);
        if (vertexArray != 0)
            glDeleteVertexArrays(1, &vertexArray);
        vertexBuffer = vertexArray = 0;
        program.reset();
        ready = inScene = false;
    }

    bool isReady() const noexcept { return ready; }
    const juce::String& getError() const noexcept { return error; }
    int getSceneWidth() const noexcept { return sceneWidth; }
    int getSceneHeight() const noexcept { return sceneHeight; }

private:
    std::unique_ptr<juce::OpenGLShaderProgram> program;
    juce::String error;
    unsigned int vertexArray = 0, vertexBuffer = 0;
    unsigned int sceneFramebuffer = 0, sceneTexture = 0, depthBuffer = 0;
    unsigned int bloomFramebuffers[2]{}, bloomTextures[2]{};
    int sceneWidth = 0, sceneHeight = 0, bloomWidth = 0, bloomHeight = 0;
    int position = -1, operation = -1, source = -1, bloom = -1;
    int texel = -1, direction = -1, threshold = -1, amount = -1;
    int exposureLocation = -1, saturationLocation = -1;
    bool ready = false, inScene = false;

    static void bindTexture(unsigned int unit, unsigned int texture)
    {
        using namespace juce::gl;
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, texture);
    }

    static void drawQuad()
    {
        juce::gl::glDrawArrays(juce::gl::GL_TRIANGLE_STRIP, 0, 4);
    }

    bool createColourTarget(unsigned int& framebuffer, unsigned int& texture, int width, int height)
    {
        using namespace juce::gl;
        glGenTextures(1, &texture);
        glGenFramebuffers(1, &framebuffer);
        if (texture == 0 || framebuffer == 0)
            return false;
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }

    bool allocateTargets(int width, int height)
    {
        using namespace juce::gl;
        releaseTargets();
        sceneWidth = width;
        sceneHeight = height;
        bloomWidth = std::max(1, (width + 3) / 4);
        bloomHeight = std::max(1, (height + 3) / 4);
        if (!createColourTarget(sceneFramebuffer, sceneTexture, width, height))
        {
            error = "RGBA16F scene framebuffer is unsupported";
            return false;
        }
        glGenRenderbuffers(1, &depthBuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthBuffer);
        if (depthBuffer == 0 || glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            error = "24-bit scene depth framebuffer is unsupported";
            return false;
        }
        for (int i = 0; i < 2; ++i)
        {
            if (!createColourTarget(bloomFramebuffers[i], bloomTextures[i], bloomWidth, bloomHeight))
            {
                error = "RGBA16F bloom framebuffer is unsupported";
                return false;
            }
        }
        return true;
    }

    void releaseTargets()
    {
        using namespace juce::gl;
        if (sceneFramebuffer != 0)
            glDeleteFramebuffers(1, &sceneFramebuffer);
        if (sceneTexture != 0)
            glDeleteTextures(1, &sceneTexture);
        if (depthBuffer != 0)
            glDeleteRenderbuffers(1, &depthBuffer);
        glDeleteFramebuffers(2, bloomFramebuffers);
        glDeleteTextures(2, bloomTextures);
        sceneFramebuffer = sceneTexture = depthBuffer = 0;
        bloomFramebuffers[0] = bloomFramebuffers[1] = 0;
        bloomTextures[0] = bloomTextures[1] = 0;
        sceneWidth = sceneHeight = bloomWidth = bloomHeight = 0;
        inScene = false;
    }

    inline static constexpr const char* vertexSource = R"GLSL(
attribute vec2 position;
varying vec2 uv;
void main()
{
    uv = position * 0.5 + 0.5;
    gl_Position = vec4(position, 0.0, 1.0);
}
)GLSL";

    inline static constexpr const char* fragmentSource = R"GLSL(
varying vec2 uv;
uniform sampler2D sourceImage;
uniform sampler2D bloomImage;
uniform int operation;
uniform vec2 texel;
uniform vec2 direction;
uniform float threshold;
uniform float amount;
uniform float exposure;
uniform float saturation;

vec3 toLinear(vec3 value)
{
    value = max(value, vec3(0.0));
    vec3 low = value / 12.92;
    vec3 high = pow((value + 0.055) / 1.055, vec3(2.4));
    return mix(low, high, step(vec3(0.04045), value));
}

vec3 toDisplay(vec3 value)
{
    value = max(value, vec3(0.0));
    vec3 low = value * 12.92;
    vec3 high = 1.055 * pow(value, vec3(1.0 / 2.4)) - 0.055;
    return mix(low, high, step(vec3(0.0031308), value));
}

vec3 highlight(vec3 colour)
{
    vec3 linearColour = toLinear(colour);
    float luminance = dot(linearColour, vec3(0.2126, 0.7152, 0.0722));
    float knee = 0.12;
    float soft = clamp(luminance - threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee);
    float excess = max(luminance - threshold, soft);
    return linearColour * (excess / max(luminance, 0.00001));
}

void main()
{
    if (operation == 0)
    {
        vec3 result = vec3(0.0);
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                result += highlight(texture2D(sourceImage, uv + (vec2(float(x), float(y)) - 1.5) * texel).rgb);
        gl_FragColor = vec4(result / 16.0, 1.0);
    }
    else if (operation == 1)
    {
        vec3 result = vec3(0.0);
        float weightSum = 0.0;
        for (int i = -4; i <= 4; ++i)
        {
            float distance = float(i);
            float weight = exp(-distance * distance / 8.0);
            result += texture2D(sourceImage, uv + direction * texel * distance).rgb * weight;
            weightSum += weight;
        }
        gl_FragColor = vec4(result / weightSum, 1.0);
    }
    else
    {
        vec3 base = toLinear(texture2D(sourceImage, uv).rgb);
        vec3 glow = max(texture2D(bloomImage, uv).rgb, vec3(0.0));
        // Retain the scene's colour grading. Reduce added light near white rather
        // than applying a global tone mapper that would flatten pastel themes.
        vec3 colour = base + amount * glow * (1.0 - 0.65 * clamp(base, 0.0, 1.0));
        colour *= exposure;
        float luminance=dot(colour,vec3(0.2126,0.7152,0.0722));
        colour=mix(vec3(luminance),colour,saturation);
        gl_FragColor = vec4(clamp(toDisplay(colour), 0.0, 1.0), 1.0);
    }
}
)GLSL";
};
