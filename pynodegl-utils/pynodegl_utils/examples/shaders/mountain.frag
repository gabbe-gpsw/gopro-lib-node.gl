#version 100

precision mediump float;
varying vec2 var_tex0_coord;
uniform sampler2D tex0_sampler;
uniform int dim;
uniform int nb_layers;
uniform float time;

uniform float gain;
uniform float lacunarity;
uniform vec4 color0;
uniform vec4 color1;
uniform float yoffset;
uniform float hscale;

float f(float t)
{
    // 6t^5 - 15t^4 + 10t^3 (new Perlin)
    return ((6.0*t - 15.0)*t + 10.0)*t*t*t;
}

float pick1d(float pos, float off)
{
    float value_point = fract(pos + off);
    float value = texture2D(tex0_sampler, vec2(value_point, 0)).x;
    return value;
}

float noise1d(float pos)
{
    float d = float(dim);
    float s = 1.0 / d;
    float v0 = pick1d(pos, 0.0 * s);
    float v1 = pick1d(pos, 1.0 * s);
    float t = pos*d - floor(pos*d);
    float tx = f(t);
    float nx = mix(v0, v1, tx);
    return nx;
}

void main(void)
{
    float sum = 0.0;
    float max_amp = 0.0;
    float freq = 1.0;
    float amp = 1.0;
    float pos = var_tex0_coord.x/2.0 + time;
    for (int i = 0; i < nb_layers; i++) {
        float nval = noise1d(pos * freq) * amp;
        sum += nval;
        max_amp += amp;
        freq *= lacunarity;
        amp *= gain;
    }
    float n = sum / max_amp;
    float h = n*hscale + yoffset;
    float cmix = step(1.0 - var_tex0_coord.y, h);
    vec4 color = mix(color0, color1, cmix);

    gl_FragColor = color;
}
