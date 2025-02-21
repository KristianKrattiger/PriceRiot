#include "products.h"
#include <cstdio>
using namespace std;

void Products::addProduct(const string& productName, double price, const string& category) {
  productsMap[productName] = {price, category};
}

bool Products::removeProduct(const string& productName) {
  auto it = productsMap.find(productName);
  if (it != productsMap.end()) {
    productsMap.erase(it);
    return true;
  }
  return false;
}

ProductDetail Products::getProduct(const string& productName) const{
  auto it = productsMap.find(productName);
  if (it != productsMap.end()) {
    return it->second;
  }
  throw std::runtime_error("Product not found: " + productName);
}

bool Products::productExists(const string& productName) const{
  return (productsMap.find(productName) != productsMap.end());
}

bool Products::updatePrice(const std::string& productName, double newPrice) {
  auto it = productsMap.find(productName);
  if (it != productsMap.end()) {
    it->second.price = newPrice;
    return true;
  }
  return false;
}

bool Products::updateCategory(const std::string& productName, const std::string& newCategory) {
  auto it = productsMap.find(productName);
  if (it != productsMap.end()) {
    it->second.category = newCategory;
  }
  return false;
}

void Products::printProducts() const {
  cout << "Products: " << endl;
  cout << productsMap.size() << endl;
  cout << "===================================================================" << endl;
  for (auto it = productsMap.begin(); it != productsMap.end(); it++) {
    cout << "Name: " << it->first << endl;
    cout << "Category: " << it->second.category << endl;
    cout << "Price: " << it->second.price << endl;
    cout << endl;
    cout << "===================================================================" << endl;
  }
}

int main() {
  Products store;

  // Add some products
  store.addProduct("Laptop", 1200.99, "Electronics");
  store.addProduct("Chair", 89.99, "Furniture");
  store.addProduct("Coffee", 4.99, "Beverages");

  // Display all products
  std::cout << "Initial product list:" << std::endl;
  store.printProducts();

  // Update product information
  store.updatePrice("Laptop", 1100.50);
  store.updateCategory("Chair", "Office Furniture");

  std::cout << "\nAfter updates:" << std::endl;
  store.printProducts();

  // Remove a product
  if (store.removeProduct("Coffee")) {
    std::cout << "\nAfter removal of Coffee:" << std::endl;
    store.printProducts();
  } else {
    std::cout << "\nCoffee was not found in the store." << std::endl;
  }

  // Access a single product's details
  try {
    ProductDetail laptopDetails = store.getProduct("Laptop");
    std::cout << "\nLaptop details - Price: " << laptopDetails.price
              << ", Category: " << laptopDetails.category << std::endl;
  } catch (const std::runtime_error &e) {
    std::cerr << e.what() << std::endl;
  }

  return 0;
}
