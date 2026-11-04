/*
    SPDX-FileCopyrightText: 2026 Fringe Shift contributors

    SPDX-License-Identifier: GPL-3.0-or-later
*/

// Writes the shader the way KWin hands it to the driver, so that a GLSL
// validator can check it against KWin's real include files. The measurement
// harness cannot do that check itself: it needs identity stubs in place of
// those includes to measure anything (see tests/stubs).

#include <cstdio>
#include <exception>
#include <fstream>
#include <string_view>

#include "kwinpreprocess.h"

int main(int argc, char **argv)
{
    using FringeShiftTest::GlslDialect;

    if (argc != 5) {
        std::fprintf(stderr, "usage: %s <fringeshift.frag> <include-dir> <es300|es100> <output>\n", argv[0]);
        return 2;
    }

    const std::string_view name{argv[3]};
    GlslDialect dialect;
    if (name == "es300") {
        dialect = GlslDialect::Es300;
    } else if (name == "es100") {
        dialect = GlslDialect::Es100;
    } else {
        std::fprintf(stderr, "unknown dialect \"%s\": expected es300 or es100\n", argv[3]);
        return 2;
    }

    try {
        const std::string source = FringeShiftTest::kwinPreprocessFragment(argv[1], argv[2], dialect);
        std::ofstream out{argv[4], std::ios::binary};
        out << source;
        if (!out) {
            std::fprintf(stderr, "cannot write %s\n", argv[4]);
            return 1;
        }
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    return 0;
}
