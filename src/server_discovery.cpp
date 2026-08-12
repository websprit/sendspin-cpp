// Copyright 2026 Sendspin Contributors
// Licensed under the Apache License, Version 2.0.

#include "sendspin/server_discovery.h"

#include "platform/server_discovery.h"

namespace sendspin {

std::string DiscoveredServer::websocket_url() const {
    const std::string normalized_path = path.empty()          ? "/sendspin"
                                        : path.front() == '/' ? path
                                                              : "/" + path;
    return "ws://" + host + ":" + std::to_string(port) + normalized_path;
}

ServerDiscovery::ServerDiscovery() : available_(platform_server_discovery_init()) {}

ServerDiscovery::~ServerDiscovery() = default;

bool ServerDiscovery::available() const {
    return available_;
}

std::optional<DiscoveredServer> ServerDiscovery::discover(std::chrono::milliseconds timeout) {
    if (!available_ || timeout.count() <= 0) {
        return std::nullopt;
    }
    return platform_discover_server(timeout);
}

}  // namespace sendspin
