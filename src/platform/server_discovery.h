// Copyright 2026 Sendspin Contributors
// Licensed under the Apache License, Version 2.0.

#pragma once

#include "sendspin/server_discovery.h"

#include <chrono>
#include <optional>

namespace sendspin {

bool platform_server_discovery_init();
std::optional<DiscoveredServer> platform_discover_server(std::chrono::milliseconds timeout);

}  // namespace sendspin
