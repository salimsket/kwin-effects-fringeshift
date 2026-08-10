// Identity stand-in for KWin's colormanagement.glsl.
//
// The real file is compiled into KWin and is not shipped as a resource, so the
// harness cannot resolve it. Stubbing it out is not an approximation of the
// thing under test: fringeshift.frag deliberately performs the whole resample in
// the SOURCE encoding, before this tail runs (see the comment above the tail in
// fringeshift.frag — resampling in linear light re-weights the taps toward the
// bright side of every edge, which is the stroke-weight change the effect exists
// to avoid). With the tail as identity, what the harness measures is exactly the
// correction, in the space the correction is defined in.
//
// Declared const rather than uniform so they optimise out and need no upload.

const int sourceNamedTransferFunction = 0;
const vec2 sourceTransferFunctionParams = vec2(0.0, 1.0);
const mat4 colorimetryTransform = mat4(1.0);

vec4 encodingToNits(vec4 color, int tf, float p0, float p1)
{
    return color;
}

vec4 nitsToDestinationEncoding(vec4 color)
{
    return color;
}

vec3 doTonemapping(vec3 color)
{
    return color;
}
