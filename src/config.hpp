#pragma once

namespace cfg {

constexpr int WIDTH = 800;
constexpr int HEIGHT = 600;

constexpr auto MODEL_PATH = "models/chalet.obj";
constexpr auto TEXTURE_PATH = "textures/chalet.jpg";
//constexpr auto TEXTURE_PATH = "textures/texture.jpg";

constexpr uint32_t PACKET_MAGIC = 0x14101991;
constexpr size_t PACKET_SIZE_BYTES = 63000;

}
