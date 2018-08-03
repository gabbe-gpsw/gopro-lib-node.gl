#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 output_color;

layout(binding = 0, std140) uniform ngl {
    mat4 ngl_modelview_matrix;
    mat4 ngl_projection_matrix;
    vec4 color2;
    float factor0;
    float factor1;
};

void main() {
    output_color = color2 * factor0 * factor1;
}
