/*
    SPDX-FileCopyrightText: 2026 Fringe Shift contributors

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "fringeshifteffect.h"

namespace KWin
{

KWIN_EFFECT_FACTORY_SUPPORTED(FringeShiftEffect, "metadata.json", return FringeShiftEffect::supported();)

} // namespace KWin

#include "main.moc"
