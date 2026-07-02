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
#include <string>

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

// ══════════════════════════════════════════════════════════════
//  Jours fériés NYSE (B4) — le repli ignorait les fériés : un jour
//  férié où l'API broker ne répond pas, le bot se croyait en séance.
//  Limite documentée : les demi-séances (13h00 ET, veille de
//  Thanksgiving / de Noël selon les années) restent hors périmètre.
// ══════════════════════════════════════════════════════════════

// ── Jour du mois du n-ième jour de semaine (n≥1, weekday 0=dim…6=sam) ──
inline int nthWeekdayOfMonth(int year, int month, int weekday, int n) {
    int firstDow = dayOfWeek(year, month, 1);
    int first    = 1 + ((weekday - firstDow + 7) % 7);
    return first + (n - 1) * 7;
}

// ── Nombre de jours du mois (grégorien, années bissextiles gérées) ──
inline int daysInMonth(int year, int month) {
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))
        return 29;
    return d[month - 1];
}

// ── Jour du mois du DERNIER jour de semaine donné du mois ──
inline int lastWeekdayOfMonth(int year, int month, int weekday) {
    int last    = daysInMonth(year, month);
    int lastDow = dayOfWeek(year, month, last);
    return last - ((lastDow - weekday + 7) % 7);
}

// ── Dimanche de Pâques (grégorien) — algorithme de Butcher/Meeus ──
inline void easterSunday(int year, int& outMonth, int& outDay) {
    int a = year % 19, b = year / 100, c = year % 100;
    int d = b / 4, e = b % 4;
    int f = (b + 8) / 25, g = (b - f + 1) / 3;
    int h = (19 * a + b - d - g + 15) % 30;
    int i = c / 4, k = c % 4;
    int l = (32 + 2 * e + 2 * i - h - k) % 7;
    int m = (a + 11 * h + 22 * l) / 451;
    outMonth = (h + l - 7 * m + 114) / 31;
    outDay   = ((h + l - 7 * m + 114) % 31) + 1;
}

// ── Good Friday = Pâques − 2 jours (peut reculer d'un mois : 1er-2 avril) ──
inline bool isGoodFriday(int year, int month, int day) {
    int em = 0, ed = 0;
    easterSunday(year, em, ed);
    int gm = em, gd = ed - 2;
    if (gd < 1) { gm -= 1; gd += daysInMonth(year, gm); }
    return month == gm && day == gd;
}

// ── Férié à date fixe avec observance NYSE ──
// Samedi → observé le vendredi précédent, dimanche → le lundi suivant.
// observeSaturday=false pour New Year : tombé un samedi, il n'est PAS
// observé (règle NYSE réelle — le 31/12/2021 était une séance normale).
inline bool matchesObservedFixedHoliday(int year, int month, int day,
                                        int holMonth, int holDay,
                                        bool observeSaturday) {
    int dow = dayOfWeek(year, holMonth, holDay);
    int obsMonth = holMonth, obsDay = holDay;
    if (dow == 6) {                                     // samedi → vendredi
        if (!observeSaturday) return false;
        if (--obsDay < 1) { obsMonth--; obsDay = daysInMonth(year, obsMonth); }
    } else if (dow == 0) {                              // dimanche → lundi
        if (++obsDay > daysInMonth(year, holMonth)) { obsMonth++; obsDay = 1; }
    }
    return month == obsMonth && day == obsDay;
}

// ── Ce jour civil (heure de l'Est) est-il un férié NYSE observé ? ──
inline bool isNyseHoliday(int year, int month, int day) {
    // New Year — 1er janvier (samedi NON observé, cf. ci-dessus)
    if (matchesObservedFixedHoliday(year, month, day, 1, 1, false)) return true;
    // MLK Day — 3e lundi de janvier
    if (month == 1 && day == nthWeekdayOfMonth(year, 1, 1, 3)) return true;
    // Presidents Day — 3e lundi de février
    if (month == 2 && day == nthWeekdayOfMonth(year, 2, 1, 3)) return true;
    // Good Friday — Pâques − 2
    if (isGoodFriday(year, month, day)) return true;
    // Memorial Day — dernier lundi de mai
    if (month == 5 && day == lastWeekdayOfMonth(year, 5, 1)) return true;
    // Juneteenth — 19 juin, férié NYSE depuis 2022
    if (year >= 2022
        && matchesObservedFixedHoliday(year, month, day, 6, 19, true)) return true;
    // Independence Day — 4 juillet
    if (matchesObservedFixedHoliday(year, month, day, 7, 4, true)) return true;
    // Labor Day — 1er lundi de septembre
    if (month == 9 && day == nthWeekdayOfMonth(year, 9, 1, 1)) return true;
    // Thanksgiving — 4e jeudi de novembre
    if (month == 11 && day == nthWeekdayOfMonth(year, 11, 4, 4)) return true;
    // Christmas — 25 décembre
    if (matchesObservedFixedHoliday(year, month, day, 12, 25, true)) return true;
    return false;
}

// ── Date civile (YYYY-MM-DD) en heure de l'Est pour un instant UTC ──
// Sert à filtrer la barre journalière du jour EN FORMATION (9.3) : la date
// de séance est la date de l'Est, pas la date UTC (décalées entre minuit ET
// et minuit UTC). Précision au jour près hors fenêtre de bascule DST (02h00
// locale) — largement suffisant pendant les heures de marché.
inline std::string usEasternDateOfUtc(std::time_t utc) {
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &utc);
#else
    gmtime_r(&utc, &tm);
#endif
    int off = usEasternUtcOffsetHours(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    std::time_t shifted = utc - static_cast<std::time_t>(off) * 3600;
#ifdef _WIN32
    gmtime_s(&tm, &shifted);
#else
    gmtime_r(&shifted, &tm);
#endif
    // L'offset peut différer une fois la date décalée (autour des bascules) :
    // recalculer sur la date de l'Est et re-décaler si besoin.
    int off2 = usEasternUtcOffsetHours(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    if (off2 != off) {
        shifted = utc - static_cast<std::time_t>(off2) * 3600;
#ifdef _WIN32
        gmtime_s(&tm, &shifted);
#else
        gmtime_r(&shifted, &tm);
#endif
    }
    char buf[11];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
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
    // Le contrôle des fériés (B4) s'appuie sur la date UTC : elle est
    // identique à la date de l'Est pendant les heures de marché (cf.
    // commentaire d'en-tête) — et hors séance, le test horaire coupe déjà.
    return etMinutes >= open && etMinutes < close
        && !isNyseHoliday(year, month, day);
}

} // namespace trading
