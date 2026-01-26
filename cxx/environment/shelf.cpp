#include "shelf.h"
#include <limits>

namespace priceriot {

// -------- BayRef --------
bool BayRef::operator==(const BayRef& o) const noexcept {
    return bay==o.bay && face==o.face && slot==o.slot;
}

// -------- BayFace --------
bool BayFace::has_stock() const noexcept {
    for (std::uint8_t i=0;i<slot_count;++i)
        if (slots[i].qty_on_face) return true;
    return false;
}

std::pair<std::uint32_t,bool> BayFace::take_one(std::optional<std::uint8_t> preferred_slot) {
    if (preferred_slot && *preferred_slot < slot_count) {
        auto& s = slots[*preferred_slot];
        if (s.qty_on_face) { --s.qty_on_face; return {s.sku_id,true}; }
    }
    for (std::uint8_t i=0;i<slot_count;++i) {
        auto& s = slots[i];
        if (s.qty_on_face) { --s.qty_on_face; return {s.sku_id,true}; }
    }
    return {0,false};
}

// -------- Bay --------
bool Bay::has_stock() const noexcept {
    for (std::uint8_t f=0; f<face_count; ++f)
        if (faces[f].has_stock()) return true;
    return false;
}

// -------- ShelfSide --------
Bay* ShelfSide::try_get(std::uint16_t idx) noexcept {
    return (idx < bay_count) ? &bays[idx] : nullptr;
}
const Bay* ShelfSide::try_get(std::uint16_t idx) const noexcept {
    return (idx < bay_count) ? &bays[idx] : nullptr;
}

// -------- Stall --------
bool Stall::free() const noexcept {
    return !blocked && !occ.has_value();
}

// -------- SideBand --------
void SideBand::map_to(const ShelfSide& side) noexcept {
    count = side.bay_count;
    for (std::uint8_t i=0;i<count;++i) {
        stalls[i].bay = BayRef{ static_cast<std::uint16_t>(i), 0, 0 };
        stalls[i].blocked = false;
        stalls[i].occ.reset();
        stalls[i].t_since_occupied = 0.f;
    }
    for (std::uint8_t i=count;i<MAX_BAYS_PER_CELL;++i) {
        stalls[i] = Stall{};
    }
}

std::optional<std::uint8_t> SideBand::find_free_for_bay(std::uint16_t bay_index) const noexcept {
    for (std::uint8_t i=0;i<count;++i)
        if (stalls[i].bay.bay == bay_index && stalls[i].free())
            return i;
    return std::nullopt;
}

std::optional<std::uint8_t> SideBand::find_any_free() const noexcept {
    for (std::uint8_t i=0;i<count;++i)
        if (stalls[i].free()) return i;
    return std::nullopt;
}

// -------- API --------
bool try_peel_to_stall(SideBand& band, std::uint8_t stall_idx, std::uint32_t agent_id) noexcept {
    if (stall_idx >= band.count) return false;
    Stall& st = band.stalls[stall_idx];
    if (!st.blocked && !st.occ.has_value()) {
        st.occ = agent_id;
        st.t_since_occupied = 0.f;
        return true;
    }
    return false;
}

PickResult try_pick(ShelfSide& side,
                    const SideBand& band,
                    std::uint8_t stall_idx,
                    std::uint32_t agent_id,
                    std::optional<float> pick_time_s) noexcept
{
    PickResult r{};
    if (stall_idx >= band.count) return r;

    const Stall& st = band.stalls[stall_idx];
    if (!st.occ.has_value() || st.occ.value() != agent_id)
        return r; // not your stall

    Bay* bay = side.try_get(st.bay.bay);
    if (!bay || bay->blocked) return r;

    // Preferred face/slot first
    if (st.bay.face < bay->face_count) {
        auto& face = bay->faces[st.bay.face];
        auto res = face.take_one(st.bay.slot < face.slot_count
                                 ? std::optional<std::uint8_t>(st.bay.slot)
                                 : std::nullopt);
        if (res.second) {
            r.sku_id = res.first;
            r.success = true;
            r.time_cost_s = pick_time_s.value_or(1.6f);
            return r;
        }
    }
    // Fallback search
    for (std::uint8_t f=0; f<bay->face_count; ++f) {
        auto res = bay->faces[f].take_one(std::nullopt);
        if (res.second) {
            r.sku_id = res.first;
            r.success = true;
            r.time_cost_s = pick_time_s.value_or(1.8f);
            return r;
        }
    }
    return r;
}

bool try_merge_from_stall(SideBand& band, std::uint8_t stall_idx, std::uint32_t agent_id) noexcept {
    if (stall_idx >= band.count) return false;
    Stall& st = band.stalls[stall_idx];
    if (!st.occ.has_value() || st.occ.value() != agent_id) return false;
    st.occ.reset();
    st.t_since_occupied = 0.f;
    return true;
}

void set_stall_blocked(SideBand& band, ShelfSide& side, std::uint8_t stall_idx, bool on) noexcept {
    if (stall_idx >= band.count) return;
    band.stalls[stall_idx].blocked = on;
    if (Bay* b = side.try_get(band.stalls[stall_idx].bay.bay)) b->blocked = on;
}

std::optional<std::uint8_t> select_stall_exact(const SideBand& band, std::uint16_t target_bay) noexcept {
    return band.find_free_for_bay(target_bay);
}

std::optional<std::uint8_t> select_stall_nearest_free(const SideBand& band, std::uint16_t target_bay) noexcept {
    if (band.count == 0) return std::nullopt;
    int best_idx = -1;
    std::uint16_t best_dist = std::numeric_limits<std::uint16_t>::max();
    for (std::uint8_t i=0;i<band.count;++i) {
        if (!band.stalls[i].free()) continue;
        std::uint16_t b = band.stalls[i].bay.bay;
        std::uint16_t d = (b > target_bay) ? (b - target_bay) : (target_bay - b);
        if (d < best_dist) { best_dist = d; best_idx = static_cast<int>(i); }
    }
    if (best_idx >= 0) return static_cast<std::uint8_t>(best_idx);
    return std::nullopt;
}

void tick_sideband(SideBand& band, float dt_s) noexcept {
    for (std::uint8_t i=0;i<band.count;++i) {
        Stall& st = band.stalls[i];
        if (st.occ.has_value()) st.t_since_occupied += dt_s;
    }
}

} // namespace priceriot
