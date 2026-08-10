// Identity stand-in for KWin's saturation.glsl. See colormanagement.glsl in this
// directory for why stubbing the tail is faithful to what is under test.

vec4 adjustSaturation(vec4 color)
{
    return color;
}
