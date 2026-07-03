// ============================================================
//  test_swing_strategy_unit.cpp  —  Tests UNITAIRES
// ============================================================
#include <gtest/gtest.h>
#include <vector>
#include <memory>
#include "strategies/SwingStrategy.hpp"
#include "core/Interfaces.hpp"

using namespace trading;

namespace {

    Bar make_bar(int i, double close, long vol = 100000L) {
        return {"2024-" + std::to_string(i), close, close+0.5, close-0.5, close, vol};
    }

// Marché plat : prix strictement constant
    std::vector<Bar> flat(double price, int n = 60) {
        std::vector<Bar> bars;
        for (int i = 0; i < n; ++i)
            bars.push_back(make_bar(i, price));
        return bars;
    }

// Hausse soutenue longue → RSI > 70 → SELL garanti
    std::vector<Bar> strong_uptrend(int n = 80) {
        std::vector<Bar> bars;
        for (int i = 0; i < n; ++i)
            bars.push_back(make_bar(i, 100.0 + i * 2.0));
        return bars;
    }

// Baisse soutenue longue
    std::vector<Bar> strong_downtrend(int n = 80) {
        std::vector<Bar> bars;
        for (int i = 0; i < n; ++i)
            bars.push_back(make_bar(i, 300.0 - i * 2.0));
        return bars;
    }

// Croisement haussier à la dernière barre :
// N-1 barres plates puis 1 seule upbar → EMAfast croise EMAslow exactement à la fin
    std::vector<Bar> bullish_cross_at_last(int stable = 40) {
        std::vector<Bar> bars;
        for (int i = 0; i < stable; ++i)
            bars.push_back(make_bar(i, 100.0));
        // Une seule upbar forte → croisement garanti à la dernière barre
        bars.push_back(make_bar(stable, 160.0, 200000L));
        return bars;
    }

} // namespace

// ════════════════════════════════════════════════════════════
//  Création & configuration
// ════════════════════════════════════════════════════════════

TEST(SwingStrategyUnit, CreateSucceeds) {
EXPECT_NE(SwingStrategy::create(), nullptr);
}

TEST(SwingStrategyUnit, NameIsCorrect) {
EXPECT_EQ(SwingStrategy::create()->name(), "SwingStrategy_EMA_RSI");
}

TEST(SwingStrategyUnit, DefaultConfigValues) {
SwingConfig cfg;
EXPECT_EQ(cfg.emaFast,    9);
EXPECT_EQ(cfg.emaSlow,   21);
EXPECT_EQ(cfg.rsiPeriod, 14);
EXPECT_DOUBLE_EQ(cfg.rsiBuyMax,       100.0);  // ≥ 100 = plafond désactivé (item 8.3)
EXPECT_DOUBLE_EQ(cfg.rsiSellMin,      70.0);
EXPECT_DOUBLE_EQ(cfg.stopLossPct,     0.05);
EXPECT_DOUBLE_EQ(cfg.takeProfitPct,   0.0);   // ≤ 0 = désactivé (item 8.2)
EXPECT_DOUBLE_EQ(cfg.trailingStopPct, 0.03);
EXPECT_DOUBLE_EQ(cfg.riskPerTradePct, 0.02);
EXPECT_EQ(cfg.minHoldDays, 3);
EXPECT_EQ(cfg.smaTrendPeriod, 200);   // filtre de régime SMA200 (item 8.1)
EXPECT_TRUE(cfg.rsiSellOnlyIfRegimeDown);  // vente RSI gatée par le régime (item 8.4)
EXPECT_TRUE(cfg.regimeReentry);            // re-entrée sur régime (item 8.5)
}

TEST(SwingStrategyUnit, CustomConfigIsStored) {
SwingConfig cfg;
cfg.symbol  = "SPY";
cfg.emaFast = 5;
cfg.emaSlow = 15;
auto s = SwingStrategy::create(cfg);
EXPECT_EQ(s->config().symbol,  "SPY");
EXPECT_EQ(s->config().emaFast,   5);
EXPECT_EQ(s->config().emaSlow,  15);
}

// ════════════════════════════════════════════════════════════
//  Cas limites
// ════════════════════════════════════════════════════════════

TEST(SwingStrategyUnit, EmptyBarsReturnsHold) {
EXPECT_EQ(SwingStrategy::create()->evaluate({}).type, SignalType::HOLD);
}

TEST(SwingStrategyUnit, InsufficientBarsReturnsHold) {
EXPECT_EQ(SwingStrategy::create()->evaluate(flat(100.0, 10)).type, SignalType::HOLD);
}

TEST(SwingStrategyUnit, InsufficientBarsReasonMentionsDonnees) {
auto sig = SwingStrategy::create()->evaluate({});
EXPECT_NE(sig.reason.find("données"), std::string::npos);
}

TEST(SwingStrategyUnit, SignalTypeAlwaysValid) {
auto s = SwingStrategy::create();
for (auto& bars : {flat(100.0), strong_uptrend(), strong_downtrend()}) {
auto t = s->evaluate(bars).type;
EXPECT_TRUE(t == SignalType::BUY  ||
t == SignalType::SELL ||
t == SignalType::HOLD);
}
}

TEST(SwingStrategyUnit, SignalPriceIsLastClose) {
auto bars = strong_uptrend();
EXPECT_DOUBLE_EQ(SwingStrategy::create()->evaluate(bars).price,
bars.back().close);
}

TEST(SwingStrategyUnit, SignalTimestampIsLastBar) {
auto bars = strong_uptrend();
EXPECT_EQ(SwingStrategy::create()->evaluate(bars).timestamp,
bars.back().date);
}

TEST(SwingStrategyUnit, SignalSymbolMatchesConfig) {
SwingConfig cfg; cfg.symbol = "SPY";
EXPECT_EQ(SwingStrategy::create(cfg)->evaluate(flat(100.0)).symbol, "SPY");
}

TEST(SwingStrategyUnit, SignalReasonIsNotEmpty) {
EXPECT_FALSE(SwingStrategy::create()->evaluate(strong_uptrend()).reason.empty());
}

TEST(SwingStrategyUnit, EvaluateIsDeterministic) {
auto s    = SwingStrategy::create();
auto bars = strong_uptrend();
auto s1   = s->evaluate(bars);
auto s2   = s->evaluate(bars);
EXPECT_EQ(s1.type,   s2.type);
EXPECT_EQ(s1.price,  s2.price);
EXPECT_EQ(s1.reason, s2.reason);
}

// ════════════════════════════════════════════════════════════
//  Logique métier
// ════════════════════════════════════════════════════════════

// RSI neutre sur marché plat (fix: toRSI(0,0) = 50, pas 100)
TEST(SwingStrategyUnit, FlatMarketReturnsHold) {
EXPECT_EQ(SwingStrategy::create()->evaluate(flat(100.0, 60)).type,
SignalType::HOLD);
}

// Hausse forte → RSI > rsiSellMin(70) → SELL
TEST(SwingStrategyUnit, StrongUptrendReturnsSell) {
EXPECT_EQ(SwingStrategy::create()->evaluate(strong_uptrend(80)).type,
SignalType::SELL);
}

// Baisse forte → pas de BUY
TEST(SwingStrategyUnit, StrongDowntrendDoesNotReturnBuy) {
EXPECT_NE(SwingStrategy::create()->evaluate(strong_downtrend(80)).type,
SignalType::BUY);
}

// Croisement haussier à la dernière barre + RSI permissif + régime haussier → BUY
TEST(SwingStrategyUnit, BullishCrossoverReturnsBuyWhenRsiPermissive) {
SwingConfig cfg;
cfg.rsiBuyMax  = 101.0;  // RSI toujours acceptable
cfg.rsiSellMin = 101.0;  // jamais de SELL via RSI
cfg.smaTrendPeriod = 5;  // régime calculable sur série courte (prix 160 > SMA5)
EXPECT_EQ(SwingStrategy::create(cfg)->evaluate(bullish_cross_at_last(40)).type,
SignalType::BUY);
}

// Filtre de régime (item 8.1) : un historique trop court pour la SMA de fond
// empêche toute entrée — la SMA200 ne peut pas être confirmée sur 41 barres.
TEST(SwingStrategyUnit, RegimeFilterBlocksBuyWhenSmaUncomputable) {
SwingConfig cfg;
cfg.rsiBuyMax  = 101.0;
cfg.rsiSellMin = 101.0;
// smaTrendPeriod par défaut = 200 > 41 barres → SMA vide → pas de BUY
EXPECT_NE(SwingStrategy::create(cfg)->evaluate(bullish_cross_at_last(40)).type,
SignalType::BUY);
}

// B3.2 : historique < smaTrendPeriod → le HOLD doit DIRE pourquoi (« régime
// inconnu, N/200 barres »), pas la raison générique « RSI=… ». Sans cela, un
// feed trop court laissait le bot en HOLD éternel sans diagnostic possible.
TEST(SwingStrategyUnit, InsufficientHistoryHoldReasonIsExplicit) {
SwingConfig cfg;                     // smaTrendPeriod = 200 par défaut
std::vector<Bar> bars;
for (int i = 0; i < 60; ++i)         // baisse douce : RSI bas, aucun signal
bars.push_back(make_bar(i, 200.0 - i * 0.1));
auto sig = SwingStrategy::create(cfg)->evaluate(bars);
EXPECT_EQ(sig.type, SignalType::HOLD);
EXPECT_NE(sig.reason.find("historique insuffisant"), std::string::npos)
    << "raison réelle : " << sig.reason;
EXPECT_NE(sig.reason.find("60/200"), std::string::npos)
    << "raison réelle : " << sig.reason;
}

// La garde d'historique ne doit JAMAIS bloquer une sortie : la SMA de fond ne
// gate que les achats (une position ouverte doit toujours pouvoir se vendre).
TEST(SwingStrategyUnit, SellStillFiresWithInsufficientSmaHistory) {
SwingConfig cfg;                     // smaTrendPeriod = 200 > 60 barres
EXPECT_EQ(SwingStrategy::create(cfg)->evaluate(strong_uptrend(60)).type,
SignalType::SELL);                   // RSI > 70, régime non confirmé → vente RSI
}

// rsiSellMin très bas → SELL rapide
TEST(SwingStrategyUnit, LowRsiSellMinTriggersSellEarly) {
SwingConfig cfg; cfg.rsiSellMin = 30.0;
std::vector<Bar> bars;
for (int i = 0; i < 60; ++i)
bars.push_back(make_bar(i, 100.0 + i * 1.0));
EXPECT_EQ(SwingStrategy::create(cfg)->evaluate(bars).type, SignalType::SELL);
}

// rsiBuyMax très bas → BUY impossible
TEST(SwingStrategyUnit, VeryLowRsiBuyMaxBlocksBuy) {
SwingConfig cfg; cfg.rsiBuyMax = 1.0;
EXPECT_NE(SwingStrategy::create(cfg)->evaluate(strong_uptrend(80)).type,
SignalType::BUY);
}

TEST(SwingStrategyUnit, LargeDatasetNoThrow) {
auto s = SwingStrategy::create();
std::vector<Bar> bars;
for (int i = 0; i < 500; ++i)
bars.push_back(make_bar(i, 100.0 + i * 0.1));
EXPECT_NO_THROW(s->evaluate(bars));
}
// ════════════════════════════════════════════════════════════
//  Compléments de couverture — conversion RiskConfig,
//  garde « indicateurs vides »
// ════════════════════════════════════════════════════════════

// Item 12 : les composition roots passent une SwingConfig au bot via cette
// conversion — le bot ne dépend jamais de la stratégie
TEST(SwingStrategyUnit, SwingConfigConvertsToRiskConfig) {
    SwingConfig cfg;
    cfg.symbol          = "SPY";
    cfg.stopLossPct     = 0.04;
    cfg.takeProfitPct   = 0.12;
    cfg.trailingStopPct = 0.02;
    cfg.riskPerTradePct = 0.01;
    cfg.minHoldDays     = 5;
    cfg.exitOnLowestLowN = 20;   // item 8s.1 : la sortie structurelle suit

    RiskConfig r = cfg;
    EXPECT_EQ(r.symbol, "SPY");
    EXPECT_DOUBLE_EQ(r.stopLossPct,     0.04);
    EXPECT_DOUBLE_EQ(r.takeProfitPct,   0.12);
    EXPECT_DOUBLE_EQ(r.trailingStopPct, 0.02);
    EXPECT_DOUBLE_EQ(r.riskPerTradePct, 0.01);
    EXPECT_EQ(r.minHoldDays, 5);
    EXPECT_EQ(r.exitOnLowestLowN, 20);
}

namespace {

// Indicateur qui ne produit jamais rien — simule un échec de calcul
class EmptyIndicator final : public IIndicator<double> {
public:
    std::vector<double> compute(const std::vector<double>&) const override { return {}; }
    std::string name() const override { return "Empty"; }
};

// Indicateur qui renvoie une série préfixée — contrôle déterministe des valeurs
// d'EMA/RSI/SMA injectées (isole la logique de signal des calculs réels)
class ValueIndicator final : public IIndicator<double> {
public:
    explicit ValueIndicator(std::vector<double> v) : v_(std::move(v)) {}
    std::vector<double> compute(const std::vector<double>&) const override { return v_; }
    std::string name() const override { return "Value"; }
private:
    std::vector<double> v_;
};

} // namespace

// Filtre de régime (item 8.1) isolé : à croisement haussier et RSI permissif
// IDENTIQUES, seul le régime (prix vs SMA) bascule la décision.
TEST(SwingStrategyUnit, RegimeFilterGatesBuyOnPriceVsSma) {
    const size_t n = 30;
    std::vector<double> emaSlow(n, 100.0);
    std::vector<double> emaFast(n, 99.0); emaFast[n-1] = 105.0;  // croisement BULLISH au dernier index
    std::vector<double> rsi(n, 50.0);                            // < rsiBuyMax (55)
    std::vector<Bar> bars;
    for (size_t i = 0; i < n; ++i) bars.push_back(make_bar((int)i, 110.0)); // lastClose 110 > 105

    auto build = [&](double smaLast) {
        std::vector<double> sma(n, 100.0); sma[n-1] = smaLast;
        return SwingStrategy(SwingConfig{},
            std::make_unique<ValueIndicator>(emaFast),
            std::make_unique<ValueIndicator>(emaSlow),
            std::make_unique<ValueIndicator>(rsi),
            std::make_unique<ValueIndicator>(sma));
    };

    // Régime haussier (prix 110 > SMA 100) → BUY
    EXPECT_EQ(build(100.0).evaluate(bars).type, SignalType::BUY);
    // Régime baissier (prix 110 < SMA 120) → entrée bloquée → HOLD
    EXPECT_EQ(build(120.0).evaluate(bars).type, SignalType::HOLD);
}

// ════════════════════════════════════════════════════════════
//  Entrée sur la force (item 8.3, D26/T3)
// ════════════════════════════════════════════════════════════
// Exiger un croisement haussier (momentum ↑) ET RSI < 55 (peu de momentum)
// s'auto-annulait : on ratait précisément les breakouts forts. Convention :
// rsiBuyMax ≥ 100 = plafond désactivé (RSI == 100.0 est atteignable sur une
// série de gains purs, donc `lastRsi < 100.0` ne suffit PAS).

// Croisement haussier + régime up + RSI épinglé à 100.0 exactement :
// plafond désactivé (rsiBuyMax = 100) → l'entrée doit passer.
TEST(SwingStrategyUnit, RsiBuyCapDisabledAllowsEntryAtMaxRsi) {
    const size_t n = 30;
    std::vector<double> emaSlow(n, 100.0);
    std::vector<double> emaFast(n, 99.0); emaFast[n-1] = 105.0;  // croisement BULLISH
    std::vector<double> rsi(n, 100.0);                           // force maximale
    std::vector<double> sma(n, 100.0);                           // prix 110 > SMA → régime up
    std::vector<Bar> bars;
    for (size_t i = 0; i < n; ++i) bars.push_back(make_bar((int)i, 110.0));

    SwingConfig cfg; cfg.rsiBuyMax = 100.0;  // ≥ 100 = désactivé
    SwingStrategy strat(cfg,
        std::make_unique<ValueIndicator>(emaFast),
        std::make_unique<ValueIndicator>(emaSlow),
        std::make_unique<ValueIndicator>(rsi),
        std::make_unique<ValueIndicator>(sma));
    EXPECT_EQ(strat.evaluate(bars).type, SignalType::BUY);
}

// ════════════════════════════════════════════════════════════
//  Vente RSI gatée par le régime (item 8.4, D26/T2)
// ════════════════════════════════════════════════════════════
// Sur QQQ, un RSI > 70 en tendance haussière de fond est historiquement un
// signal de FORCE, pas de retournement : vendre à chaque fois sortait des
// tendances au pire moment. Flag rsiSellOnlyIfRegimeDown : la vente sur RSI
// seul n'a lieu que si le régime n'est PAS haussier confirmé. Flag
// INDÉPENDANT de smaTrendPeriod : la base A/B 8.1 (smaTrendPeriod=1 →
// régime toujours « up ») ne doit pas voir ses ventes supprimées à son insu.

namespace {

// Harnais 8.4 : pas de croisement (EMA rapide sous la lente), RSI épinglé à
// 80 (> rsiSellMin 70), prix 110 ; seul le régime (SMA injectée) et le flag
// varient.
Signal evalRsiSellCase(bool flagOn, double smaLast,
                       CrossoverDetector::Cross crossWanted
                           = CrossoverDetector::Cross::NONE) {
    const size_t n = 30;
    std::vector<double> emaSlow(n, 100.0);
    // Par défaut aucun croisement ; BEARISH = passage au-dessus → en dessous.
    std::vector<double> emaFast(n, 95.0);
    if (crossWanted == CrossoverDetector::Cross::BEARISH) {
        for (size_t i = 0; i + 1 < n; ++i) emaFast[i] = 105.0;
        emaFast[n-1] = 95.0;
    }
    std::vector<double> rsi(n, 80.0);      // suracheté
    std::vector<double> sma(n, smaLast);
    std::vector<Bar> bars;
    for (size_t i = 0; i < n; ++i) bars.push_back(make_bar((int)i, 110.0));

    SwingConfig cfg;
    cfg.rsiSellOnlyIfRegimeDown = flagOn;
    cfg.regimeReentry           = false;   // isole le gate 8.4 de la re-entrée 8.5
    SwingStrategy strat(cfg,
        std::make_unique<ValueIndicator>(emaFast),
        std::make_unique<ValueIndicator>(emaSlow),
        std::make_unique<ValueIndicator>(rsi),
        std::make_unique<ValueIndicator>(sma));
    return strat.evaluate(bars);
}

} // namespace

// Flag actif + régime haussier (prix 110 > SMA 100) : le RSI seul ne vend
// plus — HOLD (on reste avec la tendance).
TEST(SwingStrategyUnit, RsiSellSuppressedInUpRegimeWhenGated) {
    EXPECT_EQ(evalRsiSellCase(true, 100.0).type, SignalType::HOLD);
}

// Flag actif + régime NON haussier (prix 110 < SMA 120) : la vente RSI
// reste active — le gate ne protège que les tendances confirmées.
TEST(SwingStrategyUnit, RsiSellStillFiresInDownRegimeWhenGated) {
    EXPECT_EQ(evalRsiSellCase(true, 120.0).type, SignalType::SELL);
}

// Flag inactif (défaut historique) + régime haussier : comportement inchangé,
// RSI > 70 vend.
TEST(SwingStrategyUnit, RsiSellUnchangedWhenGateDisabled) {
    EXPECT_EQ(evalRsiSellCase(false, 100.0).type, SignalType::SELL);
}

// Le croisement BAISSIER vend TOUJOURS, régime haussier ou pas : le gate ne
// concerne que la vente « RSI seul ».
TEST(SwingStrategyUnit, BearishCrossStillSellsInUpRegimeWhenGated) {
    EXPECT_EQ(evalRsiSellCase(true, 100.0,
                              CrossoverDetector::Cross::BEARISH).type,
              SignalType::SELL);
}

// ════════════════════════════════════════════════════════════
//  Re-entrée sur régime (item 8.5, T4 — cash drag)
// ════════════════════════════════════════════════════════════
// Le croisement EMA n'arrive qu'une fois par cycle de tendance : après une
// sortie sur trailing, le bot restait en cash pendant TOUT le reste de la
// tendance (temps investi ~2 %). Flag regimeReentry : à plat, régime
// haussier confirmé (prix > SMA de fond) et prix au-dessus des deux EMA →
// BUY sans attendre un nouveau croisement. La stratégie est sans état :
// le BUY répété est correct (TradingBot ignore un BUY en position). Les
// signaux de VENTE gardent la priorité (bloc évalué avant la re-entrée).

namespace {

// Harnais 8.5 : EMA rapide AU-DESSUS de la lente sans croisement (tendance
// déjà installée), prix 110 au-dessus des deux EMA et de la SMA (sauf cas
// contraire), RSI paramétrable.
Signal evalReentryCase(bool flagOn, double smaLast, double rsiLast = 50.0,
                       bool bearishCross = false, bool gateRsiSell = true) {
    const size_t n = 30;
    std::vector<double> emaSlow(n, 100.0);
    std::vector<double> emaFast(n, 105.0);       // au-dessus, sans croisement
    if (bearishCross) emaFast[n-1] = 95.0;       // passage au-dessus → dessous
    std::vector<double> rsi(n, rsiLast);
    std::vector<double> sma(n, smaLast);
    std::vector<Bar> bars;
    for (size_t i = 0; i < n; ++i) bars.push_back(make_bar((int)i, 110.0));

    SwingConfig cfg;
    cfg.regimeReentry           = flagOn;
    cfg.rsiSellOnlyIfRegimeDown = gateRsiSell;
    SwingStrategy strat(cfg,
        std::make_unique<ValueIndicator>(emaFast),
        std::make_unique<ValueIndicator>(emaSlow),
        std::make_unique<ValueIndicator>(rsi),
        std::make_unique<ValueIndicator>(sma));
    return strat.evaluate(bars);
}

} // namespace

// Flag actif + régime up (110 > SMA 100) + prix > EMAs, PAS de croisement :
// re-entrée → BUY, avec une raison explicite.
TEST(SwingStrategyUnit, RegimeReentryBuysWithoutCrossover) {
    auto sig = evalReentryCase(true, 100.0);
    EXPECT_EQ(sig.type, SignalType::BUY);
    EXPECT_NE(sig.reason.find("Re-entr"), std::string::npos);
}

// Flag inactif : comportement historique — sans croisement, HOLD.
TEST(SwingStrategyUnit, NoReentryWhenFlagDisabled) {
    EXPECT_EQ(evalReentryCase(false, 100.0).type, SignalType::HOLD);
}

// Flag actif mais régime NON haussier (110 < SMA 120) : pas de re-entrée.
TEST(SwingStrategyUnit, NoReentryWhenRegimeDown) {
    EXPECT_EQ(evalReentryCase(true, 120.0).type, SignalType::HOLD);
}

// Priorité des sorties : croisement baissier + flag actif → SELL, jamais
// masqué par la re-entrée.
TEST(SwingStrategyUnit, BearishCrossOutranksReentry) {
    EXPECT_EQ(evalReentryCase(true, 100.0, 50.0, /*bearishCross=*/true).type,
              SignalType::SELL);
}

// Priorité des sorties (bis) : RSI 80 avec vente RSI NON gatée (flag 8.4
// off) → SELL — la re-entrée ne masque pas la vente RSI quand elle est active.
TEST(SwingStrategyUnit, RsiSellOutranksReentryWhenUngated) {
    EXPECT_EQ(evalReentryCase(true, 100.0, 80.0, false,
                              /*gateRsiSell=*/false).type,
              SignalType::SELL);
}

// Compatibilité : un plafond ACTIF (rsiBuyMax = 55) bloque toujours une
// entrée à RSI 60 — le comportement historique reste accessible par config.
TEST(SwingStrategyUnit, ActiveRsiBuyCapStillBlocksEntry) {
    const size_t n = 30;
    std::vector<double> emaSlow(n, 100.0);
    std::vector<double> emaFast(n, 99.0); emaFast[n-1] = 105.0;
    std::vector<double> rsi(n, 60.0);                            // > 55
    std::vector<double> sma(n, 100.0);
    std::vector<Bar> bars;
    for (size_t i = 0; i < n; ++i) bars.push_back(make_bar((int)i, 110.0));

    SwingConfig cfg; cfg.rsiBuyMax = 55.0; cfg.rsiSellMin = 101.0;
    cfg.regimeReentry = false;   // isole le plafond 8.3 de la re-entrée 8.5
    SwingStrategy strat(cfg,
        std::make_unique<ValueIndicator>(emaFast),
        std::make_unique<ValueIndicator>(emaSlow),
        std::make_unique<ValueIndicator>(rsi),
        std::make_unique<ValueIndicator>(sma));
    EXPECT_EQ(strat.evaluate(bars).type, SignalType::HOLD);
}

// La conversion RiskConfig aligne le lookback sur la période de régime (item 8.1) :
// le bot doit recevoir assez de barres pour calculer la SMA de fond.
TEST(SwingStrategyUnit, RiskConfigLookbackCoversTrendPeriod) {
    SwingConfig cfg;                       // smaTrendPeriod = 200 par défaut
    RiskConfig r = cfg;
    EXPECT_EQ(r.lookback, 230);            // max(60, 200 + 30)

    SwingConfig small; small.smaTrendPeriod = 10;
    RiskConfig rs = small;
    EXPECT_EQ(rs.lookback, 60);            // plancher historique
}

// ════════════════════════════════════════════════════════════
//  Entrée breakout « plus haut de M jours » (item 8s.2)
// ════════════════════════════════════════════════════════════
// Hypothèse T3 prolongée (Sprint 8-quinquies) : entrer sur la FORCE
// démontrée — clôture au-dessus du plus haut des M barres PRÉCÉDENTES
// (barre courante exclue) en régime haussier confirmé — plutôt que sur le
// seul état « régime up + prix > EMAs » (re-entrée 8.5). Évaluée APRÈS le
// bloc VENTE (les sorties gardent la priorité) ; flag entryBreakoutM
// INDÉPENDANT de regimeReentry (les deux peuvent coexister — l'A/B des
// deux hypothèses « remplace / s'ajoute » est jugé en 8s.3, décision
// utilisateur 2026-07-03). ≤ 0 = désactivé (comportement historique).

namespace {

// Harnais 8s.2 : pas de croisement (EMA rapide au-dessus de la lente),
// RSI neutre, régime piloté par la SMA injectée. Les 29 premières barres
// clôturent à 100 (high 100,5) SAUF le plus haut de la fenêtre, injecté sur
// l'avant-dernière barre ; la barre COURANTE clôture à `lastClose` avec un
// high paramétrable (pour verrouiller son exclusion du calcul).
Signal evalBreakoutCase(int m, double smaLast, double priorHigh,
                        double lastClose, double lastHigh = 0.0,
                        bool reentryOn = false, bool bearishCross = false) {
    const size_t n = 30;
    std::vector<double> emaSlow(n, 100.0);
    std::vector<double> emaFast(n, 105.0);       // au-dessus, sans croisement
    if (bearishCross) emaFast[n-1] = 95.0;       // passage au-dessus → dessous
    std::vector<double> rsi(n, 50.0);
    std::vector<double> sma(n, smaLast);

    std::vector<Bar> bars;
    for (size_t i = 0; i + 1 < n; ++i)
        bars.push_back(make_bar((int)i, 100.0)); // high = 100,5
    bars[n-2].high = priorHigh;                  // le plus haut des M précédentes
    Bar courante = make_bar((int)n - 1, lastClose);
    if (lastHigh > 0.0) courante.high = lastHigh;
    bars.push_back(courante);

    SwingConfig cfg;
    cfg.entryBreakoutM = m;
    cfg.regimeReentry  = reentryOn;
    SwingStrategy strat(cfg,
        std::make_unique<ValueIndicator>(emaFast),
        std::make_unique<ValueIndicator>(emaSlow),
        std::make_unique<ValueIndicator>(rsi),
        std::make_unique<ValueIndicator>(sma));
    return strat.evaluate(bars);
}

} // namespace

// Cas central : régime up (110 > SMA 100), clôture 110 > plus haut des 5
// précédentes (105), PAS de croisement, re-entrée OFF → BUY sur breakout,
// avec une raison explicite.
TEST(SwingStrategyUnit, BreakoutBuysWithoutCrossover) {
    auto sig = evalBreakoutCase(5, 100.0, 105.0, 110.0);
    EXPECT_EQ(sig.type, SignalType::BUY);
    EXPECT_NE(sig.reason.find("reakout"), std::string::npos)
        << "raison réelle : " << sig.reason;
}

// La barre COURANTE est exclue du plus haut : son high (200) dépasse la
// clôture 110 — si elle comptait, 110 > 200 serait faux et rien ne sortirait.
// Le plus haut des M précédentes (105) fait foi.
TEST(SwingStrategyUnit, BreakoutExcludesCurrentBarHigh) {
    EXPECT_EQ(evalBreakoutCase(5, 100.0, 105.0, 110.0,
                               /*lastHigh=*/200.0).type,
              SignalType::BUY);
}

// Borne stricte : clôture ÉGALE au plus haut des M précédentes (110 = 110)
// n'est PAS un breakout — il faut clôturer AU-DESSUS.
TEST(SwingStrategyUnit, NoBreakoutWhenCloseEqualsPriorHigh) {
    EXPECT_EQ(evalBreakoutCase(5, 100.0, 110.0, 110.0).type,
              SignalType::HOLD);
}

// Régime NON haussier (110 < SMA 120) : pas d'entrée breakout — le filtre
// de régime (8.1) gate toutes les familles d'entrée.
TEST(SwingStrategyUnit, NoBreakoutWhenRegimeDown) {
    EXPECT_EQ(evalBreakoutCase(5, 120.0, 105.0, 110.0).type,
              SignalType::HOLD);
}

// M=0 → désactivé : comportement historique (HOLD, re-entrée off).
TEST(SwingStrategyUnit, BreakoutDisabledWhenMZero) {
    EXPECT_EQ(evalBreakoutCase(0, 100.0, 105.0, 110.0).type,
              SignalType::HOLD);
}

// Fenêtre trop courte : M=50 exige M+1 barres, il n'y en a que 30 → pas de
// jugement de breakout (et pas d'accès hors bornes).
TEST(SwingStrategyUnit, BreakoutNeedsMPlusOneBars) {
    EXPECT_EQ(evalBreakoutCase(50, 100.0, 105.0, 110.0).type,
              SignalType::HOLD);
}

// Priorité des sorties : croisement baissier + breakout vrai simultanément
// → SELL, jamais masqué par l'entrée (bloc VENTE évalué avant, comme 8.5).
TEST(SwingStrategyUnit, BearishCrossOutranksBreakout) {
    EXPECT_EQ(evalBreakoutCase(5, 100.0, 105.0, 110.0, 0.0,
                               /*reentryOn=*/false,
                               /*bearishCross=*/true).type,
              SignalType::SELL);
}

// Coexistence avec la re-entrée (hypothèse « s'ajoute ») : breakout NON
// déclenché (clôture 110 < plus haut 120) mais régime up et prix > EMAs →
// la re-entrée 8.5 achète toujours — le flag breakout ne la supprime pas.
TEST(SwingStrategyUnit, ReentryStillBuysWhenBreakoutNotTriggered) {
    auto sig = evalBreakoutCase(5, 100.0, 120.0, 110.0, 0.0,
                                /*reentryOn=*/true);
    EXPECT_EQ(sig.type, SignalType::BUY);
    EXPECT_NE(sig.reason.find("Re-entr"), std::string::npos)
        << "raison réelle : " << sig.reason;
}

// Indicateurs injectés défaillants : HOLD explicite, jamais de crash
TEST(SwingStrategyUnit, FailedIndicatorComputationReturnsHold) {
    SwingStrategy strat(SwingConfig{},
                        std::make_unique<EmptyIndicator>(),
                        std::make_unique<EmptyIndicator>(),
                        std::make_unique<EmptyIndicator>(),
                        std::make_unique<EmptyIndicator>());

    auto sig = strat.evaluate(flat(400.0, 60));
    EXPECT_TRUE(sig.isHold());
    EXPECT_NE(sig.reason.find("indicateurs"), std::string::npos);
}
