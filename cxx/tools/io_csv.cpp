#include "io_csv.h"
#include "../engine/transaction.h" // <--- CRITICAL FIX: Defines Transaction class

#include <fstream>
#include <sstream>
#include <iostream>

namespace priceriot {

    std::vector<Transaction> loadTransactions(const std::string& filename) {
        std::vector<Transaction> transactions;

        // TODO: Implement actual parsing logic here if needed.
        // For now, returning an empty vector is fine, but the compiler
        // needs to know the size of 'Transaction' to generate the vector's destructor code.

        return transactions;
    }

} // namespace priceriot