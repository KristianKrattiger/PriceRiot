#ifndef PRICERIOT_PRODUCTS_H
#define PRICERIOT_PRODUCTS_H

#include <map>
#include <string>

namespace priceriot {

// Structure holding the details for a product
struct Product {
    int sku = 0;
    std::string name;
    std::string category;
    double price = 0.0;
    double popularity = 0.0;
};

// Simple product catalog wrapper
class Products {
  public:
    // Construct and (optionally) load from CSV in the .cpp
    Products();

    // Add or upsert a product by fields
    void addProduct(int sku, const std::string &name, double price, const std::string &category,
                    double popularity);

    // Add or upsert a whole record
    void addProduct(const Product &product);

    // Remove a product by sku; returns true if removed
    bool removeProduct(int sku);

    // Lookup helpers (throw if not found)
    [[nodiscard]] Product getProduct(int sku) const;
    [[nodiscard]] std::string getProductCategory(int sku) const;
    [[nodiscard]] double getProductPrice(int sku) const;
    [[nodiscard]] double getProductPopularity(int sku) const;

    // Existence check
    [[nodiscard]] bool productExists(int sku) const;

    // Mutators
    bool updatePrice(int sku, double newPrice);
    bool updateCategory(int sku, const std::string &newCategory);
    bool updatePopularity(int sku, double newPopularity);

    // Debug print (implemented in .cpp)
    void printProducts() const;

    // Read-only access to the underlying map
    [[nodiscard]] const std::map<int, Product> &getProductsMap() const {
        return productsMap_;
    }

  private:
    std::map<int, Product> productsMap_;
};

// Some code refers to a Catalog; alias it here.
using Catalog = Products;

} // namespace priceriot

#endif // PRICERIOT_PRODUCTS_H
