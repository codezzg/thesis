#pragma once

using signal_handler_t = void(*)();

const char* xplatGetCwd();

bool xplatEnableExitHandler();
void xplatSetExitHandler(signal_handler_t handler);