#ifndef PRICERIOT_STAFF_H
#define PRICERIOT_STAFF_H

#include "shelf.h"
#include "store_inventory.h"
#include <utility>
#include <vector>

namespace priceriot {

// Simple restocker that tops bays back to an implicit target (initial on-shelf)
// If you want explicit targets, keep a parallel map<BayRef, target> and use it here.
class StockBoy {
  public:
    // Restock 1 ShelfSide; returns pairs of (sku_id, units_moved)
    std::vector<std::pair<std::uint32_t, int>> restock_side(ShelfSide &side,
                                                            InventoryPool &inventory) {
        std::vector<std::pair<std::uint32_t, int>> moved;

        for (std::uint8_t b = 0; b < side.bay_count; ++b) {
            Bay &bay = side.bays[b];
            if (bay.blocked)
                continue;

            for (std::uint8_t f = 0; f < bay.face_count; ++f) {
                BayFace &face = bay.faces[f];
                for (std::uint8_t s = 0; s < face.slot_count; ++s) {
                    ShelfSlot &slot = face.slots[s];
                    if (slot.sku_id == 0)
                        continue;

                    // naive policy: bring any visible slot up to 1 full "face" of 12 units
                    // (example)
                    const int target_qty = std::max<int>(slot.qty_on_face, 12);
                    int need = target_qty - static_cast<int>(slot.qty_on_face);
                    if (need <= 0)
                        continue;

                    int pulled = inventory.take(slot.sku_id, need);
                    if (pulled > 0) {
                        slot.qty_on_face = static_cast<std::uint16_t>(slot.qty_on_face + pulled);
                        moved.emplace_back(slot.sku_id, pulled);
                    }
                }
            }
        }
        return moved;
    }
};

} // namespace priceriot
#endif
