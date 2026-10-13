#pragma once

#include "minimarket/Types.hpp"
#include <string>
#include <fstream>
#include <vector>
#include <iomanip>

namespace minimarket {

struct MarketSnapshotRecord {
    Timestamp timestamp{0};
    double best_bid{0.0};
    double best_ask{0.0};
    double mid_price{0.0};
    double spread{0.0};
    Quantity strategy_position{0};
    double strategy_cash{0.0};
    double strategy_equity{0.0};
    double strategy_pnl{0.0};
    double cumulative_fees{0.0};
    double strategy_fill_ratio{0.0};
    Quantity strategy_inv_limit{50};
};

class CsvWriter {
public:
    static bool writeTrades(const std::string& filepath, const std::vector<Trade>& trades) {
        std::ofstream file(filepath);
        if (!file.is_open()) return false;

        file << "timestamp,trade_id,price,qty,buy_order_id,sell_order_id,buy_client_id,sell_client_id\n";
        for (const auto& t : trades) {
            file << t.timestamp << ","
                 << t.trade_id << ","
                 << t.price << ","
                 << t.qty << ","
                 << t.buy_order_id << ","
                 << t.sell_order_id << ","
                 << t.buy_client_id << ","
                 << t.sell_client_id << "\n";
        }
        return true;
    }

    static bool writeSnapshots(const std::string& filepath, const std::vector<MarketSnapshotRecord>& snapshots) {
        std::ofstream file(filepath);
        if (!file.is_open()) return false;

        file << std::fixed << std::setprecision(4);
        file << "timestamp,best_bid,best_ask,mid_price,spread,strategy_position,strategy_cash,strategy_equity,strategy_pnl,cumulative_fees,strategy_fill_ratio,strategy_inv_limit\n";
        for (const auto& s : snapshots) {
            file << s.timestamp << ","
                 << s.best_bid << ","
                 << s.best_ask << ","
                 << s.mid_price << ","
                 << s.spread << ","
                 << s.strategy_position << ","
                 << s.strategy_cash << ","
                 << s.strategy_equity << ","
                 << s.strategy_pnl << ","
                 << s.cumulative_fees << ","
                 << s.strategy_fill_ratio << ","
                 << s.strategy_inv_limit << "\n";
        }
        return true;
    }
};

} // namespace minimarket
