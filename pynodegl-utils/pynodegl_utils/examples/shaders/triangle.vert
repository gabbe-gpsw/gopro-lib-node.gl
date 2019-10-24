ngl_out vec2 var_uvcoord;
ngl_out vec3 var_normal;
ngl_out vec4 var_color;

void main()
{
    gl_Position = ngl_projection_matrix * ngl_modelview_matrix * ngl_position;
    var_uvcoord = ngl_uvcoord;
    var_normal = ngl_normal_matrix * ngl_normal;
    var_color = edge_color;
}
