#pragma once

#include <functional>
#include <vector>
#include <unordered_map>
#include <typeindex>
#include <memory>
#include <any>

namespace mc {

class EventBus {
public:
    template<typename EventType>
    using EventHandler = std::function<void(const EventType&)>;

    template<typename EventType>
    void subscribe(EventHandler<EventType> handler) {
        handlers_[typeid(EventType)].push_back(
            [handler](const std::any& event) {
                handler(std::any_cast<const EventType&>(event));
            }
        );
    }

    template<typename EventType>
    void publish(const EventType& event) {
        auto it = handlers_.find(typeid(EventType));
        if (it != handlers_.end()) {
            for (auto& handler : it->second) {
                handler(event);
            }
        }
    }

    static EventBus& get() {
        static EventBus instance;
        return instance;
    }

private:
    std::unordered_map<std::type_index, std::vector<std::function<void(const std::any&)>>> handlers_;
};

// Common Events
struct BlockBreakEvent {
    int x, y, z;
    uint16_t block_id;
};

struct BlockPlaceEvent {
    int x, y, z;
    uint16_t block_id;
};

struct PlayerMoveEvent {
    float x, y, z;
};

} // namespace mc
