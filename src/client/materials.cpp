#include "materials.hpp"
#include "client_resources.hpp"
#include "logging.hpp"

using namespace logging;

Material createMaterial(const shared::Material& mat, const NetworkResources& netRsrc)
{
	Material finalMat = {};
	{
		auto it = netRsrc.textures.find(mat.diffuseTex);
		if (it == netRsrc.textures.end()) {
			warn("Warning: diffuse texture ", mat.diffuseTex, " not found for material ", mat.name);
			finalMat.diffuse = netRsrc.defaults.diffuseTex.view;
		} else {
			finalMat.diffuse = it->second.view;
		}
	}
	{
		auto it = netRsrc.textures.find(mat.specularTex);
		if (it == netRsrc.textures.end()) {
			warn("Warning: specular texture ", mat.specularTex, " not found for material ", mat.name);
			finalMat.specular = netRsrc.defaults.specularTex.view;
		} else {
			finalMat.specular = it->second.view;
		}
	}
	{
		auto it = netRsrc.textures.find(mat.normalTex);
		if (it == netRsrc.textures.end()) {
			warn("Warning: normal texture ", mat.normalTex, " not found for material ", mat.name);
			finalMat.normal = netRsrc.defaults.normalTex.view;
		} else {
			finalMat.normal = it->second.view;
		}
	}
	return finalMat;
}
