#ifndef PRODUCTS_H
#define PRODUCTS_H

#include <string>
#include <map>
#include <stdexcept>
#include <iostream>
#include <iomanip>

// Structure holding the details for a product
struct Product {
    int id;
    std::string name;
    std::string category;
    double price;
};

class Products {
public:
    // Constructor that initializes the products map with a few starter products
    Products();

    // Add a new product (or update an existing one by id)
    void addProduct(int id, const std::string& name, double price, const std::string& category);

    // Remove a product by id; returns true if removal was successful
    bool removeProduct(int id);

    // Retrieve a product by id; throws a runtime_error if not found.
    Product getProduct(int id) const;

    std::string getProductCategory(int id) const;

    double getProductPrice(int id) const;

    // Check if a product exists by id
    bool productExists(int id) const;

    // Update the price of a product by id
    bool updatePrice(int id, double newPrice);

    // Update the category of a product by id
    bool updateCategory(int id, const std::string& newCategory);

    // Print all products
    void printProducts() const;

    const std::map<int, Product>& getProductsMap() const {
        return productsMap;
    }

private:
    std::map<int, Product> productsMap;
};

#endif // PRODUCTS_H
