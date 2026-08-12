// Copyright 2026 Sendspin Contributors
// Licensed under the Apache License, Version 2.0.

#include "platform/server_discovery.h"

#include "esp_netif.h"
#include "mdns.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

namespace sendspin {
namespace {

constexpr char SERVICE_TYPE[] = "_sendspin-server";
constexpr char SERVICE_PROTOCOL[] = "_tcp";

const mdns_ip_addr_t* preferred_ipv4(const mdns_result_t& result) {
    esp_netif_ip_info_t station_info{};
    esp_netif_t* station = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    const bool have_station_info =
        station != nullptr && esp_netif_get_ip_info(station, &station_info) == ESP_OK;
    const mdns_ip_addr_t* fallback = nullptr;

    for (const mdns_ip_addr_t* address = result.addr; address != nullptr; address = address->next) {
        if (address->addr.type != ESP_IPADDR_TYPE_V4)
            continue;
        if (fallback == nullptr)
            fallback = address;
        if (have_station_info && (address->addr.u_addr.ip4.addr & station_info.netmask.addr) ==
                                     (station_info.ip.addr & station_info.netmask.addr)) {
            return address;
        }
    }
    return fallback;
}

std::string txt_path(const mdns_result_t& result) {
    for (size_t index = 0; index < result.txt_count; ++index) {
        const mdns_txt_item_t& item = result.txt[index];
        if (item.key == nullptr || std::strcmp(item.key, "path") != 0 || item.value == nullptr) {
            continue;
        }
        const size_t length =
            result.txt_value_len != nullptr ? result.txt_value_len[index] : std::strlen(item.value);
        if (length > 0)
            return std::string(item.value, length);
    }
    return "/sendspin";
}

}  // namespace

bool platform_server_discovery_init() {
    const esp_err_t result = mdns_init();
    return result == ESP_OK || result == ESP_ERR_INVALID_STATE;
}

std::optional<DiscoveredServer> platform_discover_server(std::chrono::milliseconds timeout) {
    const auto timeout_ms = static_cast<uint32_t>(
        std::min<int64_t>(timeout.count(), std::numeric_limits<uint32_t>::max()));
    mdns_result_t* results = nullptr;
    const esp_err_t query_result =
        mdns_query_ptr(SERVICE_TYPE, SERVICE_PROTOCOL, timeout_ms, 8, &results);
    if (query_result != ESP_OK)
        return std::nullopt;

    std::optional<DiscoveredServer> discovered;
    for (const mdns_result_t* result = results; result != nullptr; result = result->next) {
        const mdns_ip_addr_t* address = preferred_ipv4(*result);
        if (address == nullptr || result->port == 0)
            continue;

        char host[16]{};
        std::snprintf(host, sizeof(host), IPSTR, IP2STR(&address->addr.u_addr.ip4));
        discovered = DiscoveredServer{
            result->instance_name != nullptr ? result->instance_name : "Sendspin server",
            host,
            result->port,
            txt_path(*result),
        };
        break;
    }
    mdns_query_results_free(results);
    return discovered;
}

}  // namespace sendspin
