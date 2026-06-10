"""
vbt_adapter — pont entre le banc swingbench et VectorBT (prototypage vectorisé).

POURQUOI. swingbench rejoue la stratégie en boucle Python (fidèle au C++, mais ~1 run/s) ;
VectorBT vectorise via numba (~1000 runs/s) → idéal pour les balayages de paramètres et
les heatmaps du Sprint 8. Ce module impose NOTRE convention partout : données locales
(../QQQ.csv via swingbench.load_bars — pas de fetch réseau, D31), coûts du moteur C++
(commission 0,1 % + slippage 5 bps par côté), benchmark = Buy & Hold du MÊME actif,
verdict en walk-forward IS/OOS.

⚠️ Les retours VectorBT sont « à pleine exposition » comme swingbench — comparer des
verdicts relatifs (alpha vs B&H, OOS), jamais les retours absolus au golden C++ (sizé 2 %).

Usage rapide :
    import vbt_adapter as va
    close = va.load_close()
    entries, exits = va.ema_cross_signals(close, 13, 21)
    pf, bh = va.portfolio(close, entries, exits), va.buy_hold(close)
    print(va.compare(pf, bh))
Smoke test : python3 vbt_adapter.py
"""
from __future__ import annotations
import warnings
warnings.filterwarnings("ignore")

import numpy as np
import pandas as pd
import vectorbt as vbt

import swingbench as sb

# Coûts du moteur C++ (item 6.1/6.4) : 0,1 % commission + 5 bps slippage, par côté
FEES = 0.001 + 0.0005
INIT_CASH = 10_000


def load_close(csv_path: str = "../QQQ.csv") -> pd.Series:
    """Série de clôtures indexée par date (mêmes barres que le harnais C++)."""
    df = sb.load_bars(csv_path)
    return pd.Series(df["close"].to_numpy(), index=pd.to_datetime(df["date"]),
                     name=csv_path.split("/")[-1].removesuffix(".csv"))


def ema_cross_signals(close: pd.Series, fast: int, slow: int):
    """Signaux de croisement EMA avec les indicateurs du banc (mêmes que le C++ —
    seedés en SMA, contrairement à l'ewm pandas)."""
    ef = pd.Series(sb.ema(close.to_numpy(), fast), index=close.index)
    es = pd.Series(sb.ema(close.to_numpy(), slow), index=close.index)
    entries = ((ef > es) & (ef.shift(1) <= es.shift(1))).fillna(False)
    exits = ((ef < es) & (ef.shift(1) >= es.shift(1))).fillna(False)
    return entries, exits


def portfolio(close: pd.Series, entries: pd.Series, exits: pd.Series,
              **kwargs) -> "vbt.Portfolio":
    """Portfolio long-only avec NOS coûts. kwargs passés à from_signals
    (ex. sl_stop=0.07, tp_stop=0.15, ts_stop=0.03 pour les stops du moteur)."""
    return vbt.Portfolio.from_signals(
        close, entries, exits,
        init_cash=INIT_CASH, fees=FEES, direction="longonly", freq="1D", **kwargs)


def buy_hold(close: pd.Series) -> "vbt.Portfolio":
    """Benchmark : Buy & Hold du même actif, mêmes coûts d'entrée."""
    return vbt.Portfolio.from_holding(close, init_cash=INIT_CASH, fees=FEES, freq="1D")


def compare(pf: "vbt.Portfolio", bh: "vbt.Portfolio") -> pd.DataFrame:
    """Tableau Stratégie vs Buy & Hold — la comparaison obligatoire après tout backtest."""
    ret, bh_ret = pf.total_return() * 100, bh.total_return() * 100
    return pd.DataFrame({
        "Stratégie": [f"{ret:+.2f}%", f"{pf.sharpe_ratio():.2f}",
                      f"{pf.max_drawdown()*100:.2f}%", int(pf.trades.count()),
                      f"{pf.trades.win_rate()*100:.1f}%"],
        "Buy & Hold": [f"{bh_ret:+.2f}%", f"{bh.sharpe_ratio():.2f}",
                       f"{bh.max_drawdown()*100:.2f}%", "-", "-"],
    }, index=["Retour total", "Sharpe", "Max drawdown", "Trades", "Win rate"])


def split(close: pd.Series, is_fraction: float = 0.7):
    """Split ancré IS/OOS (mêmes bornes que WalkForward.hpp / swingbench)."""
    cut = int(len(close) * is_fraction)
    return close.iloc[:cut], close.iloc[cut:]


def oos_verdict(signal_fn, close: pd.Series, is_fraction: float = 0.7, **pf_kwargs):
    """Verdict walk-forward d'une fonction de signaux `close -> (entries, exits)` :
    les indicateurs sont RECALCULÉS sur chaque tranche (pas de fuite IS→OOS)."""
    out = {}
    for label, c in zip(("IS", "OOS"), split(close, is_fraction)):
        e, x = signal_fn(c)
        pf, bh = portfolio(c, e, x, **pf_kwargs), buy_hold(c)
        out[label] = {
            "ret_pct": pf.total_return() * 100,
            "bh_pct": bh.total_return() * 100,
            "alpha_pct": (pf.total_return() - bh.total_return()) * 100,
            "bat_bh": bool(pf.total_return() > bh.total_return()),
            "trades": int(pf.trades.count()),
        }
    return out


if __name__ == "__main__":
    close = load_close()
    print(f"Données : {len(close)} barres [{close.index[0].date()} → {close.index[-1].date()}]")
    v = oos_verdict(lambda c: ema_cross_signals(c, 13, 21), close)
    for label, m in v.items():
        print(f"  {label:3} ret {m['ret_pct']:+8.2f}% | B&H {m['bh_pct']:+8.2f}% | "
              f"alpha {m['alpha_pct']:+8.2f}% | trades {m['trades']:2d} | "
              f"batB&H {'OUI' if m['bat_bh'] else 'NON'}")
