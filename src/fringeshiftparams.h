/*
    SPDX-FileCopyrightText: 2026 Fringe Shift contributors

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QVector2D>

#include <KSharedConfig>

namespace KWin
{

/**
 * What upload() needs of a shader: the three uniform types this parameter set
 * is made of, addressed by name.
 *
 * Stated as a constraint rather than a comment because this template is the
 * seam between the compositor and the measurement harness, so a mismatch is a
 * cross-target break — worth one line of diagnostic instead of an
 * instantiation trace through the caller.
 */
template<typename S>
concept UniformSink = requires(S &shader, const char *name, QVector2D vector, float scalar, int flag) {
    shader.setUniform(name, vector);
    shader.setUniform(name, scalar);
    shader.setUniform(name, flag);
};

/**
 * The shader's parameter set, together with the preconditions that come with it.
 *
 * fringeshift.frag cannot validate its own uniforms, so the guarantees it relies
 * on are stated and enforced here — next to the uniform names themselves, rather
 * than scattered across the config schema, the effect, and the shader's comments.
 * Anything that hands the shader a parameter set is expected to obtain it from
 * clamped() or fromConfig().
 *
 * Plain data: the members vary independently and there is no invariant to
 * protect between them, so this is a struct. The invariant belongs to the
 * *values* (see maxShift / maxSharpen) and is established by clamped().
 */
struct FringeShiftParams
{
    /**
     * Beyond +/-1 px the correction is no longer a fringe fix, and the shader's
     * 4x4 Catmull-Rom footprint stops covering the taps a larger shift needs.
     */
    static constexpr float maxShift = 1.0f;
    static constexpr float maxSharpen = 1.5f;

    /** Per-channel content shift in physical pixels, +X right, +Y down. */
    QVector2D shiftR;
    QVector2D shiftG;
    QVector2D shiftB;
    /** Unsharp amount layered on top of the shift. */
    float sharpen = 0.0f;
    /** Catmull-Rom resample; false selects bilinear (softer, no ringing). */
    bool bicubic = true;
    /** Gate `sharpen` to text-like pixels. Never gates the shift itself. */
    bool adaptive = true;
    /** Debug: render the text mask instead of the corrected image. */
    bool showMask = false;

    /**
     * Brings an unvalidated parameter set into the domain the shader is correct
     * over. Pure — this is the whole of the shader's precondition, and the only
     * thing that needs to be true of a set before upload().
     *
     * Non-finite input is replaced, not clamped: std::clamp is specified as
     * `v < lo ? lo : hi < v ? hi : v`, so a NaN compares false both ways and
     * passes straight through. One NaN uniform poisons every tap in the
     * shader's 4x4 footprint and blanks the window, and Qt's double parser
     * accepts the literals "nan" and "inf" — so a typo in kwinrc is a real way
     * to reach here with one.
     */
    static FringeShiftParams clamped(FringeShiftParams raw);

    /**
     * Reads group [Effect-fringeshift] from @a config and clamps the result.
     *
     * The generated FringeShiftSettings is a singleton whose backing config is
     * fixed by whoever instantiates it first; later calls here re-read that same
     * config rather than rebinding to @a config.
     *
     * It reads KConfig's in-memory values (KCoreConfigSkeleton::read(), not
     * load()), so picking up an edit made on disk is the caller's job. KWin
     * does reparse @a config before dispatching reconfigure(), which is what
     * makes the documented `kwriteconfig6` + D-Bus workflow work; anything
     * calling this outside that path has to reparse for itself.
     */
    static FringeShiftParams fromConfig(const KSharedConfig::Ptr &config);

    /**
     * True when these parameters would leave every pixel untouched, so the
     * effect can skip redirection entirely and cost nothing. showMask counts as
     * a change, because it replaces the image.
     */
    bool isIdentity() const;

    /**
     * Owns the uniform names. Templated on the shader rather than taking
     * KWin::GLShader so that the test harness, which has no KWin to link
     * against, uploads through the exact same names as the compositor does.
     */
    template<UniformSink Shader>
    void upload(Shader &shader) const
    {
        shader.setUniform("shiftR", shiftR);
        shader.setUniform("shiftG", shiftG);
        shader.setUniform("shiftB", shiftB);
        shader.setUniform("sharpen", sharpen);
        shader.setUniform("bicubic", bicubic ? 1 : 0);
        shader.setUniform("adaptive", adaptive ? 1 : 0);
        shader.setUniform("showMask", showMask ? 1 : 0);
    }
};

} // namespace KWin
