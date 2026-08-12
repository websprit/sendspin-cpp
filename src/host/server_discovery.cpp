// Copyright 2026 Sendspin Contributors
// Licensed under the Apache License, Version 2.0.

#include "platform/server_discovery.h"

#ifdef SENDSPIN_HAS_DNSSD
#include <arpa/inet.h>
#include <dns_sd.h>
#include <netdb.h>
#include <sys/select.h>
#endif

#include <chrono>
#include <string>

namespace sendspin {

#ifdef SENDSPIN_HAS_DNSSD
namespace {

struct BrowseContext {
    std::string name;
    std::string regtype;
    std::string domain;
    uint32_t interface_index{0};
};

void DNSSD_API browse_callback(DNSServiceRef, DNSServiceFlags flags, uint32_t interface_index,
                               DNSServiceErrorType error, const char* name, const char* regtype,
                               const char* domain, void* context) {
    if (error != kDNSServiceErr_NoError || !(flags & kDNSServiceFlagsAdd))
        return;
    auto& result = *static_cast<BrowseContext*>(context);
    if (!result.name.empty())
        return;
    result = {name, regtype, domain, interface_index};
}

struct ResolveContext {
    std::string host;
    uint16_t port{0};
    std::string path{"/sendspin"};
    bool complete{false};
};

void DNSSD_API resolve_callback(DNSServiceRef, DNSServiceFlags, uint32_t, DNSServiceErrorType error,
                                const char*, const char* hosttarget, uint16_t port,
                                uint16_t txt_len, const unsigned char* txt_record, void* context) {
    if (error != kDNSServiceErr_NoError)
        return;
    auto& result = *static_cast<ResolveContext*>(context);
    result.host = hosttarget;
    result.port = ntohs(port);
    uint8_t path_length = 0;
    const void* path = TXTRecordGetValuePtr(txt_len, txt_record, "path", &path_length);
    if (path != nullptr && path_length > 0) {
        result.path.assign(static_cast<const char*>(path), path_length);
    }
    result.complete = true;
}

template <typename Predicate>
bool process_until(DNSServiceRef ref, std::chrono::steady_clock::time_point deadline,
                   Predicate complete) {
    while (!complete() && std::chrono::steady_clock::now() < deadline) {
        const int fd = DNSServiceRefSockFD(ref);
        if (fd < 0)
            return false;
        const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(
            deadline - std::chrono::steady_clock::now());
        timeval select_timeout{};
        select_timeout.tv_sec = static_cast<long>(remaining.count() / 1000000);
        select_timeout.tv_usec = static_cast<long>(remaining.count() % 1000000);
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(fd, &read_fds);
        const int selected = select(fd + 1, &read_fds, nullptr, nullptr, &select_timeout);
        if (selected <= 0)
            return false;
        if (DNSServiceProcessResult(ref) != kDNSServiceErr_NoError)
            return false;
    }
    return complete();
}

std::optional<std::string> resolve_ipv4(const std::string& hostname) {
    struct addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* addresses = nullptr;
    if (getaddrinfo(hostname.c_str(), nullptr, &hints, &addresses) != 0)
        return std::nullopt;

    std::optional<std::string> result;
    for (const struct addrinfo* address = addresses; address != nullptr;
         address = address->ai_next) {
        char buffer[INET_ADDRSTRLEN]{};
        const auto* ipv4 = reinterpret_cast<const struct sockaddr_in*>(address->ai_addr);
        if (inet_ntop(AF_INET, &ipv4->sin_addr, buffer, sizeof(buffer)) != nullptr) {
            result = buffer;
            break;
        }
    }
    freeaddrinfo(addresses);
    return result;
}

}  // namespace
#endif

bool platform_server_discovery_init() {
#ifdef SENDSPIN_HAS_DNSSD
    return true;
#else
    return false;
#endif
}

std::optional<DiscoveredServer> platform_discover_server(std::chrono::milliseconds timeout) {
#ifndef SENDSPIN_HAS_DNSSD
    (void)timeout;
    return std::nullopt;
#else
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    BrowseContext browse_result;
    DNSServiceRef browse_ref = nullptr;
    const DNSServiceErrorType browse_error = DNSServiceBrowse(
        &browse_ref, 0, 0, "_sendspin-server._tcp", nullptr, browse_callback, &browse_result);
    if (browse_error != kDNSServiceErr_NoError)
        return std::nullopt;
    const bool found = process_until(browse_ref, deadline,
                                     [&browse_result] { return !browse_result.name.empty(); });
    DNSServiceRefDeallocate(browse_ref);
    if (!found)
        return std::nullopt;

    ResolveContext resolved;
    DNSServiceRef resolve_ref = nullptr;
    const DNSServiceErrorType resolve_error = DNSServiceResolve(
        &resolve_ref, 0, browse_result.interface_index, browse_result.name.c_str(),
        browse_result.regtype.c_str(), browse_result.domain.c_str(), resolve_callback, &resolved);
    if (resolve_error != kDNSServiceErr_NoError)
        return std::nullopt;
    const bool complete =
        process_until(resolve_ref, deadline, [&resolved] { return resolved.complete; });
    DNSServiceRefDeallocate(resolve_ref);
    if (!complete)
        return std::nullopt;

    const auto ipv4 = resolve_ipv4(resolved.host);
    if (!ipv4 || resolved.port == 0)
        return std::nullopt;
    return DiscoveredServer{browse_result.name, *ipv4, resolved.port, resolved.path};
#endif
}

}  // namespace sendspin
