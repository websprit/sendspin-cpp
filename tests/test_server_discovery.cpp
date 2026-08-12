// Copyright 2026 Sendspin Contributors
// Licensed under the Apache License, Version 2.0.

#include "sendspin/server_discovery.h"
#include <gtest/gtest.h>

namespace sendspin {

TEST(ServerDiscovery, BuildsDefaultWebsocketUrl) {
    const DiscoveredServer server{"Kitchen", "192.168.1.20", 8927, ""};
    EXPECT_EQ(server.websocket_url(), "ws://192.168.1.20:8927/sendspin");
}

TEST(ServerDiscovery, NormalizesAdvertisedPath) {
    const DiscoveredServer server{"Kitchen", "192.168.1.20", 9000, "custom"};
    EXPECT_EQ(server.websocket_url(), "ws://192.168.1.20:9000/custom");
}

TEST(ServerDiscovery, PreservesAbsoluteAdvertisedPath) {
    const DiscoveredServer server{"Kitchen", "192.168.1.20", 9000, "/sendspin"};
    EXPECT_EQ(server.websocket_url(), "ws://192.168.1.20:9000/sendspin");
}

}  // namespace sendspin
