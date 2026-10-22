#pragma once

#include "scene/Scene.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace daedalus
{
// Versioned binary encoding of a CanonicalScene for the content-addressed asset cache and batch
// conversion output. Little-endian, explicit field order, length-prefixed containers. The reader
// validates every count against the remaining payload before allocating, so truncated or corrupt
// payloads raise SceneSerializationError instead of over-allocating or reading out of bounds.
inline constexpr std::uint32_t kSceneSerializationVersion = 1U;

class SceneSerializationError final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] std::vector<std::byte> serialize_scene(const CanonicalScene& scene);
[[nodiscard]] CanonicalScene deserialize_scene(std::span<const std::byte> payload);
}
