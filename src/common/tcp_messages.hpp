#pragma once

#include <cstdint>
#include <ostream>

enum class TcpMsgType : uint8_t {
	/** Handshake */
	HELO = 0x01,
	HELO_ACK = 0x02,
	/** Client is ready to receive frame data */
	READY = 0x03,
	/** Keep the connection alive */
	KEEPALIVE = 0x04,
	/** Announce own disconnection */
	DISCONNECT = 0x05,
	START_RSRC_EXCHANGE = 0x06,
	RSRC_EXCHANGE_ACK = 0x07,
	RSRC_TYPE_TEXTURE = 0x08,
	/** The packet is part of a resource payload previously started */
	// RSRC_CONT           = 0x09,
	RSRC_TYPE_MATERIAL = 0x0A,
	RSRC_TYPE_MODEL = 0x0B,
	RSRC_TYPE_POINT_LIGHT = 0x0C,
	RSRC_TYPE_SHADER = 0x0D,
	END_RSRC_EXCHANGE = 0x1F,
	/** Tell client to start receiving UDP data */
	START_STREAMING = 0x20,
	END_STREAMING = 0x21,
	/** Client asks the server to send a specific model.
	 *  Follows a 2 bytes payload with the "model number"
	 *  (an arbitrary index into some model list on the server)
	 */
	REQ_MODEL = 0x22,
	UNKNOWN,
};

inline std::ostream& operator<<(std::ostream& s, TcpMsgType msg)
{
	switch (msg) {
		using M = TcpMsgType;
	case M::HELO:
		s << "HELO";
		break;
	case M::HELO_ACK:
		s << "HELO_ACK";
		break;
	case M::READY:
		s << "READY";
		break;
	case M::KEEPALIVE:
		s << "KEEPALIVE";
		break;
	case M::DISCONNECT:
		s << "DISCONNECT";
		break;
	case M::START_RSRC_EXCHANGE:
		s << "START_RSRC_EXCHANGE";
		break;
	case M::RSRC_EXCHANGE_ACK:
		s << "RSRC_EXCHANGE_ACK";
		break;
	case M::RSRC_TYPE_TEXTURE:
		s << "RSRC_TYPE_TEXTURE";
		break;
	case M::RSRC_TYPE_MATERIAL:
		s << "RSRC_TYPE_MATERIAL";
		break;
	case M::RSRC_TYPE_MODEL:
		s << "RSRC_TYPE_MODEL";
		break;
	case M::RSRC_TYPE_POINT_LIGHT:
		s << "RSRC_TYPE_POINT_LIGHT";
		break;
	case M::RSRC_TYPE_SHADER:
		s << "RSRC_TYPE_SHADER";
		break;
	case M::END_RSRC_EXCHANGE:
		s << "END_RSRC_EXCHANGE";
		break;
	case M::START_STREAMING:
		s << "START_STREAMING";
		break;
	case M::END_STREAMING:
		s << "END_STREAMING";
		break;
	case M::REQ_MODEL:
		s << "REQ_MODEL";
		break;
	default:
		s << "UNKNOWN";
		break;
	}
	return s;
}

constexpr TcpMsgType byte2tcpmsg(uint8_t byte)
{
	return byte == 0 || byte > static_cast<uint8_t>(TcpMsgType::UNKNOWN) ? TcpMsgType::UNKNOWN
									     : static_cast<TcpMsgType>(byte);
}

constexpr uint8_t tcpmsg2byte(TcpMsgType type)
{
	return type == TcpMsgType::UNKNOWN ? 0 : static_cast<uint8_t>(type);
}

#pragma pack(push, 1)

/** Template for the TCP messages used to send resources.
 *  It consists of a common header and a payload of type `ResType`.
 *  `ResType` is the actual content of the message, and is typically one of
 *  the structs defined in shared_resources.hpp.
 */
template <typename ResType>
struct ResourcePacket {
	TcpMsgType type;
	ResType res;
};

#pragma pack(pop)
