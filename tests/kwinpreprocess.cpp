/*
    SPDX-FileCopyrightText: 2026 Fringe Shift contributors

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "kwinpreprocess.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace FringeShiftTest
{

namespace
{

// KWin 6.8, GLShader::preprocess(), recursionDepth == 1.
constexpr std::string_view es300Prologue =
    "#version 300 es\n"
    "#extension GL_OES_EGL_image_external : require\n"
    "precision highp float;\n"
    "precision highp sampler2D;\n"
    "precision highp sampler3D;\n";

constexpr std::string_view es100Prologue =
    "#extension GL_OES_EGL_image_external : require\n"
    "#extension GL_OES_standard_derivatives : enable\n"
    "precision highp float;\n"
    "#define sampler2D highp sampler2D\n"
    "#define sampler3D highp sampler3D\n"
    "vec4 texture(in sampler2D sampler, in vec2 coordinates) {\n"
    "    return texture2D(sampler, coordinates);\n"
    "}\n";

// KWin gives up past this depth too.
constexpr int maxIncludeDepth = 10;

std::string readFile(const std::filesystem::path &path)
{
    std::ifstream in{path};
    if (!in) {
        throw std::runtime_error("cannot open " + path.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void preprocess(const std::filesystem::path &path, const std::filesystem::path &includeDir, GlslDialect dialect,
                int depth, std::ostringstream &out)
{
    if (depth > maxIncludeDepth) {
        throw std::runtime_error("too many recursive includes at " + path.string());
    }

    const bool es100 = dialect == GlslDialect::Es100;
    std::istringstream source{readFile(path)};
    for (std::string line; std::getline(source, line);) {
        const std::string_view view{line};
        // The same tests KWin makes, in the same order: a whole-line prefix
        // match, with no trimming of leading space.
        if (view.starts_with("#version")) {
            // Deleted: the prologue carries the version.
        } else if (view.starts_with("#include \"") && view.ends_with("\"")) {
            constexpr std::string_view directive = "#include \"";
            const std::string name{view.substr(directive.size(), view.size() - directive.size() - 1)};
            preprocess(includeDir / name, includeDir, dialect, depth + 1, out);
        } else if (es100 && view.starts_with("out ")) {
            out << "#define fragColor gl_FragColor";
        } else if (es100 && view.starts_with("in ")) {
            out << "varying " << view.substr(3);
        } else {
            out << line;
        }
        out << '\n';
    }
}

} // namespace

std::string kwinPreprocessFragment(const std::filesystem::path &path, const std::filesystem::path &includeDir, GlslDialect dialect)
{
    std::ostringstream out;
    out << (dialect == GlslDialect::Es300 ? es300Prologue : es100Prologue);
    preprocess(path, includeDir, dialect, 1, out);
    return out.str();
}

} // namespace FringeShiftTest
