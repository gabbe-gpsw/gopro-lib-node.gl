from pynodegl import Quad, Texture, Shader, TexturedShape, Media, Camera, Group, GLState
from pynodegl import Scale, Rotate, Translate
from pynodegl import UniformSampler, UniformVec4, AttributeVec2
from pynodegl import AnimKeyFrameScalar, AnimKeyFrameVec3

from pynodegl_utils.misc import scene

from OpenGL import GL

frag_data = """
uniform vec4 blend_color;
void main(void)
{
    gl_FragColor = blend_color;
}"""

@scene({'name': 'color', 'type': 'color'},
       {'name': 'width',  'type': 'range', 'range': [0.01,1], 'unit_base': 100},
       {'name': 'height', 'type': 'range', 'range': [0.01,1], 'unit_base': 100},
       {'name': 'translate', 'type': 'bool'},
       {'name': 'translate_x', 'type': 'range', 'range': [-1,1], 'unit_base': 100},
       {'name': 'translate_y', 'type': 'range', 'range': [-1,1], 'unit_base': 100},
       {'name': 'scale', 'type': 'bool'},
       {'name': 'scale_x', 'type': 'range', 'range': [0.01,2], 'unit_base': 100},
       {'name': 'scale_y', 'type': 'range', 'range': [0.01,2], 'unit_base': 100},
       {'name': 'rotate', 'type': 'bool'},
       {'name': 'rotate_deg', 'type': 'range', 'range': [0,360], 'unit_base': 10})
def static(cfg, color=(0.5, 0.0, 1.0, 1.0), width=0.5, height=0.5,
           translate=False, translate_x=0, translate_y=0,
           scale=False, scale_x=1, scale_y=1,
           rotate=False, rotate_deg=0):
    q = Quad((-width/2, -height/2., 0), (width, 0, 0), (0, height, 0))
    s = Shader()

    ucolor = UniformVec4("blend_color", value=color)

    s.set_fragment_data(frag_data)
    node = TexturedShape(q, s)
    node.add_uniforms(ucolor)

    if rotate:
        node = Rotate(node, axis=(0,0,1), angle=rotate_deg)

    if scale:
        node = Scale(node, factors=(scale_x, scale_y, 0))

    if translate:
        node = Translate(node, vector=(translate_x, translate_y, 0))

    return node

@scene({'name': 'rotate', 'type': 'bool'},
       {'name': 'scale', 'type': 'bool'},
       {'name': 'translate', 'type': 'bool'})
def animated(cfg, rotate=True, scale=True, translate=True):
    q = Quad((-0.5, -0.5, 0), (1, 0, 0), (0, 1, 0))
    m = Media(cfg.media_filename)
    t = Texture(data_src=m)
    s = Shader()
    node = TexturedShape(q, s, t)

    if rotate:
        node = Rotate(node, axis=(0,0,1))
        node.add_animkf(AnimKeyFrameScalar(0,  0,   "exp_in"),
                        AnimKeyFrameScalar(cfg.duration, 360))

    if scale:
        node = Scale(node)
        node.add_animkf(AnimKeyFrameVec3(0, (16/9., 0.5, 1.0), "exp_out"),
                        AnimKeyFrameVec3(cfg.duration, (4/3.,  1.0, 0.5)))

    if translate:
        node = Translate(node)
        node.add_animkf(AnimKeyFrameVec3(0,              (-0.5,  0.5, -0.7), "circular_in"),
                        AnimKeyFrameVec3(cfg.duration/2, ( 0.5, -0.5,  0.7), "sinus_in_out:0:.7"),
                        AnimKeyFrameVec3(cfg.duration,   (-0.5, -0.3, -0.5)))

    return node

@scene()
def animated_camera(cfg):
    g = Group()
    g.add_glstates(GLState(GL.GL_DEPTH_TEST, GL.GL_TRUE))

    q = Quad((-0.5, -0.5, 0), (1, 0, 0), (0, 1, 0))
    m = Media(cfg.media_filename)
    t = Texture(data_src=m)
    s = Shader()
    node = TexturedShape(q, s, t)
    g.add_children(node)

    z = -1
    q = Quad((-1.1, 0.3, z), (1, 0, 0), (0, 1, 0))
    node = TexturedShape(q, s, t)
    g.add_children(node)

    q = Quad((0.1, 0.3, z), (1, 0, 0), (0, 1, 0))
    node = TexturedShape(q, s, t)
    g.add_children(node)

    q = Quad((-1.1, -1.0, z), (1, 0, 0), (0, 1, 0))
    node = TexturedShape(q, s, t)
    g.add_children(node)

    q = Quad((0.1, -1.0, z), (1, 0, 0), (0, 1, 0))
    node = TexturedShape(q, s, t)
    g.add_children(node)

    camera = Camera(g)
    camera.set_eye(0, 0, 2)
    camera.set_center(0.0, 0.0, 0.0)
    camera.set_up(0.0, 1.0, 0.0)
    camera.set_perspective(45.0, 16.0/9.0, 0.1, 10.0)
    camera.add_eye_animkf(
            AnimKeyFrameVec3(0, (0, 0, 0.2), "exp_out"),
            AnimKeyFrameVec3(10, (0, 0, 3)))

    camera.add_fov_animkf(
            AnimKeyFrameScalar(0.5, 60.0, "exp_out"),
            AnimKeyFrameScalar(cfg.duration, 45.0))

    return camera
