#pragma once

namespace cfg {

constexpr int WIDTH = 800;
constexpr int HEIGHT = 600;

constexpr auto MODEL_PATH = "models/chalet.obj";
constexpr auto TEXTURE_PATH = "textures/chalet.jpg";
//constexpr auto TEXTURE_PATH = "textures/texture.jpg";

constexpr uint32_t PACKET_MAGIC = 0x14101991;
constexpr size_t PACKET_SIZE_BYTES = 512;

constexpr const char *CLIENT_PASSIVE_IP = "127.0.0.1";
constexpr int CLIENT_PASSIVE_PORT = 1234;

constexpr const char *CLIENT_ACTIVE_IP = "127.0.0.1";
constexpr int CLIENT_ACTIVE_PORT = 1235;

constexpr const char *SERVER_PASSIVE_IP = "127.0.0.1";
constexpr int SERVER_PASSIVE_PORT = 1235;

constexpr const char *SERVER_ACTIVE_IP = "127.0.0.1";
constexpr int SERVER_ACTIVE_PORT = 1234;

}
