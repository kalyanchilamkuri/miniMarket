#pragma once

#include "minimarket/Types.hpp"
#include "minimarket/OrderBook.hpp"
#include "minimarket/Strategy.hpp"
#include "minimarket/CsvWriter.hpp"

#include <random>
#include <vector>
#include <string>

namespace minimarket {

struct SimulationConfig {
    uint64_t seed{42};
    size_t num_events{5000};
    size_t snapshot_interval{10};
    Price initial_reference_price{10000}; // 10000 cents ($100.00)
    std::string output_dir{"results"};
    StrategyConfig strategy_config;
    Portfolio::Config portfolio_config;
};

struct SimulationStats {
    size_t total_events{0};
    size_t total_trades{0};
    size_t strategy_trades{0};
    Quantity strategy_bought_volume{0};
    Quantity strategy_sold_volume{0};
    Quantity strategy_final_inventory{0};
    Quantity strategy_max_abs_inventory{0};
    double strategy_max_drawdown{0.0};
    double strategy_final_cash{0.0};
    double strategy_final_equity{0.0};
    double strategy_final_pnl{0.0};
    double strategy_cumulative_fees{0.0};
    double strategy_fill_ratio{0.0};
};

class Simulator {
public:
    explicit Simulator(SimulationConfig config);

    // Runs the simulation from start to finish
    SimulationStats run();

    // Export generated datasets to CSV
    bool exportCsv(const std::string& out_dir) const;

    const OrderBook& book() const { return book_; }
    const MarketMakingStrategy& strategy() const { return strategy_; }
    const std::vector<Trade>& allTrades() const { return all_trades_; }
    const std::vector<MarketSnapshotRecord>& snapshots() const { return snapshots_; }

private:
    void seedInitialBook();
    void processStrategyActions();
    void handleTrades(const std::vector<Trade>& trades);
    void takeSnapshot();

    SimulationConfig config_;
    OrderBook book_;
    MarketMakingStrategy strategy_;
    std::mt19937_64 rng_;

    Timestamp current_time_{0};
    OrderId next_order_id_{1};

    // Latent unobservable reference price used by external order generator
    Price latent_price_{10000};

    // Tracking external resting order IDs for simulated cancellations
    std::vector<OrderId> active_external_orders_;

    std::vector<Trade> all_trades_;
    std::vector<MarketSnapshotRecord> snapshots_;
};

} // namespace minimarket
