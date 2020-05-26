ngl_out vec2 var_uvcoord;
ngl_out vec2 var_tex0_coord;
ngl_out vec3 var_normal;
void main()
{
    ngl_out_pos = ngl_projection_matrix * ngl_modelview_matrix * ngl_position;
    var_uvcoord = ngl_uvcoord;
    var_normal = ngl_normal_matrix * ngl_normal;
    var_tex0_coord = (tex0_coord_matrix * vec4(ngl_uvcoord, 0.0, 1.0)).xy;
}
