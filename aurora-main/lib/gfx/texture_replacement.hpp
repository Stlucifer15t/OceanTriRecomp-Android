#pragma once

#include "texture.hpp"
#include "texture_convert.hpp"
#include <aurora/native_texture.hpp>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace aurora::gfx::texture_replacement {
void initialize() noexcept;
void shutdown() noexcept;
void register_tlut(const GXTlutObj* obj, const void* data, GXTlutFmt format, uint16_t entries) noexcept;
void load_tlut(const GXTlutObj* obj, uint32_t idx) noexcept;
std::optional<TextureHandle> find_replacement(const GXTexObj_& obj) noexcept;
std::string build_texture_replacement_name(const GXTexObj_& obj) noexcept;

// Native texture replacement support
std::optional<ConvertedTexture> load_native_replacement(const NativeTextureSource& source) noexcept;
size_t get_replacement_count() noexcept;
void set_enabled(bool enabled) noexcept;
bool is_enabled() noexcept;
// Managed runtime mod roots, highest priority first. Call on the GL thread,
// then reload(). Empty restores the legacy fixed-root behavior exactly.
void set_managed_roots(std::vector<std::filesystem::path> roots) noexcept;
void reload() noexcept;
} // namespace aurora::gfx::texture_replacement
