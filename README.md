## Abstract

My Master Thesis project, written in C++14 using the Vulkan API. Runs on Linux and Windows.

This project explores the concept of a “distributed rendering engine” for videogames with the aim to split
the graphics pipeline between a server and a client. My goal is to create a hybrid model between the 
"classic heavyweight client" model (where the client makes all the processing needed for rendering) 
and the more recent "streaming" model (where the server does all the processing and sends the stream of rendered frames to the client).

In my project, models, textures and shaders live on the server, which does most of the application-stage work. 
Then, rather than rendering models and sending frames to the client, it sends preprocessed geometry data to it, 
which in turn runs all the following pipeline stages.

## Building

Requirements:

- CMake >= 2.8
- Vulkan
- GLFW
- GLM
- Assimp

Linux: the usual `cmake . && make` should suffice. Executables will be under the `build/` directory. 
Maybe the `models` and `shaders` directories must be symlinked/copied under the correct path in order to run.

Windows: you can build with Visual Studio by first running `cmake -G "Visual Studio [version]"` (same caveats for the directories as Linux).
