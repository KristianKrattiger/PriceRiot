#include "sim.h"
#include "products.h"
#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <thread>

using namespace std;

vector<Customer> loadCustomersFromCSV(const string &filename) {
    vector<Customer> customers;
    ifstream customersFile(filename);
    if (!customersFile.is_open()) {
        cerr << "Could not open file " << filename << endl;
        return customers;
    }

    string line;
    // Ignore table header
    getline(customersFile, line);

    while (getline(customersFile, line)) {
        istringstream ss(line);
        string token;
        Customer customer;
        if (getline(ss, token, ',')) { customer.id = stoi(token); }
        if (getline(ss, token, ',')) { customer.gender = token; }
        if (getline(ss, token, ',')) { customer.age = stoi(token); }
        if (getline(ss, token, ',')) { customer.annualIncome = stod(token); }
        if (getline(ss, token, ',')) { customer.totalSpent = stod(token); }
        if (getline(ss, token, ',')) { customer.yearsAsCust = stoi(token); }
        if (getline(ss, token, ',')) { customer.numPurchases = stoi(token); }
        if (getline(ss, token, ',')) { customer.averageSpend = stod(token); }
        if (getline(ss, token, ',')) { customer.numReturns = stoi(token); }
        if (getline(ss, token, ',')) { customer.lastPurchaseInDays = stoi(token); }
        if (getline(ss, token, ',')) { customer.churn = stoi(token); }
        if (getline(ss, token, ',')) { customer.promotionResponse = token; }
        customers.push_back(customer);
    }
    customersFile.close();
    return customers;
}

vector<Transaction> loadTransactionsFromCSV(const string &filename) {
    vector<Transaction> transactions;
    ifstream transactionsFile(filename);
    if (!transactionsFile.is_open()) {
        cerr << "Could not open file " << filename << endl;
        return transactions;
    }
    string line;
    // Skip header
    getline(transactionsFile, line);
    while (getline(transactionsFile, line)) {
        istringstream ss(line);
        string token;
        Transaction transaction;
        if (getline(ss, token, ',')) { transaction.transID = stoi(token); }
        if (getline(ss, token, ',')) { transaction.custID = stoi(token); }
        if (getline(ss, token, ',')) { transaction.productCategory = token; }
        if (getline(ss, token, ',')) { transaction.quantity = stoi(token); }
        if (getline(ss, token, ',')) { transaction.pricePerUnit = stod(token); }
        if (getline(ss, token, ',')) { transaction.totalSpent = stod(token); }
        if (getline(ss, token, ',')) { transaction.timestamp = token; }
        if (getline(ss, token, ',')) { transaction.satisfaction = stoi(token); }
        transactions.push_back(transaction);
    }
    transactionsFile.close();
    return transactions;
}

Transaction randomTransaction(vector<Customer>& customers, vector<Transaction>& transactions,
                                const map<int, Product>& productsMap,
                                default_random_engine &engine) {
    Transaction newTransaction;
    Customer* transCustomer = nullptr;

    uniform_int_distribution<int> actionDist(0, 1); // New customer or use existing one?
    if (int action = actionDist(engine); action == 0) { // New customer
        auto newCust = Customer();
        uniform_int_distribution<int> ageDist(18, 75);
        uniform_int_distribution<int> genderDist(0, 1);

        // Generate customer starter data
        newCust.id = customers.empty() ? 1 : customers.back().id + 1;
        newCust.age = ageDist(engine);
        newCust.gender = genderDist(engine) ? "Male" : "Female";
        customers.push_back(newCust);
        transCustomer = &customers.back();
    }else {
        // Use an existing customer
        if (customers.empty()) {
            throw runtime_error("No customers available to select from.");
        }
        uniform_int_distribution<size_t> custIndexDist(0, customers.size() - 1);
        transCustomer = &customers[custIndexDist(engine)];
    }
    newTransaction.custID = transCustomer->id;
    newTransaction.transID = transactions.empty() ? 1 : transactions.back().transID + 1;

    if (productsMap.empty()) {
        throw runtime_error("No products available to select from.");
    }
    uniform_int_distribution<size_t> productIndexDist(0, productsMap.size() - 1);
    size_t productIndex = productIndexDist(engine);
    auto it = productsMap.begin();
    advance(it, productIndex);
    const Product& selectedProduct = it->second;

    //set related details
    newTransaction.productCategory = selectedProduct.category;
    newTransaction.pricePerUnit = selectedProduct.price;

    // Pick qty
    uniform_int_distribution<size_t> qtyDist(0, 100);
    newTransaction.quantity = qtyDist(engine);

    // Calculate price of order
    newTransaction.totalSpent = newTransaction.quantity * newTransaction.pricePerUnit;

    // Generate satisfaction rating
    uniform_int_distribution<int> satisfactionDist(1, 10);
    newTransaction.satisfaction = satisfactionDist(engine);

    // Generate timestamp in "YYYY-MM-DD HH:MM:SS" format
    auto now = chrono::system_clock::now();
    time_t now_time = chrono::system_clock::to_time_t(now);
    tm local_tm;
#ifdef _WIN32
    localtime_s(&local_tm, &now_time);
#else
    localtime_r(&now_time, &local_tm);
#endif
    ostringstream oss;
    oss << put_time(&local_tm, "%Y-%m-%d %H:%M:%S");
    newTransaction.timestamp = oss.str();

    return newTransaction;
}