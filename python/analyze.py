#!/usr/bin/env python3
"""
MiniMarket Quantitative Analysis Script
Analyzes simulation CSV output (trades.csv, snapshots.csv).
Calculates:
  - Net Mark-to-Market P&L
  - Trade count and strategy fill metrics
  - Strategy fill ratio (volume filled / volume quoted)
  - Maximum absolute inventory
  - Maximum drawdown (peak-to-trough on equity curve)
Generates:
  - equity_curve.png
  - inventory.png
  - market_microstructure.png
"""

import os
import argparse
import pandas as pd
import numpy as np
import matplotlib
matplotlib.use('Agg')  # Headless backend for automation and testing
import matplotlib.pyplot as plt

def analyze_simulation(data_dir: str):
    trades_path = os.path.join(data_dir, "trades.csv")
    snapshots_path = os.path.join(data_dir, "snapshots.csv")

    if not os.path.exists(trades_path) or not os.path.exists(snapshots_path):
        raise FileNotFoundError(f"Missing CSV files in {data_dir}. Expected trades.csv and snapshots.csv.")

    trades_df = pd.read_csv(trades_path)
    snaps_df = pd.read_csv(snapshots_path)

    print("=" * 65)
    print("      MiniMarket Quantitative Strategy Performance Report")
    print("=" * 65)

    if snaps_df.empty:
        print("[Warning] Snapshots dataset is empty. Cannot compute metrics.")
        return {
            "total_market_trades": 0,
            "strat_total_trades": 0,
            "final_pnl_dollars": 0.0,
            "max_drawdown_dollars": 0.0,
            "max_abs_inventory": 0,
            "fill_ratio": 0.0,
            "fees_dollars": 0.0
        }

    # 1. Trade Statistics
    total_market_trades = len(trades_df)
    if not trades_df.empty and 'buy_client_id' in trades_df.columns:
        strat_buy_trades = trades_df[trades_df['buy_client_id'] == 'STRATEGY']
        strat_sell_trades = trades_df[trades_df['sell_client_id'] == 'STRATEGY']
    else:
        strat_buy_trades = pd.DataFrame()
        strat_sell_trades = pd.DataFrame()

    strat_total_trades = len(strat_buy_trades) + len(strat_sell_trades)
    strat_bought_volume = int(strat_buy_trades['qty'].sum()) if not strat_buy_trades.empty else 0
    strat_sold_volume = int(strat_sell_trades['qty'].sum()) if not strat_sell_trades.empty else 0
    total_strat_volume = strat_bought_volume + strat_sold_volume

    # 2. Portfolio & P&L Analysis from Snapshots
    initial_cash = float(snaps_df['strategy_cash'].iloc[0])
    initial_equity = float(snaps_df['strategy_equity'].iloc[0])
    final_equity = float(snaps_df['strategy_equity'].iloc[-1])
    final_pnl_cents = float(snaps_df['strategy_pnl'].iloc[-1])
    final_pnl_dollars = final_pnl_cents / 100.0

    cumulative_fees_cents = float(snaps_df['cumulative_fees'].iloc[-1])
    cumulative_fees_dollars = cumulative_fees_cents / 100.0

    # Max Absolute Inventory
    max_abs_inventory = int(snaps_df['strategy_position'].abs().max()) if not snaps_df['strategy_position'].empty else 0
    final_inventory = int(snaps_df['strategy_position'].iloc[-1])

    # Drawdown Analysis (Peak-to-trough on Equity)
    equity_series = snaps_df['strategy_equity']
    running_max_equity = equity_series.cummax()
    drawdown_cents = running_max_equity - equity_series
    max_drawdown_cents = float(drawdown_cents.max()) if not drawdown_cents.empty else 0.0
    max_drawdown_dollars = max_drawdown_cents / 100.0
    max_drawdown_pct = (drawdown_cents / running_max_equity).max() * 100.0 if running_max_equity.max() > 0 else 0.0

    # Strategy Fill Ratio (from C++ simulator exported snapshot column, or volume fallback)
    if 'strategy_fill_ratio' in snaps_df.columns:
        fill_ratio = float(snaps_df['strategy_fill_ratio'].iloc[-1])
    else:
        fill_ratio = strat_total_trades / len(snaps_df) if len(snaps_df) > 0 else 0.0

    # Print summary report
    print(f"Total Market Events / Snapshots : {len(snaps_df)}")
    print(f"Total Market Trades Executed    : {total_market_trades}")
    print(f"Strategy Fills Executed         : {strat_total_trades}")
    print(f"  - Strategy Buys               : {len(strat_buy_trades)} ({strat_bought_volume} shares)")
    print(f"  - Strategy Sells              : {len(strat_sell_trades)} ({strat_sold_volume} shares)")
    print(f"Strategy Fill Ratio             : {fill_ratio:.2%}")
    print("-" * 65)
    print(f"Initial Portfolio Equity        : ${initial_equity / 100.0:,.2f}")
    print(f"Final Portfolio Equity          : ${final_equity / 100.0:,.2f}")
    print(f"Net Mark-to-Market P&L          : ${final_pnl_dollars:,.2f} ({final_pnl_cents:,.1f} cents)")
    print(f"Cumulative Transaction Fees     : ${cumulative_fees_dollars:,.2f} ({cumulative_fees_cents:,.1f} cents)")
    return_on_capital = (final_pnl_cents / initial_equity) * 100.0 if initial_equity > 0 else 0.0
    print(f"Return on Initial Capital       : {return_on_capital:.4f}%")
    print("-" * 65)
    print(f"Maximum Absolute Inventory      : {max_abs_inventory} shares")
    print(f"Final Inventory Position        : {final_inventory} shares")
    print(f"Maximum Peak-to-Trough Drawdown : ${max_drawdown_dollars:,.2f} ({max_drawdown_pct:.4f}%)")
    print("=" * 65)

    # 3. Plot 1: Equity Curve & Drawdown
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(11, 7), sharex=True, gridspec_kw={'height_ratios': [2, 1]})

    time_steps = snaps_df['timestamp']
    equity_dollars = snaps_df['strategy_equity'] / 100.0
    pnl_dollars = snaps_df['strategy_pnl'] / 100.0
    fees_dollars = snaps_df['cumulative_fees'] / 100.0

    # Subplot 1: Equity Curve & High Water Mark
    hwm_dollars = running_max_equity / 100.0
    ax1.plot(time_steps, equity_dollars, color='#1f77b4', linewidth=1.8, label='Portfolio Equity ($)')
    ax1.plot(time_steps, hwm_dollars, color='#2ca02c', linestyle='--', alpha=0.6, label='High Water Mark ($)')
    ax1.fill_between(time_steps, equity_dollars, hwm_dollars, color='#d62728', alpha=0.18, label='Drawdown Area')
    ax1.set_title('MiniMarket — Mark-to-Market Equity Curve & Peak-to-Trough Drawdown', fontsize=13, fontweight='bold')
    ax1.set_ylabel('Equity ($)', fontsize=11)
    ax1.grid(True, linestyle=':', alpha=0.6)
    ax1.legend(loc='upper left', framealpha=0.9)

    # Subplot 2: Net PnL and Cumulative Fees
    ax2.plot(time_steps, pnl_dollars, color='#009688', linewidth=1.5, label=f'Net P&L (${final_pnl_dollars:,.2f})')
    ax2.plot(time_steps, -fees_dollars, color='#e91e63', linewidth=1.2, linestyle=':', label=f'Transaction Fees (-${cumulative_fees_dollars:,.2f})')
    ax2.axhline(0, color='gray', linestyle='-', linewidth=0.8, alpha=0.7)
    ax2.set_xlabel('Simulation Step / Event Timestamp', fontsize=11)
    ax2.set_ylabel('P&L / Fees ($)', fontsize=11)
    ax2.grid(True, linestyle=':', alpha=0.6)
    ax2.legend(loc='upper left', framealpha=0.9)

    plt.tight_layout()
    equity_plot_path = os.path.join(data_dir, "equity_curve.png")
    fig.savefig(equity_plot_path, dpi=200)
    plt.close(fig)

    # 4. Plot 2: Inventory Tracking with Upper & Lower Limits
    fig, ax = plt.subplots(figsize=(11, 4.5))
    inventory_series = snaps_df['strategy_position']
    ax.plot(time_steps, inventory_series, color='#ff7f0e', linewidth=1.4, label='Strategy Inventory (shares)')
    ax.axhline(0, color='black', linestyle='-', linewidth=0.8, alpha=0.7)

    inv_limit = max(50, int(max_abs_inventory))
    ax.axhline(inv_limit, color='red', linestyle='--', alpha=0.8, label=f'+Limit (+{inv_limit})')
    ax.axhline(-inv_limit, color='red', linestyle='--', alpha=0.8, label=f'-Limit (-{inv_limit})')

    ax.fill_between(time_steps, 0, inventory_series, where=(inventory_series >= 0),
                    color='#2ca02c', alpha=0.2, label='Long Position')
    ax.fill_between(time_steps, 0, inventory_series, where=(inventory_series < 0),
                    color='#d62728', alpha=0.2, label='Short Position')

    ax.set_title('MiniMarket — Inventory Management & Mean-Reverting Position Skew', fontsize=13, fontweight='bold')
    ax.set_xlabel('Simulation Step / Event Timestamp', fontsize=11)
    ax.set_ylabel('Position (Shares)', fontsize=11)
    ax.grid(True, linestyle=':', alpha=0.6)
    ax.legend(loc='upper right', framealpha=0.9)

    plt.tight_layout()
    inv_plot_path = os.path.join(data_dir, "inventory.png")
    fig.savefig(inv_plot_path, dpi=200)
    plt.close(fig)

    # 5. Plot 3: Market Microstructure (Midpoint, Bids, Asks)
    fig, ax = plt.subplots(figsize=(11, 4.5))
    mid_dollars = snaps_df['mid_price'] / 100.0
    bid_dollars = snaps_df['best_bid'] / 100.0
    ask_dollars = snaps_df['best_ask'] / 100.0

    ax.plot(time_steps, mid_dollars, color='#333333', linewidth=1.2, label='Mid Price ($)')
    ax.fill_between(time_steps, bid_dollars, ask_dollars, color='#2196f3', alpha=0.25, label='Bid-Ask Spread')

    if not strat_buy_trades.empty:
        ax.scatter(strat_buy_trades['timestamp'], strat_buy_trades['price'] / 100.0,
                   marker='^', color='green', s=25, alpha=0.8, label='Strategy Buy Fill')
    if not strat_sell_trades.empty:
        ax.scatter(strat_sell_trades['timestamp'], strat_sell_trades['price'] / 100.0,
                   marker='v', color='red', s=25, alpha=0.8, label='Strategy Sell Fill')

    ax.set_title('MiniMarket — Order Book Microstructure & Strategy Trade Executions', fontsize=13, fontweight='bold')
    ax.set_xlabel('Simulation Step / Event Timestamp', fontsize=11)
    ax.set_ylabel('Price ($)', fontsize=11)
    ax.grid(True, linestyle=':', alpha=0.6)
    ax.legend(loc='upper left', framealpha=0.9)

    plt.tight_layout()
    micro_plot_path = os.path.join(data_dir, "market_microstructure.png")
    fig.savefig(micro_plot_path, dpi=200)
    plt.close(fig)

    return {
        "total_market_trades": total_market_trades,
        "strat_total_trades": strat_total_trades,
        "final_pnl_dollars": final_pnl_dollars,
        "max_drawdown_dollars": max_drawdown_dollars,
        "max_abs_inventory": max_abs_inventory,
        "fill_ratio": fill_ratio,
        "fees_dollars": cumulative_fees_dollars
    }

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Analyze MiniMarket simulation results.")
    parser.add_argument("--dir", "--out", dest="data_dir", default="results", help="Directory containing CSV outputs")
    args = parser.parse_args()

    analyze_simulation(args.data_dir)
