#pragma once

#include <bitset>
#include <glm/glm.hpp>

class ShaderOpts {
	std::bitset<2> shaderOpts;

public:
	enum Opt {
		SHOW_GBUF_TEX = 0,
		USE_NORMAL_MAP = 1,
	};

	ShaderOpts()
	{
		setOpt(SHOW_GBUF_TEX, false);
		setOpt(USE_NORMAL_MAP, true);
	}

	void setOpt(Opt opt, bool val) { shaderOpts[opt] = val; }
	void flip(Opt opt) { shaderOpts.flip(opt); }

	glm::i32 getRepr() const { return static_cast<glm::i32>(shaderOpts.to_ulong()); }
};

