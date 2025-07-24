#pragma once

#include "core/types.hpp"
#include "world/chunk.hpp"
#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>

namespace mc::entity {

enum class EntityType : u32 {
    PLAYER = 0,
    ITEM = 1,
    EXPERIENCE_ORB = 2,
    ARROW = 60,
    ZOMBIE = 54,
    SKELETON = 51,
    CREEPER = 50,
    PIG = 90,
    COW = 92,
    SHEEP = 91
};

struct EntityMetadata {
    u8 type;
    std::vector<u8> data;
    
    EntityMetadata() : type(0) {}
    EntityMetadata(u8 t, const std::vector<u8>& d) : type(t), data(d) {}
};

class Entity {
protected:
    u32 entity_id_;
    EntityType type_;
    Location location_;
    Location velocity_;
    f32 yaw_;
    f32 pitch_;
    f32 head_yaw_;
    
    std::atomic<bool> on_ground_{false};
    std::atomic<bool> no_gravity_{false};
    std::atomic<bool> glowing_{false};
    std::atomic<bool> invisible_{false};
    std::atomic<bool> silent_{false};
    
    std::unordered_map<u8, EntityMetadata> metadata_;
    std::mutex metadata_mutex_;
    
    std::atomic<timestamp_t> last_update_;
    std::atomic<bool> dirty_{false};

public:
    Entity(u32 id, EntityType type, const Location& location) 
        : entity_id_(id), type_(type), location_(location)
        , velocity_(0, 0, 0), yaw_(0), pitch_(0), head_yaw_(0) {
        last_update_.store(std::chrono::steady_clock::now());
    }
    
    virtual ~Entity() = default;
    
    u32 get_entity_id() const { return entity_id_; }
    EntityType get_type() const { return type_; }
    
    Location get_location() const { return location_; }
    void set_location(const Location& location) { 
        location_ = location;
        mark_dirty();
    }
    
    Location get_velocity() const { return velocity_; }
    void set_velocity(const Location& velocity) { 
        velocity_ = velocity;
        mark_dirty();
    }
    
    f32 get_yaw() const { return yaw_; }
    void set_yaw(f32 yaw) { 
        yaw_ = yaw;
        mark_dirty();
    }
    
    f32 get_pitch() const { return pitch_; }
    void set_pitch(f32 pitch) { 
        pitch_ = pitch;
        mark_dirty();
    }
    
    f32 get_head_yaw() const { return head_yaw_; }
    void set_head_yaw(f32 head_yaw) { 
        head_yaw_ = head_yaw;
        mark_dirty();
    }
    
    bool is_on_ground() const { return on_ground_.load(); }
    void set_on_ground(bool on_ground) { 
        on_ground_.store(on_ground);
        mark_dirty();
    }
    
    bool has_no_gravity() const { return no_gravity_.load(); }
    void set_no_gravity(bool no_gravity) { 
        no_gravity_.store(no_gravity);
        mark_dirty();
    }
    
    bool is_glowing() const { return glowing_.load(); }
    void set_glowing(bool glowing) { 
        glowing_.store(glowing);
        mark_dirty();
    }
    
    bool is_invisible() const { return invisible_.load(); }
    void set_invisible(bool invisible) { 
        invisible_.store(invisible);
        mark_dirty();
    }
    
    bool is_silent() const { return silent_.load(); }
    void set_silent(bool silent) { 
        silent_.store(silent);
        mark_dirty();
    }
    
    void set_metadata(u8 index, const EntityMetadata& metadata) {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        metadata_[index] = metadata;
        mark_dirty();
    }
    
    EntityMetadata get_metadata(u8 index) const {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        auto it = metadata_.find(index);
        return it != metadata_.end() ? it->second : EntityMetadata();
    }
    
    std::unordered_map<u8, EntityMetadata> get_all_metadata() const {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        return metadata_;
    }
    
    world::ChunkPos get_chunk_pos() const {
        return location_.toChunkPos();
    }
    
    f64 distance_to(const Entity& other) const {
        const Location& other_pos = other.get_location();
        f64 dx = location_.x - other_pos.x;
        f64 dy = location_.y - other_pos.y;
        f64 dz = location_.z - other_pos.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    
    f64 distance_to(const Location& pos) const {
        f64 dx = location_.x - pos.x;
        f64 dy = location_.y - pos.y;
        f64 dz = location_.z - pos.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    
    bool is_dirty() const { return dirty_.load(); }
    void mark_dirty() { 
        dirty_.store(true);
        last_update_.store(std::chrono::steady_clock::now());
    }
    void clear_dirty() { dirty_.store(false); }
    
    timestamp_t get_last_update() const { return last_update_.load(); }
    
    virtual void tick() {
        if (!no_gravity_.load()) {
            velocity_.y -= 0.08;
            velocity_.y *= 0.98;
        }
        
        location_.x += velocity_.x;
        location_.y += velocity_.y;
        location_.z += velocity_.z;
        
        velocity_.x *= 0.91;
        velocity_.z *= 0.91;
        
        if (std::abs(velocity_.x) < 0.01) velocity_.x = 0;
        if (std::abs(velocity_.y) < 0.01) velocity_.y = 0;
        if (std::abs(velocity_.z) < 0.01) velocity_.z = 0;
        
        mark_dirty();
    }
    
    virtual bool should_remove() const { return false; }
};

using EntityPtr = std::shared_ptr<Entity>;

class LivingEntity : public Entity {
protected:
    f32 health_;
    f32 max_health_;
    i32 hurt_time_;
    i32 death_time_;
    
public:
    LivingEntity(u32 id, EntityType type, const Location& location, f32 max_health = 20.0f)
        : Entity(id, type, location), health_(max_health), max_health_(max_health)
        , hurt_time_(0), death_time_(0) {}
    
    f32 get_health() const { return health_; }
    f32 get_max_health() const { return max_health_; }
    
    void set_health(f32 health) {
        health_ = std::clamp(health, 0.0f, max_health_);
        mark_dirty();
    }
    
    void damage(f32 amount) {
        if (amount > 0) {
            health_ = std::max(0.0f, health_ - amount);
            hurt_time_ = 10;
            mark_dirty();
        }
    }
    
    void heal(f32 amount) {
        if (amount > 0) {
            health_ = std::min(max_health_, health_ + amount);
            mark_dirty();
        }
    }
    
    bool is_alive() const { return health_ > 0.0f; }
    bool is_dead() const { return health_ <= 0.0f; }
    
    void tick() override {
        Entity::tick();
        
        if (hurt_time_ > 0) {
            hurt_time_--;
        }
        
        if (is_dead() && death_time_ < 20) {
            death_time_++;
        }
    }
    
    bool should_remove() const override {
        return is_dead() && death_time_ >= 20;
    }
};

class EntityManager {
private:
    std::unordered_map<u32, EntityPtr> entities_;
    std::unordered_map<world::ChunkPos, std::vector<u32>, world::ChunkPosHash> entities_by_chunk_;
    std::mutex entities_mutex_;
    
    std::atomic<u32> next_entity_id_{10000};
    std::atomic<size_t> max_entities_{10000};

public:
    u32 spawn_entity(EntityPtr entity) {
        std::lock_guard<std::mutex> lock(entities_mutex_);
        
        if (entities_.size() >= max_entities_.load()) {
            return 0;
        }
        
        u32 entity_id = entity->get_entity_id();
        entities_[entity_id] = entity;
        
        world::ChunkPos chunk_pos = entity->get_chunk_pos();
        entities_by_chunk_[chunk_pos].push_back(entity_id);
        
        return entity_id;
    }
    
    void remove_entity(u32 entity_id) {
        std::lock_guard<std::mutex> lock(entities_mutex_);
        
        auto it = entities_.find(entity_id);
        if (it != entities_.end()) {
            world::ChunkPos chunk_pos = it->second->get_chunk_pos();
            entities_.erase(it);
            
            auto& chunk_entities = entities_by_chunk_[chunk_pos];
            chunk_entities.erase(
                std::remove(chunk_entities.begin(), chunk_entities.end(), entity_id),
                chunk_entities.end()
            );
        }
    }
    
    EntityPtr get_entity(u32 entity_id) const {
        std::lock_guard<std::mutex> lock(entities_mutex_);
        auto it = entities_.find(entity_id);
        return it != entities_.end() ? it->second : nullptr;
    }
    
    std::vector<EntityPtr> get_entities_in_chunk(const world::ChunkPos& chunk_pos) const {
        std::lock_guard<std::mutex> lock(entities_mutex_);
        std::vector<EntityPtr> result;
        
        auto it = entities_by_chunk_.find(chunk_pos);
        if (it != entities_by_chunk_.end()) {
            for (u32 entity_id : it->second) {
                auto entity_it = entities_.find(entity_id);
                if (entity_it != entities_.end()) {
                    result.push_back(entity_it->second);
                }
            }
        }
        
        return result;
    }
    
    std::vector<EntityPtr> get_entities_in_range(const Location& center, f64 radius) const {
        std::lock_guard<std::mutex> lock(entities_mutex_);
        std::vector<EntityPtr> result;
        
        for (const auto& [id, entity] : entities_) {
            if (entity->distance_to(center) <= radius) {
                result.push_back(entity);
            }
        }
        
        return result;
    }
    
    void tick_all_entities() {
        std::vector<EntityPtr> entities_to_tick;
        std::vector<u32> entities_to_remove;
        
        {
            std::lock_guard<std::mutex> lock(entities_mutex_);
            for (const auto& [id, entity] : entities_) {
                entities_to_tick.push_back(entity);
            }
        }
        
        for (auto& entity : entities_to_tick) {
            entity->tick();
            
            if (entity->should_remove()) {
                entities_to_remove.push_back(entity->get_entity_id());
            }
        }
        
        for (u32 entity_id : entities_to_remove) {
            remove_entity(entity_id);
        }
        
        update_chunk_assignments();
    }
    
    void update_chunk_assignments() {
        std::lock_guard<std::mutex> lock(entities_mutex_);
        
        entities_by_chunk_.clear();
        
        for (const auto& [id, entity] : entities_) {
            world::ChunkPos chunk_pos = entity->get_chunk_pos();
            entities_by_chunk_[chunk_pos].push_back(id);
        }
    }
    
    size_t get_entity_count() const {
        std::lock_guard<std::mutex> lock(entities_mutex_);
        return entities_.size();
    }
    
    u32 get_next_entity_id() {
        return next_entity_id_.fetch_add(1);
    }
    
    void set_max_entities(size_t max_entities) {
        max_entities_.store(max_entities);
    }
    
    std::vector<EntityPtr> get_all_entities() const {
        std::lock_guard<std::mutex> lock(entities_mutex_);
        std::vector<EntityPtr> result;
        
        for (const auto& [id, entity] : entities_) {
            result.push_back(entity);
        }
        
        return result;
    }
};

extern EntityManager g_entity_manager;

}
