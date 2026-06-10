#!/usr/bin/env python3
"""
Prototype Sprint 8 : teste les leviers de refonte de la stratégie et compare leur
verdict OUT-OF-SAMPLE à la config prod actuelle. C'est l'usage type du banc — un
premier read en quelques secondes avant d'écrire la moindre ligne de C++.

Leviers testés (cf. items 8.1-8.4 de ROADMAP.md) :
  - regime_sma=200   : n'acheter QUE si close > SMA200 (rester avec la tendance) [8.1]
  - let_winners_run  : désactive le take-profit fixe (laisser courir) [8.2]
  - rsi_exit=False   : ne PAS vendre sur RSI suracheté seul (ne pas sortir d'une
                       tendance haussière) [8.4]

    python3 prototype_regime.py [chemin/vers/QQQ.csv]

⚠️ Banc « tout investi quand en position » : mesure le TIMING/STRUCTURE, pas le
sizing (le C++ size à 2 % de risque — levier distinct). Le gagnant OOS ici devra
ensuite être porté dans le moteur C++ et re-validé par le harnais WalkForward.
"""
import sys
import dataclasses
import swingbench as sb


def variant(base: sb.Params, **changes) -> sb.Params:
    return dataclasses.replace(base, **changes)


def main(csv: str = "../QQQ.csv") -> None:
    df = sb.load_bars(csv)
    base = sb.prod_config()

    variants = {
        "prod (référence)":            base,
        "8.1 régime SMA200":           variant(base, regime_sma=200),
        "8.2 laisser courir (no TP)":  variant(base, let_winners_run=True),
        "8.4 pas de sortie RSI":       variant(base, rsi_exit=False),
        "8.1+8.2":                     variant(base, regime_sma=200, let_winners_run=True),
        "8.1+8.2+8.4 (combiné)":       variant(base, regime_sma=200, let_winners_run=True,
                                               rsi_exit=False),
    }

    print(f"Données : {len(df)} barres  [{df['date'].iloc[0]} → {df['date'].iloc[-1]}]")
    print("Verdict = walk-forward OOS (split 70/30) + nb de segments roulants battant B&H.\n")
    print(f"{'variante':28} {'OOS ret':>9} {'OOS B&H':>9} {'OOS alpha':>10} "
          f"{'investi':>8} {'WF batB&H':>10}")
    print("-" * 80)

    for name, p in variants.items():
        split = sb.anchored_split(df, p, 0.7)
        oos = split["OOS"]
        beats = sum(r.beats_bh for r in sb.rolling(df, p, 4))
        flag = "  ◀ bat B&H" if oos.beats_bh else ""
        print(f"{name:28} {oos.total_return_pct:+8.2f}% {oos.buy_hold_pct:+8.2f}% "
              f"{oos.alpha_pct:+9.2f}% {oos.pct_time_invested:7.1f}% "
              f"{beats:>6}/4{flag}")

    print("\nLecture : un levier utile fait MONTER l'alpha OOS et/ou le nb de segments")
    print("battant le B&H. Le combiné gagnant sera porté dans SwingStrategy (C++).")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "../QQQ.csv")
