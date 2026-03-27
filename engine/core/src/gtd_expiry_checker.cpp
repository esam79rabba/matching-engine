#include "engine/gtd_expiry_checker.h"
#include <algorithm>

namespace engine {

GtdExpiryChecker::GtdExpiryChecker(ITimeSource& clock) : clock_(clock) {}

void GtdExpiryChecker::register_partition(Partition* partition) {
    if (partition) partitions_.push_back(partition);
}

uint32_t GtdExpiryChecker::tick() {
    Timestamp now = clock_.now();
    uint32_t cancelled = 0;

    for (auto* partition : partitions_) {
        // Collect expired GTD orders across all instruments
        std::vector<OrderId> expired;

        for (const auto& [inst_id, engine] : partition->engines()) {
            engine.book().for_each_order([&](const Order& order) {
                if (order.time_in_force == TimeInForce::GTD &&
                    order.expiry_time > 0 && order.expiry_time <= now) {
                    expired.push_back(order.order_id);
                }
            });
        }

        // Sort for deterministic cancellation order
        std::sort(expired.begin(), expired.end());

        // Cancel through normal partition path
        for (OrderId oid : expired) {
            PartitionMessage cancel{};
            cancel.type = PartitionMessage::Type::Cancel;
            cancel.timestamp = now;
            cancel.orig_order_id = oid;
            auto result = partition->process(cancel);
            if (result.action == MatchResult::Action::Cancelled) ++cancelled;
        }
    }

    return cancelled;
}

}  // namespace engine
