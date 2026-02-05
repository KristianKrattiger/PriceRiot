/**
 * @file behavior_log.cpp
 * @brief Implementation of per-tick behavior event log.
 */
#include "behavior_log.h"
#include "../agents/customer_behavior.h"

#include <cmath>
#include <sstream>

namespace priceriot {

BehaviorEventLog::~BehaviorEventLog() {
    close();
}

bool BehaviorEventLog::open(const std::string &path) {
    if (file_.is_open())
        close();
    file_.open(path);
    if (!file_.is_open())
        return false;
    file_ << "sim_time,customer_id,x,z,behavior_type,state_name,decision_type,decision_target_id,"
             "basket_size,edge_index,dwell_ticks\n";
    rowsSinceFlush_ = 0;
    return true;
}

void BehaviorEventLog::close() {
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
}

void BehaviorEventLog::writeSeedComment(unsigned long long seed) {
    if (!file_.is_open())
        return;
    file_ << "# rng_seed=" << seed << "\n";
    file_.flush();
}

const char *BehaviorEventLog::decisionTypeToString(int decisionType) {
    using DT = Decision::DecisionType;
    switch (static_cast<DT>(decisionType)) {
        case DT::Move: return "Move";
        case DT::SwitchEdge: return "SwitchEdge";
        case DT::PickProduct: return "PickProduct";
        case DT::Wait: return "Wait";
        case DT::Checkout: return "Checkout";
        case DT::Despawn: return "Despawn";
        default: return "Unknown";
    }
}

void BehaviorEventLog::logTick(double simTime, int customerId, double x, double z,
                               const std::string &behaviorTypeStr, const std::string &stateName,
                               int decisionType, int targetId, int basketSize, int edgeIndex,
                               int dwellTicks) {
    if (!file_.is_open())
        return;
    if (focusedOnly_ && focusedCustomerId_ >= 0 && customerId != focusedCustomerId_)
        return;

    // CSV: escape fields that might contain comma (we use simple numeric/identifier fields)
    file_ << simTime << "," << customerId << "," << x << "," << z << ","
          << behaviorTypeStr << "," << stateName << ","
          << decisionTypeToString(decisionType) << "," << targetId << ","
          << basketSize << "," << edgeIndex << "," << dwellTicks << "\n";

    rowsSinceFlush_++;
    if (rowsSinceFlush_ >= FLUSH_EVERY_N_ROWS) {
        file_.flush();
        rowsSinceFlush_ = 0;
    }
}

} // namespace priceriot
