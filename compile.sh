: ${COMPILER:=/usr/local/src/VulkanSDK/1.0.65.0/x86_64/bin/glslangValidator}

for sh in shaders/*.{vert,frag}; do
	$COMPILER -V $sh -o $sh.spv
done
