#include "sim.h"
#include "products.h"
#include <random>
#include <chrono>
#include <iostream>

int main() {
    // Seed random engine using the current time
    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
    std::default_random_engine engine(seed);

    // Vectors to store customers and transactions
    std::vector<Customer> customers =
        loadCustomersFromCSV("../src/python/data/raw/customer_stats.csv");
    std::vector<Transaction> transactions =
        loadTransactionsFromCSV("../src/python/data/processed/transaction_info.csv");

    // Create a Products instance and print starter products
    Products productManager;
    std::cout << "Starter Products:" << std::endl;
    productManager.printProducts();

    // Retrieve the products map from the Products instance
    const std::map<int, Product>& productsMap = productManager.getProductsMap();

    // Generate and test a few random transactions
    for (int i = 0; i < 5; ++i) {
        // Generate a random transaction and add it to our list
        Transaction trans = randomTransaction(customers, transactions, productsMap, engine);
        transactions.push_back(trans);

        // Output transaction details
        std::cout << "Transaction " << trans.transID << ": "
                  << "Customer " << trans.custID
                  << ", Category: " << trans.productCategory
                  << ", Quantity: " << trans.quantity
                  << ", Price per unit: $" << trans.pricePerUnit
                  << ", Total: $" << trans.totalSpent
                  << ", Timestamp: " << trans.timestamp << std::endl;
    }

    return 0;
}
