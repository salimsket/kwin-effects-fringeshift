/*
    SPDX-FileCopyrightText: 2026 Fringe Shift contributors

    SPDX-License-Identifier: GPL-3.0-or-later

    Measures fringeshift.frag against the claims the README makes about it.

    Each check below corresponds to a specific documented property, so that
    editing the shader either keeps the numbers in the README true or says which
    one it broke. Every assertion is a magnitude or a RELATIVE channel direction:
    the harness cannot see KWin's axis flip, so absolute "up"/"down" is out of
    scope here and remains the one claim only a real session can confirm.
*/

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numbers>
#include <string>

#include <QCoreApplication>
#include <QTemporaryDir>

#include <KSharedConfig>

#include "shaderrunner.h"

using FringeShiftTest::GlError;
using FringeShiftTest::Image;
using FringeShiftTest::ShaderRunner;
using KWin::FringeShiftParams;

namespace
{

constexpr int imageSize = 256;
constexpr int margin = 16;
// Both divide the 224 interior rows into a whole number of cycles (56 and 14),
// so the single-bin transform below has no spectral leakage to correct for.
// 0.25 cyc/px is text-edge territory; 0.0625 stands in for the near-DC end,
// where an interpolator's phase response should be effectively ideal.
constexpr double highFrequency = 0.25;
constexpr double lowFrequency = 0.0625;
constexpr double patternMean = 0.5;
// Keeps the signal inside [0.2, 0.8] so the shader's clamp to [0, alpha] never
// engages: clipping would break DC conservation for reasons unrelated to the
// filter, and the DC check below is the whole stroke-weight argument.
constexpr double patternAmplitude = 0.3;

constexpr int interiorBegin = margin;
constexpr int interiorEnd = imageSize - margin;

struct Harmonic
{
    double amplitude = 0.0;
    double phase = 0.0;
};

/** Single-bin DFT of @a channel at @a frequency, over interior rows only. */
Harmonic analyse(const Image &image, int channel, double frequency)
{
    double real = 0.0;
    double imaginary = 0.0;
    for (int y = interiorBegin; y < interiorEnd; ++y) {
        double row = 0.0;
        for (int x = interiorBegin; x < interiorEnd; ++x) {
            row += image.at(x, y, channel);
        }
        row /= double(interiorEnd - interiorBegin);

        const double angle = 2.0 * std::numbers::pi * frequency * double(y);
        real += row * std::cos(angle);
        imaginary -= row * std::sin(angle);
    }
    const double count = double(interiorEnd - interiorBegin);
    return Harmonic{2.0 * std::hypot(real, imaginary) / count, std::atan2(imaginary, real)};
}

double meanOf(const Image &image, int channel)
{
    double total = 0.0;
    for (int y = interiorBegin; y < interiorEnd; ++y) {
        for (int x = interiorBegin; x < interiorEnd; ++x) {
            total += image.at(x, y, channel);
        }
    }
    const double count = double(interiorEnd - interiorBegin);
    return total / (count * count);
}

/**
 * Displacement in pixels that turns @a before into @a after, from the phase of
 * the fundamental. Positive means content moved toward increasing row index.
 * At f = 0.25 a shift of up to 1 px moves the phase by at most pi/2, so this
 * never needs unwrapping.
 */
double displacement(const Harmonic &before, const Harmonic &after, double frequency)
{
    return (before.phase - after.phase) / (2.0 * std::numbers::pi * frequency);
}

double maxAbsoluteDifference(const Image &a, const Image &b, int channelCount = 4)
{
    double worst = 0.0;
    for (int y = 0; y < a.height; ++y) {
        for (int x = 0; x < a.width; ++x) {
            for (int c = 0; c < channelCount; ++c) {
                worst = std::max(worst, std::abs(double(a.at(x, y, c)) - double(b.at(x, y, c))));
            }
        }
    }
    return worst;
}

/** Grey sinusoid varying along y, opaque. Grey so a chromatic split is visible. */
Image grating(double frequency)
{
    Image image{imageSize, imageSize};
    for (int y = 0; y < imageSize; ++y) {
        const double value = patternMean + patternAmplitude * std::cos(2.0 * std::numbers::pi * frequency * double(y));
        for (int x = 0; x < imageSize; ++x) {
            for (int c = 0; c < 3; ++c) {
                image.at(x, y, c) = float(value);
            }
            image.at(x, y, 3) = 1.0f;
        }
    }
    return image;
}

/**
 * The same sinusoid turned through 90 degrees: varying along x, flat along y.
 *
 * Every other pattern here varies along y, because every preset is vertical --
 * which means every other measurement is blind to whatever the shader does
 * along x. This one exists to look at that axis, where a vertical correction
 * is required to do nothing whatsoever.
 */
Image gratingAcross(double frequency)
{
    Image image{imageSize, imageSize};
    for (int x = 0; x < imageSize; ++x) {
        const double value = patternMean + patternAmplitude * std::cos(2.0 * std::numbers::pi * frequency * double(x));
        for (int y = 0; y < imageSize; ++y) {
            for (int c = 0; c < 3; ++c) {
                image.at(x, y, c) = float(value);
            }
            image.at(x, y, 3) = 1.0f;
        }
    }
    return image;
}

/**
 * Hard-edged bars over a varying alpha ramp, premultiplied. Harder than the
 * grating for an identity check, and it gives the alpha check something that
 * would visibly smear if alpha were ever resampled.
 */
Image bars()
{
    Image image{imageSize, imageSize};
    for (int y = 0; y < imageSize; ++y) {
        const float alpha = 0.25f + 0.75f * float(y) / float(imageSize - 1);
        for (int x = 0; x < imageSize; ++x) {
            const bool ink = ((x / 3) + (y / 5)) % 2 == 0;
            const float luma = ink ? 0.85f : 0.15f;
            for (int c = 0; c < 3; ++c) {
                image.at(x, y, c) = luma * alpha; // premultiplied, as KWin supplies
            }
            image.at(x, y, 3) = alpha;
        }
    }
    return image;
}

int failures = 0;

void check(bool passed, const char *name, const std::string &detail)
{
    std::printf("%-4s  %-22s  %s\n", passed ? "PASS" : "FAIL", name, detail.c_str());
    if (!passed) {
        ++failures;
    }
}

std::string format(const char *label, double value, const char *unit = "")
{
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "%s = %+.5f%s", label, value, unit);
    return buffer;
}

/**
 * The parameter set's own checks: no GL, no shader, no compositor.
 *
 * clamped() and isIdentity() are the whole of what the shader assumes about its
 * uniforms and the whole of what decides whether the effect runs at all, and
 * they are the only code here that is pure. Running them ahead of the GL setup
 * means a host with no headless driver still exercises them rather than
 * skipping everything.
 */
void checkParameterDomain()
{
    constexpr float nan = std::numeric_limits<float>::quiet_NaN();
    constexpr float inf = std::numeric_limits<float>::infinity();

    // Past +/-1 px the shader's 4x4 footprint stops covering the taps, and
    // sharpen has a documented 0 .. 1.5 range.
    {
        FringeShiftParams wild;
        wild.shiftR = QVector2D(5.0f, -5.0f);
        wild.shiftG = QVector2D(-0.4f, 0.4f); // already inside: must survive intact
        wild.shiftB = QVector2D(1.0f, -1.0f); // exactly on the boundary
        wild.sharpen = 9.0f;

        const FringeShiftParams got = FringeShiftParams::clamped(wild);
        const bool passed = got.shiftR == QVector2D(1.0f, -1.0f)
            && got.shiftG == QVector2D(-0.4f, 0.4f)
            && got.shiftB == QVector2D(1.0f, -1.0f)
            && got.sharpen == FringeShiftParams::maxSharpen;
        check(passed, "clamp-range", format("sharpen", double(got.sharpen)));
    }

    {
        FringeShiftParams negative;
        negative.sharpen = -3.0f;
        check(FringeShiftParams::clamped(negative).sharpen == 0.0f, "clamp-sharpen-floor", "sharpen = -3 -> 0");
    }

    // std::clamp alone would pass a NaN straight through, and one NaN uniform
    // poisons every tap of the shader's footprint. Qt's double parser accepts
    // "nan" and "inf", so kwinrc is a real route to getting one here.
    {
        FringeShiftParams poison;
        poison.shiftR = QVector2D(nan, inf);
        poison.shiftG = QVector2D(-inf, nan);
        poison.shiftB = QVector2D(nan, nan);
        poison.sharpen = nan;

        const FringeShiftParams got = FringeShiftParams::clamped(poison);
        const bool finite = std::isfinite(got.shiftR.x()) && std::isfinite(got.shiftR.y())
            && std::isfinite(got.shiftG.x()) && std::isfinite(got.shiftG.y())
            && std::isfinite(got.shiftB.x()) && std::isfinite(got.shiftB.y())
            && std::isfinite(got.sharpen);
        // Degrading to zero, not to some substitute shift, is what makes the
        // result identity — so a garbled config costs nothing instead of
        // silently displacing channels.
        check(finite && got.isIdentity(), "clamp-nonfinite", finite ? "all finite, identity" : "NON-FINITE SURVIVED");
    }

    // isIdentity() is the cost gate: true means every window gets unredirected.
    {
        const FringeShiftParams zero;

        FringeShiftParams masked;
        masked.showMask = true; // replaces the image, so never identity

        FringeShiftParams sharpened;
        sharpened.sharpen = 0.5f;

        FringeShiftParams nudged;
        nudged.shiftG = QVector2D(0.0f, 0.1f);

        // bicubic/adaptive only pick how a change is made, so on their own they
        // change nothing.
        FringeShiftParams switched;
        switched.bicubic = false;
        switched.adaptive = false;

        const bool passed = zero.isIdentity() && switched.isIdentity()
            && !masked.isIdentity() && !sharpened.isIdentity() && !nudged.isIdentity();
        check(passed, "identity-gate", "zero/flags identity; mask, sharpen, shift not");
    }
}

/** The preset with every channel shift scaled, then re-clamped. */
FringeShiftParams scaledBy(FringeShiftParams params, float scale)
{
    params.shiftR *= scale;
    params.shiftG *= scale;
    params.shiftB *= scale;
    return FringeShiftParams::clamped(params);
}

/**
 * The per-channel fractional shift the shader will actually apply, after
 * centring — the quantity its MTF loss depends on, and the nominal the measured
 * displacement is compared against below.
 *
 * This restates the shader's centring formula for the y axis (all the presets
 * here are vertical) purely to compute an expectation. It is not how centring
 * itself is verified: translation-invariance does that, without an oracle.
 */
double centredFraction(const FringeShiftParams &params)
{
    const double red = -double(params.shiftR.y());
    const double green = -double(params.shiftG.y());
    const double blue = -double(params.shiftB.y());
    const double centre = -(std::min({red, green, blue}) + std::max({red, green, blue})) * 0.5;
    return std::abs(red + centre);
}

/** The shipped defaults, read from the kcfg schema rather than restated here. */
FringeShiftParams schemaDefaults()
{
    // A path that does not exist yields every value from the schema, and keeps
    // the run from touching the user's real kwinrc.
    static QTemporaryDir directory;
    return FringeShiftParams::fromConfig(KSharedConfig::openConfig(directory.filePath(QStringLiteral("kwinrc"))));
}

} // namespace

int main(int argc, char **argv)
{
    const QCoreApplication application{argc, argv};

    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <fringeshift.frag> <stub-include-dir>\n", argv[0]);
        return 2;
    }

    const FringeShiftParams defaults = schemaDefaults();
    std::printf("defaults from schema: R(%+.3f,%+.3f) G(%+.3f,%+.3f) B(%+.3f,%+.3f) sharpen %.2f bicubic %d\n\n",
                defaults.shiftR.x(), defaults.shiftR.y(),
                defaults.shiftG.x(), defaults.shiftG.y(),
                defaults.shiftB.x(), defaults.shiftB.y(),
                double(defaults.sharpen), defaults.bicubic ? 1 : 0);

    // Pure, so it runs before the GL setup that everything below depends on.
    checkParameterDomain();

    std::unique_ptr<ShaderRunner> runner;
    try {
        runner = std::make_unique<ShaderRunner>(argv[1], argv[2]);
    } catch (const GlError &error) {
        // No headless GL here. Report as a skip so this does not read as the
        // shader being wrong — unless the checks above already found something,
        // which is a real failure and must not be buried under the skip.
        std::fprintf(stderr, "skipping shader measurements: %s\n", error.what());
        return failures == 0 ? 77 : 1;
    }

    try {
        // 1. An all-zero set is a true no-op, which is what lets the effect
        //    unredirect every window and cost nothing.
        {
            const FringeShiftParams zero;
            const Image input = bars();
            const Image output = runner->run(input, zero);
            const double worst = maxAbsoluteDifference(input, output);
            check(zero.isIdentity() && worst < 1e-5, "identity", format("max |delta|", worst));
        }

        // 2. Alpha is never shifted, so shadows and rounded corners stay intact.
        {
            const Image input = bars();
            const Image output = runner->run(input, defaults);
            double worst = 0.0;
            for (int y = 0; y < imageSize; ++y) {
                for (int x = 0; x < imageSize; ++x) {
                    worst = std::max(worst, std::abs(double(input.at(x, y, 3)) - double(output.at(x, y, 3))));
                }
            }
            check(worst < 1e-6, "alpha-untouched", format("max |delta alpha|", worst));
        }

        const Image input = grating(highFrequency);
        const Image output = runner->run(input, defaults);

        // 3. The DC transfer of a constant sub-pixel shift is exactly 1, which
        //    is the reason the effect cannot change apparent stroke weight.
        {
            double worst = 0.0;
            for (int c = 0; c < 3; ++c) {
                worst = std::max(worst, std::abs(meanOf(output, c) - meanOf(input, c)));
            }
            check(worst < 1e-4, "dc-transfer", format("max |delta mean|", worst));
        }

        // 4. The Laplacian term flattens the interpolator's MTF, so the resample
        //    must not soften. The band is one-sided in effect: an uncompensated
        //    Catmull-Rom loses several percent here, and what is measured is a
        //    small OVERSHOOT (1.032 at the shipped preset), because the
        //    compensation is quadratic in the fraction and slightly overpays at
        //    this one. That figure tracks the default; the 0.219 px preset read
        //    1.030 and a 0.10 px one is nearer flat.
        {
            double worst = 0.0;
            std::string detail;
            for (int c = 0; c < 3; ++c) {
                const double mtf = analyse(output, c, highFrequency).amplitude / analyse(input, c, highFrequency).amplitude;
                worst = std::max(worst, std::abs(mtf - 1.0));
                detail += format(c == 0 ? "R" : (c == 1 ? "G" : "B"), mtf) + "  ";
            }
            check(worst < 0.05, "mtf-at-f0.25", detail + format("worst |MTF-1|", worst));
        }

        // 5. Centring equalises the per-channel fractional shifts, so all three
        //    channels must move by the SAME distance, green opposing red and
        //    blue. An uncentred set would give green twice the fraction of the
        //    other two — the 16% chromatic mismatch the README cites. Note this
        //    says nothing about the absolute distance; that is checks 6 and 7.
        {
            const double shiftRed = displacement(analyse(input, 0, highFrequency), analyse(output, 0, highFrequency), highFrequency);
            const double shiftGreen = displacement(analyse(input, 1, highFrequency), analyse(output, 1, highFrequency), highFrequency);
            const double shiftBlue = displacement(analyse(input, 2, highFrequency), analyse(output, 2, highFrequency), highFrequency);

            const double spread = std::max({std::abs(shiftRed), std::abs(shiftGreen), std::abs(shiftBlue)})
                - std::min({std::abs(shiftRed), std::abs(shiftGreen), std::abs(shiftBlue)});
            const bool opposed = shiftGreen * shiftRed < 0.0 && shiftGreen * shiftBlue < 0.0;

            const std::string detail = format("R", shiftRed, " px") + "  " + format("G", shiftGreen, " px")
                + "  " + format("B", shiftBlue, " px") + "  " + format("spread", spread, " px");
            check(spread < 1e-4 && opposed, "chromatic-match", detail);
        }

        // 6. How much of the requested shift actually arrives.
        //
        //    Catmull-Rom is symmetric, so it is often assumed to place content
        //    exactly where asked. It does not: it is an interpolating kernel,
        //    not an ideal one, and its phase response under-delivers by an
        //    amount that grows with frequency. A configured shift is therefore
        //    a NOMINAL, not a delivered, displacement.
        //
        //    Closed form for this shader — sum the four Catmull-Rom taps as
        //    phasors and add the (in-phase) Laplacian term:
        //        H(f) = SUM w_i(frac) e^{i 2 pi f o_i} + k * 2 * (1 - cos(2 pi f))
        //        delivered = -arg(H) / (2 pi f)
        //    which gives 0.982 of nominal at f = 1/16 and 0.733 at f = 1/4 for
        //    the shipped preset's centred 0.225 px, and reproduces the measured
        //    values below to five digits. The bands are tight enough that any
        //    change to the resample or the compensation term leaves them.
        //
        //    THESE BANDS TRACK THE DEFAULT SHIFT, so changing the shipped preset
        //    moves them: gain is a function of the fraction, not a constant of
        //    the kernel. Larger fractions deliver MORE of what was asked, since
        //    the in-phase Laplacian term grows as the square of the fraction and
        //    partly offsets the kernel's phase lag. The previous 0.10 px default
        //    read 0.979 / 0.680 here. Recompute from the closed form above
        //    before concluding the harness has broken.
        {
            const double nominal = centredFraction(defaults);

            const Image lowInput = grating(lowFrequency);
            const Image lowOutput = runner->run(lowInput, defaults);
            const double lowShift = displacement(analyse(lowInput, 1, lowFrequency), analyse(lowOutput, 1, lowFrequency), lowFrequency);
            const double highShift = displacement(analyse(input, 1, highFrequency), analyse(output, 1, highFrequency), highFrequency);

            const double lowGain = std::abs(lowShift) / nominal;
            const double highGain = std::abs(highShift) / nominal;

            check(lowGain > 0.97 && lowGain < 0.995 && highGain > 0.71 && highGain < 0.75,
                  "shift-gain",
                  format("nominal", nominal, " px") + "  " + format("gain@f0.0625", lowGain)
                      + "  " + format("gain@f0.25", highGain));
        }

        // 7. The shift is a smooth sub-pixel continuum, proportional to what was
        //    asked for. This is the property the rejected derivative-based path
        //    lacked: it carried the shift in overshoot that the clamp cut off,
        //    so it moved 0.056 px when asked for 0.02 px and could not go lower.
        //
        //    Gain is not perfectly flat across scales — the compensation
        //    coefficient k is quadratic in the fraction, so a larger shift also
        //    gets slightly more of the in-phase Laplacian term. Measured spread
        //    is ~0.06 over a 4x range of requested shift (0.670 -> 0.733 at
        //    f = 1/4), i.e. proportional to within a few percent, with no floor
        //    of the kind the old path had.
        //
        //    The spread scales with the default too, for the same reason as
        //    check 6: k grows as the square of the fraction, so a bigger base
        //    spans a bigger range of k. The 0.10 px default spread ~0.03 here.
        //    What is being asserted is the ABSENCE of a floor — ratios[0], the
        //    smallest shift, still delivering two thirds of nominal — not a
        //    particular flatness.
        {
            constexpr std::array<float, 3> scales{0.25f, 0.5f, 1.0f};
            std::array<double, 3> ratios{};
            std::string detail;
            for (std::size_t i = 0; i < scales.size(); ++i) {
                const FringeShiftParams params = scaledBy(defaults, scales[i]);
                const Image scaledOutput = runner->run(input, params);
                const double moved = std::abs(displacement(analyse(input, 1, highFrequency),
                                                           analyse(scaledOutput, 1, highFrequency),
                                                           highFrequency));
                ratios[i] = moved / centredFraction(params);
                detail += format(i == 0 ? "x0.25" : (i == 1 ? "x0.5" : "x1.0"), ratios[i]) + "  ";
            }
            const double spread = *std::max_element(ratios.begin(), ratios.end())
                - *std::min_element(ratios.begin(), ratios.end());
            check(spread < 0.08 && ratios[0] > 0.6, "proportional-shift", detail + format("spread", spread));
        }

        // 8. A sub-pixel offset common to all three channels is removed entirely
        //    by the centring step, so it cannot change the output at all. This
        //    is the centring claim verified without an oracle.
        {
            FringeShiftParams offset = defaults;
            const QVector2D common{0.0f, 0.20f};
            offset.shiftR += common;
            offset.shiftG += common;
            offset.shiftB += common;

            const Image shifted = runner->run(input, FringeShiftParams::clamped(offset));
            const double worst = maxAbsoluteDifference(output, shifted);
            check(worst < 1e-5, "translation-invariant", format("max |delta|", worst));
        }

        // 9. Only the fractional part is ever filtered; a whole-texel step is an
        //    exact offset, so it must cost no MTF and land exactly on 1 px.
        {
            FringeShiftParams integral;
            integral.shiftR = QVector2D(0.0f, 1.0f);
            integral.shiftG = QVector2D(0.0f, -1.0f);
            integral.shiftB = QVector2D(0.0f, 1.0f);

            const Image stepped = runner->run(input, FringeShiftParams::clamped(integral));
            double worstMtf = 0.0;
            double worstOffset = 0.0;
            for (int c = 0; c < 3; ++c) {
                const Harmonic before = analyse(input, c, highFrequency);
                const Harmonic after = analyse(stepped, c, highFrequency);
                worstMtf = std::max(worstMtf, std::abs(after.amplitude / before.amplitude - 1.0));
                worstOffset = std::max(worstOffset, std::abs(std::abs(displacement(before, after, highFrequency)) - 1.0));
            }
            check(worstMtf < 1e-4 && worstOffset < 1e-3,
                  "integer-step-exact",
                  format("worst |MTF-1|", worstMtf) + "  " + format("worst |shift|-1", worstOffset, " px"));
        }

        // 10. Separability, on the axis nothing else here looks at.
        //
        //     The resample is separable, so a shift along one axis cannot touch
        //     content that is constant along that axis. Two exact statements
        //     follow, neither needing an oracle:
        //
        //       a. a purely VERTICAL shift must leave a horizontal grating
        //          BIT-IDENTICAL -- nothing moves, nothing changes amplitude;
        //       b. adding a vertical component to a horizontal shift must not
        //          change anything the horizontal component does.
        //
        //     Checks 1-9 all measure along y with a vertical preset, which is
        //     precisely the case where an isotropic MTF coefficient and a
        //     per-axis one agree: dot(f, f) degenerates to fy^2 when fx is
        //     zero, and a 1D pattern zeroes the perpendicular Laplacian taps.
        //     Both sides of the identity vanish, so the whole suite passed
        //     unchanged while the shader scaled its compensation by dot(f, f)
        //     and applied it to BOTH axes -- sharpening across a shift that
        //     never happened. At the measured Gen 3 geometry that was +17% MTF
        //     at Nyquist on green horizontally, with no resample loss to offset
        //     it, and ~2x over-compensation on red and blue in both axes.
        //
        //     Sharpen stays 0 throughout: the unsharp term is isotropic by
        //     design and would break this invariant on purpose.
        {
            const Image across = gratingAcross(highFrequency);

            // The y components of the shipped defaults with the x components
            // dropped. Taken from the defaults rather than written out so this
            // keeps measuring the real preset's vertical magnitude, but NOT
            // `defaults` itself: the shipped set carries a horizontal component
            // (red right, blue left), and a horizontal shift is supposed to move
            // a horizontal grating. Passing it here would assert the opposite of
            // separability and fail on a correct shader.
            FringeShiftParams vertical = defaults;
            vertical.shiftR.setX(0.0f);
            vertical.shiftG.setX(0.0f);
            vertical.shiftB.setX(0.0f);

            const double untouched = maxAbsoluteDifference(across, runner->run(across, FringeShiftParams::clamped(vertical)));

            // Same pair the other way round: the shipped preset, and the shipped
            // preset with its y components dropped. Derived rather than written
            // out for the same reason as `vertical` above -- hardcoded magnitudes
            // would keep passing while silently no longer mirroring the preset.
            FringeShiftParams flat = defaults;
            flat.shiftR.setY(0.0f);
            flat.shiftG.setY(0.0f);
            flat.shiftB.setY(0.0f);

            const FringeShiftParams skew = defaults;

            const double independent = maxAbsoluteDifference(runner->run(across, FringeShiftParams::clamped(flat)),
                                                             runner->run(across, FringeShiftParams::clamped(skew)));

            check(untouched < 1e-5 && independent < 1e-5,
                  "separable-compensation",
                  format("vertical shift on x-grating", untouched) + "  " + format("y-component leak", independent));
        }
    } catch (const GlError &error) {
        std::fprintf(stderr, "\nGL failure: %s\n", error.what());
        return 1;
    }

    std::printf("\n%s (%d failed)\n", failures == 0 ? "all checks passed" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
