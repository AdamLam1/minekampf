#include "renderer/shader.hpp"

#include <fstream>
#include <sstream>

#include <glad/gl.h>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

namespace mc {

namespace {
// Resolve a data path: try CWD first, then fall back to the directory that
// contains the executable (shaders/ are copied next to the binary). This
// lets the game run from any working directory.
std::string resolve_path(std::string_view path) {
    std::string p(path);
    {
        std::ifstream in(p, std::ios::binary);
        if (in.good()) return p;
    }
    std::string base;
#ifdef _WIN32
    char exe_path[MAX_PATH] = {};
    if (GetModuleFileNameA(nullptr, exe_path, MAX_PATH) > 0) {
        std::string dir(exe_path);
        auto slash = dir.find_last_of("\\/");
        if (slash != std::string::npos) dir.resize(slash);
        base = dir;
    }
#else
    char exe_path[PATH_MAX] = {};
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len > 0) {
        exe_path[len] = '\0';
        std::string dir(exe_path);
        auto slash = dir.find_last_of('/');
        if (slash != std::string::npos) dir.resize(slash);
        base = dir;
    }
#endif
    if (!base.empty()) {
        std::string full = base + "/" + p;
        std::ifstream in(full, std::ios::binary);
        if (in.good()) return full;
    }
    return p; // let the caller produce the "not found" error
}

std::string read_file(std::string_view path) {
    std::ifstream in(resolve_path(path), std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

uint32_t compile_stage(uint32_t type, const std::string& src) {
    uint32_t shader = glCreateShader(type);
    const char* cstr = src.c_str();
    glShaderSource(shader, 1, &cstr, nullptr);
    glCompileShader(shader);
    int ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        spdlog::error("Shader compile failed: {}", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}
} // namespace

Shader& Shader::operator=(Shader&& o) noexcept {
    if (this != &o) {
        destroy();
        program_ = o.program_;
        o.program_ = 0;
    }
    return *this;
}

Shader::~Shader() { destroy(); }

void Shader::destroy() {
    if (program_) {
        glDeleteProgram(program_);
        program_ = 0;
    }
}

bool Shader::load_from_files(std::string_view vert_path, std::string_view frag_path) {
    std::string vsrc = read_file(vert_path);
    std::string fsrc = read_file(frag_path);
    if (vsrc.empty() || fsrc.empty()) {
        spdlog::error("Could not read shader files: {} / {}", std::string(vert_path), std::string(frag_path));
        return false;
    }
    uint32_t vs = compile_stage(GL_VERTEX_SHADER, vsrc);
    uint32_t fs = compile_stage(GL_FRAGMENT_SHADER, fsrc);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }

    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    int ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
        spdlog::error("Shader link failed: {}", log);
        glDeleteProgram(program_);
        program_ = 0;
        glDeleteShader(vs);
        glDeleteShader(fs);
        return false;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return true;
}

void Shader::use() const { glUseProgram(program_); }

void Shader::set_int(const char* name, int v) const { glUniform1i(glGetUniformLocation(program_, name), v); }
void Shader::set_float(const char* name, float v) const { glUniform1f(glGetUniformLocation(program_, name), v); }
void Shader::set_vec2(const char* name, float x, float y) const { glUniform2f(glGetUniformLocation(program_, name), x, y); }
void Shader::set_vec3(const char* name, float x, float y, float z) const {
    glUniform3f(glGetUniformLocation(program_, name), x, y, z);
}
void Shader::set_vec4(const char* name, float x, float y, float z, float w) const {
    glUniform4f(glGetUniformLocation(program_, name), x, y, z, w);
}
void Shader::set_mat4(const char* name, const float* m) const {
    glUniformMatrix4fv(glGetUniformLocation(program_, name), 1, GL_FALSE, m);
}

} // namespace mc
