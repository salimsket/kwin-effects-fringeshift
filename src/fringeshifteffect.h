/*
    SPDX-FileCopyrightText: 2026 Fringe Shift contributors

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <memory>
#include <unordered_set>

#include "effect/offscreeneffect.h"

#include "fringeshiftparams.h"

namespace KWin
{

class GLShader;

/**
 * Per-channel 2D subpixel correction for QD-OLED panels.
 *
 * The QD-OLED triad places GREEN at the top vertex and RED/BLUE at the bottom
 * corners, so a nominally grey edge shows a green halo above and a warm fringe
 * below. Shifting each channel's *content* by a fraction of a pixel in the
 * opposite direction cancels the fringe without changing stroke weight: a
 * constant sub-pixel shift is a convolution whose DC transfer is exactly 1.
 *
 * Windows are redirected into an offscreen texture and redrawn through a shader
 * that resamples each channel separately.
 */
class FringeShiftEffect : public OffscreenEffect
{
    Q_OBJECT

public:
    FringeShiftEffect();
    ~FringeShiftEffect() override;

    static bool supported();

    bool isActive() const override;
    void reconfigure(ReconfigureFlags flags) override;
    int requestedEffectChainPosition() const override;

protected:
    /**
     * Falls through to an ordinary draw whenever the window is being painted
     * transformed, and only then goes through the offscreen texture.
     *
     * The correction is denominated in physical pixels and assumes the
     * offscreen texture maps 1:1 onto them. A transformed draw — a thumbnail in
     * Overview, a scaled window — breaks that on both counts: the shift is
     * scaled along with the window, so it no longer matches the triad it was
     * measured against, and the unshifted alpha fetch, which is exact at 1:1,
     * degrades to nearest-neighbour and frays the rounded corners it exists to
     * protect. Neither view is one where sub-pixel fringing is visible anyway.
     */
#ifdef FRINGESHIFT_DRAWWINDOW_RETURNS_BOOL
    // KWin 6.8+: false means OffscreenEffect could not get a buffer.
    using DrawWindowResult = bool;
#else
    using DrawWindowResult = void;
#endif
    DrawWindowResult drawWindow(const RenderTarget &renderTarget, const RenderViewport &viewport,
                                EffectWindow *window, int mask, const Region &deviceRegion,
                                WindowPaintData &data) override;

public Q_SLOTS:
    void toggle();

private Q_SLOTS:
    void slotWindowAdded(KWin::EffectWindow *w);
    void slotWindowDeleted(KWin::EffectWindow *w);
    void slotWindowFullScreenChanged(KWin::EffectWindow *w);

private:
    void readConfig();
    bool loadShader();
    void uploadUniforms();
    void updateWindows();
    void watchWindow(KWin::EffectWindow *w);
    void syncWindow(KWin::EffectWindow *w);
    void redirectWindow(KWin::EffectWindow *w);
    void unredirectWindow(KWin::EffectWindow *w);
    void unredirectAll();

    /**
     * The invariant behind m_windows: when this is false, m_windows is empty;
     * when it is true, m_windows holds exactly the windows that
     * wantsCorrection() accepts. Every path that can change the answer goes
     * through updateWindows() or syncWindow(). The one exception is a window
     * that went fullscreen when no GL context could be made current (see
     * slotWindowFullScreenChanged()); the next updateWindows() repairs it.
     */
    bool shouldRedirect() const;

    /**
     * False for a fullscreen window, which is never redirected.
     *
     * OffscreenEffect renders a redirected window into an 8-bit sRGB buffer,
     * whatever the window's own colour description is. That clips HDR and
     * wide-gamut content to SDR sRGB. A redirected window also counts as having
     * an effect, which stops KWin from putting it on a hardware plane (direct
     * scanout). Fullscreen games and video are where both of those matter
     * most, and they are not the text this effect is for.
     */
    static bool wantsCorrection(const KWin::EffectWindow *w);

    FringeShiftParams m_params;

    /** Runtime on/off, driven by the global shortcut. Not persisted. */
    bool m_enabled = true;

    std::unordered_set<KWin::EffectWindow *> m_windows;
    std::unique_ptr<GLShader> m_shader;
};

} // namespace KWin
