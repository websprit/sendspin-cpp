// Copyright 2026 Sendspin Contributors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0

/// @file server_discovery.h
/// @brief Cross-platform mDNS discovery for Sendspin servers

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace sendspin {

/// @brief A server advertised as _sendspin-server._tcp.local.
struct DiscoveredServer {
    std::string name;
    std::string host;
    uint16_t port{0};
    std::string path{"/sendspin"};

    /// @brief Returns the WebSocket URL accepted by SendspinClient::connect_to().
    std::string websocket_url() const;
};

/// @brief Discovers Sendspin servers with the platform mDNS implementation.
///
/// ESP-IDF uses espressif/mdns. Linux uses the Avahi Bonjour-compatibility
/// library (libavahi-compat-libdnssd-dev). Calls are synchronous and should not
/// run concurrently with a latency-sensitive audio callback.
class ServerDiscovery {
public:
    ServerDiscovery();
    ~ServerDiscovery();

    ServerDiscovery(const ServerDiscovery&) = delete;
    ServerDiscovery& operator=(const ServerDiscovery&) = delete;

    /// @brief Whether the platform discovery backend initialized successfully.
    bool available() const;

    /// @brief Finds the preferred IPv4 Sendspin server before timeout.
    /// @return A server endpoint, or nullopt when none was found.
    std::optional<DiscoveredServer> discover(std::chrono::milliseconds timeout);

private:
    bool available_{false};
};

}  // namespace sendspin
