#ifndef PRICERIOT_STORE_INVENTORY_H
#define PRICERIOT_STORE_INVENTORY_H

#include "products.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace priceriot {

// Central backroom (backstock) pool measured in units by sku_id.
class InventoryPool {
  public:
    // Add units to backstock
    void put(std::uint32_t sku_id, int qty) {
        if (qty <= 0)
            return;
        backstock_[sku_id] += qty;
    }

    // Remove up to 'want' units; returns actual provided
    int take(std::uint32_t sku_id, int want) {
        if (want <= 0)
            return 0;
        int &have = backstock_[sku_id];
        int d = std::min(have, want);
        have -= d;
        return d;
    }

    int available(std::uint32_t sku_id) const {
        if (auto it = backstock_.find(sku_id); it != backstock_.end())
            return it->second;
        return 0;
    }

    const std::unordered_map<std::uint32_t, int> &all() const {
        return backstock_;
    }

  private:
    std::unordered_map<std::uint32_t, int> backstock_;
};

} // namespace priceriot
#endif
