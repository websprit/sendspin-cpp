# sendspin-cpp

[![CI](https://github.com/Sendspin/sendspin-cpp/actions/workflows/ci.yml/badge.svg)](https://github.com/Sendspin/sendspin-cpp/actions/workflows/ci.yml)
[![Component Registry](https://components.espressif.com/components/sendspin/sendspin-cpp/badge.svg)](https://components.espressif.com/components/sendspin/sendspin-cpp)

Standalone C++ library implementing the [Sendspin synchronized audio streaming protocol](https://www.sendspin-audio.com/). Builds on both ESP-IDF (ESP32) and host platforms (macOS/Linux). Designed to be consumed by ESPHome but has no ESPHome dependencies.

[![A project from the Open Home Foundation](https://www.openhomefoundation.org/badges/ohf-project.png)](https://www.openhomefoundation.org/)

## Features

- Modular Sendspin role composition: artwork, color, controller, metadata, player, and visualizer
- WebSocket client and server support
- mDNS server discovery for ESP32, macOS, and Linux
- Decodes FLAC, Opus, and PCM
- Cross-platform: ESP-IDF (ESP32) and host (macOS/Linux)

## Documentation

- **[Integration Guide](docs/integration-guide.md)** -- How to integrate sendspin-cpp into your application, including required callbacks, role composition, and platform setup
- **[Internals](docs/internals.md)** -- Internal architecture, threading model, and inter-class communication

## Build

### Host (macOS/Linux)

```bash
cmake -B build
cmake --build build
```

Dependencies (fetched automatically via CMake FetchContent): ArduinoJson, micro-flac, micro-opus, IXWebSocket.
On Linux, install Avahi's Bonjour compatibility development package to enable
mDNS server discovery:

```bash
sudo apt install libavahi-compat-libdnssd-dev
sudo apt install avahi-daemon libnss-mdns
```

The Avahi daemon provides the runtime DNS-SD service, and `libnss-mdns` lets the
host backend resolve advertised `.local` names to IPv4 addresses.

### Discover a server

```cpp
#include <chrono>
#include <sendspin/server_discovery.h>

sendspin::ServerDiscovery discovery;
if (const auto server = discovery.discover(std::chrono::seconds(2))) {
    client.connect_to(server->websocket_url());
}
```

The library browses `_sendspin-server._tcp.local`, resolves the advertised port
and `path` TXT value, and returns a normalized WebSocket URL. Discovery is a
synchronous operation; applications retain control of retry cadence, server
selection, and configured-address fallback.

### ESP-IDF

Available on the [ESP-IDF Component Registry](https://components.espressif.com/components/sendspin/sendspin-cpp). Add to your project's `idf_component.yml`:

```yaml
dependencies:
  sendspin/sendspin-cpp: ">=0.1.2"
```

Requires ESP-IDF v5.1 or later.

## Examples

- **`examples/basic_client/`** -- Standalone host example with PortAudio audio output
- **`examples/tui_client/`** -- Terminal UI host example with PortAudio audio output

## License

Apache 2.0
