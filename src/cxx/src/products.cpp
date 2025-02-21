#include "products.h"
using namespace std;

Products::Products() {
    addProduct(1, "Widget", 19.99, "Gadgets");
    addProduct(2, "Gizmo", 29.99, "Gadgets");
    addProduct(3, "Doohickey", 9.99, "Accessories");
}

void Products::addProduct(int id, const std::string& name, double price, const std::string& category) {
    Product prod { id, name, category, price };
    productsMap[id] = prod;
}

bool Products::removeProduct(int id) {
    return productsMap.erase(id) > 0;
}

Product Products::getProduct(int id) const {
    auto it = productsMap.find(id);
    if(it == productsMap.end()){
        throw std::runtime_error("Product not found");
    }
    return it->second;
}

string Products::getProductCategory(int id) const {
  auto it = productsMap.find(id);
  if(it == productsMap.end()){
    throw std::runtime_error("Product not found");
  }
  return it->second.category;
}

double Products::getProductPrice(int id) const {
  auto it = productsMap.find(id);
  if(it == productsMap.end()){
    throw std::runtime_error("Product not found");
  }
  return it->second.price;
}

bool Products::productExists(int id) const {
    return productsMap.find(id) != productsMap.end();
}

bool Products::updatePrice(int id, double newPrice) {
    auto it = productsMap.find(id);
    if(it != productsMap.end()){
        it->second.price = newPrice;
        return true;
    }
    return false;
}

bool Products::updateCategory(int id, const std::string& newCategory) {
    auto it = productsMap.find(id);
    if(it != productsMap.end()){
        it->second.category = newCategory;
        return true;
    }
    return false;
}

void Products::printProducts() const {
    for(const auto &pair : productsMap) {
        const Product &p = pair.second;
        std::cout << "ID: " << p.id
                  << ", Name: " << p.name
                  << ", Category: " << p.category
                  << ", Price: $" << std::fixed << std::setprecision(2) << p.price
                  << std::endl;
    }
}
