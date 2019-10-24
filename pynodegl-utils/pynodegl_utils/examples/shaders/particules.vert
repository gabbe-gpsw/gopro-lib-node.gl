void main()
{
    vec4 position = ngl_position + vec4(positions.data[gl_InstanceID], 0.0);
    gl_Position = ngl_projection_matrix * ngl_modelview_matrix * position;
}
