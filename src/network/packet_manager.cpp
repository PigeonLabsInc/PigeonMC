#include "packet_types.hpp"
#include "chunk_packets.hpp"

namespace mc::network {

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
    register_packet<play::ChunkDataPacket>();
    register_packet<play::UnloadChunkPacket>();
    register_packet<play::UpdateViewPositionPacket>();
    register_packet<play::PlayerPositionAndLookPacket>();
    register_packet<play::BlockChangePacket>();
    register_packet<play::MultiBlockChangePacket>();
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
