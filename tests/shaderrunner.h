/*
    SPDX-FileCopyrightText: 2026 Fringe Shift contributors

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "fringeshiftparams.h"

namespace FringeShiftTest
{

class GlError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

/**
 * An RGBA image in linear float, matching the RGBA32F textures the runner uses.
 *
 * Row 0 is the first row of the framebuffer. The harness never converts that to
 * a screen-space "up" or "down": KWin supplies the axis flip through
 * GLTexture::matrix(), which nothing here reproduces. Every assertion in the
 * suite is therefore stated in magnitudes or in RELATIVE channel direction,
 * both of which survive an unknown global flip.
 */
struct Image
{
    int width = 0;
    int height = 0;
    /** RGBA, row-major, 4 floats per pixel. */
    std::vector<float> pixels;

    Image() = default;
    Image(int w, int h)
        : width(w)
        , height(h)
        , pixels(std::size_t(w) * std::size_t(h) * 4, 0.0f)
    {
    }

    float &at(int x, int y, int channel)
    {
        return pixels[(std::size_t(y) * std::size_t(width) + std::size_t(x)) * 4 + std::size_t(channel)];
    }

    float at(int x, int y, int channel) const
    {
        return pixels[(std::size_t(y) * std::size_t(width) + std::size_t(x)) * 4 + std::size_t(channel)];
    }
};

/**
 * Runs the shipped fringeshift.frag over an image, off-screen and off-KWin.
 *
 * This is the second adapter at the shader's seam — the compositor being the
 * first. It reproduces what KWin guarantees the shader (a premultiplied RGBA
 * source texture with CLAMP_TO_EDGE, the built-in textureWidth/textureHeight/
 * sampler/modulation uniforms) and nothing else; the KWin-only tail includes are
 * resolved against the identity stubs in tests/stubs.
 *
 * Parameters are uploaded through FringeShiftParams::upload(), so the uniform
 * names exercised here are literally the ones the compositor uses.
 *
 * Throws GlError if a context cannot be created or the shader fails to compile.
 */
class ShaderRunner
{
public:
    ShaderRunner(const std::filesystem::path &fragmentPath, const std::filesystem::path &includeDir);
    ~ShaderRunner();

    ShaderRunner(const ShaderRunner &) = delete;
    ShaderRunner &operator=(const ShaderRunner &) = delete;

    Image run(const Image &input, const KWin::FringeShiftParams &params);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace FringeShiftTest
