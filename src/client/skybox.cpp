#include "skybox.hpp"
#include "application.hpp"
#include "buffer_array.hpp"
#include "logging.hpp"
#include "textures.hpp"
#include "vulk_errors.hpp"
#include <array>

using namespace logging;

Image createSkybox(const Application& app)
{
	return createTextureCube(app,
		{
			"textures/skybox/devils_advocate_rt.tga",
			"textures/skybox/devils_advocate_lf.tga",
			"textures/skybox/devils_advocate_up.tga",
			"textures/skybox/devils_advocate_dn.tga",
			"textures/skybox/devils_advocate_ft.tga",
			"textures/skybox/devils_advocate_bk.tga",
		});
}
