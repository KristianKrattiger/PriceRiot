#include "spatial_hash.h"
#include "../agents/customer.h"

#include <cmath>

namespace priceriot {

SpatialHash::SpatialHash(double cellSizeMeters) noexcept
    : cellSize_(cellSizeMeters > 0.0 ? cellSizeMeters : 5.0) {}

void SpatialHash::clear() noexcept {
    buckets_.clear();
}

void SpatialHash::insert(Customer *agent, double x, double z) {
    if (!agent || cellSize_ <= 0.0)
        return;

    int ix = 0;
    int iz = 0;
    worldToCell(x, z, cellSize_, ix, iz);
    const std::int64_t key = hashKey(ix, iz);

    auto &bucket = buckets_[key].agents;
    bucket.push_back(agent);
}

std::vector<Customer *> SpatialHash::query(double x, double z, double radius) const {
    std::vector<Customer *> result;
    if (cellSize_ <= 0.0 || radius <= 0.0 || buckets_.empty())
        return result;

    int cx = 0;
    int cz = 0;
    worldToCell(x, z, cellSize_, cx, cz);

    const double r2 = radius * radius;
    const int cellRadius =
        std::max(1, static_cast<int>(std::ceil(radius / cellSize_)));

    for (int dz = -cellRadius; dz <= cellRadius; ++dz) {
        for (int dx = -cellRadius; dx <= cellRadius; ++dx) {
            const std::int64_t key = hashKey(cx + dx, cz + dz);
            auto it = buckets_.find(key);
            if (it == buckets_.end())
                continue;

            const auto &agents = it->second.agents;
            for (Customer *agent : agents) {
                if (!agent)
                    continue;
                const double ax = agent->getPosX();
                const double az = agent->getPosZ();
                const double ddx = ax - x;
                const double ddz = az - z;
                const double dist2 = ddx * ddx + ddz * ddz;
                if (dist2 <= r2) {
                    result.push_back(agent);
                }
            }
        }
    }

    return result;
}

void SpatialHash::setCellSize(double cellSizeMeters) noexcept {
    if (cellSizeMeters <= 0.0)
        return;
    cellSize_ = cellSizeMeters;
}

std::int64_t SpatialHash::hashKey(int ix, int iz) noexcept {
    // Large primes, as suggested in the requirements.
    constexpr std::int64_t kPrimeX = 73856093;
    constexpr std::int64_t kPrimeZ = 19349663;
    return static_cast<std::int64_t>(ix) * kPrimeX ^
           static_cast<std::int64_t>(iz) * kPrimeZ;
}

void SpatialHash::worldToCell(double x, double z, double cellSize, int &ix, int &iz) noexcept {
    ix = static_cast<int>(std::floor(x / cellSize));
    iz = static_cast<int>(std::floor(z / cellSize));
}

} // namespace priceriot

