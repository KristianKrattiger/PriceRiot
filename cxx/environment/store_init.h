// store_init.h
#ifndef PRICERIOT_STORE_INIT_H
#define PRICERIOT_STORE_INIT_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <yaml-cpp/yaml.h>

#include "products.h"        // priceriot::Products
#include "shelf.h"           // priceriot::ShelfSide
#include "store_inventory.h" // priceriot::InventoryPool

namespace priceriot {

// Parse and upsert products from a PRODUCTS yaml root; returns totals-by-sku (string key)
std::unordered_map<std::string, int> load_products_from_yaml(const YAML::Node &productsRoot,
                                                             Products &catalog);

// Parse and upsert products from a CSV file (sku,name,category,price,popularity)
// Returns an empty totals map (CSV has no stock-total column).
std::unordered_map<std::string, int> load_products_from_csv(const std::string &csvPath,
                                                            Products &catalog);

// Apply a planogram yaml node onto a ShelfSide and accumulate on-shelf counts by sku_id
void apply_planogram_to_side(const YAML::Node &sideY, Products &catalog, ShelfSide &side,
                             std::unordered_map<std::uint32_t, int> &onShelfQuantitySum);

// Compute backstock: totals (from products yaml) minus on-shelf (from planogram),
// with optional overrides from the STORE yaml (root["backstock"])
void compute_and_load_backstock(const YAML::Node &storeRoot, const Products &catalog,
                                const std::unordered_map<std::string, int> &totals_by_sku,
                                const std::unordered_map<std::uint32_t, int> &onShelfQuantitySum,
                                InventoryPool &backroom);

} // namespace priceriot
#endif
