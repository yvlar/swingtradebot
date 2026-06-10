#include "backtest/BackTester.hpp"
#include "bot/Logger.hpp"
#include "strategies/SwingStrategy.hpp"
#include <iostream>
#include <string>

// ─── Composition Root ─────────────────────────────────────────────────────────
// Point d'entrée du programme.
// Lance le backtester sur QQQ.csv et affiche le rapport de performance.

int main(int argc, char* argv[]) {

    std::cout << R"(
╔══════════════════════════════════════════════════════════╗
║         QQQ SWING BOT — C++20 │ Backtester              ║
╚══════════════════════════════════════════════════════════╝
)" << '\n';

    // ─── Chemin vers le fichier CSV ───────────────────────────────────────────
    // Place QQQ.csv à la racine du projet (même dossier que main.cpp)
    std::string csvPath = "QQQ.csv";
    if (argc > 1) csvPath = argv[1];  // ou passe le chemin en argument

    // ─── Configuration de la stratégie ───────────────────────────────────────
    trading::SwingConfig cfg;
    cfg.symbol           = "QQQ";
    cfg.emaFast         = 13;
    cfg.emaSlow         = 21;
    cfg.rsiPeriod       = 14;
    cfg.rsiBuyMax       = 65.0;   // était 55 → plus de signaux d'achat
    cfg.rsiSellMin      = 80.0;   // était 70 → laisse les gagnants courir
    cfg.stopLossPct     = 0.07;   // était 0.03 → stop moins serré, moins de faux signaux
    cfg.takeProfitPct   = 0.15;   // était 0.10 → objectif plus ambitieux
    cfg.trailingStopPct = 0.03;
    cfg.riskPerTradePct = 0.02;
    cfg.minHoldDays     = 2;  // Garde au moins 3 jours

    // ─── Lancement du backtest ────────────────────────────────────────────────
    try {
        trading::Backtester bt(
            cfg,
            csvPath,
            1000.0,   // capital initial: $10,000
            0.001       // commission: 0.1% par trade (réaliste)
        );

        std::cout << "Chargement de " << csvPath << "...\n";
        auto result = bt.run();
        bt.printReport(result);

    } catch (const std::exception& e) {
        std::cerr << "\n❌ Erreur: " << e.what() << "\n";
        std::cerr << "   Vérifie que QQQ.csv est dans le bon dossier.\n\n";
        return 1;
    }

    return 0;
}