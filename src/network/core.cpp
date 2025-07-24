#include "memory_pool.hpp"
#include "thread_pool.hpp"
#include "network/packet_types.hpp"

namespace mc {

BufferPool g_buffer_pool;
ThreadPool g_thread_pool;

}

namespace mc::network {

PacketManager g_packet_manager;

PacketManager::PacketManager() {
    register_packet<handshake::HandshakePacket>();
    
    register_packet<status::StatusRequestPacket>();
    register_packet<status::StatusResponsePacket>();
    register_packet<status::PingRequestPacket>();
    register_packet<status::PingResponsePacket>();
    
    register_packet<login::LoginStartPacket>();
    register_packet<login::LoginSuccessPacket>();
    
    register_packet<play::KeepAlivePacket>();
    register_packet<play::JoinGamePacket>();
    register_packet<play::PlayerPositionPacket>();
}

std::unique_ptr<Packet> PacketManager::create_packet(ConnectionState state, PacketDirection direction, i32 packet_id) const {
    auto state_it = registries_.find(state);
    if (state_it == registries_.end()) return nullptr;
    
    auto dir_it = state_it->second.find(direction);
    if (dir_it == state_it->second.end()) return nullptr;
    
    auto packet_it = dir_it->second.find(packet_id);
    if (packet_it == dir_it->second.end()) return nullptr;
    
    return packet_it->second();
}

}
