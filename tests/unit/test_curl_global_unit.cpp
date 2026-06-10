// ============================================================
//  test_curl_global_unit.cpp  —  Tests UNITAIRES
//  Cible : core/curl_global.h — garde RAII de l'init libcurl
//  (Sprint 2, item 7 : un seul curl_global_init par processus)
// ============================================================
#include <gtest/gtest.h>
#include "core/curl_global.h"

// ── Comptage des instances ────────────────────────────────

TEST(CurlGlobalUnit, NoGuardMeansZeroInstances) {
    EXPECT_EQ(CurlGlobal::instances(), 0);
}

TEST(CurlGlobalUnit, GuardLifetimeTracksInstanceCount) {
    EXPECT_EQ(CurlGlobal::instances(), 0);
    {
        CurlGlobal g;
        EXPECT_EQ(CurlGlobal::instances(), 1);
    }
    EXPECT_EQ(CurlGlobal::instances(), 0);
}

// Des gardes imbriquées doivent être inoffensives : seules la première
// et la dernière touchent l'état global de libcurl.
TEST(CurlGlobalUnit, NestedGuardsAreHarmless) {
    CurlGlobal outer;
    EXPECT_EQ(CurlGlobal::instances(), 1);
    {
        CurlGlobal inner;
        EXPECT_EQ(CurlGlobal::instances(), 2);
    }
    EXPECT_EQ(CurlGlobal::instances(), 1);
}

// ── libcurl utilisable sous la garde ──────────────────────

TEST(CurlGlobalUnit, CurlEasyInitWorksUnderGuard) {
    CurlGlobal g;
    CURL* h = curl_easy_init();
    ASSERT_NE(h, nullptr);
    curl_easy_cleanup(h);
}
