#include <set>
#include <cstdint>
#include <iostream>

namespace shardora {

namespace common {

class RangeSet {
private:
    std::set<std::pair<uint64_t, uint64_t>> ranges;

public:
    void add(uint64_t x) {
        // 检查 x 是否已存在于任何区间
        auto it = ranges.lower_bound({x, 0});
        if (it != ranges.end() && it->first <= x && x <= it->second) {
            return;
        }
        if (it != ranges.begin()) {
            auto prev_it = std::prev(it);
            if (prev_it->first <= x && x <= prev_it->second) {
                return;
            }
        }

        // 插入新区间 [x, x]
        auto new_it = ranges.insert({x, x}).first;

        // 尝试与前一个区间合并
        auto prev_it = new_it;
        if (prev_it != ranges.begin()) {
            prev_it = std::prev(prev_it);
            if (prev_it->second + 1 == x && prev_it->second < x) {
                uint64_t start = prev_it->first;
                uint64_t end = x;
                ranges.erase(prev_it);
                ranges.erase(new_it);
                new_it = ranges.insert({start, end}).first;
            }
        }

        // 尝试与后续区间连续合并
        while (true) {
            auto next_it = std::next(new_it);
            if (next_it != ranges.end() && new_it->second + 1 == next_it->first && new_it->second < next_it->first) {
                new_it->second = next_it->second;
                ranges.erase(next_it);
            } else {
                break;
            }
        }
    }
    ​​​​​​​​​​​​​​​​​​​​​​​​​​​​​​​​​​​​​​​​​​​​​​​​​
    bool contains(uint64_t x) const {
        // 查找第一个开始值不小于x的区间
        auto it = ranges.lower_bound({x, 0});
        // 检查当前区间是否包含x
        if (it != ranges.end() && it->first <= x && x <= it->second) {
            return true;
        }
        // 检查前一个区间是否包含x
        if (it != ranges.begin()) {
            auto prev_it = std::prev(it);
            if (prev_it->first <= x && x <= prev_it->second) {
                return true;
            }
        }
        return false;
    }

    // 用于调试的打印函数
    void print() const {
        for (const auto& range : ranges) {
            std::cout << "[" << range.first << ", " << range.second << "] ";
        }
        std::cout << std::endl;
    }
};

}

}
