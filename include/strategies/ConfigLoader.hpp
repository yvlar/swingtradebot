#pragma once
// ============================================================
//  ConfigLoader.hpp — chargement STRICT de SwingConfig depuis
//  un fichier JSON (item 9.1, E1/D21)
//
//  La config de production ne vit plus dans le C++ : elle est
//  chargée depuis config/prod.json et VALIDÉE au démarrage.
//  Philosophie : échec BRUYANT — champ manquant, type faux ou
//  borne violée → std::runtime_error avec un message qui nomme
//  le champ. Aucun défaut silencieux : les 14 champs de
//  SwingConfig sont tous requis (tenir cette liste synchronisée
//  avec SwingStrategy.hpp si un champ est ajouté).
//  EXCEPTION assumée : trailingAtrMult (item 8q.1) reste HORS du
//  loader — axe de recherche jugé en 8q.2, câblage différé au
//  gate d'adoption 8q.3 (décision utilisateur 2026-07-03) pour ne
//  pas toucher config/prod.json (gouverné) avant un verdict.
// ============================================================
#include "strategies/SwingStrategy.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <string>

namespace trading {

namespace configdetail {

inline const nlohmann::json& requireField(const nlohmann::json& j,
                                          const std::string& key) {
    if (!j.contains(key))
        throw std::runtime_error("Config : champ manquant « " + key + " »");
    return j.at(key);
}

inline int requireInt(const nlohmann::json& j, const std::string& key) {
    const auto& v = requireField(j, key);
    if (!v.is_number_integer())
        throw std::runtime_error("Config : type invalide pour « " + key
                                 + " » (entier attendu)");
    return v.get<int>();
}

inline double requireDouble(const nlohmann::json& j, const std::string& key) {
    const auto& v = requireField(j, key);
    if (!v.is_number())        // un entier JSON est accepté là où un double l'est
        throw std::runtime_error("Config : type invalide pour « " + key
                                 + " » (nombre attendu)");
    return v.get<double>();
}

inline bool requireBool(const nlohmann::json& j, const std::string& key) {
    const auto& v = requireField(j, key);
    if (!v.is_boolean())
        throw std::runtime_error("Config : type invalide pour « " + key
                                 + " » (booléen attendu)");
    return v.get<bool>();
}

inline std::string requireString(const nlohmann::json& j, const std::string& key) {
    const auto& v = requireField(j, key);
    if (!v.is_string())
        throw std::runtime_error("Config : type invalide pour « " + key
                                 + " » (chaîne attendue)");
    return v.get<std::string>();
}

inline void borne(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error("Config : " + message);
}

// Ouvre et parse un fichier JSON — erreurs bruyantes (fichier/syntaxe)
inline nlohmann::json parseJsonFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open())
        throw std::runtime_error("Config prod introuvable : " + path);
    try {
        return nlohmann::json::parse(f);
    } catch (const std::exception& e) {
        throw std::runtime_error("Config prod malformée (" + path + ") : "
                                 + e.what());
    }
}

} // namespace configdetail

// ─── ProdSettings — réglages OPÉRATIONNELS (hors stratégie) ──────────────────
// Vivent dans le même config/prod.json mais ne traversent PAS le backtest :
// un flag d'autorisation de live n'a aucun sens dans SwingConfig.
struct ProdSettings {
    // Gate mécanique du mode --live (A1) : tant que la DoD d'edge n'est pas
    // atteinte (checklist pré-live du RUNBOOK), ce champ RESTE false — et un
    // test d'intégration le verrouille. JAMAIS true par défaut.
    bool liveTradingApproved = false;
};

inline ProdSettings loadProdSettingsJson(const std::string& path) {
    using namespace configdetail;
    auto j = parseJsonFile(path);
    ProdSettings s;
    s.liveTradingApproved = requireBool(j, "liveTradingApproved");
    return s;
}

// Charge et VALIDE une SwingConfig depuis un fichier JSON.
// Lève std::runtime_error (message français explicite) sur toute anomalie.
// NB : takeProfitPct = 0 et rsiBuyMax ≥ 100 sont des valeurs « désactivé »
// légitimes (items 8.2/8.3) — les bornes ne les rejettent pas.
inline SwingConfig loadSwingConfigJson(const std::string& path) {
    using namespace configdetail;

    auto j = parseJsonFile(path);

    SwingConfig cfg;
    cfg.symbol                  = requireString(j, "symbol");
    cfg.emaFast                 = requireInt   (j, "emaFast");
    cfg.emaSlow                 = requireInt   (j, "emaSlow");
    cfg.rsiPeriod               = requireInt   (j, "rsiPeriod");
    cfg.rsiBuyMax               = requireDouble(j, "rsiBuyMax");
    cfg.rsiSellMin              = requireDouble(j, "rsiSellMin");
    cfg.stopLossPct             = requireDouble(j, "stopLossPct");
    cfg.takeProfitPct           = requireDouble(j, "takeProfitPct");
    cfg.trailingStopPct         = requireDouble(j, "trailingStopPct");
    cfg.riskPerTradePct         = requireDouble(j, "riskPerTradePct");
    cfg.minHoldDays             = requireInt   (j, "minHoldDays");
    cfg.smaTrendPeriod          = requireInt   (j, "smaTrendPeriod");
    cfg.rsiSellOnlyIfRegimeDown = requireBool  (j, "rsiSellOnlyIfRegimeDown");
    cfg.regimeReentry           = requireBool  (j, "regimeReentry");

    // Garde-fous de coupure (C1) : objet REQUIS — le risque fait partie de la
    // config validée, pas des défauts silencieux du binaire.
    // Copie volontaire (objet minuscule) : gcc 13 émet un faux positif
    // -Wdangling-reference sur une référence retournée par requireField.
    const nlohmann::json ks = requireField(j, "killSwitch");
    if (!ks.is_object())
        throw std::runtime_error("Config : type invalide pour « killSwitch » "
                                 "(objet attendu)");
    cfg.killSwitch.maxDailyDrawdownPct  = requireDouble(ks, "maxDailyDrawdownPct");
    cfg.killSwitch.maxConsecutiveLosses = requireInt   (ks, "maxConsecutiveLosses");
    cfg.killSwitch.maxOrdersPerDay      = requireInt   (ks, "maxOrdersPerDay");

    borne(!cfg.symbol.empty(),          "« symbol » ne doit pas être vide");
    borne(cfg.emaFast   >= 1,           "« emaFast » doit être ≥ 1");
    borne(cfg.emaSlow   >= 1,           "« emaSlow » doit être ≥ 1");
    borne(cfg.emaFast < cfg.emaSlow,    "« emaFast » doit être < « emaSlow »");
    borne(cfg.rsiPeriod >= 1,           "« rsiPeriod » doit être ≥ 1");
    borne(cfg.rsiBuyMax > 0.0,          "« rsiBuyMax » doit être > 0");
    borne(cfg.rsiSellMin >= 0.0 && cfg.rsiSellMin <= 100.0,
          "« rsiSellMin » doit être dans [0, 100]");
    borne(cfg.stopLossPct     >= 0.0 && cfg.stopLossPct     < 1.0,
          "« stopLossPct » doit être dans [0, 1)");
    borne(cfg.takeProfitPct   >= 0.0 && cfg.takeProfitPct   < 1.0,
          "« takeProfitPct » doit être dans [0, 1)");
    borne(cfg.trailingStopPct >= 0.0 && cfg.trailingStopPct < 1.0,
          "« trailingStopPct » doit être dans [0, 1)");
    borne(cfg.riskPerTradePct > 0.0 && cfg.riskPerTradePct < 1.0,
          "« riskPerTradePct » doit être dans (0, 1)");
    borne(cfg.minHoldDays    >= 0,      "« minHoldDays » doit être ≥ 0");
    borne(cfg.smaTrendPeriod >= 0,      "« smaTrendPeriod » doit être ≥ 0");
    // Kill-switch : ≤ 0 = garde-fou désactivé (légitime, cf. Models.hpp),
    // mais un drawdown ≥ 100 % est forcément une faute de saisie.
    borne(cfg.killSwitch.maxDailyDrawdownPct < 1.0,
          "« killSwitch.maxDailyDrawdownPct » doit être < 1");

    return cfg;
}

} // namespace trading
