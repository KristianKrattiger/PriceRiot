#ifndef IO_CSV_H
#define IO_CSV_H

#include <string>
#include <vector>

namespace priceriot {

// Forward declaration is sufficient here
class Transaction;

// Function to load transactions from a CSV file
std::vector<Transaction> loadTransactions(const std::string &filename);

} // namespace priceriot

#endif // IO_CSV_H