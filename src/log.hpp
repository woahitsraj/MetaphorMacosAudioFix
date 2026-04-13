#pragma once

#include "stdafx.h"

namespace Log {
void Init(const std::filesystem::path& path, bool verbose);
void Info(const char* fmt, ...);
void Warn(const char* fmt, ...);
void Error(const char* fmt, ...);
void Shutdown();
}
