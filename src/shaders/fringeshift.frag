#version 140
// Fringe Shift — per-channel 2D subpixel correction for QD-OLED.
//
// Written in core-profile spelling; KWin's shader preprocessor rewrites
// in/out/texture() and substitutes its own #version line for legacy contexts,
// so this one file covers both. Everything below therefore avoids GLSL 1.30+
// built-ins (round(), integer clamp(), textureSize()).
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

// The offscreen texture holds premultiplied, sRGB-encoded window content, with
// its y axis pointing up the screen (GLTexture::matrix() flips it when the quad
// is drawn). Sampling is by explicit texel address so the resample is exact;
// this clamp, like the texture's CLAMP_TO_EDGE, keeps taps inside the window.
vec4 fetchTexel(vec2 texel)
{
    vec2 c = clamp(texel, vec2(0.0), g_texSize - vec2(1.0));
    return texture(sampler, (c + vec2(0.5)) / g_texSize);
}

float fetchChannel(vec2 texel, int channel)
{
    vec4 t = fetchTexel(texel);
    if (channel == 0) {
        return t.r;
    }
    if (channel == 1) {
        return t.g;
    }
    return t.b;
}

// Catmull-Rom cubic Hermite weights for the 4 taps at offsets -1, 0, 1, 2.
vec4 cubicWeights(float t)
{
    float t2 = t * t;
    float t3 = t2 * t;
    return vec4(-0.5 * t3 +       t2 - 0.5 * t,
                 1.5 * t3 - 2.5 * t2 + 1.0,
                -1.5 * t3 + 2.0 * t2 + 0.5 * t,
                 0.5 * t3 - 0.5 * t2);
}

// Sub-pixel shift via 4x4 Catmull-Rom: preserves edge contrast far better than
// bilinear, at the cost of very slight ring/overshoot on extreme-contrast edges.
float sampleBicubic(vec2 pos, int channel)
{
    vec2 tc = pos - vec2(0.5);
    vec2 ip = floor(tc);
    vec2 f = tc - ip;
    vec4 wx = cubicWeights(f.x);
    vec4 wy = cubicWeights(f.y);
    float result = 0.0;
    for (int j = 0; j < 4; ++j) {
        float row = 0.0;
        for (int i = 0; i < 4; ++i) {
            row += wx[i] * fetchChannel(ip + vec2(float(i) - 1.0, float(j) - 1.0), channel);
        }
        result += wy[j] * row;
    }
    return result;
}

// Fallback: plain bilinear — softer, no ring risk at all.
float sampleBilinear(vec2 pos, int channel)
{
    vec2 tc = pos - vec2(0.5);
    vec2 ip = floor(tc);
    vec2 f = tc - ip;
    float c00 = fetchChannel(ip + vec2(0.0, 0.0), channel);
    float c10 = fetchChannel(ip + vec2(1.0, 0.0), channel);
    float c01 = fetchChannel(ip + vec2(0.0, 1.0), channel);
    float c11 = fetchChannel(ip + vec2(1.0, 1.0), channel);
    return mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
}

// 5-tap Laplacian at a texel, kept SPLIT into its two axis components. The
// same five taps as the summed form; separating them is what lets the MTF
// compensation below be applied per axis, which is what a separable resample
// actually needs.
vec2 lapAxes(vec2 pos, int channel)
{
    vec2 p = floor(pos);
    float c = fetchChannel(p, channel);
    return vec2(fetchChannel(p + vec2(-1.0,  0.0), channel)
              + fetchChannel(p + vec2( 1.0,  0.0), channel),
                fetchChannel(p + vec2( 0.0, -1.0), channel)
              + fetchChannel(p + vec2( 0.0,  1.0), channel)) - 2.0 * c;
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

    // texcoord0's y axis points up the screen; the UI convention is +Y = down.
    const vec2 axisFlip = vec2(1.0, -1.0);
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

    // Every shift splits into an exact integer texel step plus a fraction in
    // [-0.5, 0.5]. The integer part is a plain texel offset: exact, free, no MTF
    // loss at any magnitude. Only the fraction is ever filtered, so only the
    // fraction feeds the compensation term below.
    vec2 fR = sR - floor(sR + vec2(0.5));
    vec2 fG = sG - floor(sG + vec2(0.5));
    vec2 fB = sB - floor(sB + vec2(0.5));
    vec2 pR = pos - sR;
    vec2 pG = pos - sG;
    vec2 pB = pos - sB;

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
    // `amt` stays isotropic: equal in both components, dot() below reproduces
    // the summed Laplacian exactly, so Sharpen behaves as it always did.
    vec2 kR = min(ca * fR * fR, 0.35) + amt * 0.25;
    vec2 kG = min(ca * fG * fG, 0.35) + amt * 0.25;
    vec2 kB = min(ca * fB * fB, 0.35) + amt * 0.25;

    float r = sharp ? sampleBicubic(pR, 0) : sampleBilinear(pR, 0);
    float g = sharp ? sampleBicubic(pG, 1) : sampleBilinear(pG, 1);
    float b = sharp ? sampleBicubic(pB, 2) : sampleBilinear(pB, 2);

    // Alpha is never shifted: displacing the window's own coverage would fray
    // its rounded corners and its shadow. RGB stays premultiplied, so it clamps
    // to alpha rather than to 1.
    float a = fetchTexel(floor(pos)).a;
    r = clamp(r - dot(kR, lapAxes(pR, 0)), 0.0, a);
    g = clamp(g - dot(kG, lapAxes(pG, 1)), 0.0, a);
    b = clamp(b - dot(kB, lapAxes(pB, 2)), 0.0, a);

    // Same tail as KWin's base.frag, in the same order. The resample above
    // deliberately runs in the source encoding (roughly perceptual), not in
    // linear light: shifting in linear light re-weights the taps toward the
    // bright side of every edge, which is exactly the stroke-weight change this
    // effect must not introduce.
    vec4 result = vec4(r, g, b, a);
    result = encodingToNits(result, sourceNamedTransferFunction, sourceTransferFunctionParams.x, sourceTransferFunctionParams.y);
    result.rgb = (colorimetryTransform * vec4(result.rgb, 1.0)).rgb;
    result = adjustSaturation(result);
    result *= modulation;
    result.rgb = doTonemapping(result.rgb);
    fragColor = nitsToDestinationEncoding(result);
}
