#!/usr/bin/env python3
# Génère des CSV de test total-return déterministes (Sprint 7.4, D29).
#
# Trois actifs PORTENT des dividendes (Adj Close != Close) avec des régimes de
# prix différents (haussier, range, plus volatil) pour tester la robustesse
# inter-actifs du harnais multi-actifs. Un quatrième CSV (NODIV) a Adj Close ==
# Close sur toutes les lignes : il doit déclencher le garde-fou de qualité D29.
#
# Format Yahoo Finance : Date,Open,High,Low,Close,Adj Close,Volume
# Aucune dépendance externe ni aléatoire non graine : rejouable à l'identique.
import math
import datetime

N = 160  # > warmup (37 pour la config prod) → trades possibles


def write_csv(path, base, drift, amp, period, dividend, seed_phase=0.0):
    start = datetime.date(2020, 1, 1)
    rows = ["Date,Open,High,Low,Close,Adj Close,Volume"]
    d = start
    for i in range(N):
        # avance d'un jour ouvré (saute samedi/dimanche)
        while d.weekday() >= 5:
            d += datetime.timedelta(days=1)
        wave = 1.0 + amp * math.sin(i / period + seed_phase)
        close = base * (1.0 + drift * i) * wave
        # facteur d'ajustement : dividendes accumulés « retirés » en remontant
        # le temps ; vaut 1.0 sur la dernière barre, (1-dividend) sur la première
        f = (1.0 - dividend) + dividend * (i / (N - 1))
        adj = close * f
        o = close * 0.999
        h = close * 1.004
        lo = close * 0.996
        vol = 1_000_000 + (i % 7) * 50_000
        rows.append(f"{d.isoformat()},{o:.4f},{h:.4f},{lo:.4f},"
                    f"{close:.4f},{adj:.4f},{vol}")
        d += datetime.timedelta(days=1)
    with open(path, "w") as fh:
        fh.write("\n".join(rows) + "\n")


if __name__ == "__main__":
    here = __file__.rsplit("/", 1)[0]
    # Trois actifs avec dividendes (Adj Close != Close)
    write_csv(f"{here}/SPY_sample.csv", base=300.0, drift=0.0020, amp=0.04, period=18, dividend=0.10)
    write_csv(f"{here}/IWM_sample.csv", base=150.0, drift=0.0005, amp=0.07, period=11, dividend=0.06, seed_phase=1.3)
    write_csv(f"{here}/MDY_sample.csv", base=400.0, drift=0.0012, amp=0.05, period=14, dividend=0.08, seed_phase=2.1)
    # Sans dividende : Adj Close == Close (doit déclencher le garde-fou D29)
    write_csv(f"{here}/NODIV_sample.csv", base=200.0, drift=0.0015, amp=0.05, period=13, dividend=0.0)
    print("CSV de test generes.")
