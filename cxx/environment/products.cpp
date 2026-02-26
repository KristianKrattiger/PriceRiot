#include "products.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <utility>
#include <filesystem>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace priceriot {

// #region agent log
namespace {
void debugLogProduct(int sku, const std::string& name, const std::string& cat, double price) {
    std::ofstream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app);
    if(lf) lf << "{\"hypothesisId\":\"A\",\"location\":\"products.cpp:loadProductsFromCSV\",\"message\":\"Product loaded\",\"data\":{\"sku\":" << sku << ",\"name\":\"" << name << "\",\"category\":\"" << cat << "\",\"price\":" << price << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n";
}
} // namespace
// #endregion

namespace {

// Resolve the project root by walking upward from the current module (exe/.pyd)
// and looking for a directory that contains data/raw/products.csv or a common
// sentinel such as CMakeLists.txt or README.md.
static std::filesystem::path resolveProjectRootFromModule() {
    namespace fs = std::filesystem;

    fs::path modulePath;

#if defined(_WIN32)
    HMODULE hModule = nullptr;
    if (GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&resolveProjectRootFromModule),
            &hModule)) {
        char buffer[MAX_PATH]{};
        DWORD len = GetModuleFileNameA(hModule, buffer, static_cast<DWORD>(sizeof(buffer)));
        if (len > 0 && len < static_cast<DWORD>(sizeof(buffer))) {
            modulePath = fs::path(buffer);
        }
    }
#else
    Dl_info info{};
    if (dladdr(reinterpret_cast<void *>(&resolveProjectRootFromModule), &info) && info.dli_fname) {
        modulePath = fs::path(info.dli_fname);
    }
#endif

    if (modulePath.empty()) {
        // Fallback: current working directory (better than nothing)
        return fs::current_path();
    }

    fs::path dir = modulePath.parent_path();
    fs::path lastSentinelDir;

    while (!dir.empty()) {
        // Preferred: explicit CSV location
        if (std::filesystem::exists(dir / "data" / "raw" / "products.csv")) {
            return dir;
        }

        if (std::filesystem::exists(dir / "CMakeLists.txt") ||
            std::filesystem::exists(dir / "README.md") ||
            std::filesystem::exists(dir / ".git")) {
            lastSentinelDir = dir;
        }

        auto parent = dir.parent_path();
        if (parent == dir)
            break;
        dir = parent;
    }

    if (!lastSentinelDir.empty())
        return lastSentinelDir;

    return fs::current_path();
}

static std::string resolveProductsCsvPath() {
    namespace fs = std::filesystem;

    const fs::path root = resolveProjectRootFromModule();
    const fs::path canonicalCsv = root / "data" / "raw" / "products.csv";
    if (fs::exists(canonicalCsv)) {
        return canonicalCsv.string();
    }

    // Fallbacks for older workflows: relative to CWD.
    const fs::path rel1 = fs::path("data") / "raw" / "products.csv";
    if (fs::exists(rel1))
        return rel1.string();

    const fs::path rel2 = fs::path("..") / "data" / "raw" / "products.csv";
    if (fs::exists(rel2))
        return rel2.string();

    const fs::path rel3 = fs::path("..") / ".." / "data" / "raw" / "products.csv";
    if (fs::exists(rel3))
        return rel3.string();

    // Last resort: original relative path; loader will log an error if it fails.
    return std::string("data/raw/products.csv");
}

} // namespace

// Minimal CSV loader used at construction time.
// Expected columns (header optional): sku,name,category,price,popularity
static std::map<int, Product> loadProductsFromCSV_min(const std::string &path) {
    std::map<int, Product> out;

    std::ifstream in(path);
    if (!in.is_open()) {
        std::cerr << "[Products] Could not open " << path << " — starting empty.\n";
        return out;
    }

    auto trim = [](std::string s) {
        const auto issp = [](unsigned char c) { return std::isspace(c); };
        s.erase(s.begin(),
                std::find_if(s.begin(), s.end(), [&](unsigned char c) { return !issp(c); }));
        s.erase(
            std::find_if(s.rbegin(), s.rend(), [&](unsigned char c) { return !issp(c); }).base(),
            s.end());
        return s;
    };

    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (line.empty())
            continue;

        // If first line looks like a header, skip it.
        if (first) {
            first = false;
            std::string lower = line;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (lower.find("sku") != std::string::npos && lower.find("name") != std::string::npos) {
                // header row
                continue;
            }
        }

        std::stringstream ss(line);
        std::string field;

        // sku
        if (!std::getline(ss, field, ','))
            continue;
        int sku = 0;
        try {
            sku = std::stoi(trim(field));
        } catch (...) {
            continue;
        }

        // name
        std::string name;
        if (!std::getline(ss, name, ','))
            name.clear();
        name = trim(name);

        // category
        std::string category;
        if (!std::getline(ss, category, ','))
            category.clear();
        category = trim(category);

        // price
        double price = 0.0;
        if (std::getline(ss, field, ',')) {
            try {
                price = std::stod(trim(field));
            } catch (...) {
                price = 0.0;
            }
        }

        // popularity
        double popularity = 0.0;
        if (std::getline(ss, field, ',')) {
            try {
                popularity = std::stod(trim(field));
            } catch (...) {
                popularity = 0.0;
            }
        }

        out[sku] = Product{sku, name, category, price, popularity};
        // #region agent log
        debugLogProduct(sku, name, category, price);
        // #endregion
    }

    return out;
}

// ---------------- Products ----------------

Products::Products()
    : productsMap_(loadProductsFromCSV_min(resolveProductsCsvPath())) {}

// Add/Update by fields
void Products::addProduct(int sku, const std::string &name, double price,
                          const std::string &category, double popularity) {
    productsMap_[sku] = Product{sku, name, category, price, popularity};
}

// Add/Update by struct
void Products::addProduct(const Product &product) {
    productsMap_[product.sku] = product;
}

bool Products::removeProduct(int sku) {
    return productsMap_.erase(sku) > 0;
}

Product Products::getProduct(int sku) const {
    if (auto it = productsMap_.find(sku); it != productsMap_.end()) {
        return it->second;
    }
    // Not found: return a sentinel Product (sku 0) so callers don't crash.
    return Product{0, "", "", 0.0, 0.0};
}

std::string Products::getProductCategory(int sku) const {
    return getProduct(sku).category;
}

double Products::getProductPrice(int sku) const {
    return getProduct(sku).price;
}

double Products::getProductPopularity(int sku) const {
    return getProduct(sku).popularity;
}

bool Products::productExists(int sku) const {
    return productsMap_.find(sku) != productsMap_.end();
}

bool Products::updatePrice(int sku, double newPrice) {
    if (auto it = productsMap_.find(sku); it != productsMap_.end()) {
        it->second.price = newPrice;
        return true;
    }
    return false;
}

bool Products::updateCategory(int sku, const std::string &newCategory) {
    if (auto it = productsMap_.find(sku); it != productsMap_.end()) {
        it->second.category = newCategory;
        return true;
    }
    return false;
}

bool Products::updatePopularity(int sku, double newPopularity) {
    if (auto it = productsMap_.find(sku); it != productsMap_.end()) {
        it->second.popularity = newPopularity;
        return true;
    }
    return false;
}
//
// const std::map<int, Product>& Products::getProductsMap() const {
//     return productsMap_;
// }

void Products::printProducts() const {
    // No console output; use file-based behavior log or GUI for debugging.
}

} // namespace priceriot
