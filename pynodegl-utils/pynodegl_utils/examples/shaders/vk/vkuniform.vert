#version 450
#extension GL_ARB_separate_shader_objects : enable

out gl_PerVertex {
    vec4 gl_Position;
};

layout(location = 0) in vec3 ngl_position;

layout(binding = 0, std140) uniform ngl {
    mat4 ngl_modelview_matrix;
    mat4 ngl_projection_matrix;
    vec4 color2;
    float factor0;
    float factor1;
};

void main()
{
    gl_Position = ngl_projection_matrix * ngl_modelview_matrix * vec4(ngl_position, 1.0);
}
