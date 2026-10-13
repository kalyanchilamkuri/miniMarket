#include "minimarket/Simulator.hpp"
#include <iostream>
#include <string>
#include <iomanip>

void printHelp(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n\n"
              << "Options:\n"
              << "  --seed <int>       Random seed for simulation (default: 42)\n"
              << "  --events <int>     Number of events to simulate (default: 5000)\n"
              << "  --out <path>       Directory to save CSV outputs (default: results)\n"
              << "  --skew <double>    Inventory skew sensitivity gamma (default: 0.2)\n"
              << "  --max-inv <int>    Max inventory limit (default: 50)\n"
              << "  --quote-size <int> Quote size per order (default: 10)\n"
              << "  --half-spread <int>Quote half spread in ticks (default: 3)\n"
              << "  --help             Show this help message\n";
}

int main(int argc, char* argv[]) {
    minimarket::SimulationConfig config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--seed" && i + 1 < argc) {
            config.seed = std::stoull(argv[++i]);
        } else if (arg == "--events" && i + 1 < argc) {
            config.num_events = std::stoull(argv[++i]);
        } else if (arg == "--out" && i + 1 < argc) {
            config.output_dir = argv[++i];
        } else if (arg == "--skew" && i + 1 < argc) {
            config.strategy_config.inventory_skew_gamma = std::stod(argv[++i]);
        } else if (arg == "--max-inv" && i + 1 < argc) {
            config.strategy_config.inventory_limit = std::stoll(argv[++i]);
        } else if (arg == "--quote-size" && i + 1 < argc) {
            config.strategy_config.quote_size = std::stoll(argv[++i]);
        } else if (arg == "--half-spread" && i + 1 < argc) {
            config.strategy_config.half_spread = std::stoll(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            printHelp(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown or incomplete argument: " << arg << "\n";
            printHelp(argv[0]);
            return 1;
        }
    }

    std::cout << "========================================================\n"
              << "  MiniMarket — Order Book & Strategy Simulator\n"
              << "========================================================\n"
              << "Configuration:\n"
              << "  Random Seed        : " << config.seed << "\n"
              << "  Simulation Events  : " << config.num_events << "\n"
              << "  Output Directory   : " << config.output_dir << "\n"
              << "  Inventory Limit    : " << config.strategy_config.inventory_limit << "\n"
              << "  Inventory Skew γ   : " << config.strategy_config.inventory_skew_gamma << "\n"
              << "  Quote Size         : " << config.strategy_config.quote_size << "\n"
              << "  Half Spread (ticks): " << config.strategy_config.half_spread << "\n"
              << "--------------------------------------------------------\n"
              << "Running simulation...\n";

    minimarket::Simulator sim(config);
    auto stats = sim.run();

    std::cout << "Simulation completed successfully!\n"
              << "--------------------------------------------------------\n"
              << "Summary Performance Metrics:\n"
              << "  Total Market Trades: " << stats.total_trades << "\n"
              << "  Strategy Fills     : " << stats.strategy_trades << "\n"
              << "  Strategy Fill Ratio: " << std::fixed << std::setprecision(2)
              << (stats.strategy_fill_ratio * 100.0) << "%\n"
              << "  Bought Volume      : " << stats.strategy_bought_volume << " shares\n"
              << "  Sold Volume        : " << stats.strategy_sold_volume << " shares\n"
              << "  Final Inventory    : " << stats.strategy_final_inventory << " shares (limit: "
              << config.strategy_config.inventory_limit << ")\n"
              << "  Max Abs Inventory  : " << stats.strategy_max_abs_inventory << " shares (limit: "
              << config.strategy_config.inventory_limit << ")\n"
              << "  Max Drawdown       : $" << std::fixed << std::setprecision(2)
              << (stats.strategy_max_drawdown / 100.0) << " (" << stats.strategy_max_drawdown << " cents)\n"
              << "  Cumulative Fees    : $" << std::fixed << std::setprecision(2)
              << (stats.strategy_cumulative_fees / 100.0) << " (" << stats.strategy_cumulative_fees << " cents)\n"
              << "  Final Net P&L      : $" << std::fixed << std::setprecision(2)
              << (stats.strategy_final_pnl / 100.0) << " (" << stats.strategy_final_pnl << " cents)\n"
              << "  Final Total Equity : $" << std::fixed << std::setprecision(2)
              << (stats.strategy_final_equity / 100.0) << "\n"
              << "--------------------------------------------------------\n";

    if (sim.exportCsv(config.output_dir)) {
        std::cout << "CSV datasets exported to: " << config.output_dir << "/\n"
                  << "  -> " << config.output_dir << "/trades.csv\n"
                  << "  -> " << config.output_dir << "/snapshots.csv\n";
    } else {
        std::cerr << "Failed to export CSV results to " << config.output_dir << "\n";
        return 1;
    }

    std::cout << "========================================================\n";
    return 0;
}
