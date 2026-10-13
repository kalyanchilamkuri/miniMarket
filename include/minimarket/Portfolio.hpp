#pragma once

#include "minimarket/Types.hpp"
#include <optional>
#include <string>

namespace minimarket {

struct PortfolioConfig {
    double initial_cash{10000000.0};  // in cents ($100,000.00)
    double fee_per_share{0.1};         // in cents per share ($0.001 / share)
};

class Portfolio {
public:
    using Config = PortfolioConfig;

    explicit Portfolio(PortfolioConfig config = PortfolioConfig());

    void reset(double initial_mark_price = 10000.0);

    // Process a trade fill where this portfolio is either the buyer or seller
    void onTrade(Side side, Price price, Quantity qty);

    // Queries
    double cash() const { return cash_; }
    Quantity position() const { return position_; }
    double cumulativeFees() const { return cumulative_fees_; }
    uint64_t tradeCount() const { return trade_count_; }
    Quantity totalBoughtVolume() const { return total_bought_volume_; }
    Quantity totalSoldVolume() const { return total_sold_volume_; }

    // Mark-to-market valuation
    // Uses provided mark_price (e.g. midPrice or last trade price)
    double equity(double mark_price) const;
    double netPnL(double mark_price) const;

    // Track last known mark price for safe valuation when book is empty
    void updateLastMarkPrice(double mark_price) { last_mark_price_ = mark_price; }
    double lastMarkPrice() const { return last_mark_price_; }

    double equity() const { return equity(last_mark_price_); }
    double netPnL() const { return netPnL(last_mark_price_); }

private:
    PortfolioConfig config_;
    double cash_{10000000.0};
    Quantity position_{0};
    double cumulative_fees_{0.0};
    uint64_t trade_count_{0};
    Quantity total_bought_volume_{0};
    Quantity total_sold_volume_{0};
    double last_mark_price_{10000.0}; // default reference
};

} // namespace minimarket
