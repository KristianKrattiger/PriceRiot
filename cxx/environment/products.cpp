#include "products.h"

#include <algorithm>
#include <fstream>
#include <iomanip>  // for std::setw, std::left
#include <iostream> // for std::cout, std::cerr
#include <map>
#include <sstream>
#include <utility>

namespace priceriot {

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
    }

    return out;
}

// ---------------- Products ----------------

Products::Products() : productsMap_(loadProductsFromCSV_min("../../data/raw/products.csv")) {}

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
    std::cout << "=== Products (" << productsMap_.size() << ") ===\n";
    std::cout << std::left << std::setw(8) << "SKU" << std::setw(24) << "Name" << std::setw(18)
              << "Category" << std::setw(10) << "Price" << std::setw(12) << "Popularity" << "\n";

    for (const auto &[sku, p] : productsMap_) {
        std::cout << std::left << std::setw(8) << sku << std::setw(24) << p.name << std::setw(18)
                  << p.category << std::setw(10) << p.price << std::setw(12) << p.popularity
                  << "\n";
    }
    std::cout << std::endl;
}

} // namespace priceriot
