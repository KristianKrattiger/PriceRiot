// store_init.cpp
#include "store_init.h"
#include "shelf.h"
#include "store_inventory.h"
#include <../cmake-build-debug/_deps/yaml-cpp-src/include/yaml-cpp/yaml.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional> // std::hash
#include <iostream>
#include <optional>
#include <sstream>
#include <unordered_map>

namespace priceriot {

// --- small local yaml_get (this TU doesn’t see environment.cpp’s helper)
template <typename T> static T yaml_get(const YAML::Node &n, const char *key, const T &def) {
    const auto sub = n[key];
    return sub ? sub.as<T>() : def;
}

// --- stable hash -> int for string SKUs (keeps same id across runs of this process)
static int sku_string_to_int(const std::string &s) {
    // keep in 31-bit positive range
    std::uint64_t h = std::hash<std::string>{}(s);
    std::uint32_t v = static_cast<std::uint32_t>(h ^ (h >> 32));
    v &= 0x7fffffffU;
    if (v == 0)
        v = 1;
    return static_cast<int>(v);
}

// ------- Catalog loader from products YAML (separate file) -------
std::unordered_map<std::string, int> load_products_from_yaml(const YAML::Node &root,
                                                             Catalog &catalog) {
    std::unordered_map<std::string, int> totals_by_sku;

    // Accept either top-level "products" or "inventory.products"
    YAML::Node productsY;
    if (root["products"]) {
        productsY = root["products"];
    } else if (root["inventory"] && root["inventory"]["products"]) {
        productsY = root["inventory"]["products"];
    }
    if (!productsY || !productsY.IsSequence())
        return totals_by_sku;

    for (const auto &p : productsY) {
        // ---- SKU (string or int) ----
        std::string sku_str;
        if (p["sku"]) {
            try {
                sku_str = std::to_string(p["sku"].as<int>());
            } catch (...) {
                sku_str = p["sku"].as<std::string>("");
            }
        }
        if (sku_str.empty())
            continue;

        // ---- Fields (with sensible defaults) ----
        const std::string name = yaml_get<std::string>(p, "name", "SKU_" + sku_str);
        const std::string category = yaml_get<std::string>(p, "category", "misc");
        const double price = yaml_get<double>(p, "price", 0.0);
        const double pop = yaml_get<double>(p, "popularity", 0.0);

        // map string SKU -> numeric id (hash) if not numeric
        int sku_int;
        try {
            sku_int = std::stoi(sku_str);
        } catch (...) {
            sku_int = sku_string_to_int(sku_str);
        }

        // Upsert into catalog
        if (catalog.productExists(sku_int)) {
            catalog.updateCategory(sku_int, category);
            catalog.updatePrice(sku_int, price);
            catalog.updatePopularity(sku_int, pop);
        } else {
            catalog.addProduct(sku_int, name, price, category, pop);
        }

        // Accept either "total" or "initial_stock_total"
        const int total_units =
            p["total"] ? p["total"].as<int>() : yaml_get<int>(p, "initial_stock_total", 0);

        totals_by_sku[sku_str] += total_units;
    }
    return totals_by_sku;
}

// ------- Catalog loader from products CSV -------
std::unordered_map<std::string, int> load_products_from_csv(const std::string &csvPath,
                                                            Catalog &catalog) {
    std::unordered_map<std::string, int> totals_by_sku;

    std::ifstream in(csvPath);
    if (!in.is_open()) {
        std::cerr << "[Products] Could not open CSV: " << csvPath << "\n";
        return totals_by_sku;
    }

    auto trim = [](std::string s) {
        auto issp = [](unsigned char c) { return std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char c){ return !issp(c); }));
        s.erase(std::find_if(s.rbegin(), s.rend(), [&](unsigned char c){ return !issp(c); }).base(), s.end());
        return s;
    };

    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (line.empty()) continue;

        // skip header row if it contains "sku"
        if (first) {
            first = false;
            std::string lower = line;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c){ return std::tolower(c); });
            if (lower.find("sku") != std::string::npos) continue;
        }

        std::stringstream ss(line);
        std::string field;

        // sku
        if (!std::getline(ss, field, ',')) continue;
        field = trim(field);
        int sku_int = 0;
        try { sku_int = std::stoi(field); } catch (...) { continue; }

        // name
        std::string name;
        if (!std::getline(ss, name, ',')) name.clear();
        name = trim(name);

        // category
        std::string category;
        if (!std::getline(ss, category, ',')) category.clear();
        category = trim(category);

        // price
        double price = 0.0;
        if (std::getline(ss, field, ',')) {
            try { price = std::stod(trim(field)); } catch (...) {}
        }

        // popularity
        double pop = 0.0;
        if (std::getline(ss, field, ',')) {
            try { pop = std::stod(trim(field)); } catch (...) {}
        }

        if (catalog.productExists(sku_int)) {
            catalog.updatePrice(sku_int, price);
            catalog.updatePopularity(sku_int, pop);
            catalog.updateCategory(sku_int, category);
        } else {
            catalog.addProduct(sku_int, name, price, category, pop);
        }
    }
    return totals_by_sku;
}

// ------- Planogram application -------
void apply_planogram_to_side(const YAML::Node &sideY, Catalog &catalog, ShelfSide &side,
                             std::unordered_map<std::uint32_t, int> &onShelfQuantitySum) {
    using namespace priceriot;

    if (!sideY || !sideY.IsMap())
        return;

    // bay count (clamped to capacity)
    const int bay_count = std::max(0, yaml_get<int>(sideY, "bay_count", 0));
    side.bay_count =
        static_cast<std::uint8_t>(std::min<int>(bay_count, static_cast<int>(MAX_BAYS_PER_CELL)));

    const YAML::Node pog = sideY["planogram"];
    if (!pog || !pog.IsSequence())
        return;

    for (const auto &rec : pog) {
        const int bay = yaml_get<int>(rec, "bay", 0);
        const int face = yaml_get<int>(rec, "face", 0);
        const int slot = yaml_get<int>(rec, "slot", 0);

        if (bay < 0 || bay >= static_cast<int>(MAX_BAYS_PER_CELL))
            continue;
        if (face < 0 || face >= static_cast<int>(MAX_FACES_PER_BAY))
            continue;
        if (slot < 0 || slot >= static_cast<int>(MAX_SLOTS_PER_FACE))
            continue;

        // sku (string or int)
        std::string sku_str;
        if (rec["sku"]) {
            try {
                sku_str = std::to_string(rec["sku"].as<int>());
            } catch (...) {
                sku_str = rec["sku"].as<std::string>("");
            }
        }
        if (sku_str.empty())
            continue;

        int sku_id;
        try {
            sku_id = std::stoi(sku_str);
        } catch (...) {
            sku_id = sku_string_to_int(sku_str);
        }

        // ensure it’s in catalog (idempotent no-op if already there)
        if (!catalog.productExists(sku_id)) {
            catalog.addProduct(sku_id, "SKU_" + sku_str, 0.0, "misc", 0.0);
        }

        const int qty_on_face = yaml_get<int>(rec, "on_shelf_qty", 0);

        // grow face/slot counts if needed
        side.bay_count = std::max<std::uint8_t>(side.bay_count, static_cast<std::uint8_t>(bay + 1));
        Bay &B = side.bays[bay];
        B.face_count = std::max<std::uint8_t>(B.face_count, static_cast<std::uint8_t>(face + 1));
        BayFace &F = B.faces[face];
        F.slot_count = std::max<std::uint8_t>(F.slot_count, static_cast<std::uint8_t>(slot + 1));

        // write slot
        F.slots[slot].sku_id = static_cast<std::uint32_t>(sku_id);
        F.slots[slot].qty_on_face = static_cast<std::uint16_t>(std::max(0, qty_on_face));

        // accumulate visible stock
        onShelfQuantitySum[static_cast<std::uint32_t>(sku_id)] += std::max(0, qty_on_face);
    }
}

// ------- Backstock computation -------
void compute_and_load_backstock(const YAML::Node &root, const Catalog & /*catalog*/,
                                const std::unordered_map<std::string, int> &totals_by_sku,
                                const std::unordered_map<std::uint32_t, int> &onShelfQuantitySum,
                                InventoryPool &backroom) {
    // Optional explicit overrides
    std::unordered_map<std::string, int> overrides;
    if (auto bs = root["backstock"]; bs && bs.IsMap()) {
        for (auto it = bs.begin(); it != bs.end(); ++it) {
            const std::string sku_str = it->first.as<std::string>("");
            if (sku_str.empty())
                continue;
            overrides[sku_str] = it->second.as<int>(0);
        }
    }

    // For each known SKU total, decide backstock
    for (const auto &[sku_str, total] : totals_by_sku) {
        // figure out sku_id (use same hashing rule)
        int sku_id;
        try {
            sku_id = std::stoi(sku_str);
        } catch (...) {
            // mirror the same hash rule
            sku_id = sku_string_to_int(sku_str);
        }
        const std::uint32_t k = static_cast<std::uint32_t>(sku_id);

        int visible = 0;
        if (auto it = onShelfQuantitySum.find(k); it != onShelfQuantitySum.end())
            visible = it->second;

        int qty = std::max(0, total - visible);

        // explicit override?
        if (auto ov = overrides.find(sku_str); ov != overrides.end()) {
            qty = std::max(0, ov->second);
        }

        if (qty > 0)
            backroom.put(k, qty);
    }
}

} // namespace priceriot
