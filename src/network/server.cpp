#include "server.hpp"
#include "core/config.hpp"
#include "core/logger.hpp"
#include <memory>

namespace mc {

std::unique_ptr<network::NetworkServer> g_network_server;

void start_network_server() {
    if (g_network_server) return;
    g_network_server = std::make_unique<network::NetworkServer>(g_config.get_host(), g_config.get_port(), g_config.get_io_threads());
    g_network_server->start();
    g_logger.info("Network server started");
}

void stop_network_server() {
    if (!g_network_server) return;
    g_network_server->stop();
    g_network_server.reset();
    g_logger.info("Network server stopped");
}

}
