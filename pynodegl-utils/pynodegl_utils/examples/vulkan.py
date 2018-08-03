import os.path as op
import array
import colorsys
import math
import random
import pynodegl as ngl
from pynodegl_utils.misc import scene


@scene()
def vkquad(cfg):
    cfg.duration = 4
    program = ngl.Program(
        fragment=cfg.get_frag('vkquad'),
        vertex=cfg.get_vert('vkquad')
    )
    quad = ngl.Quad()
    render = ngl.Render(quad, program)
    return render


@scene()
def vktest(cfg):
    cfg.duration = 4

    colors_data = array.array('f', [1.0, 0.0, 0.0,
                                    0.0, 1.0, 0.0,
                                    0.0, 0.0, 1.0,
                                    1.0, 1.0, 1.0])

    vertices_data = array.array('f', [-0.5, -0.5, 0.0,
                                       0.5, -0.5, 0.0,
                                       0.5,  0.5, 0.0,
                                      -0.5,  0.5, 0.0])

    indices_data = array.array('i', [0, 1, 2, 2, 3, 0])

    colors_buffer = ngl.BufferVec3(data=colors_data)
    vertices_buffer = ngl.BufferVec3(data=vertices_data)
    indices_buffer = ngl.BufferUInt(data=indices_data)

    geometry = ngl.Geometry(vertices_buffer, indices=indices_buffer)
    program = ngl.Program(fragment=cfg.get_frag('vktest'),
                        vertex=cfg.get_vert('vktest'))
    render = ngl.Render(geometry, program)
    render.update_attributes(color=colors_buffer)

    return render


@scene(color2=scene.Color(),
        factor0=scene.Range(range=[0,1], unit_base=100),
        factor1=scene.Range(range=[0,1], unit_base=100))
def vkblending(cfg, color2=(1.0, 0.5, 0.0, 0.5), factor0=1.0, factor1=1.0):
    cfg.duration=5
    geometry = ngl.Quad()
    program = ngl.Program(fragment=cfg.get_frag('vkuniform'),
                          vertex=cfg.get_vert('vkuniform'))
    render = ngl.Render(geometry, program)
    render.update_uniforms(
            color2=ngl.UniformVec4(value=color2),
            factor0=ngl.UniformFloat(value=factor0),
            factor1=ngl.UniformFloat(value=factor1),
    )
    animkf = [ngl.AnimKeyFrameFloat(0, 0),
              ngl.AnimKeyFrameFloat(  cfg.duration/3.,   -360/3., 'exp_in_out'),
              ngl.AnimKeyFrameFloat(2*cfg.duration/3., -2*360/3., 'exp_in_out'),
              ngl.AnimKeyFrameFloat(  cfg.duration,      -360,    'exp_in_out')]
    render = ngl.Rotate(render, anim=ngl.AnimatedFloat(animkf))


    group = ngl.Group()

    group.add_children(ngl.Translate(render, (-0.5, 0.0, 0.0)))


    render2 = ngl.Render(geometry, program)
    render2.update_uniforms(
            color2=ngl.UniformVec4(value=(1.0, 1.0, 1.0, 0.5)),
            factor0=ngl.UniformFloat(value=factor0),
            factor1=ngl.UniformFloat(value=factor1),
    )
    blended = ngl.GraphicConfig(render2,
                          blend=True,
                          blend_src_factor='src_alpha',
                          blend_dst_factor='one_minus_src_alpha',
                          blend_src_factor_a='zero',
                          blend_dst_factor_a='one')

    group.add_children(ngl.Translate(render, (0.5, 0.0, -0.2)))
    group.add_children(ngl.Translate(blended, (-0, 0.0, -0.1)))

    group = ngl.Rotate(group, angle=45, axis=(0, 1, 0))

    group = ngl.GraphicConfig(group, depth_test=True)

    camera = ngl.Camera(group)
    camera.set_eye(0.0, 0.0, 3.0)
    camera.set_center(0.0, 0.0, 0.0)
    camera.set_up(0.0, 1.0, 0.0)
    camera.set_perspective(45.0, cfg.aspect_ratio_float)
    camera.set_clipping(1.0, 10.0)
    return camera


@scene(color2=scene.Color(),
        factor0=scene.Range(range=[0,1], unit_base=100),
        factor1=scene.Range(range=[0,1], unit_base=100))
def vkuniform(cfg, color2=(1.0, 0.0, 1.0, 1.0), factor0=1.0, factor1=1.0):
    geometry = ngl.Quad()
    program = ngl.Program(fragment=cfg.get_frag('vkuniform'),
                          vertex=cfg.get_vert('vkuniform'))
    render = ngl.Render(geometry, program)
    render.update_uniforms(
            color2=ngl.UniformVec4(value=color2),
            factor0=ngl.UniformFloat(value=factor0),
            factor1=ngl.UniformFloat(value=factor1),
    )

    return render


@scene(color0=scene.Color(),
       factor0=scene.Range(range=[0,1], unit_base=100),
       factor1=scene.Range(range=[0,1], unit_base=100))
def vkubo(cfg, color0=(1.0, 0.0, 1.0, 1.0), factor0=1.0, factor1=1.0):

    block = ngl.Block(fields=[
            ngl.UniformFloat(value=factor0),
            ngl.UniformFloat(value=factor1),
            ngl.UniformVec4(value=color0),
            ], layout='std140')

    block2 = ngl.Block(fields=[
            ngl.UniformFloat(value=1.0),
            ], layout='std430')

    geometry = ngl.Quad()
    program = ngl.Program(fragment=cfg.get_frag('vkubo'),
                          vertex=cfg.get_vert('vkubo'))
    render = ngl.Render(geometry, program)

    render.update_blocks(block=block, block2=block2)
    return render


@scene()
def vktexture_media(cfg):
    group = ngl.Group()

    m0 = cfg.medias[0]
    cfg.duration = m0.duration
    cfg.aspect_ratio = (m0.width, m0.height)

    program = ngl.Program(fragment=cfg.get_frag('vktexture'),
                          vertex=cfg.get_vert('vktexture'))
    quad = ngl.Quad((-1, -1, 0), (1, 0, 0), (0, 1, 0))
    render = ngl.Render(quad, program)
    media = ngl.Media(m0.filename)
    texture = ngl.Texture2D(data_src=media)
    render.update_textures(tex0=texture)
    #group.add_children(render)

    program = ngl.Program(fragment=cfg.get_frag('vktexture'),
                          vertex=cfg.get_vert('vktexture'))
    quad = ngl.Quad((-1, -1, 0), (2, 0, 0), (0, 2, 0))
    render = ngl.Render(quad, program)
    media = ngl.Media(m0.filename)
    texture = ngl.Texture2D(data_src=media, min_filter='nearest', mipmap_filter='linear')
    render.update_textures(tex0=texture)
    group.add_children(render)

    return group


@scene()
def vktexture_buffer(cfg):
    m0 = cfg.medias[0]
    cfg.duration = m0.duration
    cfg.aspect_ratio = (m0.width, m0.height)

    program = ngl.Program(fragment=cfg.get_frag('vktexture'),
                          vertex=cfg.get_vert('vktexture'))
    quad = ngl.Quad((-1, -1, 0), (2, 0, 0), (0, 2, 0))
    render = ngl.Render(quad, program)

    # Credits: https://icons8.com/icon/40514/dove
    icon_filename = op.join(op.dirname(__file__), 'data', 'icons8-dove.raw')
    cfg.files.append(icon_filename)
    w, h = (96, 96)
    cfg.aspect_ratio = (w, h)

    img_buf = ngl.BufferUBVec4(filename=icon_filename, label='icon raw buffer')
    texture = ngl.Texture2D(data_src=img_buf, width=w, height=h, mipmap_filter='linear')
    render.update_textures(tex0=texture)

    return render


@scene()
def vkparticules(cfg, nb_particles=2048, nb_thread=16):
    random.seed(0)

    cfg.duration = 6
    group_size = nb_particles / nb_thread

    input_positions = array.array('f')
    input_velocity = array.array('f')

    for i in range(nb_particles):
        input_positions.extend([
            random.uniform(-1.0, 1.0),
            random.uniform(-1.0, 0.0),
            0.0,
        ])

        input_velocity.extend([
            random.uniform(-0.02, 0.02),
            random.uniform(-0.05, 0.05),
        ])

    input_parameters = ngl.Block(fields=[
        ngl.BufferVec3(data=input_positions),
        ngl.BufferVec2(data=input_velocity)], layout='std430')

    animkf = [ngl.AnimKeyFrameFloat(0, 0),
              ngl.AnimKeyFrameFloat(cfg.duration, 1)]
    time = ngl.AnimatedFloat(animkf)
    duration = ngl.UniformFloat(cfg.duration)
    compute_program = ngl.ComputeProgram(compute=cfg.get_comp('vkparticules'))

    compute = ngl.Compute(nb_group_x=group_size, nb_group_y=group_size, nb_group_z=1, program=compute_program)
    compute.update_uniforms(
        time=time,
        duration=duration,
    )

    comp_result = ngl.Block(fields=[ngl.BufferVec3(nb_particles, label='positions')], layout='std430', label='comp result')
    positions_ref = ngl.BufferVec3(block=comp_result, block_field=0)

    compute.update_blocks(
        Parameters=input_parameters,
        Output=comp_result
    )

    quad_width = 0.01
    quad = ngl.Quad(
        corner=(-quad_width/2, -quad_width/2, 0),
        width=(quad_width, 0, 0),
        height=(0, quad_width, 0)
    )

    program = ngl.Program(vertex=cfg.get_vert('vkparticules'), fragment=cfg.get_frag('color'))
    render = ngl.Render(quad, program, nb_instances=nb_particles)
    render.update_uniforms(color=ngl.UniformVec4(value=(0, .6, .8, 0.9)))
    render.update_instance_attributes(position=positions_ref)

    group = ngl.Group()
    group.add_children(compute, render)

    return ngl.Camera(group)


@scene()
def vkimgconv(cfg, nb_thread=16):
    info = cfg.medias[0]
    cfg.duration = info.duration
    group_size = info.width

    media = ngl.Media(info.filename)
    input0 = ngl.Texture2D(data_src=media)
    output0 = ngl.Texture2D(width=info.width, height=info.height)
    compute_program = ngl.ComputeProgram(compute=cfg.get_comp('vkimgconv'))

    compute = ngl.Compute(nb_group_x=group_size, nb_group_y=group_size, nb_group_z=1, program=compute_program)
    compute.update_textures(src=input0, dst=output0)

    quad = ngl.Quad((-1, -1, 0), (2, 0, 0), (0, 2, 0))
    program = ngl.Program(fragment=cfg.get_frag('vktexture'), vertex=cfg.get_vert('vktexture'))
    render = ngl.Render(quad, program)
    render.update_textures(tex0=output0)

    group = ngl.Group()
    group.add_children(compute, render)

    return ngl.Camera(group)


@scene()
def vkrtt(cfg):
    info = cfg.medias[0]
    cfg.duration = info.duration

    media = ngl.Media(info.filename)
    input0 = ngl.Texture2D(data_src=media)
    output0 = ngl.Texture2D(width=1920, height=1080, min_filter='linear', mipmap_filter='linear')

    quad = ngl.Quad((-1, -1, 0), (2, 0, 0), (0, 2, 0))
    program = ngl.Program(fragment=cfg.get_frag('vktexture'), vertex=cfg.get_vert('vktexture'))
    render = ngl.Render(quad, program)
    render.update_textures(tex0=input0)

    render = ngl.GraphicConfig(render, depth_test=True, scissor_test=True, scissor=(0, 0, 720, 1080))
    rtt = ngl.RenderToTexture(render, [output0], features='depth')

    quad = ngl.Quad((-1, -1, 0), (2, 0, 0), (0, 2, 0))
    program = ngl.Program(fragment=cfg.get_frag('vktexture'), vertex=cfg.get_vert('vktexture'))
    render = ngl.Render(quad, program)
    render.update_textures(tex0=output0)

    group = ngl.Group()
    group.add_children(rtt, render)

    return ngl.Camera(group)


@scene()
def rt_test(cfg):
    m0 = cfg.medias[0]
    cfg.duration = m0.duration
    cfg.aspect_ratio = (m0.width, m0.height)

    program = ngl.Program(fragment=cfg.get_frag('vktexture'),
                          vertex=cfg.get_vert('vktexture'))
    quad = ngl.Quad((-1, -1, 0), (2, 0, 0), (0, 2, 0))
    render = ngl.Render(quad, program)

    # Credits: https://icons8.com/icon/40514/dove
    icon_filename = op.join(op.dirname(__file__), 'data', 'icons8-dove.raw')
    cfg.files.append(icon_filename)
    w, h = (96, 96)
    cfg.aspect_ratio = (w, h)

    img_buf = ngl.BufferUBVec4(filename=icon_filename, label='icon raw buffer')
    texture = ngl.Texture2D(data_src=img_buf, width=w, height=h)
    render.update_textures(tex0=texture)

    output0 = ngl.Texture2D(width=1920, height=1080)

    rtt = ngl.RenderToTexture(render, [output0], features='depth')
    quad = ngl.Quad((-1, -1, 0), (2, 0, 0), (0, 2, 0))
    program = ngl.Program(fragment=cfg.get_frag('vktexture'), vertex=cfg.get_vert('vktexture'))
    render = ngl.Render(quad, program)
    render.update_textures(tex0=output0)

    render = ngl.GraphicConfig(render,
                               blend=True,
                               blend_src_factor='src_alpha',
                               blend_dst_factor='one_minus_src_alpha',
                               blend_src_factor_a='zero',
                               blend_dst_factor_a='one')

    program = ngl.Program(fragment=cfg.get_frag('vktexture'),
                          vertex=cfg.get_vert('vktexture'))
    quad = ngl.Quad((-1, -1, 0.1), (1, 0, 0.1), (0, 1, 0.1))
    prev_render = ngl.Render(quad, program)
    media = ngl.Media(m0.filename)
    texture = ngl.Texture2D(data_src=media)
    prev_render.update_textures(tex0=texture)



    return ngl.Group(children=(prev_render,rtt,render))
