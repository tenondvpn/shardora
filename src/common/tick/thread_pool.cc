#include "common/tick/thread_pool.h"

#include "common/global_info.h"

#include <set>
#include <iostream>

namespace zjchain {

namespace common {

TickThreadPool* TickThreadPool::Instance() {
    static TickThreadPool ins;
    return &ins;
}

void TickThreadPool::AddTick(uint32_t idx, int64_t cutoff_us, TickCallbackFunction call) {
    {
        common::AutoSpinLock guard(destroy_mutex_);
        if (destroy_) {
            return;
        }
    }

    auto end_time = std::chrono::steady_clock::now() + std::chrono::microseconds(cutoff_us);
    auto item = std::make_shared<Item>(end_time, call, idx);
    {
        common::AutoSpinLock guard(tick_items_mutex_);
        tick_items_.push_back(item);
    }
}

void TickThreadPool::Destroy() {
    common::AutoSpinLock guard(destroy_mutex_);
    if (destroy_) {
        return;
    }

    destroy_ = true;
    for (auto iter = thread_pool_.begin(); iter != thread_pool_.end(); ++iter) {
        (*iter)->join();
    }

    thread_pool_.clear();
    common::AutoSpinLock items_guard(tick_items_mutex_);
    tick_items_.clear();
}

TickThreadPool::TickThreadPool() {
    uint8_t start_thread_idx = common::GlobalInfo::Instance()->message_handler_thread_count();
    for (uint32_t i = 0; i < common::GlobalInfo::Instance()->tick_thread_pool_count(); ++i) {
        thread_pool_.push_back(std::make_shared<std::thread>(
            std::thread(&TickThreadPool::Ticking, this, start_thread_idx + i)));
    }
}

TickThreadPool::~TickThreadPool() {
    Destroy();
}

void TickThreadPool::Ticking(uint8_t idx) {
    uint8_t thread_idx = idx;
    while (!destroy_) {
        uint32_t first_idx = 0;
        uint32_t now_idx = (std::numeric_limits<uint32_t>::max)();
        auto tick_item = Get(first_idx);
        std::vector<std::shared_ptr<Item>> getted_items;
        std::set<uint32_t> handled_timer;
        while (tick_item || now_idx != first_idx) {
            if (!tick_item) {
                break;
            }
            
            auto iter = handled_timer.find(tick_item->idx);
            if (iter == handled_timer.end()) {
                auto tick_now = std::chrono::steady_clock::now();
                if (tick_item->cutoff_time <= tick_now) {
                    RemoveTick(tick_item->idx);
                    tick_item->callback(thread_idx);
                } else {
                    getted_items.push_back(tick_item);
                }
            } else {
                getted_items.push_back(tick_item);
            }
            handled_timer.insert(tick_item->idx);
            tick_item = Get(now_idx);
        }

        std::this_thread::sleep_for(std::chrono::microseconds(50000ull));
        for (auto iter = getted_items.begin(); iter != getted_items.end(); ++iter) {
            (*iter)->hold = false;
        }
    }
}

TickItemPtr TickThreadPool::Get(uint32_t& idx) {
    common::AutoSpinLock guard(tick_items_mutex_);
    if (tick_items_.empty()) {
        return nullptr;
    }

    if (tick_handled_index_ >= tick_items_.size()) {
        tick_handled_index_ = 0;
    }

    uint32_t start_idx = tick_handled_index_;
    if (!(tick_items_[start_idx]->hold)) {
        tick_items_[start_idx]->hold = true;
        tick_handled_index_ = start_idx + 1;
        idx = start_idx;
        return tick_items_[start_idx];
    }

    ++start_idx;
    if (start_idx >= tick_items_.size()) {
        start_idx = 0;
    }

    while (start_idx != tick_handled_index_) {
        if (start_idx >= tick_items_.size()) {
            start_idx = 0;
            if (start_idx == tick_handled_index_) {
                break;
            }
        }

        if (!(tick_items_[start_idx]->hold)) {
            tick_items_[start_idx]->hold = true;
            tick_handled_index_ = start_idx + 1;
            idx = start_idx;
            return tick_items_[start_idx];
        }
        ++start_idx;
    }
    return nullptr;
}

void TickThreadPool::RemoveTick(uint32_t tick_idx) {
    common::AutoSpinLock guard(tick_items_mutex_);
    for (auto iter = tick_items_.begin(); iter != tick_items_.end();) {
        if ((*iter)->idx == tick_idx) {
            iter = tick_items_.erase(iter);
        } else {
            ++iter;
        }
    }
}

}  // namespace common

}  // namespace zjchain
