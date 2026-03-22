#ifndef PRICERIOT_SHELF_H
#define PRICERIOT_SHELF_H

#include <array>
#include <cstdint>
#include <optional>

#include "products.h"

namespace priceriot {

/**
 * @file shelf.h
 * @brief Aligned-stalls shelf model: one stall per bay, per cell-side.
 *        Header-only interfaces; implementations in shelf.cpp.
 */

// -----------------------------
// Tunables (header-visible knobs)
// -----------------------------
inline constexpr std::size_t MAX_BAYS_PER_CELL = 6;  // bays visible from one cell/side
inline constexpr std::size_t MAX_FACES_PER_BAY = 3;  // vertical faces per bay
inline constexpr std::size_t MAX_SLOTS_PER_FACE = 4; // SKU slots per face

// -----------------------------
// Small enums / tags
// -----------------------------
enum class Side : std::uint8_t { Left = 0, Right = 1 };
enum class Dir : std::uint8_t { Fwd = 0, Back = 1 };

// -----------------------------
// Bay addressing
// -----------------------------
struct BayRef {
    std::uint16_t bay = 0; ///< index into ShelfSide.bays
    std::uint8_t face = 0; ///< vertical face (0..faces-1)
    std::uint8_t slot = 0; ///< slot on that face (0...slots-1)

    bool operator==(const BayRef &o) const noexcept;
};

// -----------------------------
// Inventory / planogram objects
// -----------------------------
struct ShelfSlot {
    std::uint32_t sku_id = 0;      ///< global SKU id (or hashed)
    std::uint16_t qty_on_face = 0; ///< units visible at the face
};

struct BayFace {
    std::array<ShelfSlot, MAX_SLOTS_PER_FACE> slots{};
    std::uint8_t slot_count = 0;

    [[nodiscard]] bool has_stock() const noexcept;
    /// Take one unit. If preferred_slot is set and valid, try that first.
    /// @return {sku_id, success}
    std::pair<std::uint32_t, bool>
    take_one(std::optional<std::uint8_t> preferred_slot = std::nullopt);
};

struct Bay {
    std::array<BayFace, MAX_FACES_PER_BAY> faces{};
    std::uint8_t face_count = 0;
    bool blocked = false; ///< stocker/cart occupying: no picks allowed

    [[nodiscard]] bool has_stock() const noexcept;
};

struct ShelfSide {
    // Per-cell subset of bays on this side.
    std::array<Bay, MAX_BAYS_PER_CELL> bays{};
    std::uint8_t bay_count = 0;

    Bay *try_get(std::uint16_t idx) noexcept;
    [[nodiscard]] const Bay *try_get(std::uint16_t idx) const noexcept;

    /** Restore stock: add qty units to every non-empty slot on this side. */
    void addStock(std::uint16_t qty) noexcept {
        for (std::size_t b = 0; b < bay_count; ++b) {
            for (std::size_t f = 0; f < bays[b].face_count; ++f) {
                for (std::size_t s = 0; s < bays[b].faces[f].slot_count; ++s) {
                    auto &slot = bays[b].faces[f].slots[s];
                    if (slot.sku_id != 0)
                        slot.qty_on_face += qty;
                }
            }
        }
    }
};

// -----------------------------
// Stall (aligned 1:1 with bay)
// -----------------------------
struct Stall {
    BayRef bay{};                       ///< the bay this stall serves
    bool blocked = false;               ///< cart/stocker directly in the stall
    std::optional<std::uint32_t> occ{}; ///< agent_id if occupied (single-person stall)
    float t_since_occupied = 0.f;       ///< dwell metric

    [[nodiscard]] bool free() const noexcept;
};

struct SideBand {
    // Stalls aligned with the ShelfSide bays in the same cell.
    std::array<Stall, MAX_BAYS_PER_CELL> stalls{};
    std::uint8_t count = 0;

    /// Align stalls 0...count-1 to side. bays 0...bay_count-1 (face/slot default 0).
    void map_to(const ShelfSide &side) noexcept;

    /// Find a free stall serving @p bay_index; nullopt if none.
    [[nodiscard]] std::optional<std::uint8_t>
    find_free_for_bay(std::uint16_t bay_index) const noexcept;

    /// Find any free stall; nullopt if none.
    [[nodiscard]] std::optional<std::uint8_t> find_any_free() const noexcept;
};

// -----------------------------
// Results / small DTOs
// -----------------------------
struct PickResult {
    std::uint32_t sku_id = 0;
    bool success = false;
    float time_cost_s = 0.f; ///< picking time (category-tuned externally)
};

// -----------------------------
// Public API (implemented in shelf.cpp)
// -----------------------------

// Occupy a stall (lane -> stall). Returns true on success.
bool try_peel_to_stall(SideBand &band, std::uint8_t stall_idx, std::uint32_t agent_id) noexcept;

// Attempt a pick from the stall's bay for the given occupant.
// - Respects Bay::blocked
// - Uses Stall::bay (preferred face/slot), then falls back to first available.
// - pick_time_s (if provided) sets result time; otherwise a default is used.
PickResult try_pick(ShelfSide &side, const SideBand &band, std::uint8_t stall_idx,
                    std::uint32_t agent_id,
                    std::optional<float> pick_time_s = std::nullopt) noexcept;

// Leave a stall (stall -> lane when lane has space). Returns true on success.
bool try_merge_from_stall(SideBand &band, std::uint8_t stall_idx, std::uint32_t agent_id) noexcept;

// Mark/unmark a stall (and its bay) as blocked by a stocker/cart event.
void set_stall_blocked(SideBand &band, ShelfSide &side, std::uint8_t stall_idx, bool on) noexcept;

// Selection helpers
std::optional<std::uint8_t> select_stall_exact(const SideBand &band,
                                               std::uint16_t target_bay) noexcept;
std::optional<std::uint8_t> select_stall_nearest_free(const SideBand &band,
                                                      std::uint16_t target_bay) noexcept;

// Per-tick housekeeping for dwell timers.
void tick_sideband(SideBand &band, float dt_s) noexcept;
} // namespace priceriot

#endif // PRICERIOT_SHELF_H
