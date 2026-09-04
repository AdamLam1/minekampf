#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace mc {

// Minimal GL shader program helper: compiles GLSL from source strings and
// links a program. Logs errors via spdlog.
class Shader {
public:
    Shader() = default;
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& o) noexcept : program_(o.program_) { o.program_ = 0; }
    Shader& operator=(Shader&& o) noexcept;

    // Load vertex + fragment shader sources from files, compile, link.
    [[nodiscard]] bool load_from_files(std::string_view vert_path, std::string_view frag_path);

    void use() const;
    void destroy();

    [[nodiscard]] uint32_t program() const { return program_; }

    void set_int(const char* name, int v) const;
    void set_float(const char* name, float v) const;
    void set_vec2(const char* name, float x, float y) const;
    void set_vec3(const char* name, float x, float y, float z) const;
    void set_vec4(const char* name, float x, float y, float z, float w) const;
    void set_mat4(const char* name, const float* m) const;

private:
    uint32_t program_ = 0;
};

} // namespace mc
