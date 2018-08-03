#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 out_color;
layout(location = 0) in vec2 tex_coord;

layout(binding = 0, std140) uniform ngl {
    mat4 ngl_modelview_matrix;
    mat4 ngl_projection_matrix;
    float tex0_ts;
};

layout(binding = 1) uniform sampler2D tex0_sampler;

void main() {
    out_color = texture(tex0_sampler, tex_coord);
}
