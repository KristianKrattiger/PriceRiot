/**
 * @file spatial_hash.h
 * @brief Spatial hash grid for fast nearby-agent queries.
 *
 * Buckets agents into fixed-size square cells in the XZ plane.
 * Querying a small neighborhood of cells gives O(1) average-time
 * proximity lookups regardless of total agent count.
 */
#ifndef SPATIAL_HASH_H
#define SPATIAL_HASH_H

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace priceriot {

class Customer;

/**
 * @brief Spatial hash grid for agent positions.
 *
 * Each frame, the simulation should:
 * - call clear()
 * - insert all active agents once via insert()
 *
 * Call query() to retrieve agents within a given radius of a
 * position without scanning the full agent list.
 */
class SpatialHash {
  public:
    /**
     * @brief Construct a spatial hash with the given cell size.
     * @param cellSizeMeters Edge length of each cell in meters.
     */
    explicit SpatialHash(double cellSizeMeters = 5.0) noexcept;

    /**
     * @brief Remove all agents from the hash.
     *
     * Call once per frame before reinserting agents for the new positions.
     */
    void clear() noexcept;

    /**
     * @brief Insert an agent at the given world position.
     *
     * @param agent Non-null customer pointer.
     * @param x World X coordinate in meters.
     * @param z World Z coordinate in meters.
     */
    void insert(Customer *agent, double x, double z);

    /**
     * @brief Query agents within a radius of a world position.
     *
     * This inspects only a small neighborhood of grid cells around
     * (x, z). With the default 5 m cell size and ~1.5 m personal
     * space radius, this is a 3x3-cell neighborhood.
     *
     * @param x World X coordinate in meters.
     * @param z World Z coordinate in meters.
     * @param radius Query radius in meters.
     * @return Vector of agent pointers within the given radius.
     */
    [[nodiscard]] std::vector<Customer *> query(double x, double z, double radius) const;

    /// @return Current cell size in meters.
    [[nodiscard]] double getCellSize() const noexcept {
        return cellSize_;
    }

    /**
     * @brief Change the cell size.
     *
     * Does not preserve existing contents; callers should clear()
     * and reinsert agents after changing.
     */
    void setCellSize(double cellSizeMeters) noexcept;

  private:
    struct Bucket {
        std::vector<Customer *> agents;
    };

    double cellSize_;
    std::unordered_map<std::int64_t, Bucket> buckets_;

    static std::int64_t hashKey(int ix, int iz) noexcept;
    static void worldToCell(double x, double z, double cellSize, int &ix, int &iz) noexcept;
};

} // namespace priceriot

#endif // SPATIAL_HASH_H

