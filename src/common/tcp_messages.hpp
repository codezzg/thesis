#pragma once

#include <cstdint>
#include <ostream>

enum class MsgType : uint8_t {
	/** Handshake */
	HELO                = 0x01,
	HELO_ACK            = 0x02,
	/** Client is ready to receive frame data */
	READY               = 0x03,
	/** Keep the connection alive */
	KEEPALIVE           = 0x04,
	/** Announce own disconnection */
	DISCONNECT          = 0x05,
	START_DATA_EXCHANGE = 0x06,
	DATA_EXCHANGE_ACK   = 0x07,
	DATA_TYPE_TEXTURE   = 0x08,
	/** The packet is part of a data payload previously started */
	//DATA_CONT           = 0x09,
	UNKNOWN,
};

inline std::ostream& operator<<(std::ostream& s, MsgType msg) {
	switch (msg) {
		using M = MsgType;
	case M::HELO:                s << "HELO"; break;
	case M::HELO_ACK:            s << "HELO_ACK"; break;
	case M::READY:               s << "READY"; break;
	case M::KEEPALIVE:           s << "KEEPALIVE"; break;
	case M::DISCONNECT:          s << "DISCONNECT"; break;
	case M::START_DATA_EXCHANGE: s << "START_DATA_EXCHANGE"; break;
	case M::DATA_EXCHANGE_ACK:   s << "DATA_EXCHANGE_ACK"; break;
	case M::DATA_TYPE_TEXTURE:   s << "DATA_TYPE_TEXTURE"; break;
	default:                     s << "UNKNOWN"; break;
	}
	return s;
}

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
