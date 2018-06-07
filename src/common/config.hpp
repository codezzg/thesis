#pragma once

#include <cstddef>
#include <cstdint>

namespace cfg {

constexpr int WIDTH = 800;
constexpr int HEIGHT = 600;

constexpr auto MODEL_PATH = "models/chalet.obj";
constexpr auto TEX_DIFFUSE_PATH = "textures/chalet.jpg";
constexpr auto TEX_SPECULAR_PATH = "textures/chalet_spec.jpg";
// constexpr auto TEXTURE_PATH = "textures/texture.jpg";

/** Maximum size of a texture sent via network */
constexpr auto MAX_TEXTURE_SIZE = 50 * 1024 * 1024;   // 50 MiB

constexpr uint32_t PACKET_MAGIC = 0x14101991;
constexpr std::size_t PACKET_SIZE_BYTES = 480;

constexpr int SERVER_TO_CLIENT_PORT = 1234;
constexpr int CLIENT_TO_SERVER_PORT = 1235;
constexpr int RELIABLE_PORT = 1236;

constexpr int CLIENT_KEEPALIVE_INTERVAL_SECONDS = 30;
constexpr int CLIENT_KEEPALIVE_MAX_ATTEMPTS = 4;

/** Interval after which a client is dropped if no keepalives are sent */
constexpr int SERVER_KEEPALIVE_INTERVAL_SECONDS = 60;
}   // namespace cfg
