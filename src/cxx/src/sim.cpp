#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <filesystem>

using namespace std;

struct Customer {
    int id;
    double annualIncome, totalSpent, averageSpend;
    int yearsAsCust, numPurchases, numReturns,
        numSprtCntcts, satisfaction, lastPurchaseInDays, age;
    string gender, promotionResponse;
    bool optIn, churn;

};

struct Transaction {
  int custID, transID;
  double amount, pricePerUnit;
  int quantity;
  string productCategory, timestamp;
};

vector<Customer> loadCustomersFromCSV(const string &filename) {
    vector<Customer> customers;
    ifstream customersFile(filename);
    if (!customersFile.is_open()) {
      cerr << "Could not open file " << filename << endl;
      return customers;
    }

    string line;

    //ignore table header
    getline(customersFile, line);

    while (getline(customersFile, line)) {
      istringstream ss(line);
      string token;
      Customer customer;

      if (getline(ss, token, ',')) {
        customer.id = stoi(token);
      }
      if (getline(ss, token, ',')) {
        customer.gender = token;
      }
      if (getline(ss, token, ',')) {
        customer.age = stoi(token);
      }
      if (getline(ss, token, ',')) {
        customer.annualIncome = stod(token);
      }
      if (getline(ss, token, ',')) {
        customer.totalSpent = stod(token);
      }
      if (getline(ss, token, ',')) {
        customer.yearsAsCust = stoi(token);
      }
      if (getline(ss, token, ',')) {
        customer.numPurchases = stoi(token);
      }
      if (getline(ss, token, ',')) {
        customer.averageSpend = stod(token);
      }
      if (getline(ss, token, ',')) {
        customer.numReturns = stoi(token);
      }
      if (getline(ss, token, ',')) {
        customer.satisfaction = stoi(token);
      }
      if (getline(ss, token, ',')) {
        customer.lastPurchaseInDays = stoi(token);
      }
      if (getline(ss, token, ',')) {
        customer.churn = stoi(token);
      }
      if (getline(ss, token, ',')) {
        customer.promotionResponse = token;
      }

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
  getline(transactionsFile, line);
  while (getline(transactionsFile, line)) {
    istringstream ss(line);
    string token;
    Transaction transaction;
    if (getline(ss, token, ',')) {
      transaction.transID = stoi(token);
    }
    if (getline(ss, token, ',')) {
      transaction.custID = stoi(token);
    }
  }
}
double simulateTransactionAmount(double baseAmount, double volatility,
                                 default_random_engine &engine) {
    normal_distribution<double> distribution(baseAmount, volatility);
    return distribution(engine);
}

int main () {
  vector<Customer> customers =
    loadCustomersFromCSV(R"(..\src\python\data\raw\customer_stats.csv)");

  for (auto & customer : customers) {
      cout << "Customer ID: " << customer.id <<
      ", Annual Income: " << customer.annualIncome <<
      ", Total Spent: " << customer.totalSpent <<
      ", Average Spend: " << customer.averageSpend <<
      ", Last Purchase: " << customer.lastPurchaseInDays <<
      ", Age: " << customer.age << endl;
    }

}