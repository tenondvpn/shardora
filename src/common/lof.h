#pragma once

#include <stdarg.h>
#include <cassert>

#include <iostream>
#include <iterator>
#include <math.h>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

namespace zjchain {

namespace common {

class Point {
public:
    Point() {}
    Point(int32_t node_count, int32_t idx, int32_t member_idx) {
        dimension_ = node_count;
        idx_ = idx;
        member_idx_ = member_idx;
        for (int32_t i = 0; i < dimension_; i++) {
            coordinate_.push_back(0);
        }
    }

    ~Point() {
        coordinate_.clear();
    }

    inline double& operator [](int32_t idx) {
        return coordinate_[idx];
    }

    inline double operator [](int32_t idx) const {
        return coordinate_[idx];
    }

    int32_t GetDimension() const {
        return dimension_;
    }

    int32_t idx() const {
        return idx_;
    }

    const std::vector<double>& coordinate() const {
        return coordinate_;
    }

    int32_t member_idx() const {
        return member_idx_;
    }

private:
    std::vector<double> coordinate_;
    int32_t dimension_{ -1 };
    int32_t idx_{ -1 };
    int32_t all_count_{ 0 };
    int32_t pool_tx_count_{ 0 };
    int32_t member_idx_{ -1 };
};

class Lof {
public:
    Lof(std::vector<Point>& points);
    ~Lof();
    std::vector<std::pair<int32_t, double>> GetOutliers(int32_t k);

private:
    double LocalOutlierFactor(int32_t min_point, int32_t point_idx);
    void KDistance(
        int32_t k,
        int32_t point_idx,
        int32_t igns,
        std::vector<std::pair<double, int32_t>>* neighbours);
    double ReachabilityDist(
        int32_t k,
        int32_t point_idx,
        int32_t point_idx2,
        int32_t igns);
    double LocalReachabilityDensity(
        int min_pts,
        int32_t point_idx,
        int32_t igns);
    double PointDistEuclidean(const Point& l, const Point& r);

    std::vector<Point>& points_;
    int32_t now_point_idx_;
    std::unordered_map<uint64_t, double> dist_map_;

    DISALLOW_COPY_AND_ASSIGN(Lof);
};

};  // namespace common

};  // namespace zjchain
