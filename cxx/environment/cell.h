/**
 * @file cell.h
 * @brief EdgeCell: longitudinal slice of an aisle with left/right shelf inventory.
 *
 * Each EdgeCell has a left and right ShelfSide (planogram) and corresponding SideBands
 * (stalls for customer positioning). Customers peel into stalls to pick, then merge back.
 */
#ifndef PRICERIOT_CELL_H
#define PRICERIOT_CELL_H

#include "products.h"
#include "shelf.h" // ShelfSide, SideBand, PickResult, helpers
#include <cstdint>
#include <optional>

namespace priceriot {

/**
 * One longitudinal slice of an aisle with aligned stalls on each side.
 * Integrates ShelfSide (inventory) with SideBand (stall occupancy) for peel/pick/merge.
 */
class EdgeCell {
  public:
    /** Per-cell traversal policy along the aisle (forward, backward, or both). */
    enum class Traversal : std::uint8_t { fwd = 0, bwd = 1, both = 2 };

    EdgeCell(int id, double length_m, double personal_radius_m);

    // Inventory mapping
    void set_left_inventory(const ShelfSide &s) {
        leftSide = s;
        leftBand.map_to(s);
    }
    void set_right_inventory(const ShelfSide &s) {
        rightSide = s;
        rightBand.map_to(s);
    }

    const ShelfSide &get_left() const {
        return leftSide;
    }
    const ShelfSide &get_right() const {
        return rightSide;
    }

    /** True if this cell has the given SKU on left or right shelf. */
    [[nodiscard]] bool containsSku(std::uint32_t sku) const noexcept;

    // Stall queries
    [[nodiscard]] std::optional<std::uint8_t>
    find_left_stall_exact(std::uint16_t bay) const noexcept;
    [[nodiscard]] std::optional<std::uint8_t>
    find_right_stall_exact(std::uint16_t bay) const noexcept;
    [[nodiscard]] std::optional<std::uint8_t>
    find_left_stall_nearest(std::uint16_t bay) const noexcept;
    [[nodiscard]] std::optional<std::uint8_t>
    find_right_stall_nearest(std::uint16_t bay) const noexcept;

    // Peel / pick / merge
    bool peel_left(std::uint8_t stall_idx, std::uint32_t agent_id) noexcept;
    bool peel_right(std::uint8_t stall_idx, std::uint32_t agent_id) noexcept;

    PickResult pick_left(std::uint8_t stall_idx, std::uint32_t agent_id,
                         std::optional<float> pick_time_s = std::nullopt) noexcept;

    PickResult pick_right(std::uint8_t stall_idx, std::uint32_t agent_id,
                          std::optional<float> pick_time_s = std::nullopt) noexcept;

    bool merge_left(std::uint8_t stall_idx, std::uint32_t agent_id) noexcept;
    bool merge_right(std::uint8_t stall_idx, std::uint32_t agent_id) noexcept;

    // Block / unblock a stall
    void set_left_blocked(std::uint8_t stall_idx, bool on) noexcept;
    void set_right_blocked(std::uint8_t stall_idx, bool on) noexcept;

    // Sim tick
    void tick(float dt_s) noexcept;

    // Traversal policy
    [[nodiscard]] Traversal traversal() const noexcept {
        return traversal_;
    }
    void set_traversal(Traversal t) noexcept {
        traversal_ = t;
    }

    [[nodiscard]] bool allows_fwd() const noexcept {
        return traversal_ == Traversal::both || traversal_ == Traversal::fwd;
    }
    [[nodiscard]] bool allows_bwd() const noexcept {
        return traversal_ == Traversal::both || traversal_ == Traversal::bwd;
    }

    // Basic accessors
    [[nodiscard]] int id() const noexcept {
        return id_;
    }
    [[nodiscard]] double cell_length() const noexcept {
        return cellLength_m_;
    }
    [[nodiscard]] double personal_radius() const noexcept {
        return personalRadius_m_;
    }

  private:
    // helpers (implemented in cell.cpp)
    static std::optional<std::uint8_t> select_stall_exact(const SideBand &band,
                                                          std::uint16_t bay) noexcept;
    static std::optional<std::uint8_t> select_stall_nearest_free(const SideBand &band,
                                                                 std::uint16_t bay) noexcept;
    static bool try_peel_to_stall(SideBand &band, std::uint8_t stall_idx,
                                  std::uint32_t agent_id) noexcept;
    static PickResult try_pick(const ShelfSide &inv, SideBand &band, std::uint8_t stall_idx,
                               std::uint32_t agent_id, std::optional<float> pick_time_s) noexcept;
    static bool try_merge_from_stall(SideBand &band, std::uint8_t stall_idx,
                                     std::uint32_t agent_id) noexcept;
    static void set_stall_blocked(SideBand &band, const ShelfSide &inv, std::uint8_t stall_idx,
                                  bool on) noexcept;
    static void tick_sideband(SideBand &band, float dt_s) noexcept;

    const int id_;
    const double cellLength_m_;    ///< Longitudinal cell length in meters
    const double personalRadius_m_; ///< Personal space radius in meters

    ShelfSide leftSide{}, rightSide{};
    SideBand leftBand{}, rightBand{};

    bool agent_present_ = false;
    Traversal traversal_{Traversal::both};
};

} // namespace priceriot

#endif // PRICERIOT_CELL_H
