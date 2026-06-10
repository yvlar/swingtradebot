"""
swingbench — banc de prototypage de stratégies (Sprint 8) en Python.

POURQUOI CE BANC. Le moteur de production est en C++ (header-only, testé, CI). Itérer
une idée de stratégie en C++ coûte un cycle compile/test ; en Python c'est instantané.
Ce banc rejoue la MÊME donnée (../QQQ.csv) avec les MÊMES métriques (retour, alpha vs
Buy & Hold, % de temps investi, walk-forward IS/OOS) pour prototyper RAPIDEMENT les
idées du Sprint 8 — puis on ne porte dans le C++ que la stratégie qui bat le B&H EN
OUT-OF-SAMPLE.

⚠️ Ce banc est une APPROXIMATION fidèle du moteur C++ (modèle d'équité « tout investi
quand en position »), pas son clone au centime. Le golden C++ reste la source de vérité.
Rappel D28 : QQQ.csv n'est pas total-return (Close == Adj Close) → l'écart au B&H est
sous-estimé, donc tout verdict « la stratégie perd » est conservateur.
"""
from __future__ import annotations
from dataclasses import dataclass, field
import numpy as np
import pandas as pd


# ─── Configuration de la stratégie (miroir de SwingConfig C++) ───────────────
@dataclass
class Params:
    ema_fast: int = 13
    ema_slow: int = 21
    rsi_period: int = 14
    rsi_buy_max: float = 65.0
    rsi_sell_min: float = 80.0
    stop_loss: float = 0.07
    take_profit: float = 0.15
    trailing: float = 0.03
    min_hold: int = 2
    commission: float = 0.001
    slippage: float = 0.0005
    # ── Leviers du Sprint 8 (désactivés = comportement actuel) ──────────────
    regime_sma: int = 0          # >0 : n'acheter QUE si close > SMA(regime_sma)
    let_winners_run: bool = False  # True : désactive le take-profit fixe
    rsi_exit: bool = True          # False : ne PAS vendre sur RSI suracheté seul


def default_config() -> Params:
    """Config par défaut de SwingStrategy (golden C++ +9,67 %)."""
    return Params(ema_fast=9, ema_slow=21, rsi_buy_max=55.0, rsi_sell_min=70.0,
                  stop_loss=0.05, take_profit=0.10, trailing=0.03, min_hold=3)


def prod_config() -> Params:
    """Config de production (main_ibkr.cpp / ProdConfig.hpp, golden +36,5 %)."""
    return Params()  # les valeurs par défaut ci-dessus = la config prod


# ─── Indicateurs (Wilder, seedés en SMA — comme le C++) ──────────────────────
def ema(prices: np.ndarray, period: int) -> np.ndarray:
    out = np.full(len(prices), np.nan)
    if len(prices) < period:
        return out
    k = 2.0 / (period + 1.0)
    seed = prices[:period].mean()
    out[period - 1] = seed
    for i in range(period, len(prices)):
        out[i] = prices[i] * k + out[i - 1] * (1.0 - k)
    return out


def rsi(prices: np.ndarray, period: int = 14) -> np.ndarray:
    out = np.full(len(prices), np.nan)
    if len(prices) < period + 1:
        return out
    delta = np.diff(prices)
    gains = np.where(delta > 0, delta, 0.0)
    losses = np.where(delta < 0, -delta, 0.0)
    avg_gain = gains[:period].mean()
    avg_loss = losses[:period].mean()

    def to_rsi(ag, al):
        if ag < 1e-10 and al < 1e-10:
            return 50.0
        if al < 1e-10:
            return 100.0
        return 100.0 - 100.0 / (1.0 + ag / al)

    out[period] = to_rsi(avg_gain, avg_loss)
    for i in range(period + 1, len(prices)):
        avg_gain = (avg_gain * (period - 1) + gains[i - 1]) / period
        avg_loss = (avg_loss * (period - 1) + losses[i - 1]) / period
        out[i] = to_rsi(avg_gain, avg_loss)
    return out


def sma(prices: np.ndarray, period: int) -> np.ndarray:
    s = pd.Series(prices)
    return s.rolling(period).mean().to_numpy()


# ─── Chargement des données (partagé avec le C++) ────────────────────────────
def load_bars(csv_path: str = "../QQQ.csv", adjusted: bool = True) -> pd.DataFrame:
    df = pd.read_csv(csv_path)
    df = df.dropna(subset=["Close"]).reset_index(drop=True)
    df["date"] = df["Date"]
    # Adj Close par défaut (item 6.3) ; identique à Close dans le CSV actuel (D28)
    col = "Adj Close" if (adjusted and "Adj Close" in df.columns) else "Close"
    df["close"] = pd.to_numeric(df[col], errors="coerce")
    return df.dropna(subset=["close"]).reset_index(drop=True)


# ─── Backtest (modèle « tout investi quand en position ») ─────────────────────
@dataclass
class Result:
    total_return_pct: float = 0.0
    buy_hold_pct: float = 0.0
    alpha_pct: float = 0.0
    max_drawdown_pct: float = 0.0
    pct_time_invested: float = 0.0
    n_trades: int = 0
    win_rate_pct: float = 0.0
    beats_bh: bool = False
    start_date: str = ""
    end_date: str = ""
    equity: np.ndarray = field(default_factory=lambda: np.array([]))


def run_backtest(df: pd.DataFrame, p: Params) -> Result:
    """Rejoue la stratégie sur les barres fournies (indicateurs recalculés sur CETTE
    fenêtre — comme Backtester::runOn en C++)."""
    closes = df["close"].to_numpy(dtype=float)
    n = len(closes)
    warmup = p.ema_slow + p.rsi_period + 2
    if n <= warmup + 2:
        return Result()

    ef = ema(closes, p.ema_fast)
    es = ema(closes, p.ema_slow)
    rs = rsi(closes, p.rsi_period)
    regime = sma(closes, p.regime_sma) if p.regime_sma > 0 else None

    cost = p.commission + p.slippage
    equity = 1.0
    peak_eq = 1.0
    max_dd = 0.0
    eq_curve = np.empty(n)

    in_pos = False
    entry = peak = 0.0
    hold = 0
    days_invested = 0
    trades = 0
    wins = 0
    entry_eq = 0.0

    for i in range(n):
        # Mark-to-market quotidien tant qu'on tient la position
        if in_pos and i > 0:
            equity *= closes[i] / closes[i - 1]
        peak_eq = max(peak_eq, equity)
        if peak_eq > 0:
            max_dd = max(max_dd, (peak_eq - equity) / peak_eq)
        eq_curve[i] = equity

        if i < warmup or np.isnan(ef[i]) or np.isnan(es[i]) or np.isnan(rs[i]):
            continue

        bull_cross = ef[i - 1] <= es[i - 1] and ef[i] > es[i]
        bear_cross = ef[i - 1] >= es[i - 1] and ef[i] < es[i]

        if in_pos:
            hold += 1
            days_invested += 1
            peak = max(peak, closes[i])
            pnl = (closes[i] - entry) / entry
            exit_reason = None
            # Stops prioritaires (comme RiskManager::checkExitConditions)
            if pnl <= -p.stop_loss:
                exit_reason = "stop"
            elif (not p.let_winners_run) and pnl >= p.take_profit:
                exit_reason = "tp"
            elif hold >= p.min_hold and peak > 0 and (closes[i] - peak) / peak <= -p.trailing:
                exit_reason = "trail"
            elif hold >= p.min_hold and bear_cross:
                exit_reason = "signal"
            elif hold >= p.min_hold and p.rsi_exit and rs[i] > p.rsi_sell_min:
                exit_reason = "rsi"
            if exit_reason is not None:
                equity *= (1.0 - cost)
                if equity > entry_eq:
                    wins += 1
                in_pos = False
        else:
            regime_ok = regime is None or (not np.isnan(regime[i]) and closes[i] > regime[i])
            if (bull_cross and rs[i] < p.rsi_buy_max
                    and closes[i] > ef[i] and closes[i] > es[i] and regime_ok):
                in_pos = True
                entry = closes[i]
                peak = closes[i]
                hold = 0
                trades += 1
                equity *= (1.0 - cost)
                entry_eq = equity

    r = Result()
    r.total_return_pct = (equity - 1.0) * 100.0
    base = closes[warmup]
    r.buy_hold_pct = (closes[-1] - base) / base * 100.0
    r.alpha_pct = r.total_return_pct - r.buy_hold_pct
    r.max_drawdown_pct = max_dd * 100.0
    r.pct_time_invested = 100.0 * days_invested / max(1, n - warmup)
    r.n_trades = trades
    r.win_rate_pct = 100.0 * wins / trades if trades else 0.0
    r.beats_bh = r.total_return_pct > r.buy_hold_pct
    r.start_date = df["date"].iloc[0]
    r.end_date = df["date"].iloc[-1]
    r.equity = eq_curve
    return r


# ─── Walk-forward (miroir de WalkForward.hpp) ────────────────────────────────
def anchored_split(df: pd.DataFrame, p: Params, is_fraction: float = 0.7):
    cut = int(len(df) * is_fraction)
    is_df = df.iloc[:cut].reset_index(drop=True)
    oos_df = df.iloc[cut:].reset_index(drop=True)
    return {"IS": run_backtest(is_df, p), "OOS": run_backtest(oos_df, p)}


def rolling(df: pd.DataFrame, p: Params, n_segments: int = 4):
    seg = len(df) // n_segments
    out = []
    for k in range(n_segments):
        a = k * seg
        b = len(df) if k == n_segments - 1 else (k + 1) * seg
        out.append(run_backtest(df.iloc[a:b].reset_index(drop=True), p))
    return out


def fmt(r: Result) -> str:
    return (f"ret {r.total_return_pct:+7.2f}% | B&H {r.buy_hold_pct:+7.2f}% | "
            f"alpha {r.alpha_pct:+7.2f}% | investi {r.pct_time_invested:5.1f}% | "
            f"trades {r.n_trades:2d} | maxDD {r.max_drawdown_pct:5.2f}% | "
            f"batB&H {'OUI' if r.beats_bh else 'NON'}")
