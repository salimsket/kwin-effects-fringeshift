/*
    SPDX-FileCopyrightText: 2026 Fringe Shift contributors

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "fringeshiftparams.h"

#include <algorithm>
#include <cmath>

#include "fringeshiftconfig.h"

namespace KWin
{

namespace
{

/**
 * Clamp that also closes over NaN and the infinities, which std::clamp alone
 * does not: it is specified as `v < lo ? lo : hi < v ? hi : v`, and every
 * comparison against a NaN is false, so a NaN would be returned unchanged.
 * @a fallback is what a value the shader cannot use degrades to.
 */
float clampFinite(float value, float lo, float hi, float fallback)
{
    if (!std::isfinite(value)) {
        return fallback;
    }
    return std::clamp(value, lo, hi);
}

QVector2D clampShift(QVector2D v)
{
    constexpr float lo = -FringeShiftParams::maxShift;
    constexpr float hi = FringeShiftParams::maxShift;
    // A garbled shift degrades to no shift, so the channel is left where it is
    // rather than displaced by some arbitrary substitute.
    return QVector2D(clampFinite(v.x(), lo, hi, 0.0f), clampFinite(v.y(), lo, hi, 0.0f));
}

} // namespace

FringeShiftParams FringeShiftParams::clamped(FringeShiftParams raw)
{
    raw.shiftR = clampShift(raw.shiftR);
    raw.shiftG = clampShift(raw.shiftG);
    raw.shiftB = clampShift(raw.shiftB);
    raw.sharpen = clampFinite(raw.sharpen, 0.0f, maxSharpen, 0.0f);
    return raw;
}

FringeShiftParams FringeShiftParams::fromConfig(const KSharedConfig::Ptr &config)
{
    FringeShiftSettings::instance(config);
    FringeShiftSettings::self()->read();

    FringeShiftParams raw;
    raw.shiftR = QVector2D(float(FringeShiftSettings::redX()), float(FringeShiftSettings::redY()));
    raw.shiftG = QVector2D(float(FringeShiftSettings::greenX()), float(FringeShiftSettings::greenY()));
    raw.shiftB = QVector2D(float(FringeShiftSettings::blueX()), float(FringeShiftSettings::blueY()));
    raw.sharpen = float(FringeShiftSettings::sharpen());
    raw.bicubic = FringeShiftSettings::bicubic();
    raw.adaptive = FringeShiftSettings::adaptive();
    raw.showMask = FringeShiftSettings::showMask();

    return clamped(raw);
}

bool FringeShiftParams::isIdentity() const
{
    return !showMask
        && sharpen <= 0.0f
        && shiftR.isNull()
        && shiftG.isNull()
        && shiftB.isNull();
}

} // namespace KWin
