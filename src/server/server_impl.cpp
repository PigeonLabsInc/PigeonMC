#include "core/config.hpp"
#include "core/logger.hpp"
#include "core/performance_monitor.hpp"
#include "world/block.hpp"
#include "world/chunk.hpp"
#include "player/player.hpp"
#include "entity/entity.hpp"

namespace mc {

ServerConfig g_config;
Logger g_logger;
PerformanceMonitor g_performance_monitor;

}

namespace mc::world {

BlockRegistry g_block_registry;
ChunkManager g_chunk_manager;

}

namespace mc::player {

PlayerManager g_player_manager;

}

namespace mc::entity {

EntityManager g_entity_manager;

}
