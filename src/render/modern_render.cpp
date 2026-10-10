#include <glad/glad.h>
#include "modern_render.h"
#include "shader_program.h"
#include <iostream>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

ModernRender::ModernRender() = default;

ModernRender::~ModernRender() {
    Destroy();
}

void ModernRender::SetupBuffers() {
    float vertices[] = {
        // 屏幕裁剪坐标 (X, Y)    // 纹理贴图坐标 (U, V)
        // 第一个三角形 (左下 -> 右上 -> 左上)
        -1.0f, -1.0f,             0.0f, 1.0f,
         1.0f,  1.0f,             1.0f, 0.0f,
        -1.0f,  1.0f,             0.0f, 0.0f,

        // 第二个三角形 (左下 -> 右下 -> 右上)
        -1.0f, -1.0f,             0.0f, 1.0f,
         1.0f, -1.0f,             1.0f, 1.0f,
         1.0f,  1.0f,             1.0f, 0.0f
    };

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // 属性 0: 顶点位置 (X, Y)
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);

    // 属性 1: 纹理坐标 (U, V)
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

bool ModernRender::Init() {
    // 1. 初始化顶点与纹理坐标缓冲
    SetupBuffers();

    // 2. 生成 3 个纹理句柄并设置双线性过滤与边缘截断模式
    glGenTextures(3, m_textures);
    for (int i = 0; i < 3; ++i) {
        glBindTexture(GL_TEXTURE_2D, m_textures[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    // 3. 加载并初始化 YUV420P 着色器管线
    m_yuv420p_shader = std::make_unique<ShaderProgram>();
    if (!m_yuv420p_shader->Init("shaders/vertex_shader.glsl", "shaders/yuv420p_frag.glsl")) {
        std::cerr << "[ModernRender] Failed to init YUV420P shader" << std::endl;
        return false;
    }
    m_yuv420p_shader->Use();
    glUniform1i(glGetUniformLocation(m_yuv420p_shader->GetId(), "tex_y"), 0);
    glUniform1i(glGetUniformLocation(m_yuv420p_shader->GetId(), "tex_u"), 1);
    glUniform1i(glGetUniformLocation(m_yuv420p_shader->GetId(), "tex_v"), 2);

    // 4. 加载并初始化 NV12 着色器管线
    m_nv12_shader = std::make_unique<ShaderProgram>();
    if (!m_nv12_shader->Init("shaders/vertex_shader.glsl", "shaders/nv12_frag.glsl")) {
        std::cerr << "[ModernRender] Failed to init NV12 shader" << std::endl;
        return false;
    }
    m_nv12_shader->Use();
    glUniform1i(glGetUniformLocation(m_nv12_shader->GetId(), "tex_y"), 0);
    glUniform1i(glGetUniformLocation(m_nv12_shader->GetId(), "tex_uv"), 1);

    return true;
}

void ModernRender::Resize(int window_width, int window_height) {
    if (window_width <= 0 || window_height <= 0) return;
    m_window_width = window_width;
    m_window_height = window_height;
    SetupViewport();
}

void ModernRender::SetupViewport() {
    if (m_window_width <= 0 || m_window_height <= 0 || m_video_width <= 0 || m_video_height <= 0) {
        return;
    }

    float video_ar = static_cast<float>(m_video_width) / static_cast<float>(m_video_height);
    float window_ar = static_cast<float>(m_window_width) / static_cast<float>(m_window_height);

    int vp_x = 0;
    int vp_y = 0;
    int vp_w = m_window_width;
    int vp_h = m_window_height;

    if (window_ar > video_ar) {
        // 窗口更宽 -> 左右留黑边
        vp_h = m_window_height;
        vp_w = static_cast<int>(m_window_height * video_ar);
        vp_x = (m_window_width - vp_w) / 2;
        vp_y = 0;
    } else {
        // 窗口更高 -> 上下留黑边
        vp_w = m_window_width;
        vp_h = static_cast<int>(m_window_width / video_ar);
        vp_x = 0;
        vp_y = (m_window_height - vp_h) / 2;
    }

    glViewport(vp_x, vp_y, vp_w, vp_h);
}

void ModernRender::RenderFrame(const AVFrame* frame) {
    if (!frame || !frame->data[0]) {
        return;
    }

    // 1. 动态检测分辨率或像素格式变化
    if (frame->width != m_video_width || frame->height != m_video_height || frame->format != m_current_format) {
        m_video_width = frame->width;
        m_video_height = frame->height;
        m_current_format = frame->format;
        m_textures_allocated = false;
        SetupViewport();
    }

    // 2. 清除背景（确保黑边区域保持纯黑）
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // 3. 按格式更新纹理与激活着色器
    if (frame->format == AV_PIX_FMT_YUV420P) {
        UpdateYUV420PTextures(frame);

        m_yuv420p_shader->Use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_textures[0]);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_textures[1]);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, m_textures[2]);
    } else if (frame->format == AV_PIX_FMT_NV12) {
        UpdateNV12Textures(frame);

        m_nv12_shader->Use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_textures[0]);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_textures[1]);
    } else {
        std::cerr << "[ModernRender] Unsupported pixel format: " << frame->format << std::endl;
        return;
    }

    // 4. 绘制四边形
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    // 5. 解绑纹理并标记显存已分配
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    m_textures_allocated = true;
}

void ModernRender::UpdateYUV420PTextures(const AVFrame* frame) {
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    const int widths[3]  = { m_video_width, m_video_width / 2, m_video_width / 2 };
    const int heights[3] = { m_video_height, m_video_height / 2, m_video_height / 2 };

    for (int i = 0; i < 3; ++i) {
        glBindTexture(GL_TEXTURE_2D, m_textures[i]);
        // GL_RED 单通道，1 像素 = 1 字节，步进像素数等于 linesize[i]
        glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[i]);

        if (!m_textures_allocated) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, widths[i], heights[i], 0, GL_RED, GL_UNSIGNED_BYTE, frame->data[i]);
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, widths[i], heights[i], GL_RED, GL_UNSIGNED_BYTE, frame->data[i]);
        }
    }

    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

void ModernRender::UpdateNV12Textures(const AVFrame* frame) {
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // 1. Y 平面 (GL_RED): 1 像素 = 1 字节，步进像素数 = linesize[0]
    glBindTexture(GL_TEXTURE_2D, m_textures[0]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[0]);
    if (!m_textures_allocated) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, m_video_width, m_video_height, 0, GL_RED, GL_UNSIGNED_BYTE, frame->data[0]);
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_video_width, m_video_height, GL_RED, GL_UNSIGNED_BYTE, frame->data[0]);
    }

    // 2. UV 平面 (GL_RG): 1 像素 = 2 字节 (U+V)，步进像素数 = linesize[1] / 2
    glBindTexture(GL_TEXTURE_2D, m_textures[1]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[1] / 2);
    if (!m_textures_allocated) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG, m_video_width / 2, m_video_height / 2, 0, GL_RG, GL_UNSIGNED_BYTE, frame->data[1]);
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_video_width / 2, m_video_height / 2, GL_RG, GL_UNSIGNED_BYTE, frame->data[1]);
    }

    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

void ModernRender::Destroy() {
    if (m_textures[0] != 0) {
        glDeleteTextures(3, m_textures);
        m_textures[0] = m_textures[1] = m_textures[2] = 0;
    }
    if (m_vbo != 0) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_vao != 0) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }

    m_textures_allocated = false;
    m_current_format = -1;
    m_video_width = 0;
    m_video_height = 0;
}
