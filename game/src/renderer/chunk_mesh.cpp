#include "renderer/chunk_mesh.hpp"

#include <algorithm>
#include <cmath>

#include <glad/gl.h>

namespace mc {

void GpuMesh::create() {
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ibo);
    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);

    // Compact PackedVertex (24 B): integer attributes unpacked in chunk.vert.
    const GLsizei stride = sizeof(PackedVertex);
    // position: world x, y, z as int16 (whole blocks, lossless)
    glEnableVertexAttribArray(0);
    glVertexAttribIPointer(0, 4, GL_SHORT, stride, reinterpret_cast<void*>(0));
    // uv: u(16) v(16), quantized uv/16*65535
    glEnableVertexAttribArray(1);
    glVertexAttribIPointer(1, 1, GL_UNSIGNED_INT, stride, reinterpret_cast<void*>(8));
    // tile, block_light, sky_light, ao
    glEnableVertexAttribArray(2);
    glVertexAttribIPointer(2, 4, GL_UNSIGNED_BYTE, stride, reinterpret_cast<void*>(12));
    // face, r, g, b
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 4, GL_UNSIGNED_BYTE, stride, reinterpret_cast<void*>(16));
    // alpha
    glEnableVertexAttribArray(4);
    glVertexAttribIPointer(4, 1, GL_UNSIGNED_BYTE, stride, reinterpret_cast<void*>(20));

    glBindVertexArray(0);
    valid = true;
}

void GpuMesh::destroy() {
    if (vao) glDeleteVertexArrays(1, &vao);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (ibo) glDeleteBuffers(1, &ibo);
    vao = vbo = ibo = 0;
    index_count = 0;
    valid = false;
}

void GpuMesh::upload(const std::vector<Vertex>& verts, const std::vector<uint32_t>& idx) {
    if (!valid) create();
    // Pack 32 B float vertices into the 20 B GPU layout. Positions are whole
    // blocks (lossless); UVs quantize to 1/4096th of a block face — 4x finer
    // than one texel, visually identical.
    std::vector<PackedVertex> packed(verts.size());
    for (size_t i = 0; i < verts.size(); ++i) {
        const Vertex& v = verts[i];
        PackedVertex& p = packed[i];
        p.x = static_cast<int16_t>(std::lround(v.x));
        p.y = static_cast<int16_t>(std::lround(v.y));
        p.z = static_cast<int16_t>(std::lround(v.z));
        p.pad_pos = 0;
        const auto qu = static_cast<uint32_t>(
            std::lround(std::clamp(v.u, 0.0f, 16.0f) / 16.0f * 65535.0f));
        const auto qv = static_cast<uint32_t>(
            std::lround(std::clamp(v.v, 0.0f, 16.0f) / 16.0f * 65535.0f));
        p.uv = qu | (qv << 16);
        p.tile = static_cast<uint8_t>(v.w);
        p.bl = v.bl; p.sl = v.sl; p.ao = v.ao;
        p.face = v.face;
        p.r = v.r; p.g = v.g; p.b = v.b; p.a = v.a;
        p.pad0 = p.pad1 = p.pad2 = 0;
    }
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(packed.size() * sizeof(PackedVertex)),
                 packed.empty() ? nullptr : packed.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(idx.size() * sizeof(uint32_t)),
                 idx.empty() ? nullptr : idx.data(), GL_DYNAMIC_DRAW);
    index_count = static_cast<int>(idx.size());
}

void GpuMesh::draw() const {
    if (!valid || index_count == 0) return;
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

} // namespace mc
