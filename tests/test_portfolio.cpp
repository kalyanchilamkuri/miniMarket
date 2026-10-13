#include <gtest/gtest.h>
#include "minimarket/Portfolio.hpp"

using namespace minimarket;

class PortfolioTest : public ::testing::Test {
protected:
    Portfolio::Config config{100000.0, 0.5}; // Initial cash = 100,000 cents, fee = 0.5 cents / share
    Portfolio portfolio{config};
};

// Test: Initial portfolio state
TEST_F(PortfolioTest, InitialState) {
    EXPECT_DOUBLE_EQ(portfolio.cash(), 100000.0);
    EXPECT_EQ(portfolio.position(), 0);
    EXPECT_DOUBLE_EQ(portfolio.cumulativeFees(), 0.0);
    EXPECT_DOUBLE_EQ(portfolio.equity(100.0), 100000.0);
    EXPECT_DOUBLE_EQ(portfolio.netPnL(100.0), 0.0);
    EXPECT_EQ(portfolio.tradeCount(), 0);
}

// Test: Buy trade accounting with transaction fees
TEST_F(PortfolioTest, BuyAccounting) {
    // Buy 10 shares at 100 cents.
    // Gross cost = 10 * 100 = 1000 cents.
    // Fee = 10 * 0.5 = 5 cents.
    // Total cash outlay = 1005 cents.
    portfolio.onTrade(Side::Buy, 100, 10);

    EXPECT_EQ(portfolio.position(), 10);
    EXPECT_DOUBLE_EQ(portfolio.cumulativeFees(), 5.0);
    EXPECT_DOUBLE_EQ(portfolio.cash(), 100000.0 - 1005.0);
    EXPECT_EQ(portfolio.totalBoughtVolume(), 10);
    EXPECT_EQ(portfolio.totalSoldVolume(), 0);

    // If marked at 100 (same price):
    // Equity = cash (98995) + position (10 * 100) = 99995.
    // Net PnL = Equity - 100000 = -5.0 (exactly the transaction fee).
    EXPECT_DOUBLE_EQ(portfolio.equity(100.0), 99995.0);
    EXPECT_DOUBLE_EQ(portfolio.netPnL(100.0), -5.0);

    // If price appreciates to 110:
    // Equity = 98995 + 10 * 110 = 100095.
    // Net PnL = +95 cents (100 gross gain - 5 fee).
    EXPECT_DOUBLE_EQ(portfolio.equity(110.0), 100095.0);
    EXPECT_DOUBLE_EQ(portfolio.netPnL(110.0), 95.0);
}

// Test: Round-trip trade (buy then sell) realized P&L
TEST_F(PortfolioTest, RoundTripRealizedPnL) {
    // 1. Buy 20 shares at 50 cents. Fee = 20 * 0.5 = 10. Cash = 100000 - 1010 = 98990.
    portfolio.onTrade(Side::Buy, 50, 20);
    EXPECT_EQ(portfolio.position(), 20);

    // 2. Sell 20 shares at 60 cents. Gross = 1200. Fee = 10. Net cash received = 1190.
    // Cash = 98990 + 1190 = 100180.
    portfolio.onTrade(Side::Sell, 60, 20);
    EXPECT_EQ(portfolio.position(), 0);
    EXPECT_DOUBLE_EQ(portfolio.cumulativeFees(), 20.0);
    EXPECT_DOUBLE_EQ(portfolio.cash(), 100180.0);

    // With 0 position, equity is equal to cash regardless of mark price
    EXPECT_DOUBLE_EQ(portfolio.equity(60.0), 100180.0);
    EXPECT_DOUBLE_EQ(portfolio.equity(1000.0), 100180.0);
    // Net PnL = +180 cents (gross gain 200 - fees 20)
    EXPECT_DOUBLE_EQ(portfolio.netPnL(60.0), 180.0);
}

// Test: Short position mark-to-market
TEST_F(PortfolioTest, ShortPositionAccounting) {
    // Sell 10 shares short at 100 cents.
    // Gross cash = +1000, fee = 5. Net cash = +995.
    portfolio.onTrade(Side::Sell, 100, 10);
    EXPECT_EQ(portfolio.position(), -10);
    EXPECT_DOUBLE_EQ(portfolio.cash(), 100000.0 + 995.0);

    // If market falls to 90 cents (favorable for short):
    // Equity = 100995 + (-10 * 90) = 100995 - 900 = 100095.
    // Net PnL = +95 cents.
    EXPECT_DOUBLE_EQ(portfolio.equity(90.0), 100095.0);
    EXPECT_DOUBLE_EQ(portfolio.netPnL(90.0), 95.0);

    // If market rises to 110 cents (unfavorable for short):
    // Equity = 100995 + (-10 * 110) = 100995 - 1100 = 99895.
    // Net PnL = -105 cents.
    EXPECT_DOUBLE_EQ(portfolio.equity(110.0), 99895.0);
    EXPECT_DOUBLE_EQ(portfolio.netPnL(110.0), -105.0);
}

// Regression Test: Reset resets position, cash, fees, and mark price
TEST_F(PortfolioTest, ResetResetsMarkPrice) {
    portfolio.onTrade(Side::Buy, 150, 10);
    EXPECT_DOUBLE_EQ(portfolio.lastMarkPrice(), 150.0);

    portfolio.reset(100.0);
    EXPECT_EQ(portfolio.position(), 0);
    EXPECT_DOUBLE_EQ(portfolio.cash(), 100000.0);
    EXPECT_DOUBLE_EQ(portfolio.cumulativeFees(), 0.0);
    EXPECT_DOUBLE_EQ(portfolio.lastMarkPrice(), 100.0);
}
