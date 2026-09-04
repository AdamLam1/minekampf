#pragma once

// Crash-safe file primitives shared by every save writer/loader:
//   write_atomic    : tmp file -> rotate old to .bak -> rename into place
//   read_with_fallback : target file, else target.bak (crash mid-rotate)
// Failure modes covered: power loss during write leaves the previous copy in
// .bak and the half-written data only ever in .tmp (never loaded).

#include <filesystem>
#include <fstream>
#include <vector>

namespace mc::savefs {

namespace fs = std::filesystem;

// Writes `data` atomically: fills `target.tmp`, rotates the current file to
// `target.bak`, then renames tmp over target. Returns false if any step
// failed (data stays recoverable: old copy in .bak, new copy in .tmp).
inline bool write_atomic(const fs::path& target, const void* data, size_t size) {
    fs::path tmp = target.string() + ".tmp";
    fs::path bak = target.string() + ".bak";

    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        if (size > 0) out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        out.flush();
        if (!out.good()) return false;
    } // close before renames

    std::error_code ec;
    if (fs::exists(target, ec)) {
        fs::remove(bak, ec);
        fs::rename(target, bak, ec);
        if (ec) {
            // Locked or unrenamable: try remove+rename as a fallback so the
            // new data still lands; the .bak copy is already safe.
            fs::remove(target, ec);
            fs::rename(tmp, target, ec);
            return !ec;
        }
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        // Restore the old file from .bak so the game never sees a hole.
        if (fs::exists(bak, ec)) fs::rename(bak, target, ec);
        return false;
    }
    return true;
}

// Reads `target`, falling back to `target.bak` when the main file is
// missing, unreadable, or REJECTED BY THE VALIDATOR (e.g. a parse check).
// A corrupt-but-readable main file must not shadow the good .bak copy.
// Returns false when neither candidate yields a valid payload.
template <typename Validator>
bool read_with_fallback_validated(const fs::path& target, std::vector<uint8_t>& out,
                                  Validator&& valid) {
    for (const fs::path& candidate : {target, fs::path(target.string() + ".bak")}) {
        std::error_code ec;
        if (!fs::exists(candidate, ec)) continue;
        std::ifstream in(candidate, std::ios::binary | std::ios::ate);
        if (!in) continue;
        std::streamsize size = in.tellg();
        if (size <= 0) continue;
        in.seekg(0, std::ios::beg);
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        in.read(reinterpret_cast<char*>(bytes.data()), size);
        if (!(in.good() || in.eof())) continue;
        if (!valid(bytes)) continue; // corrupt main -> try the .bak copy
        out = std::move(bytes);
        return true;
    }
    return false;
}

// Raw variant: any non-empty readable file counts as valid.
inline bool read_with_fallback(const fs::path& target, std::vector<uint8_t>& out) {
    return read_with_fallback_validated(target, out,
                                        [](const std::vector<uint8_t>&) { return true; });
}

} // namespace mc::savefs
