/*
 * Copyright (c) 2021, Jesse Buhagiar <jooster669@gmail.com>
 * Copyright (c) 2021, Stephan Unverwerth <s.unverwerth@serenityos.org>
 * Copyright (c) 2022, Jelle Raaijmakers <jelle@gmta.nl>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Debug.h>
#include <LibGL/GLContext.h>
#include <LibGL/Image.h>
#include <LibGPU/ImageDataLayout.h>

namespace GL {

void GLContext::gl_active_texture(GLenum texture)
{
    RETURN_WITH_ERROR_IF(texture < GL_TEXTURE0 || texture >= GL_TEXTURE0 + m_device_info.num_texture_units, GL_INVALID_ENUM);

    m_active_texture_unit_index = texture - GL_TEXTURE0;
    m_active_texture_unit = &m_texture_units.at(m_active_texture_unit_index);
}

void GLContext::gl_bind_texture(GLenum target, GLuint texture)
{
    RETURN_WITH_ERROR_IF(m_in_draw_state, GL_INVALID_OPERATION);
    RETURN_WITH_ERROR_IF(target != GL_TEXTURE_1D
            && target != GL_TEXTURE_2D
            && target != GL_TEXTURE_3D
            && target != GL_TEXTURE_1D_ARRAY
            && target != GL_TEXTURE_2D_ARRAY
            && target != GL_TEXTURE_CUBE_MAP,
        GL_INVALID_ENUM);

    // FIXME: We only support GL_TEXTURE_2D for now
    if (target != GL_TEXTURE_2D) {
        dbgln("gl_bind_texture(target = {:#x}): currently only GL_TEXTURE_2D is supported", target);
        return;
    }

    RefPtr<Texture2D> texture_2d;

    if (texture == 0) {
        // Texture name 0 refers to the default texture
        texture_2d = get_default_texture<Texture2D>(target);
    } else {
        // Find this texture name in our previously allocated textures
        auto it = m_allocated_textures.find(texture);
        if (it != m_allocated_textures.end()) {
            auto texture_object = it->value;
            if (!texture_object.is_null()) {
                // Texture must have been created with the same target
                RETURN_WITH_ERROR_IF(!texture_object->is_texture_2d(), GL_INVALID_OPERATION);
                texture_2d = static_cast<Texture2D*>(texture_object.ptr());
            }
        }

        // OpenGL 1.x supports binding texture names that were not previously generated by glGenTextures.
        // If there is not an allocated texture, meaning it was not previously generated by glGenTextures,
        // we can keep texture_object null to both allocate and bind the texture with the passed in texture name.
        // FIXME: Later OpenGL versions such as 4.x enforce that texture names being bound were previously generated
        //        by glGenTextures.
        if (!texture_2d) {
            texture_2d = adopt_ref(*new Texture2D());
            m_allocated_textures.set(texture, texture_2d);
        }
    }

    m_active_texture_unit->set_texture_2d_target_texture(texture_2d);
    m_sampler_config_is_dirty = true;
}

void GLContext::gl_client_active_texture(GLenum target)
{
    RETURN_WITH_ERROR_IF(target < GL_TEXTURE0 || target >= GL_TEXTURE0 + m_device_info.num_texture_units, GL_INVALID_ENUM);

    m_client_active_texture = target - GL_TEXTURE0;
}

void GLContext::gl_copy_tex_image_2d(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border)
{
    APPEND_TO_CALL_LIST_AND_RETURN_IF_NEEDED(gl_copy_tex_image_2d, target, level, internalformat, x, y, width, height, border);
    RETURN_WITH_ERROR_IF(m_in_draw_state, GL_INVALID_OPERATION);

    RETURN_WITH_ERROR_IF(internalformat == GL_NONE, GL_INVALID_ENUM);
    auto pixel_type_or_error = get_validated_pixel_type(target, internalformat, GL_NONE, GL_NONE);
    RETURN_WITH_ERROR_IF(pixel_type_or_error.is_error(), pixel_type_or_error.release_error().code());

    RETURN_WITH_ERROR_IF(level < 0 || level > Texture2D::LOG2_MAX_TEXTURE_SIZE, GL_INVALID_VALUE);
    RETURN_WITH_ERROR_IF(width < 0 || height < 0 || width > (2 + Texture2D::MAX_TEXTURE_SIZE) || height > (2 + Texture2D::MAX_TEXTURE_SIZE), GL_INVALID_VALUE);
    if (!m_device_info.supports_npot_textures)
        RETURN_WITH_ERROR_IF(!is_power_of_two(width) || !is_power_of_two(height), GL_INVALID_VALUE);
    RETURN_WITH_ERROR_IF(border != 0, GL_INVALID_VALUE);

    auto texture_2d = m_active_texture_unit->texture_2d_target_texture();
    VERIFY(!texture_2d.is_null());

    auto internal_pixel_format = pixel_format_for_internal_format(internalformat);
    if (level == 0) {
        texture_2d->set_device_image(m_rasterizer->create_image(internal_pixel_format, width, height, 1, Texture2D::LOG2_MAX_TEXTURE_SIZE));
        m_sampler_config_is_dirty = true;
    }

    auto pixel_type = pixel_type_or_error.release_value();
    if (pixel_type.format == GPU::PixelFormat::DepthComponent) {
        m_rasterizer->blit_from_depth_buffer(
            *texture_2d->device_image(),
            level,
            { static_cast<u32>(width), static_cast<u32>(height) },
            { x, y },
            { 0, 0, 0 });
    } else if (pixel_type.format == GPU::PixelFormat::StencilIndex) {
        dbgln("{}: GL_STENCIL_INDEX is not yet supported", __FUNCTION__);
    } else {
        m_rasterizer->blit_from_color_buffer(
            *texture_2d->device_image(),
            level,
            { static_cast<u32>(width), static_cast<u32>(height) },
            { x, y },
            { 0, 0, 0 });
    }
}

void GLContext::gl_copy_tex_sub_image_2d(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height)
{
    APPEND_TO_CALL_LIST_AND_RETURN_IF_NEEDED(gl_copy_tex_sub_image_2d, target, level, xoffset, yoffset, x, y, width, height);
    RETURN_WITH_ERROR_IF(m_in_draw_state, GL_INVALID_OPERATION);

    RETURN_WITH_ERROR_IF(level < 0 || level > Texture2D::LOG2_MAX_TEXTURE_SIZE, GL_INVALID_VALUE);
    RETURN_WITH_ERROR_IF(width < 0 || height < 0 || width > (2 + Texture2D::MAX_TEXTURE_SIZE) || height > (2 + Texture2D::MAX_TEXTURE_SIZE), GL_INVALID_VALUE);

    auto texture_2d = m_active_texture_unit->texture_2d_target_texture();
    VERIFY(!texture_2d.is_null());
    RETURN_WITH_ERROR_IF(texture_2d->device_image().is_null(), GL_INVALID_OPERATION);

    m_rasterizer->blit_from_color_buffer(
        *texture_2d->device_image(),
        level,
        { static_cast<u32>(width), static_cast<u32>(height) },
        { x, y },
        { xoffset, yoffset, 0 });

    // FIXME: use GPU::PixelFormat for Texture2D's internal format
    if (texture_2d->internal_format() == GL_DEPTH_COMPONENT) {
        m_rasterizer->blit_from_depth_buffer(
            *texture_2d->device_image(),
            level,
            { static_cast<u32>(width), static_cast<u32>(height) },
            { x, y },
            { 0, 0, 0 });
    } else if (texture_2d->internal_format() == GL_STENCIL_INDEX) {
        dbgln("{}: GL_STENCIL_INDEX is not yet supported", __FUNCTION__);
    } else {
        m_rasterizer->blit_from_color_buffer(
            *texture_2d->device_image(),
            level,
            { static_cast<u32>(width), static_cast<u32>(height) },
            { x, y },
            { 0, 0, 0 });
    }
}

void GLContext::gl_delete_textures(GLsizei n, GLuint const* textures)
{
    RETURN_WITH_ERROR_IF(n < 0, GL_INVALID_VALUE);
    RETURN_WITH_ERROR_IF(m_in_draw_state, GL_INVALID_OPERATION);

    for (auto i = 0; i < n; i++) {
        GLuint name = textures[i];
        if (name == 0)
            continue;

        auto texture_object = m_allocated_textures.find(name);
        if (texture_object == m_allocated_textures.end() || texture_object->value.is_null())
            continue;

        m_name_allocator.free(name);

        auto texture = texture_object->value;

        // Check all texture units
        for (auto& texture_unit : m_texture_units) {
            if (texture->is_texture_2d() && texture_unit.texture_2d_target_texture() == texture) {
                // If a texture that is currently bound is deleted, the binding reverts to 0 (the default texture)
                texture_unit.set_texture_2d_target_texture(get_default_texture<Texture2D>(GL_TEXTURE_2D));
            }
        }

        m_allocated_textures.remove(name);
    }
}

void GLContext::gl_gen_textures(GLsizei n, GLuint* textures)
{
    RETURN_WITH_ERROR_IF(n < 0, GL_INVALID_VALUE);
    RETURN_WITH_ERROR_IF(m_in_draw_state, GL_INVALID_OPERATION);

    m_name_allocator.allocate(n, textures);

    // Initialize all texture names with a nullptr
    for (auto i = 0; i < n; ++i) {
        GLuint name = textures[i];
        m_allocated_textures.set(name, nullptr);
    }
}

void GLContext::gl_get_tex_image(GLenum target, GLint level, GLenum format, GLenum type, void* pixels)
{
    RETURN_WITH_ERROR_IF(level < 0 || level > Texture2D::LOG2_MAX_TEXTURE_SIZE, GL_INVALID_VALUE);
    RETURN_WITH_ERROR_IF(format == GL_NONE || type == GL_NONE, GL_INVALID_ENUM);
    auto pixel_type_or_error = get_validated_pixel_type(target, GL_NONE, format, type);
    RETURN_WITH_ERROR_IF(pixel_type_or_error.is_error(), pixel_type_or_error.release_error().code());

    auto texture_2d = m_active_texture_unit->texture_2d_target_texture();
    VERIFY(!texture_2d.is_null());

    u32 width = texture_2d->width_at_lod(level);
    u32 height = texture_2d->height_at_lod(level);

    GPU::ImageDataLayout output_layout = {
        .pixel_type = pixel_type_or_error.release_value(),
        .packing = get_packing_specification(PackingType::Pack),
        .dimensions = {
            .width = width,
            .height = height,
            .depth = 1,
        },
        .selection = {
            .width = width,
            .height = height,
            .depth = 1,
        },
    };

    texture_2d->download_texture_data(level, output_layout, pixels);
}

void GLContext::gl_get_tex_parameter_integerv(GLenum target, GLint level, GLenum pname, GLint* params)
{
    RETURN_WITH_ERROR_IF(m_in_draw_state, GL_INVALID_OPERATION);
    // FIXME: support targets other than GL_TEXTURE_2D
    RETURN_WITH_ERROR_IF(target != GL_TEXTURE_2D, GL_INVALID_ENUM);
    // FIXME: support other parameter names
    RETURN_WITH_ERROR_IF(pname < GL_TEXTURE_WIDTH || pname > GL_TEXTURE_HEIGHT, GL_INVALID_ENUM);
    RETURN_WITH_ERROR_IF(level < 0 || level > Texture2D::LOG2_MAX_TEXTURE_SIZE, GL_INVALID_VALUE);
    // FIXME: GL_INVALID_VALUE is generated if target is GL_TEXTURE_BUFFER and level is not zero
    // FIXME: GL_INVALID_OPERATION is generated if GL_TEXTURE_COMPRESSED_IMAGE_SIZE is queried on texture images with an uncompressed internal format or on proxy targets

    VERIFY(!m_active_texture_unit->texture_2d_target_texture().is_null());
    auto const texture_2d = m_active_texture_unit->texture_2d_target_texture();

    switch (pname) {
    case GL_TEXTURE_HEIGHT:
        *params = texture_2d->height_at_lod(level);
        break;
    case GL_TEXTURE_WIDTH:
        *params = texture_2d->width_at_lod(level);
        break;
    }
}

GLboolean GLContext::gl_is_texture(GLuint texture)
{
    RETURN_VALUE_WITH_ERROR_IF(m_in_draw_state, GL_INVALID_OPERATION, GL_FALSE);

    if (texture == 0)
        return GL_FALSE;

    auto it = m_allocated_textures.find(texture);
    if (it == m_allocated_textures.end())
        return GL_FALSE;

    return it->value.is_null() ? GL_FALSE : GL_TRUE;
}

void GLContext::gl_multi_tex_coord(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
    APPEND_TO_CALL_LIST_AND_RETURN_IF_NEEDED(gl_multi_tex_coord, target, s, t, r, q);

    RETURN_WITH_ERROR_IF(target < GL_TEXTURE0 || target >= GL_TEXTURE0 + m_device_info.num_texture_units, GL_INVALID_ENUM);

    m_current_vertex_tex_coord[target - GL_TEXTURE0] = { s, t, r, q };
}

void GLContext::gl_tex_coord(GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
    APPEND_TO_CALL_LIST_AND_RETURN_IF_NEEDED(gl_tex_coord, s, t, r, q);

    m_current_vertex_tex_coord[0] = { s, t, r, q };
}

void GLContext::gl_tex_coord_pointer(GLint size, GLenum type, GLsizei stride, void const* pointer)
{
    RETURN_WITH_ERROR_IF(m_in_draw_state, GL_INVALID_OPERATION);
    RETURN_WITH_ERROR_IF(!(size == 1 || size == 2 || size == 3 || size == 4), GL_INVALID_VALUE);
    RETURN_WITH_ERROR_IF(!(type == GL_SHORT || type == GL_INT || type == GL_FLOAT || type == GL_DOUBLE), GL_INVALID_ENUM);
    RETURN_WITH_ERROR_IF(stride < 0, GL_INVALID_VALUE);

    auto& tex_coord_pointer = m_client_tex_coord_pointer[m_client_active_texture];
    tex_coord_pointer = { .size = size, .type = type, .stride = stride, .pointer = pointer };
}

void GLContext::gl_tex_env(GLenum target, GLenum pname, GLfloat param)
{
    APPEND_TO_CALL_LIST_AND_RETURN_IF_NEEDED(gl_tex_env, target, pname, param);
    RETURN_WITH_ERROR_IF(m_in_draw_state, GL_INVALID_OPERATION);

    RETURN_WITH_ERROR_IF(target != GL_TEXTURE_ENV && target != GL_TEXTURE_FILTER_CONTROL, GL_INVALID_ENUM);
    RETURN_WITH_ERROR_IF(target == GL_TEXTURE_FILTER_CONTROL && pname != GL_TEXTURE_LOD_BIAS, GL_INVALID_ENUM);

    switch (target) {
    case GL_TEXTURE_ENV:
        switch (pname) {
        case GL_ALPHA_SCALE:
            RETURN_WITH_ERROR_IF(param != 1.f && param != 2.f && param != 4.f, GL_INVALID_VALUE);
            m_active_texture_unit->set_alpha_scale(param);
            break;
        case GL_COMBINE_ALPHA: {
            auto param_enum = static_cast<GLenum>(param);
            switch (param_enum) {
            case GL_ADD:
            case GL_ADD_SIGNED:
            case GL_INTERPOLATE:
            case GL_MODULATE:
            case GL_REPLACE:
            case GL_SUBTRACT:
                m_active_texture_unit->set_alpha_combinator(param_enum);
                break;
            default:
                RETURN_WITH_ERROR_IF(true, GL_INVALID_ENUM);
            }
            break;
        }
        case GL_COMBINE_RGB: {
            auto param_enum = static_cast<GLenum>(param);
            switch (param_enum) {
            case GL_ADD:
            case GL_ADD_SIGNED:
            case GL_DOT3_RGB:
            case GL_DOT3_RGBA:
            case GL_INTERPOLATE:
            case GL_MODULATE:
            case GL_REPLACE:
            case GL_SUBTRACT:
                m_active_texture_unit->set_rgb_combinator(param_enum);
                break;
            default:
                RETURN_WITH_ERROR_IF(true, GL_INVALID_ENUM);
            }
            break;
        }
        case GL_OPERAND0_ALPHA:
        case GL_OPERAND1_ALPHA:
        case GL_OPERAND2_ALPHA: {
            auto param_enum = static_cast<GLenum>(param);
            switch (param_enum) {
            case GL_ONE_MINUS_SRC_ALPHA:
            case GL_SRC_ALPHA:
                m_active_texture_unit->set_alpha_operand(pname - GL_OPERAND0_ALPHA, param_enum);
                break;
            default:
                RETURN_WITH_ERROR_IF(true, GL_INVALID_ENUM);
            }
            break;
        }
        case GL_OPERAND0_RGB:
        case GL_OPERAND1_RGB:
        case GL_OPERAND2_RGB: {
            auto param_enum = static_cast<GLenum>(param);
            switch (param_enum) {
            case GL_ONE_MINUS_SRC_ALPHA:
            case GL_ONE_MINUS_SRC_COLOR:
            case GL_SRC_ALPHA:
            case GL_SRC_COLOR:
                m_active_texture_unit->set_rgb_operand(pname - GL_OPERAND0_RGB, param_enum);
                break;
            default:
                RETURN_WITH_ERROR_IF(true, GL_INVALID_ENUM);
            }
            break;
        }
        case GL_RGB_SCALE:
            RETURN_WITH_ERROR_IF(param != 1.f && param != 2.f && param != 4.f, GL_INVALID_VALUE);
            m_active_texture_unit->set_rgb_scale(param);
            break;
        case GL_SRC0_ALPHA:
        case GL_SRC1_ALPHA:
        case GL_SRC2_ALPHA: {
            auto param_enum = static_cast<GLenum>(param);
            switch (param_enum) {
            case GL_CONSTANT:
            case GL_PREVIOUS:
            case GL_PRIMARY_COLOR:
            case GL_TEXTURE:
            case GL_TEXTURE0 ... GL_TEXTURE31:
                m_active_texture_unit->set_alpha_source(pname - GL_SRC0_ALPHA, param_enum);
                break;
            default:
                RETURN_WITH_ERROR_IF(true, GL_INVALID_ENUM);
            }
            break;
        }
        case GL_SRC0_RGB:
        case GL_SRC1_RGB:
        case GL_SRC2_RGB: {
            auto param_enum = static_cast<GLenum>(param);
            switch (param_enum) {
            case GL_CONSTANT:
            case GL_PREVIOUS:
            case GL_PRIMARY_COLOR:
            case GL_TEXTURE:
            case GL_TEXTURE0 ... GL_TEXTURE31:
                m_active_texture_unit->set_rgb_source(pname - GL_SRC0_RGB, param_enum);
                break;
            default:
                RETURN_WITH_ERROR_IF(true, GL_INVALID_ENUM);
            }
            break;
        }
        case GL_TEXTURE_ENV_MODE: {
            auto param_enum = static_cast<GLenum>(param);
            switch (param_enum) {
            case GL_ADD:
            case GL_BLEND:
            case GL_COMBINE:
            case GL_DECAL:
            case GL_MODULATE:
            case GL_REPLACE:
                m_active_texture_unit->set_env_mode(param_enum);
                break;
            default:
                RETURN_WITH_ERROR_IF(true, GL_INVALID_ENUM);
            }
            break;
        }
        default:
            RETURN_WITH_ERROR_IF(true, GL_INVALID_ENUM);
        }
        break;
    case GL_TEXTURE_FILTER_CONTROL:
        switch (pname) {
        case GL_TEXTURE_LOD_BIAS:
            m_active_texture_unit->set_level_of_detail_bias(param);
            break;
        default:
            VERIFY_NOT_REACHED();
        }
        break;
    default:
        VERIFY_NOT_REACHED();
    }

    m_sampler_config_is_dirty = true;
}

void GLContext::gl_tex_gen(GLenum coord, GLenum pname, GLint param)
{
    APPEND_TO_CALL_LIST_AND_RETURN_IF_NEEDED(gl_tex_gen, coord, pname, param);
    RETURN_WITH_ERROR_IF(m_in_draw_state, GL_INVALID_OPERATION);

    RETURN_WITH_ERROR_IF(coord < GL_S || coord > GL_Q, GL_INVALID_ENUM);
    RETURN_WITH_ERROR_IF(pname != GL_TEXTURE_GEN_MODE, GL_INVALID_ENUM);
    RETURN_WITH_ERROR_IF(param != GL_EYE_LINEAR
            && param != GL_OBJECT_LINEAR
            && param != GL_SPHERE_MAP
            && param != GL_NORMAL_MAP
            && param != GL_REFLECTION_MAP,
        GL_INVALID_ENUM);
    RETURN_WITH_ERROR_IF((coord == GL_R || coord == GL_Q) && param == GL_SPHERE_MAP, GL_INVALID_ENUM);
    RETURN_WITH_ERROR_IF(coord == GL_Q && (param == GL_REFLECTION_MAP || param == GL_NORMAL_MAP), GL_INVALID_ENUM);

    GLenum const capability = GL_TEXTURE_GEN_S + (coord - GL_S);
    texture_coordinate_generation(m_active_texture_unit_index, capability).generation_mode = param;
    m_texcoord_generation_dirty = true;
}

void GLContext::gl_tex_gen_floatv(GLenum coord, GLenum pname, GLfloat const* params)
{
    APPEND_TO_CALL_LIST_AND_RETURN_IF_NEEDED(gl_tex_gen_floatv, coord, pname, params);
    RETURN_WITH_ERROR_IF(m_in_draw_state, GL_INVALID_OPERATION);

    RETURN_WITH_ERROR_IF(coord < GL_S || coord > GL_Q, GL_INVALID_ENUM);
    RETURN_WITH_ERROR_IF(pname != GL_TEXTURE_GEN_MODE
            && pname != GL_OBJECT_PLANE
            && pname != GL_EYE_PLANE,
        GL_INVALID_ENUM);

    GLenum const capability = GL_TEXTURE_GEN_S + (coord - GL_S);

    switch (pname) {
    case GL_TEXTURE_GEN_MODE: {
        auto param = static_cast<GLenum>(params[0]);
        RETURN_WITH_ERROR_IF(param != GL_EYE_LINEAR
                && param != GL_OBJECT_LINEAR
                && param != GL_SPHERE_MAP
                && param != GL_NORMAL_MAP
                && param != GL_REFLECTION_MAP,
            GL_INVALID_ENUM);
        RETURN_WITH_ERROR_IF((coord == GL_R || coord == GL_Q) && param == GL_SPHERE_MAP, GL_INVALID_ENUM);
        RETURN_WITH_ERROR_IF(coord == GL_Q && (param == GL_REFLECTION_MAP || param == GL_NORMAL_MAP), GL_INVALID_ENUM);

        texture_coordinate_generation(m_active_texture_unit_index, capability).generation_mode = param;
        break;
    }
    case GL_OBJECT_PLANE:
        texture_coordinate_generation(m_active_texture_unit_index, capability).object_plane_coefficients = { params[0], params[1], params[2], params[3] };
        break;
    case GL_EYE_PLANE: {
        auto const& inverse_model_view = m_model_view_matrix.inverse();
        auto input_coefficients = FloatVector4 { params[0], params[1], params[2], params[3] };

        // Note: we are allowed to store transformed coefficients here, according to the documentation on
        //       `glGetTexGen`:
        //
        // "The returned values are those maintained in eye coordinates. They are not equal to the values
        //  specified using glTexGen, unless the modelview matrix was identity when glTexGen was called."

        texture_coordinate_generation(m_active_texture_unit_index, capability).eye_plane_coefficients = inverse_model_view * input_coefficients;
        break;
    }
    default:
        VERIFY_NOT_REACHED();
    }

    m_texcoord_generation_dirty = true;
}

void GLContext::gl_tex_image_2d(GLenum target, GLint level, GLint internal_format, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, GLvoid const* data)
{
    RETURN_WITH_ERROR_IF(m_in_draw_state, GL_INVALID_OPERATION);

    RETURN_WITH_ERROR_IF(internal_format == GL_NONE || format == GL_NONE || type == GL_NONE, GL_INVALID_ENUM);
    auto pixel_type_or_error = get_validated_pixel_type(target, internal_format, format, type);
    RETURN_WITH_ERROR_IF(pixel_type_or_error.is_error(), pixel_type_or_error.release_error().code());

    RETURN_WITH_ERROR_IF(level < 0 || level > Texture2D::LOG2_MAX_TEXTURE_SIZE, GL_INVALID_VALUE);
    RETURN_WITH_ERROR_IF(width < 0 || height < 0 || width > (2 + Texture2D::MAX_TEXTURE_SIZE) || height > (2 + Texture2D::MAX_TEXTURE_SIZE), GL_INVALID_VALUE);
    if (!m_device_info.supports_npot_textures)
        RETURN_WITH_ERROR_IF(!is_power_of_two(width) || !is_power_of_two(height), GL_INVALID_VALUE);
    RETURN_WITH_ERROR_IF(border != 0, GL_INVALID_VALUE);

    auto texture_2d = m_active_texture_unit->texture_2d_target_texture();
    VERIFY(!texture_2d.is_null());

    if (level == 0) {
        // FIXME: OpenGL has the concept of texture and mipmap completeness. A texture has to fulfill certain criteria to be considered complete.
        // Trying to render while an incomplete texture is bound will result in an error.
        // Here we simply create a complete device image when mipmap level 0 is attached to the texture object. This has the unfortunate side effect
        // that constructing GL textures in any but the default mipmap order, going from level 0 upwards will cause mip levels to stay uninitialized.
        // To be spec compliant we should create the device image once the texture has become complete and is used for rendering the first time.
        // All images that were attached before the device image was created need to be stored somewhere to be used to initialize the device image once complete.
        auto internal_pixel_format = pixel_format_for_internal_format(internal_format);
        texture_2d->set_device_image(m_rasterizer->create_image(internal_pixel_format, width, height, 1, Texture2D::LOG2_MAX_TEXTURE_SIZE));
        m_sampler_config_is_dirty = true;
    }

    GPU::ImageDataLayout input_layout = {
        .pixel_type = pixel_type_or_error.release_value(),
        .packing = get_packing_specification(PackingType::Unpack),
        .dimensions = {
            .width = static_cast<u32>(width),
            .height = static_cast<u32>(height),
            .depth = 1,
        },
        .selection = {
            .width = static_cast<u32>(width),
            .height = static_cast<u32>(height),
            .depth = 1,
        },
    };

    texture_2d->upload_texture_data(level, internal_format, input_layout, data);
}

void GLContext::gl_tex_parameter(GLenum target, GLenum pname, GLfloat param)
{
    APPEND_TO_CALL_LIST_AND_RETURN_IF_NEEDED(gl_tex_parameter, target, pname, param);

    RETURN_WITH_ERROR_IF(m_in_draw_state, GL_INVALID_OPERATION);

    // FIXME: We currently only support GL_TETXURE_2D targets. 1D, 3D and CUBE should also be supported (https://docs.gl/gl2/glTexParameter)
    RETURN_WITH_ERROR_IF(target != GL_TEXTURE_2D, GL_INVALID_ENUM);

    // FIXME: implement the remaining parameters. (https://docs.gl/gl2/glTexParameter)
    RETURN_WITH_ERROR_IF(!(pname == GL_TEXTURE_MIN_FILTER
                             || pname == GL_TEXTURE_MAG_FILTER
                             || pname == GL_TEXTURE_WRAP_S
                             || pname == GL_TEXTURE_WRAP_T),
        GL_INVALID_ENUM);

    // We assume GL_TEXTURE_2D (see above)
    auto texture_2d = m_active_texture_unit->texture_2d_target_texture();
    VERIFY(!texture_2d.is_null());

    switch (pname) {
    case GL_TEXTURE_MIN_FILTER:
        RETURN_WITH_ERROR_IF(!(param == GL_NEAREST
                                 || param == GL_LINEAR
                                 || param == GL_NEAREST_MIPMAP_NEAREST
                                 || param == GL_LINEAR_MIPMAP_NEAREST
                                 || param == GL_NEAREST_MIPMAP_LINEAR
                                 || param == GL_LINEAR_MIPMAP_LINEAR),
            GL_INVALID_ENUM);

        texture_2d->sampler().set_min_filter(param);
        break;

    case GL_TEXTURE_MAG_FILTER:
        RETURN_WITH_ERROR_IF(!(param == GL_NEAREST
                                 || param == GL_LINEAR),
            GL_INVALID_ENUM);

        texture_2d->sampler().set_mag_filter(param);
        break;

    case GL_TEXTURE_WRAP_S:
        RETURN_WITH_ERROR_IF(!(param == GL_CLAMP
                                 || param == GL_CLAMP_TO_BORDER
                                 || param == GL_CLAMP_TO_EDGE
                                 || param == GL_MIRRORED_REPEAT
                                 || param == GL_REPEAT),
            GL_INVALID_ENUM);

        texture_2d->sampler().set_wrap_s_mode(param);
        break;

    case GL_TEXTURE_WRAP_T:
        RETURN_WITH_ERROR_IF(!(param == GL_CLAMP
                                 || param == GL_CLAMP_TO_BORDER
                                 || param == GL_CLAMP_TO_EDGE
                                 || param == GL_MIRRORED_REPEAT
                                 || param == GL_REPEAT),
            GL_INVALID_ENUM);

        texture_2d->sampler().set_wrap_t_mode(param);
        break;

    default:
        VERIFY_NOT_REACHED();
    }

    m_sampler_config_is_dirty = true;
}

void GLContext::gl_tex_parameterfv(GLenum target, GLenum pname, GLfloat const* params)
{
    APPEND_TO_CALL_LIST_AND_RETURN_IF_NEEDED(gl_tex_parameterfv, target, pname, params);

    RETURN_WITH_ERROR_IF(m_in_draw_state, GL_INVALID_OPERATION);

    // FIXME: We currently only support GL_TETXURE_2D targets. 1D, 3D and CUBE should also be supported (https://docs.gl/gl2/glTexParameter)
    RETURN_WITH_ERROR_IF(target != GL_TEXTURE_2D, GL_INVALID_ENUM);

    // FIXME: implement the remaining parameters. (https://docs.gl/gl2/glTexParameter)
    RETURN_WITH_ERROR_IF(!(pname == GL_TEXTURE_BORDER_COLOR), GL_INVALID_ENUM);

    // We assume GL_TEXTURE_2D (see above)
    auto texture_2d = m_active_texture_unit->texture_2d_target_texture();
    RETURN_WITH_ERROR_IF(texture_2d.is_null(), GL_INVALID_OPERATION);

    switch (pname) {
    case GL_TEXTURE_BORDER_COLOR:
        texture_2d->sampler().set_border_color(params[0], params[1], params[2], params[3]);
        break;
    default:
        VERIFY_NOT_REACHED();
    }

    m_sampler_config_is_dirty = true;
}

void GLContext::gl_tex_sub_image_2d(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid const* data)
{
    RETURN_WITH_ERROR_IF(m_in_draw_state, GL_INVALID_OPERATION);

    // We only support symbolic constants for now
    RETURN_WITH_ERROR_IF(level < 0 || level > Texture2D::LOG2_MAX_TEXTURE_SIZE, GL_INVALID_VALUE);
    RETURN_WITH_ERROR_IF(width < 0 || height < 0 || width > (2 + Texture2D::MAX_TEXTURE_SIZE) || height > (2 + Texture2D::MAX_TEXTURE_SIZE), GL_INVALID_VALUE);

    // A 2D texture array must have been defined by a previous glTexImage2D operation
    auto texture_2d = m_active_texture_unit->texture_2d_target_texture();
    VERIFY(!texture_2d.is_null());
    RETURN_WITH_ERROR_IF(texture_2d->device_image().is_null(), GL_INVALID_OPERATION);

    RETURN_WITH_ERROR_IF(format == GL_NONE || type == GL_NONE, GL_INVALID_ENUM);
    auto pixel_type_or_error = get_validated_pixel_type(target, texture_2d->internal_format(), format, type);
    RETURN_WITH_ERROR_IF(pixel_type_or_error.is_error(), pixel_type_or_error.release_error().code());

    RETURN_WITH_ERROR_IF(xoffset < 0 || yoffset < 0 || xoffset + width > texture_2d->width_at_lod(level) || yoffset + height > texture_2d->height_at_lod(level), GL_INVALID_VALUE);

    GPU::ImageDataLayout input_layout = {
        .pixel_type = pixel_type_or_error.release_value(),
        .packing = get_packing_specification(PackingType::Unpack),
        .dimensions = {
            .width = static_cast<u32>(width),
            .height = static_cast<u32>(height),
            .depth = 1,
        },
        .selection = {
            .width = static_cast<u32>(width),
            .height = static_cast<u32>(height),
            .depth = 1,
        },
    };

    texture_2d->replace_sub_texture_data(level, input_layout, { xoffset, yoffset, 0 }, data);
}

void GLContext::sync_device_sampler_config()
{
    if (!m_sampler_config_is_dirty)
        return;

    m_sampler_config_is_dirty = false;

    for (unsigned i = 0; i < m_texture_units.size(); ++i) {
        auto const& texture_unit = m_texture_units[i];
        if (!texture_unit.texture_2d_enabled())
            continue;

        GPU::SamplerConfig config;

        auto texture_2d = texture_unit.texture_2d_target_texture();
        VERIFY(!texture_2d.is_null());
        config.bound_image = texture_2d->device_image();
        config.level_of_detail_bias = texture_unit.level_of_detail_bias();

        auto const& sampler = texture_2d->sampler();

        switch (sampler.min_filter()) {
        case GL_NEAREST:
            config.texture_min_filter = GPU::TextureFilter::Nearest;
            config.mipmap_filter = GPU::MipMapFilter::None;
            break;
        case GL_LINEAR:
            config.texture_min_filter = GPU::TextureFilter::Linear;
            config.mipmap_filter = GPU::MipMapFilter::None;
            break;
        case GL_NEAREST_MIPMAP_NEAREST:
            config.texture_min_filter = GPU::TextureFilter::Nearest;
            config.mipmap_filter = GPU::MipMapFilter::Nearest;
            break;
        case GL_LINEAR_MIPMAP_NEAREST:
            config.texture_min_filter = GPU::TextureFilter::Linear;
            config.mipmap_filter = GPU::MipMapFilter::Nearest;
            break;
        case GL_NEAREST_MIPMAP_LINEAR:
            config.texture_min_filter = GPU::TextureFilter::Nearest;
            config.mipmap_filter = GPU::MipMapFilter::Linear;
            break;
        case GL_LINEAR_MIPMAP_LINEAR:
            config.texture_min_filter = GPU::TextureFilter::Linear;
            config.mipmap_filter = GPU::MipMapFilter::Linear;
            break;
        default:
            VERIFY_NOT_REACHED();
        }

        switch (sampler.mag_filter()) {
        case GL_NEAREST:
            config.texture_mag_filter = GPU::TextureFilter::Nearest;
            break;
        case GL_LINEAR:
            config.texture_mag_filter = GPU::TextureFilter::Linear;
            break;
        default:
            VERIFY_NOT_REACHED();
        }

        switch (sampler.wrap_s_mode()) {
        case GL_CLAMP:
            config.texture_wrap_u = GPU::TextureWrapMode::Clamp;
            break;
        case GL_CLAMP_TO_BORDER:
            config.texture_wrap_u = GPU::TextureWrapMode::ClampToBorder;
            break;
        case GL_CLAMP_TO_EDGE:
            config.texture_wrap_u = GPU::TextureWrapMode::ClampToEdge;
            break;
        case GL_REPEAT:
            config.texture_wrap_u = GPU::TextureWrapMode::Repeat;
            break;
        case GL_MIRRORED_REPEAT:
            config.texture_wrap_u = GPU::TextureWrapMode::MirroredRepeat;
            break;
        default:
            VERIFY_NOT_REACHED();
        }

        switch (sampler.wrap_t_mode()) {
        case GL_CLAMP:
            config.texture_wrap_v = GPU::TextureWrapMode::Clamp;
            break;
        case GL_CLAMP_TO_BORDER:
            config.texture_wrap_v = GPU::TextureWrapMode::ClampToBorder;
            break;
        case GL_CLAMP_TO_EDGE:
            config.texture_wrap_v = GPU::TextureWrapMode::ClampToEdge;
            break;
        case GL_REPEAT:
            config.texture_wrap_v = GPU::TextureWrapMode::Repeat;
            break;
        case GL_MIRRORED_REPEAT:
            config.texture_wrap_v = GPU::TextureWrapMode::MirroredRepeat;
            break;
        default:
            VERIFY_NOT_REACHED();
        }

        auto& fixed_function_env = config.fixed_function_texture_environment;

        auto get_env_mode = [](GLenum mode) {
            switch (mode) {
            case GL_ADD:
                return GPU::TextureEnvMode::Add;
            case GL_BLEND:
                return GPU::TextureEnvMode::Blend;
            case GL_COMBINE:
                return GPU::TextureEnvMode::Combine;
            case GL_DECAL:
                return GPU::TextureEnvMode::Decal;
            case GL_MODULATE:
                return GPU::TextureEnvMode::Modulate;
            case GL_REPLACE:
                return GPU::TextureEnvMode::Replace;
            default:
                VERIFY_NOT_REACHED();
            }
        };
        fixed_function_env.env_mode = get_env_mode(texture_unit.env_mode());

        fixed_function_env.alpha_scale = texture_unit.alpha_scale();
        fixed_function_env.rgb_scale = texture_unit.rgb_scale();

        auto get_combinator = [](GLenum combinator) {
            switch (combinator) {
            case GL_ADD:
                return GPU::TextureCombinator::Add;
            case GL_ADD_SIGNED:
                return GPU::TextureCombinator::AddSigned;
            case GL_DOT3_RGB:
                return GPU::TextureCombinator::Dot3RGB;
            case GL_DOT3_RGBA:
                return GPU::TextureCombinator::Dot3RGBA;
            case GL_INTERPOLATE:
                return GPU::TextureCombinator::Interpolate;
            case GL_MODULATE:
                return GPU::TextureCombinator::Modulate;
            case GL_REPLACE:
                return GPU::TextureCombinator::Replace;
            case GL_SUBTRACT:
                return GPU::TextureCombinator::Subtract;
            default:
                VERIFY_NOT_REACHED();
            }
        };
        fixed_function_env.alpha_combinator = get_combinator(texture_unit.alpha_combinator());
        fixed_function_env.rgb_combinator = get_combinator(texture_unit.rgb_combinator());

        auto get_operand = [](GLenum operand) {
            switch (operand) {
            case GL_ONE_MINUS_SRC_ALPHA:
                return GPU::TextureOperand::OneMinusSourceAlpha;
            case GL_ONE_MINUS_SRC_COLOR:
                return GPU::TextureOperand::OneMinusSourceColor;
            case GL_SRC_ALPHA:
                return GPU::TextureOperand::SourceAlpha;
            case GL_SRC_COLOR:
                return GPU::TextureOperand::SourceColor;
            default:
                VERIFY_NOT_REACHED();
            }
        };
        auto get_source = [](GLenum source) {
            switch (source) {
            case GL_CONSTANT:
                return GPU::TextureSource::Constant;
            case GL_PREVIOUS:
                return GPU::TextureSource::Previous;
            case GL_PRIMARY_COLOR:
                return GPU::TextureSource::PrimaryColor;
            case GL_TEXTURE:
                return GPU::TextureSource::Texture;
            case GL_TEXTURE0 ... GL_TEXTURE31:
                return GPU::TextureSource::TextureStage;
            default:
                VERIFY_NOT_REACHED();
            }
        };
        for (size_t j = 0; j < 3; ++j) {
            fixed_function_env.alpha_operand[j] = get_operand(texture_unit.alpha_operand(j));
            fixed_function_env.alpha_source[j] = get_source(texture_unit.alpha_source(j));
            if (fixed_function_env.alpha_source[j] == GPU::TextureSource::TextureStage)
                fixed_function_env.alpha_source_texture_stage = texture_unit.alpha_source(j) - GL_TEXTURE0;

            fixed_function_env.rgb_operand[j] = get_operand(texture_unit.rgb_operand(j));
            fixed_function_env.rgb_source[j] = get_source(texture_unit.rgb_source(j));
            if (fixed_function_env.rgb_source[j] == GPU::TextureSource::TextureStage)
                fixed_function_env.rgb_source_texture_stage = texture_unit.rgb_source(j) - GL_TEXTURE0;
        }

        config.border_color = sampler.border_color();
        m_rasterizer->set_sampler_config(i, config);
    }
}

void GLContext::sync_device_texcoord_config()
{
    if (!m_texcoord_generation_dirty)
        return;
    m_texcoord_generation_dirty = false;

    auto options = m_rasterizer->options();

    for (size_t i = 0; i < m_device_info.num_texture_units; ++i) {

        u8 enabled_coordinates = GPU::TexCoordGenerationCoordinate::None;
        for (GLenum capability = GL_TEXTURE_GEN_S; capability <= GL_TEXTURE_GEN_Q; ++capability) {
            auto const context_coordinate_config = texture_coordinate_generation(i, capability);
            if (!context_coordinate_config.enabled)
                continue;

            GPU::TexCoordGenerationConfig* texcoord_generation_config;
            switch (capability) {
            case GL_TEXTURE_GEN_S:
                enabled_coordinates |= GPU::TexCoordGenerationCoordinate::S;
                texcoord_generation_config = &options.texcoord_generation_config[i][0];
                break;
            case GL_TEXTURE_GEN_T:
                enabled_coordinates |= GPU::TexCoordGenerationCoordinate::T;
                texcoord_generation_config = &options.texcoord_generation_config[i][1];
                break;
            case GL_TEXTURE_GEN_R:
                enabled_coordinates |= GPU::TexCoordGenerationCoordinate::R;
                texcoord_generation_config = &options.texcoord_generation_config[i][2];
                break;
            case GL_TEXTURE_GEN_Q:
                enabled_coordinates |= GPU::TexCoordGenerationCoordinate::Q;
                texcoord_generation_config = &options.texcoord_generation_config[i][3];
                break;
            default:
                VERIFY_NOT_REACHED();
            }

            switch (context_coordinate_config.generation_mode) {
            case GL_OBJECT_LINEAR:
                texcoord_generation_config->mode = GPU::TexCoordGenerationMode::ObjectLinear;
                texcoord_generation_config->coefficients = context_coordinate_config.object_plane_coefficients;
                break;
            case GL_EYE_LINEAR:
                texcoord_generation_config->mode = GPU::TexCoordGenerationMode::EyeLinear;
                texcoord_generation_config->coefficients = context_coordinate_config.eye_plane_coefficients;
                break;
            case GL_SPHERE_MAP:
                texcoord_generation_config->mode = GPU::TexCoordGenerationMode::SphereMap;
                break;
            case GL_REFLECTION_MAP:
                texcoord_generation_config->mode = GPU::TexCoordGenerationMode::ReflectionMap;
                break;
            case GL_NORMAL_MAP:
                texcoord_generation_config->mode = GPU::TexCoordGenerationMode::NormalMap;
                break;
            }
        }
        options.texcoord_generation_enabled_coordinates[i] = enabled_coordinates;
    }

    m_rasterizer->set_options(options);
}

}
