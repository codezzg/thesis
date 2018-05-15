#pragma once

namespace cfg {

constexpr int WIDTH = 800;
constexpr int HEIGHT = 600;

constexpr auto MODEL_PATH = "models/chalet.obj";
constexpr auto TEX_DIFFUSE_PATH = "textures/chalet.jpg";
constexpr auto TEX_SPECULAR_PATH = "textures/chalet_spec.jpg";
//constexpr auto TEXTURE_PATH = "textures/texture.jpg";

constexpr uint32_t PACKET_MAGIC = 0x14101991;
constexpr std::size_t PACKET_SIZE_BYTES = 512;

constexpr const char *CLIENT_PASSIVE_IP = "127.0.0.1";
constexpr int CLIENT_PASSIVE_PORT = 1234;

constexpr const char *CLIENT_ACTIVE_IP = "127.0.0.1";
constexpr int CLIENT_ACTIVE_PORT = 1235;

constexpr const char *SERVER_PASSIVE_IP = "127.0.0.1";
constexpr int SERVER_PASSIVE_PORT = 1235;

constexpr const char *SERVER_ACTIVE_IP = "127.0.0.1";
constexpr int SERVER_ACTIVE_PORT = 1234;

constexpr const char *SERVER_RELIABLE_IP = "127.0.0.1";
constexpr int SERVER_RELIABLE_PORT = 1236;

constexpr int CLIENT_KEEPALIVE_INTERVAL_SECONDS = 30;
constexpr int CLIENT_KEEPALIVE_MAX_ATTEMPTS = 4;

/** Interval after which a client is dropped if no keepalives are sent */
constexpr int SERVER_KEEPALIVE_INTERVAL_SECONDS = 60;
}
