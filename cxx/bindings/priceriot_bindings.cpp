/**
 * @file priceriot_bindings.cpp
 * @brief pybind11 Python extension module "simulation".
 *
 * Exposes the headless Simulator and its data types to Python.
 * No SFML, no ImGui, no rendering code is compiled in here.
 *
 * Build: CMake target "simulation" (see CMakeLists.txt)
 *
 * ─── Quick-start ──────────────────────────────────────────────────────────
 *
 *   import simulation
 *
 *   sim = simulation.Simulator("store.yaml")
 *   sim.set_spawn_interval(3.0)        # seconds between customer spawns
 *   sim.set_mission_probability(0.4)   # 40 % of customers are mission shoppers
 *
 *   # Option A – blocking run (3600 sim-seconds, 1/60 s fixed timestep)
 *   sim.run(3600.0)
 *
 *   # Option B – manual loop (inspect at any tick)
 *   for _ in range(216_000):           # 216000 * (1/60) == 3600 s
 *       sim.step(1.0 / 60.0)
 *
 *   txns  = sim.get_transactions()     # list[Transaction]
 *   custs = sim.get_customers()        # list[Customer]
 *   sim.export_transactions("out.csv") # write CSV
 *
 *   # Inspecting a transaction
 *   t = txns[0]
 *   print(t.trans_id, t.cust_id, t.total_spent, t.timestamp, t.satisfaction)
 *   for item in t.items():
 *       print(item.name, item.quantity, item.price_per_unit, item.total)
 *
 *   # Inspecting a customer
 *   c = custs[0]
 *   print(c.id, c.age, c.gender, c.loyalty_rating, c.num_purchases)
 *
 *   # Pandas integration
 *   import pandas as pd
 *
 *   rows = []
 *   for t in txns:
 *       for item in t.items():
 *           rows.append({**t.to_dict(), **item.to_dict()})
 *   df = pd.DataFrame(rows)
 *
 *   cust_df = pd.DataFrame([c.to_dict() for c in custs])
 */

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>       // auto-converts std::vector, std::string, etc.
#include <sstream>
#include <stdexcept>

// Engine headers — no SFML dependency
#include "../engine/simulator.h"
#include "../agents/customer.h"
#include "../engine/transaction.h"
#include "../agents/task.h"
#include "../agents/worker.h"

namespace py = pybind11;
using namespace priceriot;

// ─────────────────────────────────────────────────────────────────────────────
// __repr__ helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::string lineItemRepr(const LineItem &li) {
    std::ostringstream os;
    os << "<LineItem id=" << li.id
       << " name='" << li.name << "'"
       << " qty=" << li.quantity
       << " ppu=" << li.pricePerUnit
       << " total=" << li.total << ">";
    return os.str();
}

static std::string transactionRepr(const Transaction &tx) {
    std::ostringstream os;
    os << "<Transaction id=" << tx.getTransID()
       << " cust=" << tx.getCustID()
       << " total=" << tx.getTotalSpent()
       << " items=" << tx.getItems().size() << ">";
    return os.str();
}

static std::string customerRepr(const Customer &c) {
    std::ostringstream os;
    os << "<Customer id=" << c.getId()
       << " age=" << c.getAge()
       << " gender='" << c.getGender() << "'"
       << " loyalty=" << c.getLoyaltyRating()
       << " purchases=" << c.getNumPurchases() << ">";
    return os.str();
}

static std::string simulatorRepr(const Simulator &s) {
    std::ostringstream os;
    os << "<Simulator elapsed=" << s.getElapsedTime()
       << "s agents=" << s.getAgents().size()
       << " transactions=" << s.getTransactionCount() << ">";
    return os.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// Module
// ─────────────────────────────────────────────────────────────────────────────
PYBIND11_MODULE(simulation, m) {
    m.doc() =
        "PriceRiot retail simulation engine.\n\n"
        "Provides a headless Simulator that runs the full agent-based retail "
        "store model and returns Transaction and Customer analytics data.";

    // ─── TaskType / Task ─────────────────────────────────────────────────────
    py::enum_<TaskType>(m, "TaskType",
        "Type of staff task workers can execute.")
        .value("StockShelves",    TaskType::StockShelves)
        .value("ProcessRegister", TaskType::ProcessRegister)
        .value("AssistCustomer",  TaskType::AssistCustomer)
        .export_values();

    py::class_<Task>(m, "Task",
        "A single unit of staff work, such as stocking shelves or processing a register lane.")
        .def_readonly("id",        &Task::id,
                      "Unique task identifier (int).")
        .def_readonly("type",      &Task::type,
                      "TaskType enum.")
        .def_readonly("priority",  &Task::priority,
                      "Relative priority; higher values are scheduled first.")
        .def_readonly("target_id", &Task::targetId,
                      "Target lane / shelf / customer identifier.")
        .def_readonly("created_at",&Task::createdAt,
                      "Sim-time at which the task was enqueued (seconds).");

    // ─── TripPurpose enum ─────────────────────────────────────────────────────
    py::enum_<Customer::TripPurpose>(m, "TripPurpose",
        "Shopping trip intent: influences basket size and navigation strategy.")
        .value("StockUp", Customer::TripPurpose::StockUp,
               "Large, planned shop – high basket size multiplier")
        .value("TopUp",   Customer::TripPurpose::TopUp,
               "Small, routine shop – moderate basket")
        .value("Mission", Customer::TripPurpose::Mission,
               "Focused on a specific SKU list – minimal wandering")
        .export_values();

    // ─── LineItem ─────────────────────────────────────────────────────────────
    py::class_<LineItem>(m, "LineItem",
        "A single product line within a Transaction.")
        .def_readonly("id",             &LineItem::id,
                      "SKU identifier (int)")
        .def_readonly("name",           &LineItem::name,
                      "Human-readable product name")
        .def_readonly("quantity",       &LineItem::quantity,
                      "Number of units purchased")
        .def_readonly("price_per_unit", &LineItem::pricePerUnit,
                      "Unit price at time of purchase")
        .def_readonly("total",          &LineItem::total,
                      "quantity × price_per_unit")
        .def("to_dict", [](const LineItem &li) -> py::dict {
            return py::dict(
                py::arg("item_id")        = li.id,
                py::arg("item_name")      = li.name,
                py::arg("quantity")       = li.quantity,
                py::arg("price_per_unit") = li.pricePerUnit,
                py::arg("item_total")     = li.total
            );
        }, "Return fields as a plain dict. Keys match the CSV column names "
           "produced by Simulator.export_transactions().")
        .def("__repr__", &lineItemRepr);

    // ─── Transaction ──────────────────────────────────────────────────────────
    py::class_<Transaction>(m, "Transaction",
        "A completed purchase transaction (checkout event).")
        .def("trans_id",     &Transaction::getTransID,
             "Unique, monotonically-increasing transaction ID (int)")
        .def("cust_id",      &Transaction::getCustID,
             "ID of the Customer who made this purchase")
        .def("total_spent",  &Transaction::getTotalSpent,
             "Sum of all line-item totals (float)")
        .def("satisfaction", &Transaction::getSatisfaction,
             "Customer satisfaction score 1–10 (int). "
             "Currently a simple heuristic; future versions will model "
             "queue wait time, out-of-stock events, etc.")
        .def("timestamp",    &Transaction::getTimestamp,
             "Simulation-time string at checkout, formatted HH:MM:SS")
        .def("items",        &Transaction::getItems,
             "list[LineItem] – one entry per distinct SKU purchased. "
             "Returns a copy; safe to hold across step() calls.")
        .def("to_dict", [](const Transaction &tx) -> py::dict {
            return py::dict(
                py::arg("transaction_id") = tx.getTransID(),
                py::arg("customer_id")    = tx.getCustID(),
                py::arg("timestamp")      = tx.getTimestamp(),
                py::arg("satisfaction")   = tx.getSatisfaction(),
                py::arg("total_spent")    = tx.getTotalSpent()
            );
        }, "Return header fields as a plain dict (no line items). "
           "Combine with item.to_dict() to build a flat DataFrame row.")
        .def("__repr__", &transactionRepr);

    // ─── Customer ─────────────────────────────────────────────────────────────
    py::class_<Customer, std::shared_ptr<Customer>>(m, "Customer",
        "A shopper entity with demographics, behaviour profile, and history.\n\n"
        "Customers are owned by the Simulator and returned as shared_ptr so the "
        "Python reference stays valid even after the agent despawns.")

        // Demographics
        .def("id",             &Customer::getId,
             "Unique customer ID (int). Monotonically assigned at spawn.")
        .def("age",            &Customer::getAge,
             "Age drawn from N(38, 10) at spawn (int)")
        .def("gender",         &Customer::getGender,
             "\"Male\" or \"Female\" (str)")
        .def("annual_income",  &Customer::getAnnualIncome,
             "Annual income in thousands, drawn from log-normal dist (float)")
        .def("family_size",    &Customer::getFamilySize,
             "Household size (Poisson λ=3, min 1) influences basket size (int)")

        // Shopping history
        .def("total_spent",    &Customer::getTotalSpent,
             "Cumulative spend across all transactions (float)")
        .def("average_spend",  &Customer::getAverageSpend,
             "total_spent / num_purchases (float)")
        .def("loyalty_rating", &Customer::getLoyaltyRating,
             "Loyalty score 0–100, updated after each transaction (float)")
        .def("num_purchases",  &Customer::getNumPurchases,
             "Number of completed transactions (int)")
        .def("num_returns",    &Customer::getNumReturns,
             "Number of returned items (int; currently always 0 in v1)")
        .def("is_churn",       &Customer::isChurn,
             "Churn flag (bool; set by future ML model)")

        // Behaviour profile
        .def("trip_purpose",         &Customer::getTripPurpose,
             "TripPurpose enum: StockUp / TopUp / Mission")
        .def("basket_size_multiplier",&Customer::getBasketSizeMultiplier,
             "Scales target basket size (float, default 1.0)")
        .def("price_sensitivity",    &Customer::getPriceSensitivity,
             "0–1 price aversion (float, default 0.5)")
        .def("impulsivity",          &Customer::getImpulsivity,
             "0–1 chance of unplanned picks (float, default 0.35)")
        .def("patience",             &Customer::getPatience,
             "0–1 willingness to walk to a shorter queue (float)")
        .def("crowd_sensitivity",    &Customer::getCrowdSensitivity,
             "0–1 aversion to long queues (float)")

        // Navigation / position
        .def("pos_x",          &Customer::getPosX,
             "World X position in metres (float)")
        .def("pos_z",          &Customer::getPosZ,
             "World Z position in metres (float)")

        .def("to_dict", [](const Customer &c) -> py::dict {
            return py::dict(
                py::arg("id")                    = c.getId(),
                py::arg("age")                   = c.getAge(),
                py::arg("gender")                = c.getGender(),
                py::arg("annual_income")         = c.getAnnualIncome(),
                py::arg("family_size")           = c.getFamilySize(),
                py::arg("total_spent")           = c.getTotalSpent(),
                py::arg("average_spend")         = c.getAverageSpend(),
                py::arg("loyalty_rating")        = c.getLoyaltyRating(),
                py::arg("num_purchases")         = c.getNumPurchases(),
                py::arg("num_returns")           = c.getNumReturns(),
                py::arg("is_churn")              = c.isChurn(),
                py::arg("basket_size_multiplier")= c.getBasketSizeMultiplier(),
                py::arg("price_sensitivity")     = c.getPriceSensitivity(),
                py::arg("impulsivity")           = c.getImpulsivity()
            );
        }, "Return all fields as a plain dict. "
           "Handy for pd.DataFrame([c.to_dict() for c in sim.get_customers()]).")
        .def("__repr__", &customerRepr);

    // ─── Simulator ────────────────────────────────────────────────────────────
    py::class_<Simulator>(m, "Simulator",
        "Headless retail store simulation engine.\n\n"
        "Loads a store graph from YAML, spawns autonomous customer agents each "
        "tick, and records completed Transactions.  Thread-safe for reading "
        "transactions while the simulation is paused.\n\n"
        "Example::\n\n"
        "    sim = simulation.Simulator('store.yaml')\n"
        "    sim.set_spawn_interval(5.0)\n"
        "    sim.set_mission_probability(0.3)\n"
        "    sim.run(3600.0)\n"
        "    df = pd.DataFrame([t.to_dict() for t in sim.get_transactions()])\n")

        // ── Construction ──────────────────────────────────────────────────────
        .def(py::init<const std::string &, float, float, std::uint32_t>(),
             py::arg("yaml_path"),
             py::arg("spawn_interval")      = 5.0f,
             py::arg("mission_probability") = 0.5f,
             py::arg("seed")                = 0u,
             "Construct and load the store.\n\n"
             "Args:\n"
             "    yaml_path: Path to store.yaml (relative to cwd or absolute).\n"
             "    spawn_interval: Seconds of sim-time between customer spawns.\n"
             "    mission_probability: Fraction of spawned customers assigned "
             "MissionBehavior (0 = all Default, 1 = all Mission).\n"
             "    seed: Random seed; 0 = non-deterministic (default). "
             "Non-zero gives reproducible runs.\n\n"
             "Raises:\n"
             "    RuntimeError: If the YAML file cannot be parsed.\n")

        // ── Tick API ──────────────────────────────────────────────────────────
        .def("run", &Simulator::run,
             py::arg("duration_seconds"),
             py::arg("dt") = 1.0f / 60.0f,
             "Blocking headless run.\n\n"
             "Advances the simulation until ``elapsed_time >= duration_seconds``.\n\n"
             "Args:\n"
             "    duration_seconds: How much sim-time to simulate.\n"
             "    dt: Fixed timestep per tick (default 1/60 s ≈ 16.7 ms).\n")
        .def("step", &Simulator::step,
             py::arg("dt"),
             "Advance the simulation by exactly *dt* seconds (one tick).\n\n"
             "Use this instead of run() when you need to inspect state between "
             "ticks or implement a custom loop.\n\n"
             "Args:\n"
             "    dt: Timestep in seconds (e.g. 1/60).\n")
        .def("reset", &Simulator::reset,
             "Tear down all agents and transactions, then reload from YAML.\n\n"
             "Useful for running multiple independent trials without recreating "
             "the Simulator object.\n")

        // ── Data access ───────────────────────────────────────────────────────
        .def("get_transactions", &Simulator::getTransactions,
             "Return a snapshot (copy) of all completed transactions.\n\n"
             "Thread-safe: safe to call while another thread is stepping.\n\n"
             "Returns:\n"
             "    list[Transaction]\n")
        .def("get_customers", &Simulator::getCustomers,
             "Return all Customer objects ever spawned (active + despawned).\n\n"
             "Customers are shared_ptr-managed; the returned list is safe to "
             "hold after further step() calls.\n\n"
             "Returns:\n"
             "    list[Customer]\n")
        .def("transaction_count", &Simulator::getTransactionCount,
             "Return the number of completed transactions (cheap, no copy).\n\n"
             "Returns:\n"
             "    int\n")
        .def("export_transactions", &Simulator::exportTransactions,
             py::arg("path"),
             "Write all completed transactions to a CSV file.\n\n"
             "Columns: transaction_id, customer_id, timestamp, satisfaction, "
             "total_spent, item_id, item_name, quantity, price_per_unit, item_total.\n\n"
             "Args:\n"
             "    path: Output file path (str).\n\n"
             "Raises:\n"
             "    RuntimeError: If the file cannot be opened.\n")

        // ── Config ────────────────────────────────────────────────────────────
        .def("set_spawn_interval", &Simulator::setSpawnInterval,
             py::arg("seconds"),
             "Set the sim-time interval between customer spawns (float).\n"
             "Takes effect on the next step().\n")
        .def("set_mission_probability", &Simulator::setMissionProbability,
             py::arg("p"),
             "Set the fraction of customers assigned MissionBehavior (0–1, float).\n"
             "Takes effect on the next spawn.\n")
        .def("set_seed", &Simulator::setSeed,
             py::arg("seed"),
             "Set the RNG seed for reproducible runs (uint32). "
             "Call before run() or after reset() to get repeatable results.\n")

        // ── Read-only properties ───────────────────────────────────────────────
        .def_property_readonly("elapsed_time",
             &Simulator::getElapsedTime,
             "Total sim-time elapsed since construction or last reset() (float, seconds).")
        .def_property_readonly("spawn_interval",
             &Simulator::getSpawnInterval,
             "Current spawn interval in sim-seconds (float).")
        .def_property_readonly("mission_probability",
             &Simulator::getMissionProbability,
             "Current mission probability 0–1 (float).")
        .def_property_readonly("active_agent_count",
             [](const Simulator &s) { return s.getAgents().size(); },
             "Number of customer agents currently in the store (int).")

        // ── Aggregated traffic metrics ───────────────────────────────────────
        .def("get_cell_heatmap", &Simulator::getCellVisitCounts,
             "Return aggregated per-cell visit counts as a nested list.\n\n"
             "Outer index is edge index, inner index is cell index along that "
             "edge. Each value is the number of simulation ticks in which at "
             "least one agent occupied that cell.\n")

        // ── Queue metrics ────────────────────────────────────────────────────
        .def("get_queue_sample_times", &Simulator::getQueueSampleTimes,
             "Return the simulation times (seconds) at which queue lengths "
             "were sampled (one entry per tick).")
        .def("get_queue_lengths_history", &Simulator::getQueueLengthsHistory,
             "Return queue lengths per lane at each sampled time.\n\n"
             "Outer index is lane index in [0, getLaneCount()-1], inner index "
             "is sample index aligned with get_queue_sample_times().\n")

        // ── Workers / tasks ───────────────────────────────────────────────────
        .def("get_workers", [](const Simulator &s) {
            py::list out;
            auto snaps = s.getWorkerSnapshots();
            for (const auto &ws : snaps) {
                py::dict d;
                d["id"]              = ws.id;
                d["pos_x"]           = ws.posX;
                d["pos_z"]           = ws.posZ;
                d["can_stock"]       = ws.canStock;
                d["can_serve"]       = ws.canServe;
                d["happiness"]       = ws.happiness;
                d["task_efficiency"] = ws.taskEfficiency;
                if (ws.hasTask) {
                    py::dict td;
                    td["type"]      = ws.taskType;
                    td["target_id"] = ws.taskTargetId;
                    d["current_task"] = td;
                } else {
                    d["current_task"] = py::none();
                }
                out.append(std::move(d));
            }
            return out;
        }, "Return a list of dicts describing current workers and their tasks.")

        .def("__repr__", &simulatorRepr);
}