/*
    SPDX-FileCopyrightText: 2026 Fringe Shift contributors

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <KCModule>

class KPluginMetaData;
class QAction;
class QDoubleSpinBox;
class QPushButton;
class QSlider;

namespace Ui
{
class FringeShiftKCM;
}

namespace KWin
{

struct FringeShiftParams;

/**
 * The settings page behind Desktop Effects -> Fringe Shift -> "Configure...".
 *
 * It owns no policy: every value it edits is a FringeShiftSettings entry, bound
 * by widget name (kcfg_<Entry>) and driven by KConfigDialogManager, so load(),
 * save(), defaults() and the Apply/Reset state come from the base class. The
 * only four things written here are the slider/spin-box pairing — a QSlider
 * carries an int and cannot be bound to a Double entry directly — the preset
 * buttons, which are likewise a second way to drive the bound widgets, the
 * D-Bus poke that makes the running compositor re-read what was saved, and the
 * sub-pixel-rendering notice, which reads a setting this page does not own.
 */
class FringeShiftKCM : public KCModule
{
    Q_OBJECT

public:
    // QObject, not QWidget: KCModule is not itself a widget, and KPluginFactory
    // instantiates the module with a QObject parent.
    explicit FringeShiftKCM(QObject *parent, const KPluginMetaData &data);
    ~FringeShiftKCM() override;

    void load() override;
    void save() override;

private:
    /**
     * Refreshes the notice about font sub-pixel rendering from `kdeglobals`.
     *
     * That setting belongs to the Fonts module, not to this one, and can change
     * while this page is open — so it is re-read rather than cached, and the
     * page only ever reports it. Absent counts as "not confirmed off": with no
     * key, fontconfig decides, and on many systems it decides RGB.
     */
    void updateSubPixelNotice();

    /**
     * Pairs an unmanaged @a slider with the bound @a spin over [@a min, @a max].
     *
     * The spin box is the widget KConfigDialogManager knows about; the slider is
     * a second view onto it. Driving the slider therefore dirties the page and
     * "Defaults" reaches it, both for free — whereas binding the slider itself
     * would quantise a ±1 px setting to the integers -1, 0 and 1, silently.
     */
    void pairSlider(QSlider *slider, QDoubleSpinBox *spin, double min, double max, double step);

    /**
     * Makes @a button load @a params into the bound widgets.
     *
     * @a params is copied into the connection, so the caller can pass a preset
     * literal and the table in the .cpp stays the one place the numbers appear.
     */
    void connectPreset(QPushButton *button, const FringeShiftParams &params);

    /**
     * Writes @a params into the bound widgets — not into FringeShiftSettings.
     *
     * KConfigDialogManager watches the widgets, so going through them is what
     * lights up Apply, lets Reset take a click back, and keeps a preset from
     * reaching `kwinrc` until the user asks for it. Writing the settings object
     * instead would leave the page's dirty state disagreeing with both the
     * widgets in front of the user and the file on disk.
     *
     * `showMask` is deliberately not among the widgets it touches: it is a debug
     * flag with no control on this page, so a preset that set it would have
     * nothing to unset it.
     */
    void applyPreset(const FringeShiftParams &params);

    Ui::FringeShiftKCM *m_ui;
    /** Carried by the notice, and only while the notice is the warning one. */
    QAction *m_openFontSettings;
};

} // namespace KWin
