#pragma once

#include "endpoint_xplatform.hpp"
#include "server.hpp"
#include "shared_resources.hpp"
#include <string>

struct ResourceBatch;

/** @return Number of bytes sent, or -1 */
int64_t batch_sendTexture(socket_t clientSocket, Server& server, const std::string& texName, shared::TextureFormat fmt);

bool sendResourceBatch(socket_t clientSocket, Server& server, const ResourceBatch& batch, TexturesQueue& texturesQueue);
