#ifndef SIM_H
#define SIM_H

#include <vector>
#include <filesystem>
#include <map>
#include <string>
#include <random>
#include "products.h"

struct Customer {
    int id{};
    double annualIncome{}, totalSpent{}, averageSpend{};
    int yearsAsCust{}, numPurchases{}, numReturns{},
        numSprtContacts{}, lastPurchaseInDays{}, age{};
    std::string gender, promotionResponse;
    bool optIn{}, churn{};
};

struct Transaction {
    int custID{}, transID{};
    double amount{}, pricePerUnit{}, totalSpent{};
    int quantity{}, satisfaction{};
    std::string productCategory, timestamp;
};

std::vector<Customer> loadCustomersFromCSV(const std::string &filename);
std::vector<Transaction> loadTransactionsFromCSV(const std::string &filename);
Transaction randomTransaction(std::vector<Customer>& customers, std::vector<Transaction>& transactions,
                                const std::map<int, Product>& productsMap,
                                std::default_random_engine &engine);

#endif // SIM_H