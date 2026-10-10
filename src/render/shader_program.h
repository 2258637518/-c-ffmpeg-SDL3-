#pragma once

#include <glad/glad.h>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>
#include <filesystem>

/**
 * @brief 着色器程序封装类 (负责 GLSL 读取、GPU 编译、链接与激活)
 */
class ShaderProgram {
public:
    ShaderProgram() : program_id_(0) {}
    ~ShaderProgram() {
        if (program_id_ != 0) {
            glDeleteProgram(program_id_);
        }
    }

    // 禁用拷贝以防止 GPU 资源句柄被重复释放
    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;

    /**
     * @brief 加载并编译顶点着色器与片段着色器，链接为着色器程序
     */
    bool Init(const std::string& vertex_path, const std::string& fragment_path) {
        // 1. 读取着色器文本内容
        std::string vertex_code = ReadFile(vertex_path);
        std::string fragment_code = ReadFile(fragment_path);
        if (vertex_code.empty() || fragment_code.empty()) {
            return false;
        }

        const char* v_code_cstr = vertex_code.c_str();
        const char* f_code_cstr = fragment_code.c_str();

        // 2. 编译顶点着色器 (Vertex Shader)
        unsigned int vertex_shader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertex_shader, 1, &v_code_cstr, nullptr);
        glCompileShader(vertex_shader);
        if (!CheckCompileErrors(vertex_shader, "VERTEX")) {
            glDeleteShader(vertex_shader);
            return false;
        }

        // 3. 编译片段着色器 (Fragment Shader)
        unsigned int fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragment_shader, 1, &f_code_cstr, nullptr);
        glCompileShader(fragment_shader);
        if (!CheckCompileErrors(fragment_shader, "FRAGMENT")) {
            glDeleteShader(vertex_shader);
            glDeleteShader(fragment_shader);
            return false;
        }

        // 4. 链接为完整的着色器程序 (Shader Program)
        program_id_ = glCreateProgram();
        glAttachShader(program_id_, vertex_shader);
        glAttachShader(program_id_, fragment_shader);
        glLinkProgram(program_id_);
        if (!CheckCompileErrors(program_id_, "PROGRAM")) {
            glDeleteShader(vertex_shader);
            glDeleteShader(fragment_shader);
            return false;
        }

        // 5. 链接成功后，中间的着色器对象已不需要，及时释放显存
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        return true;
    }

    /**
     * @brief 激活该着色器程序 (调用 glUseProgram)
     */
    void Use() const {
        if (program_id_ != 0) {
            glUseProgram(program_id_);
        }
    }

    unsigned int GetId() const { return program_id_; }

private:
    unsigned int program_id_;

    // 辅助函数：读取磁盘文件文本（使用 std::filesystem 自动向上探测工程路径）
    std::string ReadFile(const std::string& file_path) {
        namespace fs = std::filesystem;
        std::vector<fs::path> candidates = {
            fs::path(file_path),
            fs::current_path() / file_path,
            fs::current_path() / ".." / file_path,
            fs::current_path() / ".." / ".." / file_path,
            fs::path("G:/my_project/ModernRender") / file_path
        };

        fs::path resolved_path;
        bool found = false;
        for (const auto& p : candidates) {
            std::error_code ec;
            if (fs::exists(p, ec) && !fs::is_directory(p, ec)) {
                resolved_path = p;
                found = true;
                break;
            }
        }

        if (!found) {
            std::cerr << "[ShaderProgram] Error: Cannot find shader file: " << file_path << std::endl;
            std::cerr << "  Current working directory is: " << fs::current_path().string() << std::endl;
            return "";
        }

        std::ifstream file(resolved_path);
        if (!file.is_open()) {
            std::cerr << "[ShaderProgram] Error: Failed to open file: " << resolved_path.string() << std::endl;
            return "";
        }

        std::stringstream stream;
        stream << file.rdbuf();
        return stream.str();
    }

    // 辅助函数：查询并打印显卡驱动编译/链接日志
    bool CheckCompileErrors(unsigned int id, const std::string& type) {
        int success = 0;
        char info_log[1024] = {0};

        if (type != "PROGRAM") {
            glGetShaderiv(id, GL_COMPILE_STATUS, &success);
            if (!success) {
                glGetShaderInfoLog(id, sizeof(info_log), nullptr, info_log);
                std::cerr << "[ShaderProgram] Compilation Error (" << type << "):\n" << info_log << std::endl;
                return false;
            }
        } else {
            glGetProgramiv(id, GL_LINK_STATUS, &success);
            if (!success) {
                glGetProgramInfoLog(id, sizeof(info_log), nullptr, info_log);
                std::cerr << "[ShaderProgram] Linking Error (" << type << "):\n" << info_log << std::endl;
                return false;
            }
        }
        return true;
    }
};
