#include "common/lof.h"

#include <algorithm>
#include <queue>

namespace zjchain {

namespace common {

Lof::Lof(std::vector<Point>& points) : points_(points) {}

Lof::~Lof() {}

double Lof::LocalOutlierFactor(int32_t k, int32_t point_idx) {
    std::vector<std::pair<double, int32_t>> neighbours;
    KDistance(k, point_idx, -1, &neighbours);
    double instance_lrd = LocalReachabilityDensity(k, point_idx, -1);
    double sum = 0;
    for (auto iter = neighbours.begin(); iter != neighbours.end(); ++iter) {
        auto neighbour_lrd = LocalReachabilityDensity(k, iter->second, iter->second);
        sum += neighbour_lrd / instance_lrd;
    }

    return sum / (double)neighbours.size();
}

using KItemType = std::pair<double, int32_t>;
struct CompareItem {
    bool operator() (const KItemType& l, const KItemType& r) {
        return l.first < r.first;
    }
};

void Lof::KDistance(
        int32_t k,
        int32_t point_idx,
        int32_t igns,
        std::vector<std::pair<double, int32_t>>* neighbours) {
    std::priority_queue<KItemType, std::vector<KItemType>, CompareItem> kqueue;
    for (uint32_t i = 0; i < points_.size(); i++) {
        if (igns == points_[i].idx() || points_[i].idx() == now_point_idx_) {
            continue;
        }

        auto dist = PointDistEuclidean(points_[point_idx], points_[i]);
        kqueue.push(std::pair<double, int32_t>(dist, points_[i].idx()));
        if ((int32_t)kqueue.size() > k) {
            kqueue.pop();
        }
    }

    neighbours->clear();
    neighbours->reserve(k);
    for (int32_t i = 0; i < k; ++i) {
        neighbours->push_back(kqueue.top());
        kqueue.pop();
    }
}

double Lof::ReachabilityDist(
        int32_t k,
        int32_t point_idx,
        int32_t point_idx2,
        int32_t igns) {
    std::vector<std::pair<double, int32_t>> neighbours;
    KDistance(k, point_idx2, igns, &neighbours);
    double k_dist = neighbours[0].first;
    double dist = PointDistEuclidean(points_[point_idx], points_[point_idx2]);
    if (k_dist > dist) {
        return k_dist;
    }
        
    return dist;
}

double Lof::PointDistEuclidean(const Point& l, const Point& r) {
    uint64_t key = (uint64_t)l.idx() << 32 | (uint64_t)r.idx();
    auto iter = dist_map_.find(key);
    if (iter != dist_map_.end()) {
        return iter->second;
    }

    double sum = 0.0;
    int32_t dimension = l.GetDimension();
    for (int32_t i = 0; i < dimension; i++) {
        sum += (l[i] - r[i]) * (l[i] - r[i]);
    }

    double res = std::sqrt(sum / (double)dimension);
    dist_map_[key] = res;
    return res;
}

double Lof::LocalReachabilityDensity(
        int k,
        int32_t point_idx,
        int32_t igns) {
    std::vector<std::pair<double, int32_t>> neighbours;
    KDistance(k, point_idx, igns, &neighbours);
    double sumReachDist = 0.0;
    for (auto iter = neighbours.begin(); iter != neighbours.end(); ++iter) {
        auto res = ReachabilityDist(
            k,
            point_idx,
            iter->second,
            igns);
        sumReachDist += res;
    }

    if (sumReachDist <= 0.00000001) {
        return -99999999.99;
    }

    return (double)neighbours.size() / sumReachDist;
}

std::vector<std::pair<int32_t, double>> Lof::GetOutliers(int32_t k) {
    std::vector<Point> vec_InstancesBackUp;
    std::vector<std::pair<int32_t, double>> res_vec;
    for (uint32_t i = 0; i < points_.size(); i++) {
        now_point_idx_ = i;
        double value = LocalOutlierFactor(k, i);
        if (value > 1.0) {
            res_vec.push_back(std::pair<int32_t, double>(points_[i].member_idx(), value));
        }
    }

    std::sort(res_vec.begin(), res_vec.end(),
        [](const std::pair<int32_t, double>& l, const std::pair<int32_t, double>& r) {
        return l.second > r.second;
    });

    return res_vec;
}

};  // namespace common

};  // namespace zjchain
