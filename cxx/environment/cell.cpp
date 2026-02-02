#include "cell.h"

namespace priceriot {

// --- Section: EdgeCell constructor and SKU queries ---
EdgeCell::EdgeCell(int id, double length_m, double personal_radius_m)
    : id_(id), cellLength_m_(length_m < 0.0 ? 0.0 : length_m),
      personalRadius_m_(personal_radius_m < 0.0 ? 0.0 : personal_radius_m) {
    // Start with empty sidebands; they’ll be aligned when inventory is set.
    leftBand.count = 0;
    rightBand.count = 0;
}

bool EdgeCell::containsSku(std::uint32_t sku) const noexcept {
    auto scan = [sku](const ShelfSide &s) {
        for (std::uint8_t b = 0; b < s.bay_count; ++b) {
            for (std::uint8_t f = 0; f < s.bays[b].face_count; ++f) {
                const auto &face = s.bays[b].faces[f];
                for (std::uint8_t sl = 0; sl < face.slot_count; ++sl) {
                    if (face.slots[sl].qty_on_face > 0 && face.slots[sl].sku_id == sku)
                        return true;
                }
            }
        }
        return false;
    };
    return scan(leftSide) || scan(rightSide);
}

std::optional<std::uint8_t> EdgeCell::select_stall_exact(const SideBand &band,
                                                         std::uint16_t bay) noexcept {
    if (bay < band.count) {
        const Stall &s = band.stalls[bay];
        if (s.free() && !s.blocked)
            return static_cast<std::uint8_t>(bay);
    }
    return std::nullopt;
}

std::optional<std::uint8_t> EdgeCell::select_stall_nearest_free(const SideBand &band,
                                                                std::uint16_t target_bay) noexcept {
    if (band.count == 0)
        return std::nullopt;
    if (target_bay >= band.count)
        target_bay = band.count - 1;

    if (band.stalls[target_bay].free() && !band.stalls[target_bay].blocked)
        return static_cast<std::uint8_t>(target_bay);

    for (std::uint16_t d = 1; d < band.count; ++d) {
        if (target_bay >= d) {
            auto i = static_cast<std::uint16_t>(target_bay - d);
            if (band.stalls[i].free() && !band.stalls[i].blocked)
                return static_cast<std::uint8_t>(i);
        }
        if (target_bay + d < band.count) {
            auto i = static_cast<std::uint16_t>(target_bay + d);
            if (band.stalls[i].free() && !band.stalls[i].blocked)
                return static_cast<std::uint8_t>(i);
        }
    }
    return std::nullopt;
}

bool EdgeCell::try_peel_to_stall(SideBand &band, std::uint8_t stall_idx,
                                 std::uint32_t agent_id) noexcept {
    if (stall_idx >= band.count)
        return false;
    Stall &s = band.stalls[stall_idx];
    if (!s.free() || s.blocked)
        return false;
    s.occ = agent_id;
    s.t_since_occupied = 0.f;
    return true;
}

PickResult EdgeCell::try_pick(const ShelfSide &inv_const, SideBand &band, std::uint8_t stall_idx,
                              std::uint32_t agent_id, std::optional<float> pick_time_s) noexcept {
    PickResult r{};
    if (stall_idx >= band.count)
        return r;

    Stall &s = band.stalls[stall_idx];
    if (!s.occ.has_value() || s.occ.value() != agent_id)
        return r;

    ShelfSide &inv = const_cast<ShelfSide &>(inv_const);
    if (s.bay.bay >= inv.bay_count)
        return r;

    Bay &bay = inv.bays[s.bay.bay];
    if (bay.blocked)
        return r;

    // Try preferred face/slot first.
    if (s.bay.face < bay.face_count) {
        BayFace &face = bay.faces[s.bay.face];
        std::optional<std::uint8_t> preferred =
            (s.bay.slot < face.slot_count) ? std::optional<std::uint8_t>(s.bay.slot) : std::nullopt;
        auto take = face.take_one(preferred);
        if (take.second) {
            r.sku_id = take.first;
            r.success = true;
        }
    }
    // Fallback: any other face in the bay.
    if (!r.success) {
        for (std::uint8_t f = 0; f < bay.face_count; ++f) {
            if (f == s.bay.face)
                continue;
            BayFace &face = bay.faces[f];
            auto take = face.take_one(std::nullopt);
            if (take.second) {
                r.sku_id = take.first;
                r.success = true;
                break;
            }
        }
    }

    if (r.success)
        r.time_cost_s = pick_time_s.value_or(0.f);
    return r;
}

bool EdgeCell::try_merge_from_stall(SideBand &band, std::uint8_t stall_idx,
                                    std::uint32_t agent_id) noexcept {
    if (stall_idx >= band.count)
        return false;
    Stall &s = band.stalls[stall_idx];
    if (!s.occ.has_value() || s.occ.value() != agent_id)
        return false;
    s.occ.reset();
    s.t_since_occupied = 0.f;
    return true;
}

void EdgeCell::set_stall_blocked(SideBand &band, const ShelfSide &inv_const, std::uint8_t stall_idx,
                                 bool on) noexcept {
    if (stall_idx >= band.count)
        return;
    Stall &s = band.stalls[stall_idx];
    s.blocked = on;
    ShelfSide &inv = const_cast<ShelfSide &>(inv_const);
    if (stall_idx < inv.bay_count)
        inv.bays[stall_idx].blocked = on;
}

void EdgeCell::tick_sideband(SideBand &band, float dt_s) noexcept {
    for (std::uint8_t i = 0; i < band.count; ++i) {
        if (band.stalls[i].occ.has_value()) {
            band.stalls[i].t_since_occupied += dt_s;
        }
    }
}

// ---- public wrappers ----
std::optional<std::uint8_t> EdgeCell::find_left_stall_exact(std::uint16_t bay) const noexcept {
    return select_stall_exact(leftBand, bay);
}
std::optional<std::uint8_t> EdgeCell::find_right_stall_exact(std::uint16_t bay) const noexcept {
    return select_stall_exact(rightBand, bay);
}
std::optional<std::uint8_t> EdgeCell::find_left_stall_nearest(std::uint16_t bay) const noexcept {
    return select_stall_nearest_free(leftBand, bay);
}
std::optional<std::uint8_t> EdgeCell::find_right_stall_nearest(std::uint16_t bay) const noexcept {
    return select_stall_nearest_free(rightBand, bay);
}

bool EdgeCell::peel_left(std::uint8_t stall_idx, std::uint32_t agent_id) noexcept {
    bool ok = try_peel_to_stall(leftBand, stall_idx, agent_id);
    if (ok)
        agent_present_ = true;
    return ok;
}
bool EdgeCell::peel_right(std::uint8_t stall_idx, std::uint32_t agent_id) noexcept {
    bool ok = try_peel_to_stall(rightBand, stall_idx, agent_id);
    if (ok)
        agent_present_ = true;
    return ok;
}

PickResult EdgeCell::pick_left(std::uint8_t stall_idx, std::uint32_t agent_id,
                               std::optional<float> pick_time_s) noexcept {
    return try_pick(leftSide, leftBand, stall_idx, agent_id, pick_time_s);
}
PickResult EdgeCell::pick_right(std::uint8_t stall_idx, std::uint32_t agent_id,
                                std::optional<float> pick_time_s) noexcept {
    return try_pick(rightSide, rightBand, stall_idx, agent_id, pick_time_s);
}

bool EdgeCell::merge_left(std::uint8_t stall_idx, std::uint32_t agent_id) noexcept {
    bool ok = try_merge_from_stall(leftBand, stall_idx, agent_id);
    if (ok) {
        bool any = false;
        for (std::uint8_t i = 0; i < leftBand.count; ++i)
            if (leftBand.stalls[i].occ) {
                any = true;
                break;
            }
        for (std::uint8_t i = 0; i < rightBand.count && !any; ++i)
            if (rightBand.stalls[i].occ) {
                any = true;
                break;
            }
        if (!any)
            agent_present_ = false;
    }
    return ok;
}

bool EdgeCell::merge_right(std::uint8_t stall_idx, std::uint32_t agent_id) noexcept {
    bool ok = try_merge_from_stall(rightBand, stall_idx, agent_id);
    if (ok) {
        bool any = false;
        for (std::uint8_t i = 0; i < leftBand.count; ++i)
            if (leftBand.stalls[i].occ) {
                any = true;
                break;
            }
        for (std::uint8_t i = 0; i < rightBand.count && !any; ++i)
            if (rightBand.stalls[i].occ) {
                any = true;
                break;
            }
        if (!any)
            agent_present_ = false;
    }
    return ok;
}

void EdgeCell::set_left_blocked(std::uint8_t stall_idx, bool on) noexcept {
    set_stall_blocked(leftBand, leftSide, stall_idx, on);
}
void EdgeCell::set_right_blocked(std::uint8_t stall_idx, bool on) noexcept {
    set_stall_blocked(rightBand, rightSide, stall_idx, on);
}

void EdgeCell::tick(float dt_s) noexcept {
    tick_sideband(leftBand, dt_s);
    tick_sideband(rightBand, dt_s);
}

} // namespace priceriot
