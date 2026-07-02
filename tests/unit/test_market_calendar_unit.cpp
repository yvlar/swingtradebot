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

// ════════════════════════════════════════════════════════════
//  Jours fériés NYSE (B4) — le repli ignorait totalement les
//  fériés : un jour férié où l'API broker ne répond pas, le bot
//  se croyait en séance.
// ════════════════════════════════════════════════════════════

namespace {

// Le marché est-il ouvert à 15h00 heure de l'Est ce jour-là ?
// (15h ET = plein cœur de séance un jour ouvré normal)
bool openAt15Et(int y, int m, int d) {
    int off = usEasternUtcOffsetHours(y, m, d);
    return isUsEquityMarketOpenUtc(utcInstant(y, m, d, 15 + off, 0));
}

} // namespace

// Pâques — algorithme de Butcher/Meeus, dates connues
TEST(MarketCalendarUnit, EasterSundayKnownDates) {
    int m = 0, d = 0;
    easterSunday(2024, m, d); EXPECT_EQ(m, 3);  EXPECT_EQ(d, 31);
    easterSunday(2025, m, d); EXPECT_EQ(m, 4);  EXPECT_EQ(d, 20);
    easterSunday(2026, m, d); EXPECT_EQ(m, 4);  EXPECT_EQ(d, 5);
}

// Les 10 fériés NYSE de 2024, tous fermés en pleine séance (15h ET)
TEST(MarketCalendarUnit, Nyse2024HolidaysAreClosed) {
    EXPECT_FALSE(openAt15Et(2024,  1,  1));   // New Year (lundi)
    EXPECT_FALSE(openAt15Et(2024,  1, 15));   // MLK — 3e lundi de janvier
    EXPECT_FALSE(openAt15Et(2024,  2, 19));   // Presidents Day — 3e lundi de février
    EXPECT_FALSE(openAt15Et(2024,  3, 29));   // Good Friday (Pâques − 2)
    EXPECT_FALSE(openAt15Et(2024,  5, 27));   // Memorial Day — dernier lundi de mai
    EXPECT_FALSE(openAt15Et(2024,  6, 19));   // Juneteenth
    EXPECT_FALSE(openAt15Et(2024,  7,  4));   // Independence Day
    EXPECT_FALSE(openAt15Et(2024,  9,  2));   // Labor Day — 1er lundi de septembre
    EXPECT_FALSE(openAt15Et(2024, 11, 28));   // Thanksgiving — 4e jeudi de novembre
    EXPECT_FALSE(openAt15Et(2024, 12, 25));   // Christmas
}

// Observances : férié tombant un samedi → vendredi précédent fermé ;
// un dimanche → lundi suivant fermé.
TEST(MarketCalendarUnit, ObservedHolidaysShiftToAdjacentWeekday) {
    // 2026-07-04 = samedi → vendredi 3 juillet fermé
    EXPECT_FALSE(openAt15Et(2026, 7, 3));
    // 2027-07-04 = dimanche → lundi 5 juillet fermé
    EXPECT_FALSE(openAt15Et(2027, 7, 5));
    // 2021-12-25 = samedi → vendredi 24 décembre 2021 fermé
    EXPECT_FALSE(openAt15Et(2021, 12, 24));
}

// Règle NYSE réelle : New Year tombant un SAMEDI n'est PAS observé —
// le vendredi 31 décembre 2021 était une séance normale (cas 2022).
TEST(MarketCalendarUnit, NewYearOnSaturdayIsNotObserved) {
    EXPECT_TRUE(openAt15Et(2021, 12, 31));
}

// Juneteenth n'est férié NYSE que depuis 2022
TEST(MarketCalendarUnit, JuneteenthNotAHolidayBefore2022) {
    // 2021-06-18 (vendredi) : séance normale malgré le 19 juin (samedi) 2021
    EXPECT_TRUE(openAt15Et(2021, 6, 18));
    // 2023-06-19 (lundi) : fermé
    EXPECT_FALSE(openAt15Et(2023, 6, 19));
}

// Les jours ouvrés ordinaires restent ouverts (non-régression)
TEST(MarketCalendarUnit, OrdinaryTradingDaysRemainOpen) {
    EXPECT_TRUE(openAt15Et(2024,  1,  2));    // mardi après New Year
    EXPECT_TRUE(openAt15Et(2024,  7,  5));    // vendredi après le 4 juillet
    EXPECT_TRUE(openAt15Et(2024, 11, 29));    // vendredi après Thanksgiving
    EXPECT_TRUE(openAt15Et(2024, 12, 24));    // veille de Noël (demi-séance : hors périmètre)
}
