/*
    SPDX-FileCopyrightText: 2026 Fringe Shift contributors

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <filesystem>
#include <string>

namespace FringeShiftTest
{

/** The two prologues KWin's GLShader::preprocess() chooses between. */
enum class GlslDialect {
    /** GLSL ES 3.00: what a GLES 3 context gets, i.e. every desktop GPU. */
    Es300,
    /** GLSL ES 1.00: a GLES 2 context. No #version; in/out/texture() rewritten. */
    Es100,
};

/**
 * Turns a KWin fragment shader file into the source KWin hands the driver.
 *
 * Mirrors GLShader::preprocess() in KWin 6.8 (src/opengl/glshader.cpp): KWin
 * deletes every #version line, adds its own prologue for the context, and
 * resolves `#include "name"` recursively. On a GLES 2 context it also rewrites
 * every line that starts with "in " or "out ". The one difference is where the
 * includes come from: KWin reads them from its compiled-in resources
 * (":/opengl/"), and this reads them from @a includeDir.
 *
 * Throws std::runtime_error when a file cannot be read.
 */
std::string kwinPreprocessFragment(const std::filesystem::path &path, const std::filesystem::path &includeDir, GlslDialect dialect);

} // namespace FringeShiftTest
