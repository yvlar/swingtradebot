#!/usr/bin/env python3
"""
Prototype Sprint 8 — refonte des SORTIES (items 8.2 + 8.4), entrée prod inchangée.

On isole l'effet des sorties : même déclencheur d'entrée que la prod (croisement EMA
9/21 + RSI<65), on fait varier UNIQUEMENT la politique de sortie :
  - take-profit fixe ON/OFF        (8.2 : laisser courir)
  - sortie sur RSI suracheté ON/OFF (8.4 : ne pas sortir d'une tendance)
  - largeur du trailing stop        (devient la sortie principale sans TP)

Verdict = walk-forward OOS (split 70/30) + nb de segments roulants battant le B&H.
swingbench reflète la machine à états de sortie du moteur C++ (stops/trailing/minHold).

    python3 prototype_exits.py
"""
import dataclasses
import swingbench as sb


def variant(base, **ch):
    return dataclasses.replace(base, **ch)


def verdict(df, p):
    oos = sb.anchored_split(df, p, 0.7)["OOS"]
    beats = sum(r.beats_bh for r in sb.rolling(df, p, 4))
    return oos, beats


def main(csv="../QQQ.csv"):
    df = sb.load_bars(csv)
    base = sb.prod_config()

    print(f"Données : {len(df)} barres  [{df['date'].iloc[0]} → {df['date'].iloc[-1]}]")
    print("Entrée = prod (EMA 9/21 + RSI<65) ; on ne fait varier que les SORTIES.\n")

    print("A) Effet on/off du take-profit (8.2) et de la sortie RSI (8.4)")
    print(f"   {'variante':34} {'OOS ret':>8} {'OOS alpha':>10} {'investi':>8} {'WF batB&H':>10}")
    print("   " + "-" * 72)
    grid = [
        ("prod (réf : TP on, RSI on)",      variant(base)),
        ("8.2 sans TP",                     variant(base, let_winners_run=True)),
        ("8.4 sans sortie RSI",             variant(base, rsi_exit=False)),
        ("8.2+8.4 (sorties refondues)",     variant(base, let_winners_run=True, rsi_exit=False)),
    ]
    for name, p in grid:
        oos, beats = verdict(df, p)
        flag = "  ◀" if oos.beats_bh else ""
        print(f"   {name:34} {oos.total_return_pct:+7.2f}% {oos.alpha_pct:+9.2f}% "
              f"{oos.pct_time_invested:7.1f}% {beats:>6}/4{flag}")

    print("\nB) Sans TP + sans RSI : balayage de la largeur du trailing stop")
    print(f"   {'trailing':>10} {'OOS ret':>8} {'OOS alpha':>10} {'investi':>8} "
          f"{'trades':>7} {'maxDD':>7} {'WF batB&H':>10}")
    print("   " + "-" * 72)
    best = None
    for ts in [0.03, 0.05, 0.08, 0.10, 0.12, 0.15, 0.20]:
        p = variant(base, let_winners_run=True, rsi_exit=False, trailing=ts)
        oos, beats = verdict(df, p)
        flag = "  ◀" if oos.beats_bh else ""
        print(f"   {ts:9.0%} {oos.total_return_pct:+7.2f}% {oos.alpha_pct:+9.2f}% "
              f"{oos.pct_time_invested:7.1f}% {oos.n_trades:7d} {oos.max_drawdown_pct:6.2f}% "
              f"{beats:>6}/4{flag}")
        if best is None or oos.alpha_pct > best[1]:
            best = (ts, oos.alpha_pct, beats)

    print(f"\n→ meilleur trailing OOS : {best[0]:.0%} (alpha {best[1]:+.2f} pts, "
          f"{best[2]}/4 segments battent le B&H)")
    print("⚠️ D28 : B&H sous-estimé → un alpha positif a une marge réelle plus mince.")


if __name__ == "__main__":
    main()
