// Stand-in for KWin's colormanagement.glsl.
//
// The real file is compiled into KWin and is not shipped as a resource, so the
// harness cannot resolve it. This stub keeps only what the measurement needs:
// a real transfer function, so that the linear-light path decodes to light
// exactly as KWin does it, and nothing else.
//
// - encodingToNits() is KWin's sRGB branch (transfer function 0), with the
//   same premultiplied-alpha handling, a scale of 1 and an offset of 0. So
//   "nits" here is relative light, 0 .. 1.
// - nitsToDestinationEncoding() is its exact inverse, so the image leaves the
//   shader in the encoding it came in with. On the source-encoding path the
//   tail is a round trip, which leaves every value as it was to float
//   precision. The harness measures that path in the encoding, as before.
// - Colorimetry and tone mapping are identity: both run after the resample,
//   and a matrix commutes with it.
//
// Declared const rather than uniform so they optimise out and need no upload.

const int sourceNamedTransferFunction = 0;
const vec2 sourceTransferFunctionParams = vec2(0.0, 1.0);
const mat4 colorimetryTransform = mat4(1.0);

vec3 stubSrgbToLinear(vec3 color)
{
    vec3 lo = color / 12.92;
    vec3 hi = pow((color + 0.055) / 1.055, vec3(2.4));
    return mix(hi, lo, step(color, vec3(0.04045)));
}

vec3 stubLinearToSrgb(vec3 color)
{
    vec3 lo = color * 12.92;
    vec3 hi = pow(max(color, vec3(0.0)), vec3(1.0 / 2.4)) * 1.055 - 0.055;
    return mix(hi, lo, step(color, vec3(0.0031308)));
}

vec4 encodingToNits(vec4 color, int tf, float luminanceOffset, float luminanceScale)
{
    color.rgb /= max(color.a, 0.001);
    color.rgb = stubSrgbToLinear(color.rgb) * luminanceScale + vec3(luminanceOffset);
    color.rgb *= color.a;
    return color;
}

vec4 nitsToDestinationEncoding(vec4 color)
{
    color.rgb /= max(color.a, 0.001);
    color.rgb = stubLinearToSrgb(color.rgb);
    color.rgb *= color.a;
    return color;
}

vec3 doTonemapping(vec3 color)
{
    return color;
}
