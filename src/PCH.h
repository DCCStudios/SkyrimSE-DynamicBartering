#pragma once

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include <spdlog/sinks/basic_file_sink.h>

#include <SimpleIni.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <format>
#include <unordered_map>
#include <vector>

using namespace std::literals;
namespace logger = SKSE::log;
