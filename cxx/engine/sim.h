#ifndef SIM_H
#define SIM_H

#include <vector>
#include <map>
#include <string>
#include <random>
#include <chrono>
#include "products.h"
#include "customer.h"
#include "transaction.h"
#include "basket.h"


double makeWeight(const Customer& customer);

void newCustomer(std::vector<Customer>& customers, std::default_random_engine &engine);

std::shared_ptr<Customer> selectCustomer(
    std::vector<std::shared_ptr<Customer>>& customers,
    std::default_random_engine& engine
);

void transDetails (Transaction &newTransaction, const std::map<int, Product>& productsMap,
                        std::default_random_engine &engine);

std::string formatSimTime(const std::chrono::system_clock::time_point &simTime);

Transaction randomTransaction(std::vector<Customer>& customers, const std::vector<Transaction>& transactions,
                                const std::map<int, Product>& productsMap,
                                std::default_random_engine &engine);

void updateCustomerHistory(Customer* currentCust, const Transaction&);

void runSim();
#endif // SIM_H