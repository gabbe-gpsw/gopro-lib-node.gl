#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(std140, binding = 2) uniform block {
    float  factor0;
    float  factor1;
    vec4   color0;
} z;

layout(std430, binding = 3) readonly buffer block2 {
    float  factor2;
} z2;

layout(location = 0) out vec4 output_color;

void main() {
    output_color = z.color0 * z.factor0 * z.factor1 * z2.factor2;
}
