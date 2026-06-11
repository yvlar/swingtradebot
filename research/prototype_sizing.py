#!/usr/bin/env python3
"""
Prototype Sprint 9 — position sizing (item 9.0b) sur la config V2.

L'edge de TIMING de la V2 (sorties refondues : sans TP fixe, sans sortie RSI) est
prouvé en OOS à pleine exposition (cf. prototype_exits.py + golden C++). Reste à
choisir COMBIEN investir pour capter cet edge en dollars sans drawdown excessif.

On réutilise EXACTEMENT la machine entrée/sortie de swingbench (mêmes signaux,
mêmes stops), on n'ajoute qu'un POIDS d'exposition w sur la mark-to-market :

    equity *= 1 + w_{i-1} * (close_i/close_{i-1} - 1)      # rendement du jour
    coût    = |w_i - w_{i-1}| * (commission+slippage)       # entrée/sortie/rebal

Méthodes comparées (toutes sur V2) :
  - exposition fixe         w ∈ {50, 75, 95, 100} %
  - vol-targeting           w = vol_cible / vol_réalisée_20j  (capé)
  - Kelly fractionnaire     w = λ · μ/σ²  (estimé sur 60j glissants, capé)

Verdict = OOS (anchored 70/30) : rendement, alpha vs B&H, maxDD, Sharpe, Sortino,
Calmar, % temps investi + nb de segments roulants battant le B&H.

⚠️ Rappels du banc : B&H sous-estimé (D28) → un alpha positif a une marge réelle
plus mince. Le sizing ne change PAS le % de temps investi (mêmes dates d'entrée/
sortie) — il module l'AMPLITUDE de l'exposition.

    python3 prototype_sizing.py
"""
import dataclasses
import numpy as np
import swingbench as sb

COST = sb.prod_config().commission + sb.prod_config().slippage   # 0.1% + 5bps
ANN = 252.0


# ─── Backtest avec poids d'exposition (réutilise les signaux de swingbench) ───
def run_sized(df, p, weight_fn):
    """weight_fn(ctx) -> poids cible ∈ [0, wmax] quand EN POSITION (0 sinon).
    ctx expose : i, closes, rets, vol20 (annualisée), in_pos. Décidé à la clôture i,
    appliqué au rendement i→i+1 (pas de look-ahead)."""
    closes = df["close"].to_numpy(dtype=float)
    n = len(closes)
    warmup = p.ema_slow + p.rsi_period + 2
    if n <= warmup + 2:
        return None

    ef, es, rs = sb.ema(closes, p.ema_fast), sb.ema(closes, p.ema_slow), sb.rsi(closes, p.rsi_period)
    rets = np.zeros(n)
    rets[1:] = closes[1:] / closes[:-1] - 1.0
    # vol réalisée annualisée glissante 20j (sur le sous-jacent, connue à la clôture i)
    vol20 = np.full(n, np.nan)
    for i in range(20, n):
        vol20[i] = np.std(rets[i - 19:i + 1], ddof=1) * np.sqrt(ANN)

    equity = 1.0
    peak_eq = 1.0
    max_dd = 0.0
    port_rets = np.zeros(n)        # rendements quotidiens du PORTEFEUILLE (pour Sharpe/Sortino)
    w_prev = 0.0
    in_pos = False
    entry = peak = 0.0
    hold = 0
    days_invested = trades = wins = 0
    entry_eq = 0.0

    for i in range(n):
        # 1) rendement du jour avec le poids décidé hier
        if i > 0:
            r = w_prev * rets[i]
            equity *= (1.0 + r)
            port_rets[i] = r
        peak_eq = max(peak_eq, equity)
        if peak_eq > 0:
            max_dd = max(max_dd, (peak_eq - equity) / peak_eq)

        # 2) décisions à la clôture i (identiques à swingbench)
        w_target = w_prev
        if i >= warmup and not (np.isnan(ef[i]) or np.isnan(es[i]) or np.isnan(rs[i])):
            bull = ef[i - 1] <= es[i - 1] and ef[i] > es[i]
            bear = ef[i - 1] >= es[i - 1] and ef[i] < es[i]
            if in_pos:
                hold += 1
                days_invested += 1
                peak = max(peak, closes[i])
                pnl = (closes[i] - entry) / entry
                exit_now = (pnl <= -p.stop_loss
                            or ((not p.let_winners_run) and pnl >= p.take_profit)
                            or (hold >= p.min_hold and peak > 0 and (closes[i] - peak) / peak <= -p.trailing)
                            or (hold >= p.min_hold and bear)
                            or (hold >= p.min_hold and p.rsi_exit and rs[i] > p.rsi_sell_min))
                if exit_now:
                    in_pos = False
                    w_target = 0.0
                    if equity > entry_eq:
                        wins += 1
                else:
                    w_target = weight_fn(dict(i=i, vol20=vol20[i], rets=rets, in_pos=True))
            else:
                if (bull and rs[i] < p.rsi_buy_max and closes[i] > ef[i] and closes[i] > es[i]):
                    in_pos = True
                    entry = peak = closes[i]
                    hold = 0
                    trades += 1
                    w_target = weight_fn(dict(i=i, vol20=vol20[i], rets=rets, in_pos=True))
                    entry_eq = equity   # repère P&L approx (avant coût de rebal)

        # 3) coût de la variation d'exposition (entrée/sortie/rebalancement)
        dz = abs(w_target - w_prev)
        if dz > 0:
            equity *= (1.0 - dz * COST)
        w_prev = w_target

    # ── métriques ──
    pr = port_rets[1:]
    mu, sd = pr.mean(), pr.std(ddof=1)
    downside = pr[pr < 0]
    dd_sd = downside.std(ddof=1) if len(downside) > 1 else np.nan
    sharpe = (mu / sd * np.sqrt(ANN)) if sd > 0 else 0.0
    sortino = (mu / dd_sd * np.sqrt(ANN)) if dd_sd and dd_sd > 0 else 0.0
    total = (equity - 1.0) * 100.0
    base = closes[warmup]
    bh = (closes[-1] - base) / base * 100.0
    years = (n - warmup) / ANN
    cagr = ((equity) ** (1.0 / years) - 1.0) * 100.0 if years > 0 and equity > 0 else 0.0
    calmar = (cagr / (max_dd * 100.0)) if max_dd > 0 else 0.0
    return dict(total=total, bh=bh, alpha=total - bh, maxdd=max_dd * 100.0,
                sharpe=sharpe, sortino=sortino, calmar=calmar,
                invested=100.0 * days_invested / max(1, n - warmup),
                trades=trades, beats=total > bh)


# ─── Fonctions de poids ───────────────────────────────────────────────────────
def fixed(w):
    return lambda ctx: w

def vol_target(target_ann, wmax=1.0):
    def f(ctx):
        v = ctx["vol20"]
        if v is None or np.isnan(v) or v <= 0:
            return wmax
        return float(np.clip(target_ann / v, 0.0, wmax))
    return f

def frac_kelly(lam, window=60, wmax=1.5):
    def f(ctx):
        i, rets = ctx["i"], ctx["rets"]
        if i < window:
            return min(1.0, wmax)
        w = rets[i - window + 1:i + 1]
        var = np.var(w, ddof=1)
        if var <= 0:
            return min(1.0, wmax)
        return float(np.clip(lam * w.mean() / var, 0.0, wmax))
    return f


def oos(df, p, weight_fn):
    cut = int(len(df) * 0.7)
    return run_sized(df.iloc[cut:].reset_index(drop=True), p, weight_fn)

def roll_beats(df, p, weight_fn, k=4):
    seg = len(df) // k
    c = 0
    for j in range(k):
        a = j * seg
        b = len(df) if j == k - 1 else (j + 1) * seg
        r = run_sized(df.iloc[a:b].reset_index(drop=True), p, weight_fn)
        if r and r["beats"]:
            c += 1
    return c


def main(csv="../QQQ.csv"):
    df = sb.load_bars(csv)
    v2 = dataclasses.replace(sb.prod_config(), let_winners_run=True, rsi_exit=False)

    # Référence B&H pur (tout investi, jamais en cash) pour situer le risque
    bh_ref = run_sized(df.iloc[int(len(df) * 0.7):].reset_index(drop=True), v2, lambda ctx: 1.0)

    print(f"Config V2 (sorties refondues) — OOS anchored 70/30 sur {len(df)} barres")
    print(f"% temps investi (constant, ~indépendant du sizing) ≈ {bh_ref['invested']:.0f}%\n")
    hdr = f"{'méthode':28} {'ret':>8} {'alpha':>8} {'maxDD':>7} {'Sharpe':>7} {'Sortino':>8} {'Calmar':>7} {'WF':>4}"
    print(hdr); print("-" * len(hdr))

    rows = [
        ("exposition fixe 50%",  fixed(0.50)),
        ("exposition fixe 75%",  fixed(0.75)),
        ("exposition fixe 95%",  fixed(0.95)),
        ("exposition fixe 100%", fixed(1.00)),
        ("vol-target 10% (cap1)", vol_target(0.10, 1.0)),
        ("vol-target 15% (cap1)", vol_target(0.15, 1.0)),
        ("vol-target 20% (cap1)", vol_target(0.20, 1.0)),
        ("vol-target 15% (cap1.5)", vol_target(0.15, 1.5)),
        ("Kelly λ=0.25",          frac_kelly(0.25)),
        ("Kelly λ=0.5",           frac_kelly(0.5)),
    ]
    for name, wf in rows:
        r = oos(df, v2, wf)
        wf4 = roll_beats(df, v2, wf)
        flag = "  ◀" if r["beats"] else ""
        print(f"{name:28} {r['total']:+7.2f}% {r['alpha']:+7.2f}% {r['maxdd']:6.2f}% "
              f"{r['sharpe']:7.2f} {r['sortino']:8.2f} {r['calmar']:7.2f} {wf4:>3}/4{flag}")

    print("-" * len(hdr))
    print(f"{'Buy & Hold (réf, 100% perm.)':28} {bh_ref['bh']:+7.2f}% {0.0:+7.2f}% "
          f"{'  n/a':>7} {'  n/a':>7} {'   n/a':>8} {'  n/a':>7} {'—':>4}")
    print("\n⚠️ D28 : B&H sous-estimé (~+0,55%/an) → marge réelle de l'alpha plus mince.")
    print("Le sizing ne déplace pas les dates d'entrée/sortie : il module l'amplitude.")


if __name__ == "__main__":
    main()
