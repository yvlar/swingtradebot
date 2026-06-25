#pragma once
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>

namespace trading {

// ─── DataQuality ──────────────────────────────────────────────────────────────
// Garde-fou de qualité des DONNÉES (Sprint 7, item 7.4, D29).
//
// LEÇON D29 : QQQ.csv avait `Adj Close == Close` sur ses 1858 lignes — un export
// SANS ajustement (dividendes ignorés). Le mécanisme total-return de
// CsvDataFeed (6.3) était donc inerte sur ce dataset, et personne ne l'avait
// vérifié avant de planifier 6.3. La qualité des données doit être TESTÉE comme
// le code.
//
// Cette fonction relit les COLONNES BRUTES du CSV (Close et Adj Close). Elle ne
// peut pas s'appuyer sur CsvDataFeed : ce dernier écrase déjà Close par Adj Close
// à l'analyse (`b.close = adjClose`), donc l'information « Adj == Close » est
// perdue une fois les Bar construites. On relit le fichier.

struct DataQualityReport {
    size_t rows               = 0;      // lignes de données valides analysées
    size_t adjEqualsCloseRows = 0;      // lignes où |Adj Close - Close| < epsilon
    // Vrai si TOUTES les lignes ont Adj Close == Close : symptôme d'un export
    // sans ajustement (dividendes/splits absents — D29). Un seul jour avec
    // dividende suffit à lever le drapeau.
    bool   suspectNoDividends = false;
};

// Audite un CSV Yahoo (Date,Open,High,Low,Close,Adj Close,Volume) et signale
// l'absence suspecte de dividendes/splits. epsilon par défaut : 1e-9.
inline DataQualityReport auditTotalReturnCsv(const std::string& csvPath,
                                             double epsilon = 1e-9) {
    DataQualityReport rep;
    std::ifstream file(csvPath);
    if (!file.is_open()) return rep;  // fichier absent → rien à signaler

    std::string line;
    bool firstLine = true;
    while (std::getline(file, line)) {
        if (firstLine) { firstLine = false; continue; }  // en-tête
        if (line.empty() || line[0] == '#') continue;

        // Découpe en colonnes ; Close = colonne 4, Adj Close = colonne 5
        std::vector<std::string> tok;
        std::stringstream ss(line);
        std::string field;
        while (std::getline(ss, field, ',')) tok.push_back(field);
        if (tok.size() < 6) continue;
        if (tok[4].empty() || tok[5].empty()
            || tok[4] == "null" || tok[5] == "null") continue;

        try {
            const double close    = std::stod(tok[4]);
            const double adjClose = std::stod(tok[5]);
            ++rep.rows;
            if (std::abs(adjClose - close) < epsilon) ++rep.adjEqualsCloseRows;
        } catch (...) {
            continue;  // ligne malformée — ignorée
        }
    }

    rep.suspectNoDividends = (rep.rows > 0 && rep.adjEqualsCloseRows == rep.rows);
    return rep;
}

} // namespace trading
