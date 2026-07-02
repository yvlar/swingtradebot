// ============================================================
//  test_config_loader_unit.cpp  —  Tests UNITAIRES
//  Cible : loadSwingConfigJson — chargement STRICT de la config
//  de production depuis un JSON validé (item 9.1, E1/D21)
// ============================================================
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include "strategies/ConfigLoader.hpp"

using namespace trading;
namespace fs = std::filesystem;

namespace {

// Fixture : un fichier JSON temporaire par test, supprimé au TearDown
class ConfigLoaderUnit : public ::testing::Test {
protected:
    std::string path_;

    void SetUp() override {
        path_ = "unit_config_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".json";
    }
    void TearDown() override {
        if (fs::exists(path_)) fs::remove(path_);
    }

    void write(const std::string& content) {
        std::ofstream f(path_);
        f << content;
    }

    // JSON complet et valide = les défauts actuels de SwingConfig
    static std::string validJson() {
        return R"({
            "symbol": "QQQ", "emaFast": 9, "emaSlow": 21, "rsiPeriod": 14,
            "rsiBuyMax": 100.0, "rsiSellMin": 70.0,
            "stopLossPct": 0.05, "takeProfitPct": 0.0, "trailingStopPct": 0.03,
            "riskPerTradePct": 0.02, "minHoldDays": 3, "smaTrendPeriod": 200,
            "rsiSellOnlyIfRegimeDown": true, "regimeReentry": true,
            "killSwitch": {
                "maxDailyDrawdownPct": 0.05,
                "maxConsecutiveLosses": 4,
                "maxOrdersPerDay": 10
            }
        })";
    }

    // Attend un runtime_error dont le message contient `fragment`
    void expectThrowContaining(const std::string& fragment) {
        try {
            loadSwingConfigJson(path_);
            FAIL() << "aucune exception levée (attendu : " << fragment << ")";
        } catch (const std::runtime_error& e) {
            EXPECT_NE(std::string(e.what()).find(fragment), std::string::npos)
                << "message réel : " << e.what();
        }
    }
};

} // namespace

// Les 14 champs sont relus à l'identique
TEST_F(ConfigLoaderUnit, ValidFileLoadsAllFields) {
    write(validJson());
    SwingConfig c = loadSwingConfigJson(path_);

    EXPECT_EQ(c.symbol, "QQQ");
    EXPECT_EQ(c.emaFast, 9);
    EXPECT_EQ(c.emaSlow, 21);
    EXPECT_EQ(c.rsiPeriod, 14);
    EXPECT_DOUBLE_EQ(c.rsiBuyMax,       100.0);
    EXPECT_DOUBLE_EQ(c.rsiSellMin,       70.0);
    EXPECT_DOUBLE_EQ(c.stopLossPct,      0.05);
    EXPECT_DOUBLE_EQ(c.takeProfitPct,    0.0);
    EXPECT_DOUBLE_EQ(c.trailingStopPct,  0.03);
    EXPECT_DOUBLE_EQ(c.riskPerTradePct,  0.02);
    EXPECT_EQ(c.minHoldDays, 3);
    EXPECT_EQ(c.smaTrendPeriod, 200);
    EXPECT_TRUE(c.rsiSellOnlyIfRegimeDown);
    EXPECT_TRUE(c.regimeReentry);
}

// Champ manquant : l'erreur NOMME le champ (échec bruyant, pas de défaut muet)
TEST_F(ConfigLoaderUnit, MissingFieldThrowsNamingIt) {
    write(R"({
        "symbol": "QQQ", "emaSlow": 21, "rsiPeriod": 14,
        "rsiBuyMax": 100.0, "rsiSellMin": 70.0,
        "stopLossPct": 0.05, "takeProfitPct": 0.0, "trailingStopPct": 0.03,
        "riskPerTradePct": 0.02, "minHoldDays": 3, "smaTrendPeriod": 200,
        "rsiSellOnlyIfRegimeDown": true, "regimeReentry": true
    })");                                            // emaFast absent
    expectThrowContaining("emaFast");
}

// Mauvais type ("9" au lieu de 9, et 9.5 pour un entier)
TEST_F(ConfigLoaderUnit, WrongTypeThrows) {
    std::string j = validJson();
    j.replace(j.find("\"emaFast\": 9"), 12, "\"emaFast\": \"9\"");
    write(j);
    expectThrowContaining("emaFast");

    std::string j2 = validJson();
    j2.replace(j2.find("\"minHoldDays\": 3"), 16, "\"minHoldDays\": 3.5");
    write(j2);
    expectThrowContaining("minHoldDays");
}

// Bornes : emaFast >= emaSlow et stopLossPct hors [0,1) sont rejetés
TEST_F(ConfigLoaderUnit, BoundViolationThrows) {
    std::string j = validJson();
    j.replace(j.find("\"emaFast\": 9"), 12, "\"emaFast\": 21");
    write(j);
    expectThrowContaining("emaFast");

    std::string j2 = validJson();
    j2.replace(j2.find("\"stopLossPct\": 0.05"), 19, "\"stopLossPct\": 1.5");
    write(j2);
    expectThrowContaining("stopLossPct");
}

// ── Kill-switch externalisé (C1) — le risque fait partie de la config ──

TEST_F(ConfigLoaderUnit, KillSwitchValuesAreLoaded) {
    write(validJson());
    SwingConfig c = loadSwingConfigJson(path_);
    EXPECT_DOUBLE_EQ(c.killSwitch.maxDailyDrawdownPct, 0.05);
    EXPECT_EQ(c.killSwitch.maxConsecutiveLosses, 4);
    EXPECT_EQ(c.killSwitch.maxOrdersPerDay, 10);
}

TEST_F(ConfigLoaderUnit, KillSwitchMissingObjectThrows) {
    write(R"({
        "symbol": "QQQ", "emaFast": 9, "emaSlow": 21, "rsiPeriod": 14,
        "rsiBuyMax": 100.0, "rsiSellMin": 70.0,
        "stopLossPct": 0.05, "takeProfitPct": 0.0, "trailingStopPct": 0.03,
        "riskPerTradePct": 0.02, "minHoldDays": 3, "smaTrendPeriod": 200,
        "rsiSellOnlyIfRegimeDown": true, "regimeReentry": true
    })");                                            // killSwitch absent
    expectThrowContaining("killSwitch");
}

TEST_F(ConfigLoaderUnit, KillSwitchMissingFieldThrowsNamingIt) {
    std::string j = validJson();
    j.replace(j.find("\"maxOrdersPerDay\": 10"), 21, "\"autreChose\": 10");
    write(j);
    expectThrowContaining("maxOrdersPerDay");
}

TEST_F(ConfigLoaderUnit, KillSwitchBoundViolationThrows) {
    std::string j = validJson();
    j.replace(j.find("\"maxDailyDrawdownPct\": 0.05"), 27,
              "\"maxDailyDrawdownPct\": 1.5");
    write(j);
    expectThrowContaining("maxDailyDrawdownPct");
}

TEST_F(ConfigLoaderUnit, MalformedJsonThrows) {
    write("{ pas du json ");
    expectThrowContaining("malformée");
}

TEST_F(ConfigLoaderUnit, MissingFileThrows) {
    // path_ n'existe pas (rien n'a été écrit)
    expectThrowContaining("introuvable");
}
