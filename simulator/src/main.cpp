/// FIX Participant Simulator — standalone entry point.
///
/// Simulates a market-making participant connecting to the venue over FIX 4.4.
///
/// Usage:
///   fix-simulator [host] [port] [sender_comp_id] [symbol] [duration_sec] [tls]
///
/// Example:
///   fix-simulator 127.0.0.1 9876 SIM_MM BTC-USD 30
///   fix-simulator 127.0.0.1 9877 SIM_MM BTC-USD 60 tls

#include "simulator/participant_simulator.h"

#include <iostream>
#include <iomanip>
#include <string>

int main(int argc, char* argv[]) {
    simulator::SimulatorConfig cfg;
    cfg.host              = argc > 1 ? argv[1] : "127.0.0.1";
    cfg.port              = argc > 2 ? static_cast<uint16_t>(std::stoi(argv[2])) : 9876;
    cfg.sender_comp_id    = argc > 3 ? argv[3] : "SIM_MM";
    cfg.symbol            = argc > 4 ? argv[4] : "BTC-USD";
    cfg.duration_seconds  = argc > 5 ? std::stoi(argv[5]) : 30;
    cfg.use_tls           = argc > 6 && std::string(argv[6]) == "tls";

    std::cout << "=== FIX Participant Simulator ===" << std::endl;
    std::cout << "  Host:       " << cfg.host << std::endl;
    std::cout << "  Port:       " << cfg.port << std::endl;
    std::cout << "  CompID:     " << cfg.sender_comp_id << std::endl;
    std::cout << "  Symbol:     " << cfg.symbol << std::endl;
    std::cout << "  Duration:   " << cfg.duration_seconds << "s" << std::endl;
    std::cout << "  TLS:        " << (cfg.use_tls ? "yes" : "no") << std::endl;
    std::cout << "  Base Price: " << cfg.base_price << std::endl;
    std::cout << "  Spread:     " << cfg.spread << std::endl;
    std::cout << "  Order Qty:  " << cfg.order_qty << std::endl;
    std::cout << "  Rate:       " << cfg.orders_per_second << " orders/sec" << std::endl;
    std::cout << std::endl;

    simulator::ParticipantSimulator sim(cfg);

    // Log execution reports
    sim.set_exec_callback([](const std::string& exec_type, const std::string& /*details*/) {
        if (exec_type == "0") std::cout << "  [ACK] Order accepted" << std::endl;
        else if (exec_type == "F") std::cout << "  [FILL] Trade executed" << std::endl;
        else if (exec_type == "4") std::cout << "  [CXL] Order cancelled" << std::endl;
        else if (exec_type == "8") std::cout << "  [REJ] Order rejected" << std::endl;
    });

    std::cout << "Connecting to " << cfg.host << ":" << cfg.port << "..." << std::endl;
    auto stats = sim.run();

    std::cout << std::endl;
    std::cout << "=== Simulation Results ===" << std::endl;
    std::cout << "  Orders Sent:     " << stats.orders_sent << std::endl;
    std::cout << "  Acks Received:   " << stats.acks_received << std::endl;
    std::cout << "  Fills Received:  " << stats.fills_received << std::endl;
    std::cout << "  Rejects:         " << stats.rejects_received << std::endl;
    std::cout << "  Cancels Sent:    " << stats.cancels_sent << std::endl;
    std::cout << "  Cancel Acks:     " << stats.cancel_acks << std::endl;
    std::cout << "  Errors:          " << stats.errors << std::endl;
    std::cout << "  Total Volume:    " << stats.total_volume << std::endl;

    return (stats.errors > 0) ? 1 : 0;
}
