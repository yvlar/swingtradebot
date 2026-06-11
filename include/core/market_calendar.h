#pragma once
// ============================================================
//  market_calendar.h  —  Calendrier de marché US (UTC/DST)
//
//  Sprint 5, item 20 (E8/D26-bis) : l'ancien repli horaire
//  d'IBKRDataFeed supposait un décalage FIXE UTC-5 (EST), faux
//  8 mois par an pendant l'heure d'été (EDT = UTC-4) — et ouvrait
//  le marché dès 9h00 au lieu de 9h30. Tout est désormais calculé
//  en UTC, avec la règle DST US officielle.
//
//  Fonctions pures, sans réseau ni état → testables unitairement
//  (tests/unit/test_market_calendar_unit.cpp).
// ============================================================
#include <ctime>

namespace trading {

// ── Jour de la semaine (0=dimanche … 6=samedi) — algorithme de Sakamoto ──
// Valable pour le calendrier grégorien, sans dépendre de mktime/timezone.
inline int dayOfWeek(int year, int month, int day) {
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int y = year;
    if (month < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[month - 1] + day) % 7;
}

// ── Jour du mois du n-ième dimanche (n≥1) d'un mois donné ──
inline int nthSundayOfMonth(int year, int month, int n) {
    int firstDow     = dayOfWeek(year, month, 1);   // 0=dimanche
    int firstSunday  = 1 + ((7 - firstDow) % 7);
    return firstSunday + (n - 1) * 7;
}

// ── Heure d'été US (EDT) en vigueur pour une date ? ──
// Règle US depuis 2007 : EDT du 2e dimanche de mars au 1er dimanche de
// novembre (bascule à 02h00 locale). En dehors → EST (UTC-5).
// La bascule a lieu à 02h00, loin des heures de marché : on l'évalue donc
// à la granularité du jour, sans ambiguïté pendant l'ouverture.
inline bool isUsEasternDst(int year, int month, int day) {
    if (month < 3 || month > 11) return false;          // déc-fév : EST
    if (month > 3 && month < 11) return true;           // avr-oct : EDT
    if (month == 3)  return day >= nthSundayOfMonth(year, 3, 2);
    /* month == 11 */ return day <  nthSundayOfMonth(year, 11, 1);
}

// ── Décalage heure de l'Est ↔ UTC (en heures) pour une date ──
inline int usEasternUtcOffsetHours(int year, int month, int day) {
    return isUsEasternDst(year, month, day) ? 4 : 5;    // EDT=UTC-4, EST=UTC-5
}

// ── Le marché actions US (NYSE/NASDAQ) est-il ouvert à cet instant UTC ? ──
// Séance régulière : 9h30-16h00 heure de l'Est, du lundi au vendredi.
// La date du calendrier est identique en EST/EDT pendant les heures de
// marché (13h30-21h00 UTC), on la lit donc directement depuis l'UTC.
inline bool isUsEquityMarketOpenUtc(std::time_t utc) {
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &utc);
#else
    gmtime_r(&utc, &tm);
#endif
    int year   = tm.tm_year + 1900;
    int month  = tm.tm_mon + 1;
    int day    = tm.tm_mday;
    int offset = usEasternUtcOffsetHours(year, month, day);

    // Minutes depuis minuit en heure de l'Est (peut déborder sur la veille)
    int etMinutes = tm.tm_hour * 60 + tm.tm_min - offset * 60;
    int etWday    = tm.tm_wday;
    if (etMinutes < 0) { etMinutes += 24 * 60; etWday = (etWday + 6) % 7; }

    if (etWday == 0 || etWday == 6) return false;       // week-end
    const int open  = 9 * 60 + 30;                      // 9h30
    const int close = 16 * 60;                          // 16h00
    return etMinutes >= open && etMinutes < close;
}

} // namespace trading
