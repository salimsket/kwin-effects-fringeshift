#version 140
// Fringe Shift — per-channel 2D subpixel correction for QD-OLED.
//
// KWin never compiles the #version line below. Its preprocessor
// (GLShader::preprocess) deletes it and adds its own prologue. On a GLES 3
// context that is "#version 300 es" plus highp defaults. On a GLES 2 context
// there is no version line (GLSL ES 1.00), and the preprocessor also rewrites
// in/out/texture(). The shader is therefore written in core spelling, but it
// avoids everything that GLSL ES 1.00 does not have: round(), integer clamp(),
// textureSize(), texelFetch(), and array indices that are not loop indices.
// The GLES 2 rewrite works line by line: only the two declarations of
// texcoord0 and fragColor can start with "in " or "out ".
#include "colormanagement.glsl"
#include "saturation.glsl"

uniform sampler2D sampler;
uniform vec4 modulation;
uniform int textureWidth;
uniform int textureHeight;

// Per-channel content shift in physical pixels, +X right, +Y down.
uniform vec2 shiftR;
uniform vec2 shiftG;
uniform vec2 shiftB;
uniform float sharpen;   // unsharp amount, 0 .. 1.5
uniform int bicubic;     // 1 = Catmull-Rom resample, 0 = bilinear
uniform int adaptive;    // 1 = gate `sharpen` to text-like pixels
uniform int showMask;    // 1 = render the mask instead of the image

in vec2 texcoord0;
out vec4 fragColor;

vec2 g_texSize;

// The offscreen texture holds premultiplied, 8-bit, sRGB-encoded window
// content: KWin's OffscreenEffect always renders it that way, whatever the
// window's own colour description is. Which way its y axis points depends on
// the KWin version (see axisFlip in main()). Sampling is by explicit texel
// address so the resample is exact; this clamp, like the texture's
// CLAMP_TO_EDGE, keeps taps inside the window.
vec4 fetchTexel(vec2 texel)
{
    vec2 c = clamp(texel, vec2(0.0), g_texSize - vec2(1.0));
    return texture(sampler, (c + vec2(0.5)) / g_texSize);
}

// Resampling kernels, evaluated at the distance from a tap to the sample point,
// one component per colour channel. Catmull-Rom (a = -0.5) keeps edge contrast
// far better than bilinear, at the cost of very slight ring/overshoot on
// extreme-contrast edges. It is zero from |x| = 2 on.
vec3 catmullRom(vec3 x)
{
    x = abs(x);
    vec3 inner = (1.5 * x - 2.5) * x * x + 1.0;
    vec3 outer = ((-0.5 * x + 2.5) * x - 4.0) * x + 2.0;
    return mix(inner, outer, step(1.0, x)) * (1.0 - step(2.0, x));
}

// Fallback: the bilinear tent. Softer, with no risk of ringing at all.
vec3 tent(vec3 x)
{
    return max(1.0 - abs(x), 0.0);
}

// Content-adaptive mask. It gates SHARPNESS only: the geometric shift is
// deliberately uniform, because scaling a displacement per pixel warps glyphs
// (parts of a letter move, parts do not).
//
// Text = high local luminance contrast + low WIDE-AREA saturation. Subpixel
// text rendering deliberately colours individual edge pixels, so saturation has
// to be measured over a wide, strided window: those 1px complementary fringes
// average back toward neutral there, whereas genuinely coloured content (icons,
// photos) stays saturated. Measuring it over a 3x3 instead made the mask switch
// itself off on exactly the text it is meant to correct.
float textMask(vec2 pos)
{
    vec2 cc = floor(pos);

    float lmin = 1.0e9;
    float lmax = -1.0e9;
    for (int j = -1; j <= 1; ++j) {
        for (int k = -1; k <= 1; ++k) {
            vec3 s = fetchTexel(cc + vec2(float(k), float(j))).rgb;
            float L = (s.r + s.g + s.b) / 3.0;
            lmin = min(lmin, L);
            lmax = max(lmax, L);
        }
    }
    float lrange = lmax - lmin;

    vec3 wide = vec3(0.0);
    for (int a = 0; a < 4; ++a) {
        for (int b = 0; b < 4; ++b) {
            wide += fetchTexel(cc + vec2(-3.0 + 2.0 * float(b), -3.0 + 2.0 * float(a))).rgb;
        }
    }
    wide /= 16.0;
    float mx = max(wide.r, max(wide.g, wide.b));
    float mn = min(wide.r, min(wide.g, wide.b));
    float sat = (mx > 1.0e-4) ? (mx - mn) / mx : 0.0;

    float cfac = smoothstep(0.06, 0.25, lrange);   // includes low-contrast UI text
    float sfac = 1.0 - smoothstep(0.25, 0.50, sat);
    return clamp(cfac * sfac, 0.0, 1.0);
}

void main()
{
    g_texSize = vec2(float(textureWidth), float(textureHeight));
    vec2 pos = texcoord0 * g_texSize;
    vec2 n = floor(pos);

    // The mask only gates Sharpness, so skip its 25 taps when Sharpness is 0.
    float mask = 1.0;
    if (adaptive != 0 && (sharpen > 0.001 || showMask != 0)) {
        mask = textMask(pos);
    }
    if (showMask != 0) {
        fragColor = vec4(mask, mask, mask, 1.0);
        return;
    }

    // The shift is applied UNIFORMLY, never scaled by the mask. A constant
    // sub-pixel shift is a convolution: its DC transfer is exactly 1.0, so it
    // cannot change apparent stroke weight, and it raises perceived luminance
    // MTF (0.938 -> 0.963 at f=0.25 for this triad geometry). Scaling it per
    // pixel turns it into a WARP instead: the mask swings from 1 to 0 within a
    // single glyph, so parts of a letter move and parts do not. That breaks DC
    // conservation (text gains weight) and throws away most of the acuity gain
    // (+0.61% edge energy masked vs +1.75% uniform).

    // Map the UI convention (+Y = down) onto texcoord0. CMake substitutes the
    // y factor from the KWin version: -1.0 on KWin <= 6.7, where texcoord0's
    // y axis points up the screen; 1.0 on 6.8+, where the offscreen texture's
    // FlipY content transform makes it point down.
    const vec2 axisFlip = vec2(1.0, @FRINGESHIFT_AXIS_FLIP_Y@);
    vec2 sR = shiftR * axisFlip;
    vec2 sG = shiftG * axisFlip;
    vec2 sB = shiftB * axisFlip;

    // Centre the three shifts about zero. A common sub-pixel offset added to all
    // channels translates the whole image by well under a pixel — invisible —
    // while leaving the RELATIVE channel geometry, which is what actually
    // cancels the fringe, bit-identical. The point is to equalise the
    // per-channel FRACTIONAL shifts, because the resample's MTF loss depends on
    // that fraction. Without centring, green carries twice red/blue's fraction,
    // so green loses the most high-frequency detail and grey text reads reddish:
    // measured chromatic mismatch was 16% at G +0.30 / RB -0.15, and 0% after.
    // It also lowers the largest fraction (0.30 -> 0.225), so there is less loss
    // to compensate for to begin with.
    vec2 ctr = -(min(sR, min(sG, sB)) + max(sR, max(sG, sB))) * 0.5;
    sR += ctr;
    sG += ctr;
    sB += ctr;

    // From here on the three channels travel together, one per component:
    // sx.r is red's x shift, sy.b is blue's y shift, and so on.
    vec3 sx = vec3(sR.x, sG.x, sB.x);
    vec3 sy = vec3(sR.y, sG.y, sB.y);

    // Every shift splits into an exact integer texel step plus a fraction in
    // [-0.5, 0.5]. The integer part is a plain texel offset: exact, free, no MTF
    // loss at any magnitude. Only the fraction is ever filtered, so only the
    // fraction feeds the compensation term below.
    vec3 fx = sx - floor(sx + 0.5);
    vec3 fy = sy - floor(sy + 0.5);

    // One path for every shift magnitude: an interpolator, whose linear phase
    // gives true sub-pixel positioning, plus a Laplacian that flattens its MTF
    // so it does not soften. Measured |MTF-1| at a 0.10px fraction: bilinear
    // 0.200 -> 0.013, Catmull-Rom 0.056 -> 0.011.
    //
    // A derivative-based shift was used here before. It also avoided softening,
    // but it carried the shift entirely in overshoot, which the clamp cuts off:
    // on a hard black/white edge it moved 0.056px when asked for 0.02px and
    // could not go below that, so there was no smooth sub-pixel continuum. It
    // also overshot 1.050 vs 1.009 for this path at 0.10px — 5x the clipped
    // ringing, which reads as aliasing on text edges. This path stays in the
    // source encoding and is proportional, at the cost of ~0.2% stroke weight.
    bool sharp = bicubic != 0;
    float amt = sharpen * mask;
    float ca = sharp ? 0.8625 : 1.7216;
    // Per axis, never isotropic. The resample is SEPARABLE: the x fraction
    // costs MTF along x only, the y fraction along y only. A single coefficient
    // from dot(f, f) charges each axis for both, which over-compensates a
    // diagonal shift ~2x and, worse, sharpens across a purely vertical one --
    // ca was fitted on a 1D grating, where dot(f, f) degenerates to fy^2, so
    // that spurious term was never in the measurement it came from.
    //
    // `amt` stays isotropic: equal in both axes, the two Laplacian components
    // below sum to the ordinary 5-tap Laplacian, so Sharpen behaves as it
    // always did.
    vec3 kx = min(ca * fx * fx, 0.35) + amt * 0.25;
    vec3 ky = min(ca * fy * fy, 0.35) + amt * 0.25;

    // ONE 5x5 WINDOW SERVES ALL THREE CHANNELS. KWin draws the offscreen
    // texture 1:1 at a whole-pixel position, so `pos` sits on the centre of
    // texel n = floor(pos), and a channel shifted by s samples at n + 0.5 - s.
    // The tap at offset o from n is therefore o + s away from the sample point,
    // whatever the pixel: the weights depend only on the uniforms. The bound:
    // FringeShiftParams clamps every shift to +/-1 px, so after the centring
    // above |s| <= 1. Catmull-Rom is zero from distance 2 on, so no tap outside
    // o = -2 .. 2 has a weight. Each channel's Laplacian sits on the texel
    // nearest its sample point, n - round(s), and its taps reach one texel
    // further: also inside -2 .. 2. So 25 fetches give every channel its
    // resample and its Laplacian, and the centre tap gives alpha. Fetching per
    // channel took 64.
    //
    // The Laplacian centre comes from the same round() as the fractions above,
    // not from floor() of the interpolated sample position. At a fraction of
    // exactly 0.5 that position sits on a texel boundary, and interpolation
    // error in texcoord0 would pick the side pixel by pixel.
    vec3 cx = -floor(sx + 0.5);
    vec3 cy = -floor(sy + 0.5);

    // Per x offset, one component per channel: the resample weight, the 1D
    // Laplacian (1, -2, 1) about that channel's centre, and an indicator of
    // the centre itself. The y versions are made per row in the loop below.
    vec3 wX[5];
    vec3 lapX[5];
    vec3 ctrX[5];
    for (int i = 0; i < 5; ++i) {
        float o = float(i) - 2.0;
        wX[i] = sharp ? catmullRom(o + sx) : tent(o + sx);
        vec3 d = abs(o - cx);
        ctrX[i] = 1.0 - step(0.5, d);
        lapX[i] = step(0.5, d) - step(1.5, d) - 2.0 * ctrX[i];
    }

    // The 2D Laplacian is split into its axis components, because the
    // compensation above is per axis: an x component taken along the centre
    // row and a y component taken down the centre column.
    vec3 res = vec3(0.0);
    vec3 lapAlongX = vec3(0.0);
    vec3 lapAlongY = vec3(0.0);
    float a = 0.0;
    for (int j = 0; j < 5; ++j) {
        float o = float(j) - 2.0;
        vec3 wY = sharp ? catmullRom(o + sy) : tent(o + sy);
        vec3 d = abs(o - cy);
        vec3 ctrY = 1.0 - step(0.5, d);
        vec3 lapY = step(0.5, d) - step(1.5, d) - 2.0 * ctrY;

        vec3 rowW = vec3(0.0);
        vec3 rowLap = vec3(0.0);
        vec3 rowCtr = vec3(0.0);
        for (int i = 0; i < 5; ++i) {
            vec4 t = fetchTexel(n + vec2(float(i) - 2.0, o));
            rowW += wX[i] * t.rgb;
            rowLap += lapX[i] * t.rgb;
            rowCtr += ctrX[i] * t.rgb;
            if (i == 2 && j == 2) {
                a = t.a;
            }
        }
        res += wY * rowW;
        lapAlongX += ctrY * rowLap;
        lapAlongY += lapY * rowCtr;
    }

    // Alpha is never shifted: displacing the window's own coverage would fray
    // its rounded corners and its shadow. RGB stays premultiplied, so it clamps
    // to alpha rather than to 1.
    vec3 rgb = clamp(res - kx * lapAlongX - ky * lapAlongY, 0.0, a);

    // Same tail as KWin's base.frag, in the same order. The resample above
    // deliberately runs in the source encoding (roughly perceptual), not in
    // linear light: shifting in linear light re-weights the taps toward the
    // bright side of every edge, which is exactly the stroke-weight change this
    // effect must not introduce.
    vec4 result = vec4(rgb, a);
    result = encodingToNits(result, sourceNamedTransferFunction, sourceTransferFunctionParams.x, sourceTransferFunctionParams.y);
    result.rgb = (colorimetryTransform * vec4(result.rgb, 1.0)).rgb;
    result = adjustSaturation(result);
    result *= modulation;
    result.rgb = doTonemapping(result.rgb);
    fragColor = nitsToDestinationEncoding(result);
}
