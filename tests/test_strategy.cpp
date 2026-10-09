#include <gtest/gtest.h>
#include "minimarket/Strategy.hpp"
#include "minimarket/OrderBook.hpp"
#include "minimarket/Simulator.hpp"

using namespace minimarket;

class StrategyTest : public ::testing::Test {
protected:
    StrategyConfig strat_config{
        "STRATEGY",
        20,   // inventory limit = 20
        5,    // quote size = 5
        0.5,  // gamma = 0.5 ticks per share
        2     // half spread = 2 ticks
    };
    MarketMakingStrategy strategy{strat_config};
    OrderBook book;
};

// Test: Midpoint quoting when flat (zero inventory)
TEST_F(StrategyTest, QuotingWhenFlat) {
    // Book has bids at 98, asks at 102. Midpoint = 100.
    book.addLimitOrder(1, "EXT", Side::Buy, 98, 10, 1);
    book.addLimitOrder(2, "EXT", Side::Sell, 102, 10, 1);

    OrderId next_id = 100;
    auto actions = strategy.onMarketUpdate(book, 1, next_id);

    // Should place a Buy and a Sell
    ASSERT_EQ(actions.size(), 2);

    // Reservation price = 100 - (0.5 * 0) = 100.
    // Desired bid = 100 - 2 = 98. Desired ask = 100 + 2 = 102.
    // Passive rule clips to best bid/ask bounds: bid <= best_ask - 1 (101), ask >= best_bid + 1 (99).
    EXPECT_EQ(strategy.activeBidPrice(), 98);
    EXPECT_EQ(strategy.activeAskPrice(), 102);
}

// Test: Inventory skew lowers reservation price when long
TEST_F(StrategyTest, InventorySkewWhenLong) {
    book.addLimitOrder(1, "EXT", Side::Buy, 90, 50, 1);
    book.addLimitOrder(2, "EXT", Side::Sell, 110, 50, 1);
    // Midpoint = 100

    // Simulate receiving fills to make inventory +10 (long)
    Trade fill{1, 100, 1, "STRATEGY", "EXT", 100, 10, 1};
    strategy.onTradeFill(fill, Side::Buy);
    EXPECT_EQ(strategy.portfolio().position(), 10);

    OrderId next_id = 200;
    auto actions = strategy.onMarketUpdate(book, 2, next_id);

    // R(q) = 100 - (0.5 * 10) = 95.
    // Desired bid = 95 - 2 = 93.
    // Desired ask = 95 + 2 = 97.
    // Notice quotes shifted down by 5 ticks to encourage offloading inventory!
    EXPECT_EQ(strategy.activeBidPrice(), 93);
    EXPECT_EQ(strategy.activeAskPrice(), 97);
}

// Test: Inventory skew raises reservation price when short
TEST_F(StrategyTest, InventorySkewWhenShort) {
    book.addLimitOrder(1, "EXT", Side::Buy, 90, 50, 1);
    book.addLimitOrder(2, "EXT", Side::Sell, 110, 50, 1);
    // Midpoint = 100

    // Simulate receiving fills to make inventory -10 (short)
    Trade fill{1, 1, 200, "EXT", "STRATEGY", 100, 10, 1};
    strategy.onTradeFill(fill, Side::Sell);
    EXPECT_EQ(strategy.portfolio().position(), -10);

    OrderId next_id = 300;
    auto actions = strategy.onMarketUpdate(book, 2, next_id);

    // R(q) = 100 - (0.5 * -10) = 105.
    // Desired bid = 105 - 2 = 103.
    // Desired ask = 105 + 2 = 107.
    // Notice quotes shifted up by 5 ticks to encourage buying back to cover!
    EXPECT_EQ(strategy.activeBidPrice(), 103);
    EXPECT_EQ(strategy.activeAskPrice(), 107);
}

// Test: Hard inventory limits prevent quotes beyond limit
TEST_F(StrategyTest, EnforcesInventoryLimitHardCap) {
    book.addLimitOrder(1, "EXT", Side::Buy, 90, 50, 1);
    book.addLimitOrder(2, "EXT", Side::Sell, 110, 50, 1);

    // Fill strategy up to exactly the inventory limit (20 shares)
    Trade fill{1, 100, 1, "STRATEGY", "EXT", 100, 20, 1};
    strategy.onTradeFill(fill, Side::Buy);
    EXPECT_EQ(strategy.portfolio().position(), 20);

    OrderId next_id = 400;
    auto actions = strategy.onMarketUpdate(book, 3, next_id);

    // Must NOT quote a Buy order since position is already at limit!
    EXPECT_FALSE(strategy.activeBidId().has_value());
    // Must quote an Ask order to liquidate
    EXPECT_TRUE(strategy.activeAskId().has_value());
}

// Test: Simulator reproducibility with identical seed
TEST(SimulationReproducibilityTest, SameSeedProducesIdenticalResults) {
    SimulationConfig config1;
    config1.seed = 12345;
    config1.num_events = 500;

    SimulationConfig config2 = config1;

    Simulator sim1(config1);
    auto stats1 = sim1.run();

    Simulator sim2(config2);
    auto stats2 = sim2.run();

    EXPECT_EQ(stats1.total_trades, stats2.total_trades);
    EXPECT_EQ(stats1.strategy_trades, stats2.strategy_trades);
    EXPECT_EQ(stats1.strategy_final_inventory, stats2.strategy_final_inventory);
    EXPECT_DOUBLE_EQ(stats1.strategy_final_pnl, stats2.strategy_final_pnl);
    EXPECT_DOUBLE_EQ(stats1.strategy_cumulative_fees, stats2.strategy_cumulative_fees);

    // Bitwise check on all trades
    ASSERT_EQ(sim1.allTrades().size(), sim2.allTrades().size());
    for (size_t i = 0; i < sim1.allTrades().size(); ++i) {
        const auto& t1 = sim1.allTrades()[i];
        const auto& t2 = sim2.allTrades()[i];
        EXPECT_EQ(t1.price, t2.price);
        EXPECT_EQ(t1.qty, t2.qty);
        EXPECT_EQ(t1.buy_order_id, t2.buy_order_id);
        EXPECT_EQ(t1.sell_order_id, t2.sell_order_id);
    }
}
