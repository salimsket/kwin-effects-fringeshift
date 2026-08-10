/*
    SPDX-FileCopyrightText: 2026 Fringe Shift contributors

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "shaderrunner.h"

#include <fstream>
#include <sstream>
#include <string_view>

#include <epoxy/egl.h>
#include <epoxy/gl.h>

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

namespace FringeShiftTest
{

namespace
{

// Passthrough covering the viewport with a single oversized triangle, so the
// runner needs no vertex buffers at all. texcoord0 is the name fringeshift.frag
// expects from KWin's vertex stage.
constexpr const char *vertexSource = R"(#version 140
out vec2 texcoord0;
void main()
{
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    texcoord0 = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

std::string readFile(const std::filesystem::path &path)
{
    std::ifstream in{path};
    if (!in) {
        throw GlError("cannot open " + path.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

/**
 * Resolves `#include "name"` one level deep against @a includeDir.
 *
 * GLSL has no #include; in production KWin's shader preprocessor resolves these
 * against its own compiled-in resources. That is the one piece of KWin the
 * harness has to stand in for, and it is why tests/stubs exists.
 */
std::string readWithIncludes(const std::filesystem::path &path, const std::filesystem::path &includeDir)
{
    std::istringstream source{readFile(path)};
    std::ostringstream out;

    for (std::string line; std::getline(source, line);) {
        const std::string_view view{line};
        const auto directive = view.find("#include");
        if (directive == std::string_view::npos) {
            out << line << '\n';
            continue;
        }
        const auto open = view.find('"', directive);
        const auto close = open == std::string_view::npos ? std::string_view::npos : view.find('"', open + 1);
        if (open == std::string_view::npos || close == std::string_view::npos) {
            out << line << '\n';
            continue;
        }
        const std::string name{view.substr(open + 1, close - open - 1)};
        out << "// " << line << " (resolved by the harness)\n";
        out << readFile(includeDir / name) << '\n';
    }
    return out.str();
}

GLuint compile(GLenum stage, const std::string &source, const char *label)
{
    const GLuint shader = glCreateShader(stage);
    const char *text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::string log(std::size_t(length > 0 ? length : 1), '\0');
        glGetShaderInfoLog(shader, length, nullptr, log.data());
        glDeleteShader(shader);
        throw GlError(std::string{label} + " failed to compile:\n" + log);
    }
    return shader;
}

/**
 * The harness side of FringeShiftParams::upload()'s structural interface —
 * the same three setUniform overloads KWin::GLShader offers by name.
 */
class ProgramUniforms
{
public:
    explicit ProgramUniforms(GLuint program)
        : m_program(program)
    {
    }

    // A uniform the compiler stripped as unused reports location -1; that is not
    // an error here, it just means this parameter did not reach the output.
    void setUniform(const char *name, const QVector2D &value)
    {
        const GLint location = glGetUniformLocation(m_program, name);
        if (location >= 0) {
            glUniform2f(location, value.x(), value.y());
        }
    }

    void setUniform(const char *name, float value)
    {
        const GLint location = glGetUniformLocation(m_program, name);
        if (location >= 0) {
            glUniform1f(location, value);
        }
    }

    void setUniform(const char *name, int value)
    {
        const GLint location = glGetUniformLocation(m_program, name);
        if (location >= 0) {
            glUniform1i(location, value);
        }
    }

    void setUniform(const char *name, float x, float y, float z, float w)
    {
        const GLint location = glGetUniformLocation(m_program, name);
        if (location >= 0) {
            glUniform4f(location, x, y, z, w);
        }
    }

private:
    GLuint m_program;
};

EGLDisplay openDisplay()
{
    const char *clientExtensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    if (clientExtensions && std::string_view{clientExtensions}.find("EGL_MESA_platform_surfaceless") != std::string_view::npos) {
        const EGLDisplay display = eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
        if (display != EGL_NO_DISPLAY) {
            return display;
        }
    }
    return eglGetDisplay(EGL_DEFAULT_DISPLAY);
}

} // namespace

class ShaderRunner::Private
{
public:
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    GLuint program = 0;
    GLuint vertexArray = 0;
};

ShaderRunner::ShaderRunner(const std::filesystem::path &fragmentPath, const std::filesystem::path &includeDir)
    : d(std::make_unique<Private>())
{
    d->display = openDisplay();
    if (d->display == EGL_NO_DISPLAY) {
        throw GlError("no EGL display; a headless GL driver (e.g. Mesa llvmpipe) is required");
    }
    if (!eglInitialize(d->display, nullptr, nullptr)) {
        throw GlError("eglInitialize failed");
    }
    if (!eglBindAPI(EGL_OPENGL_API)) {
        throw GlError("this EGL has no desktop OpenGL");
    }

    const EGLint configAttributes[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_NONE};
    EGLConfig config = nullptr;
    EGLint configCount = 0;
    if (!eglChooseConfig(d->display, configAttributes, &config, 1, &configCount) || configCount == 0) {
        throw GlError("no EGL config with desktop OpenGL support");
    }

    // 3.3 core: the newest profile that is certain to accept the shader's
    // "#version 140", which is the spelling KWin itself compiles on a core
    // context. Requesting less risks a driver defaulting to a 2.x context.
    const EGLint contextAttributes[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
        EGL_NONE};
    d->context = eglCreateContext(d->display, config, EGL_NO_CONTEXT, contextAttributes);
    if (d->context == EGL_NO_CONTEXT) {
        throw GlError("could not create an OpenGL 3.3 core context");
    }
    // Surfaceless: everything is rendered into an FBO, so there is no drawable.
    if (!eglMakeCurrent(d->display, EGL_NO_SURFACE, EGL_NO_SURFACE, d->context)) {
        throw GlError("eglMakeCurrent failed (EGL_KHR_surfaceless_context missing?)");
    }

    const GLuint vertex = compile(GL_VERTEX_SHADER, vertexSource, "harness vertex shader");
    const GLuint fragment = compile(GL_FRAGMENT_SHADER, readWithIncludes(fragmentPath, includeDir), "fringeshift.frag");

    d->program = glCreateProgram();
    glAttachShader(d->program, vertex);
    glAttachShader(d->program, fragment);
    glBindFragDataLocation(d->program, 0, "fragColor");
    glLinkProgram(d->program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint linked = GL_FALSE;
    glGetProgramiv(d->program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint length = 0;
        glGetProgramiv(d->program, GL_INFO_LOG_LENGTH, &length);
        std::string log(std::size_t(length > 0 ? length : 1), '\0');
        glGetProgramInfoLog(d->program, length, nullptr, log.data());
        throw GlError("link failed:\n" + log);
    }

    // Core profile draws nothing without a bound vertex array, even when the
    // vertex shader reads no attributes.
    glGenVertexArrays(1, &d->vertexArray);
}

ShaderRunner::~ShaderRunner()
{
    if (d->display != EGL_NO_DISPLAY) {
        if (d->context != EGL_NO_CONTEXT) {
            glDeleteVertexArrays(1, &d->vertexArray);
            glDeleteProgram(d->program);
            eglMakeCurrent(d->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroyContext(d->display, d->context);
        }
        eglTerminate(d->display);
    }
}

Image ShaderRunner::run(const Image &input, const KWin::FringeShiftParams &params)
{
    // RGBA32F throughout. The effect ships against 8-bit content, but the claims
    // under test are about the filter's transfer function, and 8-bit
    // quantisation (~0.004 peak-to-peak) is the same order as the MTF errors
    // being measured, which would swamp them.
    GLuint sourceTexture = 0;
    glGenTextures(1, &sourceTexture);
    glBindTexture(GL_TEXTURE_2D, sourceTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // Matches the redirected window texture; fetchTexel() clamps to match.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, input.width, input.height, 0, GL_RGBA, GL_FLOAT, input.pixels.data());

    GLuint targetTexture = 0;
    glGenTextures(1, &targetTexture);
    glBindTexture(GL_TEXTURE_2D, targetTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, input.width, input.height, 0, GL_RGBA, GL_FLOAT, nullptr);

    GLuint framebuffer = 0;
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, targetTexture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw GlError("incomplete framebuffer (no RGBA32F render target?)");
    }

    glViewport(0, 0, input.width, input.height);
    glUseProgram(d->program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sourceTexture);

    ProgramUniforms uniforms{d->program};
    // What KWin's own machinery supplies: the sampler unit, the built-in
    // texture dimensions (GLShader::TextureWidth/TextureHeight), and an
    // identity modulation.
    uniforms.setUniform("sampler", 0);
    uniforms.setUniform("textureWidth", input.width);
    uniforms.setUniform("textureHeight", input.height);
    uniforms.setUniform("modulation", 1.0f, 1.0f, 1.0f, 1.0f);
    params.upload(uniforms);

    glBindVertexArray(d->vertexArray);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    Image output{input.width, input.height};
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, input.width, input.height, GL_RGBA, GL_FLOAT, output.pixels.data());

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteTextures(1, &targetTexture);
    glDeleteTextures(1, &sourceTexture);

    if (const GLenum error = glGetError(); error != GL_NO_ERROR) {
        throw GlError("GL error 0x" + std::to_string(error) + " during render");
    }
    return output;
}

} // namespace FringeShiftTest
