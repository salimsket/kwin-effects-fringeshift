/*
    SPDX-FileCopyrightText: 2026 Fringe Shift contributors

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "fringeshifteffect.h"

#include <KGlobalAccel>
#include <KLocalizedString>
#include <QAction>
#include <QLoggingCategory>

#include "effect/effecthandler.h"
#include "opengl/glshader.h"
#include "opengl/glshadermanager.h"

Q_LOGGING_CATEGORY(KWIN_FRINGESHIFT, "kwin_effect_fringeshift", QtWarningMsg)

namespace KWin
{

FringeShiftEffect::FringeShiftEffect()
    : OffscreenEffect()
{
    QAction *toggleAction = new QAction(this);
    toggleAction->setAutoRepeat(false);
    toggleAction->setObjectName(QStringLiteral("FringeShiftToggle"));
    toggleAction->setText(i18n("Toggle Subpixel Fringe Correction"));
    // Not Ctrl+Alt+F<n>: KWin's VirtualTerminalFilter sits ahead of the global
    // shortcut filter and eats that whole range as a VT switch, so the action
    // would never fire — and on a host with no fbcon the switch looks like a
    // session freeze.
    const QList<QKeySequence> shortcut{Qt::META | Qt::CTRL | Qt::Key_F9};
    KGlobalAccel::self()->setDefaultShortcut(toggleAction, shortcut);
    KGlobalAccel::self()->setShortcut(toggleAction, shortcut);
    connect(toggleAction, &QAction::triggered, this, &FringeShiftEffect::toggle);

    connect(effects, &EffectsHandler::windowAdded, this, &FringeShiftEffect::slotWindowAdded);
    connect(effects, &EffectsHandler::windowDeleted, this, &FringeShiftEffect::slotWindowDeleted);

    readConfig();
    if (loadShader()) {
        updateWindows();
    }
}

FringeShiftEffect::~FringeShiftEffect() = default;

bool FringeShiftEffect::supported()
{
    return effects->isOpenGLCompositing();
}

void FringeShiftEffect::readConfig()
{
    m_params = FringeShiftParams::fromConfig(effects->config());
}

bool FringeShiftEffect::shouldRedirect() const
{
    // Redirecting a window costs an offscreen pass per frame, so drop out
    // entirely when the parameters would not change a single pixel.
    return m_shader && m_enabled && !m_params.isIdentity();
}

bool FringeShiftEffect::loadShader()
{
    m_shader = ShaderManager::instance()->generateShaderFromFile(
        ShaderTrait::MapTexture | ShaderTrait::Modulate | ShaderTrait::AdjustSaturation | ShaderTrait::TransformColorspace,
        QString(),
        QStringLiteral(":/effects/fringeshift/shaders/fringeshift.frag"));

#ifdef FRINGESHIFT_GLSHADER_HAS_ISVALID
    const bool loaded = m_shader && m_shader->isValid();
#else
    const bool loaded = m_shader != nullptr;
#endif
    if (!loaded) {
        qCCritical(KWIN_FRINGESHIFT) << "Failed to load the fringeshift shader";
        m_shader.reset();
        return false;
    }

    uploadUniforms();
    return true;
}

void FringeShiftEffect::uploadUniforms()
{
    if (!m_shader) {
        return;
    }

    ShaderBinder binder{m_shader.get()};
    m_params.upload(*m_shader);
}

void FringeShiftEffect::redirectWindow(EffectWindow *w)
{
    if (m_windows.contains(w)) {
        return;
    }

    redirect(w);
    setShader(w, m_shader.get());
    m_windows.insert(w);
}

void FringeShiftEffect::unredirectAll()
{
    for (EffectWindow *w : m_windows) {
        unredirect(w);
    }
    m_windows.clear();
}

void FringeShiftEffect::updateWindows()
{
    if (shouldRedirect()) {
        const auto windows = effects->stackingOrder();
        for (EffectWindow *w : windows) {
            redirectWindow(w);
        }
    } else {
        unredirectAll();
    }

    effects->addRepaintFull();
}

void FringeShiftEffect::slotWindowAdded(EffectWindow *w)
{
    // A new window arrives outside updateWindows(), so it has to re-check the
    // same invariant for itself.
    if (!shouldRedirect()) {
        return;
    }
    redirectWindow(w);
}

void FringeShiftEffect::slotWindowDeleted(EffectWindow *w)
{
    if (auto it = m_windows.find(w); it != m_windows.end()) {
        m_windows.erase(it);
    }
}

void FringeShiftEffect::toggle()
{
    // KWin only guarantees a current GL context on create/destroy/reconfigure and
    // inside the paint stages — not here, in a global-shortcut handler. redirect()
    // and unredirect() create and destroy each window's offscreen texture and
    // framebuffer, and tearing those down with no context current wedges the
    // driver hard enough to take the session with it. So if the context cannot be
    // had, the toggle simply does not happen: a shortcut that did nothing is a far
    // better outcome than the one this guard exists to prevent.
    if (!effects->makeOpenGLContextCurrent()) {
        qCWarning(KWIN_FRINGESHIFT) << "No current OpenGL context; ignoring the toggle";
        return;
    }

    m_enabled = !m_enabled;
    updateWindows();
}

void FringeShiftEffect::drawWindow(const RenderTarget &renderTarget, const RenderViewport &viewport,
                                   EffectWindow *window, int mask, const Region &deviceRegion,
                                   WindowPaintData &data)
{
    if (mask & PAINT_WINDOW_TRANSFORMED) {
        // Effect::drawWindow() is the plain hand-off to the rest of the chain,
        // leaving the window redirected but not routing this frame through the
        // offscreen texture.
        Effect::drawWindow(renderTarget, viewport, window, mask, deviceRegion, data);
        return;
    }

    OffscreenEffect::drawWindow(renderTarget, viewport, window, mask, deviceRegion, data);
}

void FringeShiftEffect::reconfigure(ReconfigureFlags flags)
{
    // Tested as a flag, not compared: ReconfigureAll is the only bit today, but
    // an added one would make an equality check silently drop the reconfigure.
    if (!(flags & Effect::ReconfigureAll)) {
        return;
    }

    readConfig();
    uploadUniforms();
    updateWindows();
}

bool FringeShiftEffect::isActive() const
{
    return !m_windows.empty();
}

int FringeShiftEffect::requestedEffectChainPosition() const
{
    // Late in the chain: the correction is about the pixels that actually reach
    // the panel, so it should run after effects that change window contents.
    return 98;
}

} // namespace KWin

#include "moc_fringeshifteffect.cpp"
