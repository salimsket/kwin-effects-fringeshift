/*
    SPDX-FileCopyrightText: 2026 Fringe Shift contributors

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "FringeShiftKCM.h"

#include <QAction>
#include <QCheckBox>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QProcess>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>

#include <KConfigGroup>
#include <KLocalizedString>
#include <KMessageWidget>
#include <KSharedConfig>

#include "fringeshiftconfig.h" // generated from fringeshiftconfig.kcfg
#include "fringeshiftparams.h"
#include "ui_FringeShiftKCM.h"

namespace KWin
{

namespace
{
/** Slider steps per pixel of shift. 1/1000 px matches the spin boxes' third decimal. */
constexpr int sliderScale = 1000;

int toSlider(double value)
{
    return qRound(value * sliderScale);
}

/**
 * True when font sub-pixel rendering is known to be off.
 *
 * The key is the one the Fonts module writes; an absent key is *not* off,
 * because with nothing in kdeglobals the decision falls to fontconfig, which on
 * a good many systems answers RGB.
 */
bool subPixelRenderingIsOff()
{
    KSharedConfig::Ptr globals = KSharedConfig::openConfig(QStringLiteral("kdeglobals"));
    // This process did not write it and may have been open since before the
    // Fonts page was last used, so go back to the file rather than trusting
    // KConfig's in-memory copy.
    globals->reparseConfiguration();
    const QString mode = globals->group(QStringLiteral("General")).readEntry("XftSubPixel", QString());
    return mode.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0;
}

/**
 * What the preset buttons load, written in FringeShiftParams' own vocabulary
 * rather than as a table of doubles: a preset *is* a parameter set, so the type
 * that states the shader's domain is the right one to express one in — and every
 * value below is inside that domain by construction, which the spin boxes'
 * ranges then re-enforce anyway.
 *
 * Each is the `[Effect-fringeshift]` block of the same name in presets.conf.
 * That file is the reference archive and carries the derivation of every number
 * here; the .ui carries the one-line version as each button's tooltip. Nothing
 * reads presets.conf at runtime, so these are a second copy of four of its
 * blocks — the four that are worth one click.
 *
 * All four share `sharpen = 0`: the archive's argument is that the panel's
 * apparent emitter is *larger* than the drawn one because light diffuses, and
 * that the extra low-pass is part of what makes the correction look right — so
 * sharpening fights the term that fixed it rather than recovering detail.
 */
namespace presets
{
/** Lattice averaged over ~60 cells of a macro photograph. 0.437 / 0.449 px. */
constexpr FringeShiftParams mpe{
    .shiftR = {0.225f, -0.219f},
    .shiftG = {0.0f, 0.219f},
    .shiftB = {-0.225f, -0.219f},
};
/** The same panel from the scale drawing of one cell. 0.472 / 0.441 px. */
constexpr FringeShiftParams spe{
    .shiftR = {0.221f, -0.236f},
    .shiftG = {0.069f, 0.236f},
    .shiftB = {-0.221f, -0.236f},
};
/**
 * Read directly off a labelled reference diagram of the panel's subpixel
 * layout and confirmed by eye on live hardware with fringe-tune.
 *
 * Identical to the schema's defaults, deliberately: this IS the current
 * default reading, and the button exists so the dialog states which reading
 * it is where "Defaults" cannot.
 */
constexpr FringeShiftParams osorio{
    .shiftR = {0.2000f, -0.3000f},
    .shiftG = {0.0500f, 0.2000f},
    .shiftB = {-0.3000f, -0.3000f},
};
/** Vertical only, ~0.3x the measured geometry: the floor of the useful range. */
constexpr FringeShiftParams triad{
    .shiftR = {0.0f, -0.05f},
    .shiftG = {0.0f, 0.10f},
    .shiftB = {0.0f, -0.05f},
};
} // namespace presets
} // namespace

FringeShiftKCM::FringeShiftKCM(QObject *parent, const KPluginMetaData &data)
    : KCModule(parent, data)
    , m_ui(new Ui::FringeShiftKCM)
    , m_openFontSettings(new QAction(i18n("Open Font Settings…"), this))
{
    m_ui->setupUi(widget());

    m_ui->subPixelNotice->setWordWrap(true);
    m_ui->subPixelNotice->setCloseButtonVisible(false);
    connect(m_openFontSettings, &QAction::triggered, this, [] {
        // This hands the job to the module that owns it rather than writing
        // XftSubPixel here. Writing the key is the easy half; making it reach
        // running applications is the Fonts module's apply path — X resources
        // through krdb plus a platform-theme refresh — and most toolkits re-read
        // it only on restart even then. A button that wrote the file and left
        // the session unchanged would look like it had worked.
        //
        // startDetached() reports only whether the program could be started, so
        // the fallback covers a host that ships systemsettings without kcmshell6
        // rather than letting the button do nothing at all.
        if (!QProcess::startDetached(QStringLiteral("kcmshell6"), {QStringLiteral("kcm_fonts")})) {
            QProcess::startDetached(QStringLiteral("systemsettings"), {QStringLiteral("kcm_fonts")});
        }
    });
    updateSubPixelNotice();

    // All spare width goes to the two slider columns, so the X and Y groups stay
    // aligned down the three channel rows however wide the dialog is opened.
    m_ui->gridLayout->setColumnStretch(2, 1);
    m_ui->gridLayout->setColumnStretch(5, 1);

    // The schema is kcfgfile arg="true", so the generated singleton has no
    // config of its own to fall back on: whoever touches it first fixes the
    // backing file for the process, and self() calls qFatal() if that has not
    // happened. Inside KWin the effect does it; here, nothing else will.
    // instance() after first use is ignored, so a second page is harmless.
    FringeShiftSettings::instance(KSharedConfig::openConfig(QStringLiteral("kwinrc")));

    // The README's workflow writes kwinrc with kwriteconfig6, and this process
    // is not the one KWin reparses for. read() would return KConfig's in-memory
    // values, i.e. whatever was on disk when systemsettings started; load() goes
    // back to the file, so the page opens on what is actually configured.
    FringeShiftSettings::self()->load();

    // Ranges come from FringeShiftParams rather than the .ui or the schema,
    // because that is where the shader's domain is stated and enforced. A widget
    // that allowed more would simply have its value clamped back on the next
    // reconfigure, with nothing in the dialog to say so.
    constexpr double maxShift = FringeShiftParams::maxShift;
    pairSlider(m_ui->redXSlider, m_ui->kcfg_RedX, -maxShift, maxShift, 0.005);
    pairSlider(m_ui->redYSlider, m_ui->kcfg_RedY, -maxShift, maxShift, 0.005);
    pairSlider(m_ui->greenXSlider, m_ui->kcfg_GreenX, -maxShift, maxShift, 0.005);
    pairSlider(m_ui->greenYSlider, m_ui->kcfg_GreenY, -maxShift, maxShift, 0.005);
    pairSlider(m_ui->blueXSlider, m_ui->kcfg_BlueX, -maxShift, maxShift, 0.005);
    pairSlider(m_ui->blueYSlider, m_ui->kcfg_BlueY, -maxShift, maxShift, 0.005);
    pairSlider(m_ui->sharpenSlider, m_ui->kcfg_Sharpen, 0.0, FringeShiftParams::maxSharpen, 0.05);

    // Tick every quarter pixel; on the sharpen slider that is every 0.25 of amount.
    for (QSlider *slider : widget()->findChildren<QSlider *>()) {
        slider->setTickInterval(sliderScale / 4);
    }

    // After pairSlider, so a preset's spin-box writes reach the sliders the same
    // way a load does.
    connectPreset(m_ui->presetMpeButton, presets::mpe);
    connectPreset(m_ui->presetSpeButton, presets::spe);
    connectPreset(m_ui->presetOsorioButton, presets::osorio);
    connectPreset(m_ui->presetTriadButton, presets::triad);

    // After the pairing, so the manager's initial load reaches the sliders through
    // the spin boxes' valueChanged.
    addConfig(FringeShiftSettings::self(), widget());
}

FringeShiftKCM::~FringeShiftKCM()
{
    delete m_ui;
}

void FringeShiftKCM::pairSlider(QSlider *slider, QDoubleSpinBox *spin, double min, double max, double step)
{
    spin->setRange(min, max);
    spin->setSingleStep(step);
    slider->setRange(toSlider(min), toSlider(max));
    slider->setSingleStep(toSlider(step));
    slider->setPageStep(toSlider(step) * 10);

    connect(slider, &QSlider::valueChanged, spin, [spin](int value) {
        spin->setValue(double(value) / sliderScale);
    });
    // Blocked on the way back: without it a drag would round-trip through the
    // spin box's rounding and fight the handle under the pointer.
    connect(spin, &QDoubleSpinBox::valueChanged, slider, [slider](double value) {
        const QSignalBlocker block(slider);
        slider->setValue(toSlider(value));
    });

    slider->setValue(toSlider(spin->value()));
}

void FringeShiftKCM::connectPreset(QPushButton *button, const FringeShiftParams &params)
{
    // Copied into the connection: the caller passes a constant from the table
    // above, and nothing needs to outlive this call.
    connect(button, &QPushButton::clicked, this, [this, params] {
        applyPreset(params);
    });
}

void FringeShiftKCM::applyPreset(const FringeShiftParams &params)
{
    // Every write here goes to a kcfg_-bound widget, so KConfigDialogManager sees
    // it as an edit: Apply lights up, Reset undoes the click, and nothing reaches
    // kwinrc until the user asks. A setValue() that changes nothing emits nothing,
    // so re-pressing the button the values already came from does not dirty the
    // page.
    m_ui->kcfg_RedX->setValue(params.shiftR.x());
    m_ui->kcfg_RedY->setValue(params.shiftR.y());
    m_ui->kcfg_GreenX->setValue(params.shiftG.x());
    m_ui->kcfg_GreenY->setValue(params.shiftG.y());
    m_ui->kcfg_BlueX->setValue(params.shiftB.x());
    m_ui->kcfg_BlueY->setValue(params.shiftB.y());
    m_ui->kcfg_Sharpen->setValue(params.sharpen);
    m_ui->kcfg_Bicubic->setChecked(params.bicubic);
    m_ui->kcfg_Adaptive->setChecked(params.adaptive);
}

void FringeShiftKCM::updateSubPixelNotice()
{
    KMessageWidget *notice = m_ui->subPixelNotice;

    if (subPixelRenderingIsOff()) {
        notice->setMessageType(KMessageWidget::Positive);
        notice->setText(i18n("Font sub-pixel rendering is set to None, which is what this correction needs."));
        notice->removeAction(m_openFontSettings);
        return;
    }

    notice->setMessageType(KMessageWidget::Warning);
    notice->setText(i18n("Set font anti-aliasing sub-pixel rendering to <b>None</b>. It displaces the same three "
                         "channels inside the glyph rasteriser, so the two corrections stack and the fringing "
                         "gets worse rather than better — and it only reaches text, while this effect corrects "
                         "the whole window."));
    if (!notice->actions().contains(m_openFontSettings)) {
        notice->addAction(m_openFontSettings);
    }
}

void FringeShiftKCM::load()
{
    KCModule::load();
    // Re-read rather than cache: the Fonts page can be used while this one is
    // open, and load() is also what "Reset" runs.
    updateSubPixelNotice();
}

void FringeShiftKCM::save()
{
    KCModule::save();

    // Must equal KPlugin.Id in the effect's metadata.json. Nothing validates it:
    // a mismatch writes kwinrc correctly and the dialog closes happily, while the
    // running effect keeps its old values until the compositor restarts.
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"),
                                                          QStringLiteral("/Effects"),
                                                          QStringLiteral("org.kde.kwin.Effects"),
                                                          QStringLiteral("reconfigureEffect"));
    message.setArguments({QStringLiteral("fringeshift")});
    // Async: a blocking call here stalls System Settings if the compositor is
    // busy, and hangs it outright if there is no KWin on the bus at all.
    QDBusConnection::sessionBus().asyncCall(message);
}

} // namespace KWin

#include "moc_FringeShiftKCM.cpp"
