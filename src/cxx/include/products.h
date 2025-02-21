#ifndef PRODUCTS_H
#define PRODUCTS_H

#include <string>
#include <map>
#include <iostream>
#include <stdexcept>
// Structure holding the details for a product
struct ProductDetail {
    double price;
    std::string category;
};

class Products {
public:
    // Add a new product or update an existing one
    void addProduct(const std::string& productName, double price, const std::string& category);

    // Remove a product by name; returns true if removal was successful
    bool removeProduct(const std::string& productName);

    // Retrieve the product detail (price and category) for a given product
    // Throws a runtime_error if the product is not found.
    ProductDetail getProduct(const std::string& productName) const;

    bool productExists(const std::string& productName) const;

    bool updatePrice(const std::string& productName, double newPrice);

    bool updateCategory(const std::string& productName, const std::string& newCategory);

    void printProducts() const;

private:
    std::map<std::string, ProductDetail> productsMap;
};

#endif // PRODUCTS_H
