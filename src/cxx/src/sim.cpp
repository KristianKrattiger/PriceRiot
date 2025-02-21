#include "sim.h"
#include "products.h"

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

double simulateTransactionAmount(double baseAmount, double volatility,
                                 default_random_engine &engine) {
    normal_distribution<double> distribution(baseAmount, volatility);
    return distribution(engine);
}

int main () {
    vector<Customer> customers =
        loadCustomersFromCSV(R"(..\src\python\data\raw\customer_stats.csv)");
    vector<Transaction> transactions =
        loadTransactionsFromCSV(R"(..\src\python\data\processed\transaction_info.csv)");

    default_random_engine engine(chrono::system_clock::now().time_since_epoch().count());
    uniform_int_distribution<int> actionDist(0, 1);
    uniform_int_distribution<int> ageDist(18, 80);
    uniform_int_distribution<int> numPurchasesDist(1, 50);
    uniform_int_distribution<int> productDist(1, 50);
    uniform_real_distribution<double> incomeDist(10.0, 200.0);

    // Output loaded customer data
    for (auto &customer : customers) {
        cout << "Customer ID: " << customer.id
             << ", Annual Income: " << customer.annualIncome
             << ", Total Spent: " << customer.totalSpent
             << ", Average Spend: " << customer.averageSpend
             << ", Last Purchase: " << customer.lastPurchaseInDays
             << ", Age: " << customer.age << endl;
    }

    // Output loaded transaction data
    for (auto &transaction : transactions) {
        cout << "Transaction ID: " << transaction.transID
             << ", CustID: " << transaction.custID
             << ", Product Category: " << transaction.productCategory
             << ", Quantity: " << transaction.quantity
             << ", Price per unit: " << transaction.pricePerUnit
             << ", Total transaction amount: " << transaction.totalSpent
             << ", Purchase date: " << transaction.timestamp << endl;
    }

    return 0;
}
