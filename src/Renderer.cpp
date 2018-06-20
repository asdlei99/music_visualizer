#include <iostream>
using std::cout;
using std::endl;
#include <chrono>
namespace chrono = std::chrono;

#include "Renderer.h"

void GLAPIENTRY MessageCallback(GLenum source,
                                GLenum type,
                                GLuint id,
                                GLenum severity,
                                GLsizei length,
                                const GLchar* message,
                                const void* userParam) {
    fprintf(stderr, "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
            (type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : ""), type, severity, message);
}

// TODO Test the output of the shaders. Use dummy data in audio_data. Compute similarity between
// expected images and produced images.

// TODO add a previously rendered uniform so that a single buffer can be repetitvely applied

Renderer& Renderer::operator=(Renderer&& o) {
    glDeleteFramebuffers(num_user_buffers, fbos.data());
    // The default window's fbo is now bound
    glDeleteTextures(2 * num_user_buffers, texs.data());
    // Textures in texs are unboud from their targets

    fbos = std::move(o.fbos);
    texs = std::move(o.texs);
    buffers_last_drawn = std::move(o.buffers_last_drawn);
    num_user_buffers = o.num_user_buffers;
    frame_counter = o.frame_counter;
    o.fbos.clear();
    o.texs.clear();
    o.num_user_buffers = 0;
    return *this;
}

Renderer::Renderer(const ShaderConfig& config, const ShaderPrograms& shaders, const Window& window)
    : config(config), shaders(shaders), window(window), frame_counter(0), num_user_buffers(0), buffers_last_drawn(config.mBuffers.size(), 0) {
#ifdef _DEBUG
    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback(MessageCallback, 0);
#endif

    if (config.mBlend) {
        // I chose the following blending func because it allows the user to completely
        // replace the colors in the buffer by setting their output alpha to 1.
        // unfortunately it forces the user to make one of three choices:
        // 1) replace color in the framebuffer
        // 2) leave framebuffer unchanged
        // 3) mix new color with old color
        glEnable(GL_BLEND);
        // mix(old_color.rgb, new_color.rgb, new_color_alpha)
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    else {
        glDisable(GL_BLEND);
    }

    num_user_buffers = config.mBuffers.size();

    // Create framebuffers and textures
    for (int i = 0; i < num_user_buffers; ++i) {
        GLuint tex1, tex2, fbo;

        const Buffer& buff = config.mBuffers[i];
        int width = buff.width;
        int height = buff.height;
        if (buff.is_window_size) {
            width = window.width;
            height = window.height;
        }

        glActiveTexture(GL_TEXTURE0 + i);
        glGenTextures(1, &tex1);
        glBindTexture(GL_TEXTURE_2D, tex1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glGenTextures(1, &tex2);
        glGenTextures(1, &tex2);
        glBindTexture(GL_TEXTURE_2D, tex2);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex1, 0);

        texs.push_back(tex1);
        texs.push_back(tex2);
        fbos.push_back(fbo);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    start_time = std::chrono::steady_clock::now();
}

Renderer::~Renderer() {
    // revert opengl state

    glDeleteFramebuffers(num_user_buffers, fbos.data());
    // The default window's fbo is now bound

    glDeleteTextures(2 * num_user_buffers, texs.data());
    // Textures in texs are unboud from their targets
}

void Renderer::update(audio_data& data) {
    // Update audio textures
    // glActivateTexture activates a certain texture unit.
    // each texture unit holds one texture of each dimension of texture
    //     {GL_TEXTURE_1D, GL_TEXTURE_2D, GL_TEXTURE_3D, GL_TEXTURE_CUBEMAP}
    // because I'm using four 1D textures I need to store them in separate texture units
    //
    // glUniform1i(textureLoc, int) sets what texture unit the sampler in the shader reads from
    //
    // A texture binding created with glBindTexture remains active until a different texture is
    // bound to the same target (in the active unit? I think), or until the bound texture is deleted
    // with glDeleteTextures. So I do not need to rebind
    // glBindTexture(GL_TEXTURE_1D, tex[X]);
    data.mtx.lock();
    glActiveTexture(GL_TEXTURE0 + 0);
    glTexSubImage1D(GL_TEXTURE_1D, 0, 0, VISUALIZER_BUFSIZE, GL_RED, GL_FLOAT, data.audio_r);
    glActiveTexture(GL_TEXTURE0 + 1);
    glTexSubImage1D(GL_TEXTURE_1D, 0, 0, VISUALIZER_BUFSIZE, GL_RED, GL_FLOAT, data.audio_l);
    glActiveTexture(GL_TEXTURE0 + 2);
    glTexSubImage1D(GL_TEXTURE_1D, 0, 0, VISUALIZER_BUFSIZE, GL_RED, GL_FLOAT, data.freq_r);
    glActiveTexture(GL_TEXTURE0 + 3);
    glTexSubImage1D(GL_TEXTURE_1D, 0, 0, VISUALIZER_BUFSIZE, GL_RED, GL_FLOAT, data.freq_l);
    data.mtx.unlock();

    update();
}

void Renderer::update() {
    if (window.size_changed) {
        // Resize textures for window sized buffers
        for (int i = 0; i < config.mBuffers.size(); ++i) {
            const Buffer& buff = config.mBuffers[i];
            int width = buff.width;
            int height = buff.height;
            if (buff.is_window_size) {
                width = window.width;
                height = window.height;
            }
            glBindTexture(GL_TEXTURE_2D, texs[2 * i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glBindTexture(GL_TEXTURE_2D, texs[2 * i + 1]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        }
        frame_counter = 0;
        start_time = std::chrono::steady_clock::now();
        // for (int& bld : buffers_last_drawn) {
        //     bld = 0;
        // }
    }
}

void Renderer::render() {
    auto now = std::chrono::steady_clock::now();
    elapsed_time = (now - start_time).count() / 1e9f;

    // Render buffers
    for (const int r : config.mRender_order) {
        const Buffer& buff = config.mBuffers[r];
        int width = buff.width;
        int height = buff.height;
        if (buff.is_window_size) {
            width = window.width;
            height = window.height;
        }
        shaders.use_program(r);
        upload_uniforms(buff, r);
        glActiveTexture(GL_TEXTURE0 + r);
        glBindTexture(GL_TEXTURE_2D, texs[2 * r + buffers_last_drawn[r]]);
        glBindFramebuffer(GL_FRAMEBUFFER, fbos[r]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texs[2 * r + (buffers_last_drawn[r] + 1) % 2], 0);
        glViewport(0, 0, width, height);
        glClearColor(buff.clear_color[0], buff.clear_color[1], buff.clear_color[2], 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDrawArrays(GL_POINTS, 0, buff.geom_iters);
        buffers_last_drawn[r] += 1;
        buffers_last_drawn[r] %= 2;
        // bind most recently drawn texture to texture unit r so other buffers can use it
        glBindTexture(GL_TEXTURE_2D, texs[2 * r + buffers_last_drawn[r]]);
    }

    // Render image
    shaders.use_program(num_user_buffers);
    const Buffer& buff = config.mImage;
    upload_uniforms(config.mImage, num_user_buffers);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, window.width, window.height);
    glClearColor(buff.clear_color[0], buff.clear_color[1], buff.clear_color[2], 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDrawArrays(GL_POINTS, 0, buff.geom_iters);
    frame_counter++;
}

void Renderer::upload_uniforms(const Buffer& buff, const int buff_index) const {
    // Builtin uniforms
    glUniform2f(shaders.get_uniform_loc(buff_index, 0), window.mouse.x, window.mouse.y);
    glUniform1i(shaders.get_uniform_loc(buff_index, 1), window.mouse.down);
    glUniform2f(shaders.get_uniform_loc(buff_index, 2), window.width, window.height);
    glUniform1f(shaders.get_uniform_loc(buff_index, 3), elapsed_time);
    glUniform1i(shaders.get_uniform_loc(buff_index, 4), frame_counter);
    glUniform1f(shaders.get_uniform_loc(buff_index, 5), float(buff.geom_iters));
    int uniform_offset = 6;
    for (int i = 0; i < 4; ++i) // Point samplers to texture units
        glUniform1i(shaders.get_uniform_loc(buff_index, uniform_offset + i), i);
    uniform_offset = ShaderPrograms::num_builtin_uniforms;

    // User's samplers
    for (int i = 0; i < num_user_buffers; ++i) // Point samplers to texture units
        glUniform1i(shaders.get_uniform_loc(buff_index, uniform_offset + i), i);
    uniform_offset += config.mBuffers.size();

    // User's uniforms
    for (int i = 0; i < config.mUniforms.size(); ++i) {
        const std::vector<float>& uv = config.mUniforms[i].values;
        switch (uv.size()) {
        case 1:
            glUniform1f(shaders.get_uniform_loc(buff_index, uniform_offset + i), uv[0]);
            break;
        case 2:
            glUniform2f(shaders.get_uniform_loc(buff_index, uniform_offset + i), uv[0], uv[1]);
            break;
        case 3:
            glUniform3f(shaders.get_uniform_loc(buff_index, uniform_offset + i), uv[0], uv[1], uv[2]);
            break;
        case 4:
            glUniform4f(shaders.get_uniform_loc(buff_index, uniform_offset + i), uv[0], uv[1], uv[2], uv[3]);
            break;
        }
    }
}
