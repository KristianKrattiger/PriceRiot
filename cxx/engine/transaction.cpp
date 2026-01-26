#include "transaction.h"
#include "customer.h"
#include "products.h"
#include "basket.h" // Added for Basket definition used in randomTransaction
#include <iostream>
#include <iomanip>
#include <numeric>
#include <algorithm>

namespace priceriot {

Transaction::Transaction(int customerId, int transactionId, std::vector<LineItem> items, int satisfaction, std::string timestamp)
    : custID(customerId), transID(transactionId), items(std::move(items)), satisfaction(satisfaction), timestamp(std::move(timestamp)) {
    totalSpent = std::accumulate(this->items.begin(), this->items.end(), 0.0,
                                 [](double sum, const LineItem& item) { return sum + item.total; });
}

// Dummy constructors for compilation if needed
Transaction::Transaction() : custID(0), transID(0), satisfaction(0), totalSpent(0.0) {}
Transaction::Transaction(int c, int t) : custID(c), transID(t), satisfaction(0), totalSpent(0.0) {}

// Getters
int Transaction::getCustID() const { return custID; }
int Transaction::getTransID() const { return transID; }
int Transaction::getSatisfaction() const { return satisfaction; }
std::string Transaction::getTimestamp() const { return timestamp; }
const std::vector<LineItem>& Transaction::getItems() const { return items; }
double Transaction::getTotalSpent() const { return totalSpent; }

// Setters
void Transaction::setCustID(int c) { custID = c; }
void Transaction::setTransID(int id) { transID = id; }
void Transaction::setSatisfaction(int s) { satisfaction = s; }
void Transaction::setTimestamp(const std::string& ts) { timestamp = ts; }
void Transaction::setItems(const std::vector<LineItem>& newItems) { items = newItems; }


Transaction Transaction::randomTransaction(std::vector<std::shared_ptr<Customer>>& customers,
                                           const std::vector<Transaction>& history,
                                           const std::map<int, Product>& productCatalog,
                                           std::default_random_engine& engine) {

    std::shared_ptr<Customer> custPtr;
    int basketSize = 0;

    if (customers.empty()) {
        auto [newCust, newB] = newCustomer(customers, engine);
        custPtr = newCust;
        basketSize = 1;
    } else {
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        if (dist(engine) < 0.2) {
            auto [newCust, newB] = newCustomer(customers, engine);
            custPtr = newCust;
            basketSize = 1;
        } else {
            custPtr = selectCustomer(customers, engine);
            basketSize = 5;
        }
    }

    // Very basic placeholder logic for generating a transaction
    // In a real sim, you'd populate items here
    std::vector<LineItem> items;
    return Transaction(custPtr->getId(), 0, items, 5, "0");
}

} // namespace priceriot