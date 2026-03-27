#include "refdata/ref_data_service.h"

namespace refdata {

void RefDataService::add_instrument(const engine::InstrumentConfig& config) {
    instruments_[config.instrument_id] = config;
}

void RefDataService::update_instrument(engine::InstrumentId id, const engine::InstrumentConfig& config) {
    instruments_[id] = config;
}

std::optional<engine::InstrumentConfig> RefDataService::get_instrument(engine::InstrumentId id) const {
    auto it = instruments_.find(id);
    if (it == instruments_.end()) return std::nullopt;
    return it->second;
}

std::vector<engine::InstrumentId> RefDataService::list_instruments() const {
    std::vector<engine::InstrumentId> ids;
    ids.reserve(instruments_.size());
    for (const auto& [id, _] : instruments_) ids.push_back(id);
    return ids;
}

engine::InstrumentId RefDataService::resolve_symbol(const std::string& symbol) const {
    auto it = symbol_map_.find(symbol);
    return (it != symbol_map_.end()) ? it->second : 0;
}

void RefDataService::register_symbol(const std::string& symbol, engine::InstrumentId id) {
    symbol_map_[symbol] = id;
}

engine::Price RefDataService::get_price_multiplier(engine::InstrumentId id) const {
    auto it = price_multipliers_.find(id);
    return (it != price_multipliers_.end()) ? it->second : 1;
}

engine::Quantity RefDataService::get_qty_multiplier(engine::InstrumentId id) const {
    auto it = qty_multipliers_.find(id);
    return (it != qty_multipliers_.end()) ? it->second : 1;
}

void RefDataService::add_participant(const ParticipantInfo& info) {
    participants_[info.id] = info;
    if (!info.sender_comp_id.empty()) {
        sender_comp_id_map_[info.sender_comp_id] = info.id;
    }
}

std::optional<RefDataService::ParticipantInfo> RefDataService::get_participant(engine::ParticipantId id) const {
    auto it = participants_.find(id);
    if (it == participants_.end()) return std::nullopt;
    return it->second;
}

std::vector<engine::ParticipantId> RefDataService::list_participants() const {
    std::vector<engine::ParticipantId> ids;
    for (const auto& [id, _] : participants_) ids.push_back(id);
    return ids;
}

engine::ParticipantId RefDataService::resolve_participant(const std::string& sender_comp_id) const {
    auto it = sender_comp_id_map_.find(sender_comp_id);
    return (it != sender_comp_id_map_.end()) ? it->second : 0;
}

void RefDataService::add_session(const SessionConfig& config) {
    sessions_[config.session_id] = config;
}

std::optional<RefDataService::SessionConfig> RefDataService::get_session(engine::SessionId id) const {
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return std::nullopt;
    return it->second;
}

void RefDataService::assign_to_partition(engine::InstrumentId inst_id, engine::PartitionId part_id) {
    partition_map_[inst_id] = part_id;
}

engine::PartitionId RefDataService::get_partition(engine::InstrumentId inst_id) const {
    auto it = partition_map_.find(inst_id);
    return (it != partition_map_.end()) ? it->second : 0;
}

}  // namespace refdata
