#pragma once

#include <cstdint>
#include <memory>

struct AVFrame;

class ShaderProgram;
class ModernRender {

public:
    ModernRender();
    ~ModernRender();

    // 严禁拷贝：显卡资源句柄具有唯一所有权
    ModernRender(const ModernRender&) = delete;
    ModernRender& operator=(const ModernRender&) = delete;

    // 1. 初始化 GPU 渲染环境（编译加载 VS/FS、创建配置 VAO/VBO、设置默认状态）
    bool Init();

    // 2. 窗口大小改变时通知（根据当前视频宽高比重新计算 Letterbox 居中视口）
    void Resize(int window_width, int window_height);

    // 3. 核心上屏渲染接口：输入解码器产出的 AVFrame
    void RenderFrame(const AVFrame* frame);

    // 4. 显存资源释放（释放纹理、VAO、VBO 与 Shader）
    void Destroy();

private:
    void SetupBuffers();
    void SetupViewport();
    void UpdateYUV420PTextures(const AVFrame* frame);
    void UpdateNV12Textures(const AVFrame* frame);

private:
    // 显存资源句柄
    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    unsigned int m_textures[3] = { 0 }; // 纹理句柄缓存

    // 着色器程序对象
    std::unique_ptr<ShaderProgram> m_yuv420p_shader;
    std::unique_ptr<ShaderProgram> m_nv12_shader;

    // 状态缓存（分辨率变化时需重新分配纹理显存）
    int m_video_width = 0;
    int m_video_height = 0;
    int m_window_width = 0;
    int m_window_height = 0;
    int m_current_format = -1; // 记录当前像素格式 (AV_PIX_FMT_YUV420P / AV_PIX_FMT_NV12)
    bool m_textures_allocated = false; // 显存是否已完成分配
};
