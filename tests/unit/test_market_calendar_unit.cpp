// ============================================================
//  test_market_calendar_unit.cpp  —  Tests UNITAIRES
//  Cible : market_calendar.h — heures de marché US en UTC + DST
//
//  Item 20 : l'ancien repli codait UTC-5 en dur (faux 8 mois/an
//  en heure d'été EDT=UTC-4) et ouvrait dès 9h00 au lieu de 9h30.
// ============================================================
#include <gtest/gtest.h>
#include <ctime>
#include "core/market_calendar.h"

using namespace trading;

namespace {

// Construit un time_t à partir d'une date/heure UTC (sans dépendre du
// fuseau local de la machine de test — timegm n'est pas portable, on
// passe par les champs et un calcul direct via une référence connue).
std::time_t utcInstant(int y, int mon, int d, int h, int mi) {
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon  = mon - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min  = mi;
    tm.tm_sec  = 0;
#ifdef _WIN32
    return _mkgmtime(&tm);
#else
    return timegm(&tm);
#endif
}

// Reproduction de l'ANCIEN calcul (UTC-5 fixe, ouverture 9h00) pour
// prouver qu'il divergeait du bon résultat en été.
bool legacyFixedOffsetOpen(std::time_t t) {
    std::tm tm{};
    gmtime_r(&t, &tm);
    int hour_est = (tm.tm_hour - 5 + 24) % 24;
    int wday     = tm.tm_wday;
    if (wday == 0 || wday == 6) return false;
    return (hour_est >= 9 && (hour_est < 16 || (hour_est == 9 && tm.tm_min >= 30)));
}

} // namespace

// ════════════════════════════════════════════════════════════
//  Bascule DST — 2e dimanche de mars … 1er dimanche de novembre
// ════════════════════════════════════════════════════════════

TEST(MarketCalendarUnit, DstBoundariesFor2024) {
    // 2024 : 2e dimanche de mars = 10, 1er dimanche de novembre = 3
    EXPECT_EQ(nthSundayOfMonth(2024, 3, 2),  10);
    EXPECT_EQ(nthSundayOfMonth(2024, 11, 1),  3);

    EXPECT_FALSE(isUsEasternDst(2024, 1, 15));   // janvier : EST
    EXPECT_FALSE(isUsEasternDst(2024, 3, 9));    // veille de bascule : EST
    EXPECT_TRUE (isUsEasternDst(2024, 3, 10));   // 2e dimanche mars : EDT
    EXPECT_TRUE (isUsEasternDst(2024, 7, 1));    // plein été : EDT
    EXPECT_TRUE (isUsEasternDst(2024, 11, 2));   // veille de fin : EDT
    EXPECT_FALSE(isUsEasternDst(2024, 11, 3));   // 1er dimanche nov : EST
    EXPECT_FALSE(isUsEasternDst(2024, 12, 25));  // décembre : EST
}

TEST(MarketCalendarUnit, OffsetIsFourInSummerFiveInWinter) {
    EXPECT_EQ(usEasternUtcOffsetHours(2024, 7, 1), 4);   // EDT
    EXPECT_EQ(usEasternUtcOffsetHours(2024, 1, 2), 5);   // EST
}

// ════════════════════════════════════════════════════════════
//  Heures d'ouverture — hiver (EST = UTC-5)
// ════════════════════════════════════════════════════════════

TEST(MarketCalendarUnit, WinterSessionOpensAt1430Utc) {
    // 2024-01-02 (mardi) : ouverture 9h30 EST = 14h30 UTC
    EXPECT_FALSE(isUsEquityMarketOpenUtc(utcInstant(2024, 1, 2, 14, 29)));
    EXPECT_TRUE (isUsEquityMarketOpenUtc(utcInstant(2024, 1, 2, 14, 30)));
    EXPECT_TRUE (isUsEquityMarketOpenUtc(utcInstant(2024, 1, 2, 20, 59)));
    // Clôture 16h00 EST = 21h00 UTC (borne exclusive)
    EXPECT_FALSE(isUsEquityMarketOpenUtc(utcInstant(2024, 1, 2, 21, 0)));
}

// ════════════════════════════════════════════════════════════
//  Heures d'ouverture — été (EDT = UTC-4) : le cas que l'ancien
//  calcul UTC-5 fixe se trompait
// ════════════════════════════════════════════════════════════

TEST(MarketCalendarUnit, SummerSessionOpensAt1330Utc) {
    // 2024-07-01 (lundi) : ouverture 9h30 EDT = 13h30 UTC
    EXPECT_FALSE(isUsEquityMarketOpenUtc(utcInstant(2024, 7, 1, 13, 29)));
    EXPECT_TRUE (isUsEquityMarketOpenUtc(utcInstant(2024, 7, 1, 13, 30)));
    EXPECT_TRUE (isUsEquityMarketOpenUtc(utcInstant(2024, 7, 1, 19, 59)));
    // Clôture 16h00 EDT = 20h00 UTC
    EXPECT_FALSE(isUsEquityMarketOpenUtc(utcInstant(2024, 7, 1, 20, 0)));
}

// PREUVE du bug corrigé (acceptation item 20) : à 13h30 UTC un jour d'été,
// le marché est OUVERT (9h30 EDT) — mais l'ancien calcul UTC-5 le voyait
// à 8h30 « EST » et le déclarait FERMÉ.
TEST(MarketCalendarUnit, SummerOpenWasMisjudgedByLegacyFixedOffset) {
    std::time_t t = utcInstant(2024, 7, 1, 13, 30);   // lundi, 9h30 EDT
    EXPECT_TRUE (isUsEquityMarketOpenUtc(t));          // correct : ouvert
    EXPECT_FALSE(legacyFixedOffsetOpen(t));            // ancien bug : fermé
}

// Symétrique à la clôture : 20h00 UTC en été = 16h00 EDT (fermé), mais
// l'ancien calcul voyait 15h00 « EST » et laissait le marché OUVERT.
TEST(MarketCalendarUnit, SummerCloseWasMisjudgedByLegacyFixedOffset) {
    std::time_t t = utcInstant(2024, 7, 1, 20, 0);    // lundi, 16h00 EDT
    EXPECT_FALSE(isUsEquityMarketOpenUtc(t));          // correct : fermé
    EXPECT_TRUE (legacyFixedOffsetOpen(t));            // ancien bug : ouvert
}

// ════════════════════════════════════════════════════════════
//  Week-end et 9h00-9h30 (l'ancien code ouvrait dès 9h00)
// ════════════════════════════════════════════════════════════

TEST(MarketCalendarUnit, WeekendIsClosed) {
    // 2024-07-06 = samedi, 2024-07-07 = dimanche
    EXPECT_FALSE(isUsEquityMarketOpenUtc(utcInstant(2024, 7, 6, 17, 0)));
    EXPECT_FALSE(isUsEquityMarketOpenUtc(utcInstant(2024, 7, 7, 17, 0)));
}

// 9h00-9h29 ET : marché ENCORE fermé (ouverture à 9h30). En hiver,
// 9h15 EST = 14h15 UTC.
TEST(MarketCalendarUnit, PreOpenNineToNineThirtyIsClosed) {
    EXPECT_FALSE(isUsEquityMarketOpenUtc(utcInstant(2024, 1, 2, 14, 15)));
}
