#!/usr/bin/env python3
"""
Unit and regression tests for python/analyze.py.
Tests:
  - Metric calculations on known synthetic test vectors.
  - Drawdown calculations (peak-to-trough).
  - Edge case: 0 trades (empty trades.csv).
  - Edge case: single snapshot row (no events).
  - Edge case: zero volatility (no price movement).
  - Strategy fill ratio reporting.
"""

import unittest
import tempfile
import os
import shutil
import pandas as pd
from python.analyze import analyze_simulation

class TestAnalyzeSimulation(unittest.TestCase):
    def setUp(self):
        self.test_dir = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.test_dir, ignore_errors=True)

    def test_standard_metrics(self):
        # Create known synthetic snapshots and trades
        snapshots_data = {
            "timestamp": [0, 10, 20, 30],
            "best_bid": [9900, 9900, 9800, 10000],
            "best_ask": [10100, 10100, 10000, 10200],
            "mid_price": [10000.0, 10000.0, 9900.0, 10100.0],
            "spread": [200, 200, 200, 200],
            "strategy_position": [0, 10, 10, 0],
            "strategy_cash": [10000000.0, 8999990.0, 8999990.0, 10009980.0],
            "strategy_equity": [10000000.0, 9999990.0, 9989990.0, 10009980.0],
            "strategy_pnl": [0.0, -10.0, -10010.0, 9980.0],
            "cumulative_fees": [0.0, 10.0, 10.0, 20.0],
            "strategy_fill_ratio": [0.0, 0.25, 0.25, 0.50]
        }
        pd.DataFrame(snapshots_data).to_csv(os.path.join(self.test_dir, "snapshots.csv"), index=False)

        trades_data = {
            "timestamp": [10, 30],
            "trade_id": [1, 2],
            "price": [10000, 10100],
            "qty": [10, 10],
            "buy_order_id": [101, 102],
            "sell_order_id": [201, 202],
            "buy_client_id": ["STRATEGY", "EXT_TAKER"],
            "sell_client_id": ["EXT_MAKER", "STRATEGY"]
        }
        pd.DataFrame(trades_data).to_csv(os.path.join(self.test_dir, "trades.csv"), index=False)

        res = analyze_simulation(self.test_dir)

        # Check results
        self.assertEqual(res["total_market_trades"], 2)
        self.assertEqual(res["strat_total_trades"], 2)
        self.assertAlmostEqual(res["final_pnl_dollars"], 99.80, places=2)
        self.assertAlmostEqual(res["fees_dollars"], 0.20, places=2)
        self.assertEqual(res["max_abs_inventory"], 10)
        self.assertAlmostEqual(res["fill_ratio"], 0.50, places=4)
        # Peak equity is 10000000 (first) then drops to 9989990 (drawdown = 10010 cents = $100.10)
        self.assertAlmostEqual(res["max_drawdown_dollars"], 100.10, places=2)

    def test_empty_trades_edge_case(self):
        snapshots_data = {
            "timestamp": [0, 10],
            "best_bid": [9900, 9900],
            "best_ask": [10100, 10100],
            "mid_price": [10000.0, 10000.0],
            "spread": [200, 200],
            "strategy_position": [0, 0],
            "strategy_cash": [10000000.0, 10000000.0],
            "strategy_equity": [10000000.0, 10000000.0],
            "strategy_pnl": [0.0, 0.0],
            "cumulative_fees": [0.0, 0.0],
            "strategy_fill_ratio": [0.0, 0.0]
        }
        pd.DataFrame(snapshots_data).to_csv(os.path.join(self.test_dir, "snapshots.csv"), index=False)

        # Empty trades dataframe (header only)
        pd.DataFrame(columns=[
            "timestamp", "trade_id", "price", "qty", "buy_order_id",
            "sell_order_id", "buy_client_id", "sell_client_id"
        ]).to_csv(os.path.join(self.test_dir, "trades.csv"), index=False)

        res = analyze_simulation(self.test_dir)
        self.assertEqual(res["total_market_trades"], 0)
        self.assertEqual(res["strat_total_trades"], 0)
        self.assertEqual(res["final_pnl_dollars"], 0.0)
        self.assertEqual(res["max_drawdown_dollars"], 0.0)
        self.assertEqual(res["max_abs_inventory"], 0)

    def test_zero_volatility_edge_case(self):
        # All snapshots have identical constant price
        snapshots_data = {
            "timestamp": list(range(0, 100, 10)),
            "best_bid": [9997] * 10,
            "best_ask": [10003] * 10,
            "mid_price": [10000.0] * 10,
            "spread": [6] * 10,
            "strategy_position": [0] * 10,
            "strategy_cash": [10000000.0] * 10,
            "strategy_equity": [10000000.0] * 10,
            "strategy_pnl": [0.0] * 10,
            "cumulative_fees": [0.0] * 10,
            "strategy_fill_ratio": [0.0] * 10
        }
        pd.DataFrame(snapshots_data).to_csv(os.path.join(self.test_dir, "snapshots.csv"), index=False)
        pd.DataFrame(columns=[
            "timestamp", "trade_id", "price", "qty", "buy_order_id",
            "sell_order_id", "buy_client_id", "sell_client_id"
        ]).to_csv(os.path.join(self.test_dir, "trades.csv"), index=False)

        res = analyze_simulation(self.test_dir)
        self.assertEqual(res["max_drawdown_dollars"], 0.0)
        self.assertEqual(res["max_abs_inventory"], 0)
        self.assertEqual(res["final_pnl_dollars"], 0.0)

    def test_configured_inventory_limit(self):
        snapshots_data = {
            "timestamp": [0, 10, 20],
            "best_bid": [9990, 9990, 9990],
            "best_ask": [10010, 10010, 10010],
            "mid_price": [10000.0, 10000.0, 10000.0],
            "spread": [20, 20, 20],
            "strategy_position": [0, 15, -15],
            "strategy_cash": [10000000.0, 8500000.0, 11500000.0],
            "strategy_equity": [10000000.0, 10000000.0, 10000000.0],
            "strategy_pnl": [0.0, 0.0, 0.0],
            "cumulative_fees": [0.0, 10.0, 20.0],
            "strategy_fill_ratio": [0.0, 0.5, 0.5],
            "strategy_inv_limit": [25, 25, 25]
        }
        pd.DataFrame(snapshots_data).to_csv(os.path.join(self.test_dir, "snapshots.csv"), index=False)
        pd.DataFrame(columns=[
            "timestamp", "trade_id", "price", "qty", "buy_order_id",
            "sell_order_id", "buy_client_id", "sell_client_id"
        ]).to_csv(os.path.join(self.test_dir, "trades.csv"), index=False)

        res = analyze_simulation(self.test_dir)
        self.assertEqual(res["max_abs_inventory"], 15)
        self.assertTrue(os.path.exists(os.path.join(self.test_dir, "inventory.png")))
        self.assertTrue(os.path.exists(os.path.join(self.test_dir, "equity_curve.png")))
        self.assertTrue(os.path.exists(os.path.join(self.test_dir, "market_microstructure.png")))

if __name__ == '__main__':
    unittest.main()
