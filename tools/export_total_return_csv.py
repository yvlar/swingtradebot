#!/usr/bin/env python3
"""Ré-export de séries TOTAL-RETURN au format CSV Yahoo Finance (Sprint 7.4, D29).

Le QQQ.csv historique du dépôt a été exporté SANS ajustement (Adj Close == Close
sur toutes ses lignes) : les dividendes y sont invisibles et le Buy & Hold de
référence est sous-estimé. Ce script télécharge les barres journalières via
l'API chart de Yahoo Finance et écrit des CSV où `Close` est le cours brut
(non ajusté des dividendes) et `Adj Close` le cours ajusté dividendes + splits —
exactement le format attendu par CsvDataFeed (qui sert Adj Close, voir 6.3).

Usage :
    python3 tools/export_total_return_csv.py SPY IWM MDY "QQQ:data/QQQ_TR.csv"

Chaque argument est un symbole (écrit dans data/<SYMBOLE>.csv) ou
« SYMBOLE:chemin.csv » pour choisir le fichier de sortie. La période couvre
celle du QQQ.csv du dépôt (2019-01-01 → 2026-02-14).

Les CSV produits sont COMMITÉS : les tests d'intégration multi-actifs figent
leurs valeurs — ne ré-exporter qu'avec une justification documentée (les
ajustements Yahoo évoluent à chaque nouveau dividende).
"""
import json
import sys
import time
import urllib.request

PERIOD1 = 1546300800   # 2019-01-01 UTC (début du QQQ.csv du dépôt)
PERIOD2 = 1771113600   # 2026-02-14 UTC (fin du QQQ.csv du dépôt)


def fetch(symbol: str) -> dict:
    url = (f"https://query1.finance.yahoo.com/v8/finance/chart/{symbol}"
           f"?period1={PERIOD1}&period2={PERIOD2}&interval=1d&events=div%2Csplit")
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)


def export(symbol: str, out_path: str) -> None:
    data = fetch(symbol)["chart"]["result"][0]
    ts = data["timestamp"]
    quote = data["indicators"]["quote"][0]
    adj = data["indicators"]["adjclose"][0]["adjclose"]

    rows, skipped = [], 0
    for i, t in enumerate(ts):
        o, h, l, c, v, a = (quote["open"][i], quote["high"][i], quote["low"][i],
                            quote["close"][i], quote["volume"][i], adj[i])
        if None in (o, h, l, c, v, a):
            skipped += 1          # barre incomplète côté Yahoo : on l'ignore
            continue
        date = time.strftime("%Y-%m-%d", time.gmtime(t))
        rows.append(f"{date},{o:.6f},{h:.6f},{l:.6f},{c:.6f},{a:.6f},{v}")

    with_div = sum(1 for r in rows if r.split(",")[4] != r.split(",")[5])
    with open(out_path, "w") as f:
        f.write("Date,Open,High,Low,Close,Adj Close,Volume\n")
        f.write("\n".join(rows) + "\n")
    print(f"{symbol}: {len(rows)} barres -> {out_path} "
          f"({with_div} lignes avec Adj Close != Close, {skipped} ignorées)")
    if with_div == 0:
        sys.exit(f"ERREUR {symbol}: Adj Close == Close partout — export inutile (D29)")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for arg in sys.argv[1:]:
        sym, _, path = arg.partition(":")
        export(sym, path or f"data/{sym}.csv")
