#ifndef BASKET_H
#define BASKET_H

#include "products.h"
#include "transaction.h" // Needed for LineItem return type
#include <map>
#include <memory>
#include <random>
#include <vector>

namespace priceriot {

class Customer;

class Basket {
  public:
    Basket() = default;
    explicit Basket(std::shared_ptr<Customer> customerRef);
    ~Basket() = default;

    void addProduct(const Product &product);
    bool removeProduct(const Product &product);
    void clear();

    [[nodiscard]] int getSize() const;
    [[nodiscard]] double getTotal() const;

    // Note: 'getQuantity' and 'recalcTotal' removed to match provided .cpp implementation

    [[nodiscard]] Transaction toTransaction(int transID, int satisfaction,
                                            const std::string &ts) const;

    [[nodiscard]] std::shared_ptr<Customer> getCustomer() const;

  private:
    std::shared_ptr<Customer> customer_;
    double running_total_ = 0.0;
    std::vector<Product> products_;
};

// --- Helpers ---

// Matched signature to basket.cpp implementation
int estimateBasketSize(double mean, double dispersion_k, std::default_random_engine &engine);

void populateBasket(Basket &basket, const std::map<int, Product> &productsMap,
                    std::default_random_engine &engine);

} // namespace priceriot

#endif