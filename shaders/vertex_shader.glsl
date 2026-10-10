#version 330 core

    // 1. 从 CPU 的 VBO 接收输入属性 (Input Attributes)
    // location = 0 对应我们在 C++ 中配置的第 0 号属性通道（顶点屏幕坐标 X, Y）
    layout (location = 0) in vec2 aPos;

    // location = 1 对应第 1 号属性通道（纹理贴图坐标 U, V）
    layout (location = 1) in vec2 aTexCoord;

    // 2. 输出给下一棒（片段着色器 Fragment Shader）的变量
    out vec2 TexCoord;

    void main()
    {
        // gl_Position 是 OpenGL 内置的关键字，代表顶点的最终屏幕裁剪坐标
        // 思考：我们的输入 aPos 只有二维 (X, Y)，但 gl_Position 需要四维向量 (X, Y, Z, W)
        // 2D 视频不需要深度(Z)，所以 Z 取 0.0，齐次坐标 W 保持 1.0
        gl_Position = vec4(aPos.x, aPos.y, 0.0, 1.0);

        // 将纹理坐标传递下去
        TexCoord = aTexCoord;
    }