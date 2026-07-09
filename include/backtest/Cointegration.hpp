#pragma once
#include <vector>
#include <cmath>
#include <cstddef>

namespace trading {

// ─── Cointegration ────────────────────────────────────────────────────────────
// Réouverture Sprint 18 (piste §5.1 de CONCLUSION_RECHERCHE_EDGE.md) : le
// pairs-trading du Sprint 12 utilisait un spread NAÏF β = 1 sans test de
// stationnarité — verdict D49 : « le spread naïf sans test de cointégration est
// du bruit ». Ce module apporte les DEUX briques manquantes d'Engle-Granger,
// en math pur À LA MAIN (aucune dépendance tierce, style du dépôt) :
//
//  1. `olsFit` — étape 1 d'Engle-Granger : régression OLS roulante CAUSALE des
//     log-prix (y = α + β·x sur la fenêtre [end−window+1, end]) → hedge ratio β
//     estimé au fil de l'eau, jamais avec des données > end (anti look-ahead).
//  2. `dfTStat` — étape 2 : test de Dickey-Fuller sur le RÉSIDU e = y − α − β·x.
//     On régresse Δe_t sur e_{t−1} (avec constante) et on retourne le t-stat de
//     ρ̂ : très négatif ⇔ le résidu retourne fortement vers sa moyenne ⇔ la
//     paire est cointégrée sur la fenêtre.
//
// SIMPLIFICATION ASSUMÉE (documentée, comme les défauts de slippage D22) :
// Dickey-Fuller NON augmenté (lag 0 — les résidus d'ETF journaliers ont peu de
// structure sérielle à cette granularité), comparé à une VALEUR CRITIQUE FIGÉE
// (MacKinnon, 2 variables avec constante, 5 % ≈ −3,34) portée en knob de config
// par l'appelant — PAS de p-value calculée. C'est l'étape 2 pragmatique
// standard d'Engle-Granger, suffisante pour un GATE binaire d'entrée.

// Résultat d'une régression OLS y = α + β·x. `ok` = false si la fenêtre est
// trop courte ou x quasi constant (Var(x) ≈ 0 → β indéfini).
struct OlsFit {
    double alpha = 0.0;
    double beta  = 0.0;
    bool   ok    = false;
};

// Régression OLS CAUSALE de y sur x, fenêtre [end−window+1, end] incluse.
// β = Cov(x,y)/Var(x), α = ȳ − β·x̄. N'utilise JAMAIS d'indice > end (le hedge
// ratio du pairs-trading est estimé à l'instant de décision — anti look-ahead).
inline OlsFit olsFit(const std::vector<double>& y, const std::vector<double>& x,
                     size_t end, size_t window) {
    OlsFit f;
    if (window < 2 || end >= y.size() || end >= x.size() || end + 1 < window)
        return f;
    const size_t lo = end + 1 - window;

    double mx = 0.0, my = 0.0;
    for (size_t i = lo; i <= end; ++i) { mx += x[i]; my += y[i]; }
    mx /= static_cast<double>(window);
    my /= static_cast<double>(window);

    double sxx = 0.0, sxy = 0.0;
    for (size_t i = lo; i <= end; ++i) {
        sxx += (x[i] - mx) * (x[i] - mx);
        sxy += (x[i] - mx) * (y[i] - my);
    }
    // x quasi constant → β indéfini (garde epsilon absolue : les log-prix sont
    // d'ordre ~1-10, une variance < 1e-18 est dégénérée).
    if (sxx < 1e-18) return f;

    f.beta  = sxy / sxx;
    f.alpha = my - f.beta * mx;
    f.ok    = true;
    return f;
}

// Résidu d'Engle-Granger e_j = y_j − α − β·x_j sur [lo, hi) avec les
// coefficients estimés À L'INSTANT DE DÉCISION (l'appelant passe le fit calculé
// sur des données ≤ l'instant courant — aucune fuite).
inline std::vector<double> egResidual(const std::vector<double>& y,
                                      const std::vector<double>& x,
                                      const OlsFit& fit, size_t lo, size_t hi) {
    std::vector<double> e;
    if (!fit.ok || lo >= hi || hi > y.size() || hi > x.size()) return e;
    e.reserve(hi - lo);
    for (size_t i = lo; i < hi; ++i)
        e.push_back(y[i] - fit.alpha - fit.beta * x[i]);
    return e;
}

// t-stat de Dickey-Fuller (lag 0, avec constante) sur une série e :
// régression Δe_t = c + ρ·e_{t−1} + u_t, t = ρ̂ / SE(ρ̂). Très négatif ⇔ forte
// réversion (stationnarité) ; ~0 ou positif ⇔ marche aléatoire / divergence.
// Retourne 0.0 (« non concluant », jamais ≤ une critique négative) si la série
// est trop courte (< 8 points : m − 2 degrés de liberté exigés) ou dégénérée.
inline double dfTStat(const std::vector<double>& e) {
    const size_t n = e.size();
    if (n < 8) return 0.0;
    const size_t m = n - 1;   // observations de la régression (t = 1..n−1)

    // Moyennes de e_{t−1} et de Δe_t.
    double mlag = 0.0, mdiff = 0.0;
    for (size_t t = 1; t < n; ++t) { mlag += e[t - 1]; mdiff += e[t] - e[t - 1]; }
    mlag  /= static_cast<double>(m);
    mdiff /= static_cast<double>(m);

    // ρ̂ = Σ(e_{t−1}−ē)(Δe_t−Δē) / Σ(e_{t−1}−ē)².
    double sll = 0.0, sld = 0.0;
    for (size_t t = 1; t < n; ++t) {
        const double dl = e[t - 1] - mlag;
        sll += dl * dl;
        sld += dl * ((e[t] - e[t - 1]) - mdiff);
    }
    if (sll < 1e-18) return 0.0;   // résidu quasi constant → non concluant
    const double rho = sld / sll;

    // s² = RSS / (m − 2) (2 paramètres estimés : constante + ρ), SE = s/√Σ(e_{t−1}−ē)².
    double rss = 0.0;
    for (size_t t = 1; t < n; ++t) {
        const double fitv = mdiff + rho * (e[t - 1] - mlag);
        const double res  = (e[t] - e[t - 1]) - fitv;
        rss += res * res;
    }
    const double s2 = rss / static_cast<double>(m - 2);
    if (s2 < 1e-24) {
        // Ajustement PARFAIT (série déterministe) : t-stat infini en théorie —
        // on retourne un sentinel signé (±999) selon le sens de la réversion.
        return rho < 0.0 ? -999.0 : (rho > 0.0 ? 999.0 : 0.0);
    }
    const double se = std::sqrt(s2 / sll);
    return rho / se;
}

} // namespace trading
