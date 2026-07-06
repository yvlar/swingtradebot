#!/usr/bin/env python3
# ─── Export total-return depuis Yahoo Finance (item 8d.1, Sprint 8-septies) ───
#
# Télécharge l'historique JOURNALIER complet d'un ou plusieurs actifs via
# l'API « v8 chart » de Yahoo et écrit des CSV au format attendu par
# CsvDataFeed / auditTotalReturnCsv :
#
#     Date,Open,High,Low,Close,Adj Close,Volume
#
# Points de discipline (ROADMAP, items 7.4/D29/8d.1) :
#   - « Adj Close » est la série TOTAL-RETURN (dividendes réinvestis) : c'est
#     elle que CsvDataFeed sert à la stratégie ET au Buy & Hold. Le garde-fou
#     auditTotalReturnCsv refusera un export où Adj == Close partout.
#   - La date de FIN est FIGÉE (END_DATE ci-dessous) : les comptes de barres
#     et les goldens qui en dépendent doivent être reproductibles, et on ne
#     livre jamais la barre du jour en formation (leçon E6/D25).
#   - Aucune dépendance hors bibliothèque standard (reproductible partout).
#
# Usage :
#     python3 scripts/export_total_return.py [SYMBOLE ...]
# Sans argument : exporte QQQ, SPY, IWM, MDY vers <SYMBOLE>_max.csv à la
# racine du dépôt (chaque actif depuis sa première cotation, range=max).

import json
import ssl
import sys
import urllib.parse
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

# Fin d'historique FIGÉE (exclusive) : barres jusqu'au 2026-07-01 inclus.
END_DATE = datetime(2026, 7, 2, tzinfo=timezone.utc)

# Yahoo exige un User-Agent de navigateur (429 sinon — vérifié le 2026-07-04).
USER_AGENT = ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
              "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36")

SYMBOLES_DEFAUT = ["QQQ", "SPY", "IWM", "MDY"]
RACINE_DEPOT = Path(__file__).resolve().parent.parent


def telecharge(symbole: str) -> dict:
    """Récupère le JSON v8 chart complet (period1=0 → début de cotation)."""
    # Les indices (ex. ^VIX) portent un caret non-URL-safe : on l'encode
    # (^VIX → %5EVIX) sans toucher au nom logique du symbole.
    sym_url = urllib.parse.quote(symbole, safe="")
    url = (f"https://query2.finance.yahoo.com/v8/finance/chart/{sym_url}"
           f"?period1=0&period2={int(END_DATE.timestamp())}"
           f"&interval=1d&events=div%2Csplit")
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    ctx = ssl.create_default_context()
    with urllib.request.urlopen(req, context=ctx, timeout=60) as rep:
        return json.load(rep)


def exporte(symbole: str) -> Path:
    """Écrit <SYMBOLE>_max.csv à la racine du dépôt ; retourne le chemin."""
    donnees = telecharge(symbole)
    resultat = donnees["chart"]["result"][0]
    horodatages = resultat["timestamp"]
    indicateurs = resultat["indicators"]
    cotes = indicateurs["quote"][0]
    # Un INDICE de volatilité (ex. ^VIX) ne verse pas de dividende → Yahoo peut
    # omettre le bloc « adjclose » (ou le renvoyer égal au close). On dégrade
    # proprement vers le close : le CSV aura Adj Close == Close, ce que
    # auditTotalReturnCsv signalera « suspectNoDividends » — ATTENDU pour un
    # indice, pas un export cassé (D29). Un indice n'est PAS une série
    # total-return : il n'est jamais TRADÉ, seulement lu comme SIGNAL de régime.
    if "adjclose" in indicateurs and indicateurs["adjclose"]:
        adj = indicateurs["adjclose"][0]["adjclose"]
    else:
        adj = cotes["close"]

    # Nom de fichier logique (sans caret) : ^VIX → VIX_max.csv.
    chemin = RACINE_DEPOT / f"{symbole.lstrip('^')}_max.csv"
    lignes = ["Date,Open,High,Low,Close,Adj Close,Volume"]
    for i, ts in enumerate(horodatages):
        o, h, b = cotes["open"][i], cotes["high"][i], cotes["low"][i]
        c, a, v = cotes["close"][i], adj[i], cotes["volume"][i]
        # Yahoo insère parfois des lignes nulles (séance sans donnée) : on
        # les saute — la garde de densité du test 8d.1 vérifie le compte.
        if None in (o, h, b, c, a, v):
            continue
        date = datetime.fromtimestamp(ts, tz=timezone.utc).strftime("%Y-%m-%d")
        lignes.append(f"{date},{o:.6f},{h:.6f},{b:.6f},{c:.6f},{a:.6f},{int(v)}")

    chemin.write_text("\n".join(lignes) + "\n", encoding="utf-8")
    print(f"{symbole}: {len(lignes) - 1} barres → {chemin.name} "
          f"(du {lignes[1].split(',')[0]} au {lignes[-1].split(',')[0]})")
    return chemin


if __name__ == "__main__":
    symboles = sys.argv[1:] or SYMBOLES_DEFAUT
    for s in symboles:
        exporte(s)
