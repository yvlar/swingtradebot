#!/usr/bin/env python3
"""
Baseline : rejoue les configs ACTUELLES (défaut + prod) et leur verdict walk-forward.
Sert de point de référence avant de prototyper les idées du Sprint 8.

    python3 run_baseline.py [chemin/vers/QQQ.csv]
"""
import sys
import swingbench as sb


def main(csv: str = "../QQQ.csv") -> None:
    df = sb.load_bars(csv)
    print(f"Données : {len(df)} barres  [{df['date'].iloc[0]} → {df['date'].iloc[-1]}]")
    print("⚠️  D28 : QQQ.csv n'est pas total-return → écart au B&H sous-estimé.\n")

    for name, p in [("config DÉFAUT", sb.default_config()),
                    ("config PROD  ", sb.prod_config())]:
        r = sb.run_backtest(df, p)
        print(f"{name} (full)  {sb.fmt(r)}")

    print("\n── Walk-forward de la config PROD (le verdict qui compte) ──")
    p = sb.prod_config()
    split = sb.anchored_split(df, p, 0.7)
    print(f"  IS  [{split['IS'].start_date}→{split['IS'].end_date}]  {sb.fmt(split['IS'])}")
    print(f"  OOS [{split['OOS'].start_date}→{split['OOS'].end_date}]  {sb.fmt(split['OOS'])}")

    print("  Walk-forward roulant (4 segments) :")
    beats = 0
    for i, r in enumerate(sb.rolling(df, p, 4), 1):
        beats += r.beats_bh
        print(f"    WF#{i} [{r.start_date}→{r.end_date}]  {sb.fmt(r)}")
    print(f"    → {beats}/4 segments battent le Buy & Hold")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "../QQQ.csv")
