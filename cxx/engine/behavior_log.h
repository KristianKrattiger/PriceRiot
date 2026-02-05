/**
 * @file behavior_log.h
 * @brief Per-tick customer behavior event log for debugging and replay.
 *
 * Writes CSV rows (sim_time, customer_id, x, z, behavior_type, state_name,
 * decision_type, decision_target_id, basket_size, edge_index, dwell_ticks).
 * Supports focused-customer-only logging to control volume.
 */
#ifndef BEHAVIOR_LOG_H
#define BEHAVIOR_LOG_H

#include <fstream>
#include <string>

namespace priceriot {

class BehaviorEventLog {
  public:
    BehaviorEventLog() = default;
    ~BehaviorEventLog();

    /** Open log file and write CSV header. Returns true on success. */
    bool open(const std::string &path);

    /** Close the log file. */
    void close();

    /** If true, only log the focused customer (see setFocusedCustomerId). */
    void setFocusedOnly(bool focusedOnly) { focusedOnly_ = focusedOnly; }
    [[nodiscard]] bool getFocusedOnly() const { return focusedOnly_; }

    /** Focused customer id; -1 means "all" when not focused-only. */
    void setFocusedCustomerId(int id) { focusedCustomerId_ = id; }
    [[nodiscard]] int getFocusedCustomerId() const { return focusedCustomerId_; }

    /**
     * Write one tick row. Skips if focused-only and customerId != focused.
     * decisionType: Decision::DecisionType as int (Move=0, SwitchEdge=1, etc.).
     */
    void logTick(double simTime, int customerId, double x, double z,
                 const std::string &behaviorTypeStr, const std::string &stateName,
                 int decisionType, int targetId, int basketSize, int edgeIndex, int dwellTicks);

    /** Write RNG seed as a comment line for future replay. Call once when logging starts. */
    void writeSeedComment(unsigned long long seed);

    [[nodiscard]] bool isOpen() const { return file_.is_open(); }

  private:
    std::ofstream file_;
    bool focusedOnly_ = true;
    int focusedCustomerId_ = -1;
    static constexpr int FLUSH_EVERY_N_ROWS = 100;
    int rowsSinceFlush_ = 0;

    static const char *decisionTypeToString(int decisionType);
};

} // namespace priceriot

#endif // BEHAVIOR_LOG_H
