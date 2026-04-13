#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <initguid.h>
#include <audioclient.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <spatialaudioclient.h>
#include <xaudio2.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <list>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
