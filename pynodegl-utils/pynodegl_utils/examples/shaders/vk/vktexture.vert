#version 450
#extension GL_ARB_separate_shader_objects : enable

out gl_PerVertex {
    vec4 gl_Position;
};

layout(location = 0) in vec3 ngl_position;
layout(location = 1) in vec2 ngl_uvcoord;
layout(location = 0) out vec2 tex_coord;

layout(binding = 0, std140) uniform ngl {
    mat4 ngl_modelview_matrix;
    mat4 ngl_projection_matrix;
    float tex0_ts;
};

void main()
{
    gl_Position = ngl_modelview_matrix * ngl_projection_matrix * vec4(ngl_position, 1.0);
    tex_coord = ngl_uvcoord;
}
