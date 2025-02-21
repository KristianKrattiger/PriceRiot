#ifndef SIM_H
#define SIM_H

#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <thread>
#include <map>
#include <string>
#include "products.h"
using namespace std;

struct Customer {
    int id{};
    double annualIncome{}, totalSpent{}, averageSpend{};
    int yearsAsCust{}, numPurchases{}, numReturns{},
        numSprtContacts{}, satisfaction{}, lastPurchaseInDays{}, age{};
    string gender, promotionResponse;
    bool optIn{}, churn{};
};

struct Transaction {
    int custID{}, transID{};
    double amount{}, pricePerUnit{}, totalSpent{};
    int quantity{};
    string productCategory, timestamp;
};

vector<Customer> loadCustomersFromCSV(const string &filename);
vector<Transaction> loadTransactionsFromCSV(const string &filename);
double simulateTransactionAmount(double baseAmount, double volatility,
                                 default_random_engine &engine);

#endif // SIM_H