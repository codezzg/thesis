#pragma once

#include <cstdint>

enum class MsgType : uint8_t {
	/** Handshake */
	HELO       = 0x01,
	HELO_ACK   = 0x02,
	/** Client is ready to receive frame data */
	READY      = 0x03,
	/** Keep the connection alive */
	KEEPALIVE  = 0x04,
	/** Announce own disconnection */
	DISCONNECT = 0x05,
	UNKNOWN,
};

constexpr MsgType byte2msg(uint8_t byte) {
	return byte == 0 || byte > static_cast<uint8_t>(MsgType::UNKNOWN)
		? MsgType::UNKNOWN
		: static_cast<MsgType>(byte);
}

constexpr uint8_t msg2byte(MsgType type) {
	return type == MsgType::UNKNOWN
		? 0
		: static_cast<uint8_t>(type);
}
