#pragma once
// ============================================================
//  LiveGate.hpp — gate MÉCANIQUE du mode --live (A1, passe 3)
//
//  Avant : --live = un flag + 5 s de sleep. Sur un conteneur
//  détaché avec restart automatique, le mode réel redémarrait
//  sans aucun opérateur. Le « la prod reste paper tant que pas
//  d'edge » de la ROADMAP n'était que du texte.
//
//  Désormais, QUATRE couches, toutes obligatoires :
//    1. liveTradingApproved=true dans config/prod.json
//       (verrouillé à false par un test d'intégration tant que
//       la DoD d'edge n'est pas atteinte — checklist RUNBOOK) ;
//    2. au moins un canal d'alerte configuré (env SWINGBOT_*) ;
//    3. stdin est un TERMINAL (un conteneur détaché ou un
//       redémarrage automatique ne peut JAMAIS passer) ;
//    4. l'opérateur tape « OUI » (exact) au clavier.
//
//  Logique PURE et testable : le TTY et le flux de confirmation
//  sont injectés — aucune dépendance système ici.
// ============================================================
#include <istream>
#include <optional>
#include <string>

namespace trading {

struct LiveGateInput {
    bool liveFlag          = false;  // --live présent sur la ligne de commande
    bool approvedInConfig  = false;  // ProdSettings.liveTradingApproved
    bool stdinIsTty        = false;  // isatty(stdin)
    bool alertChannelReady = false;  // au moins un canal AlertConfig activé
};

// Rend nullopt si le démarrage est autorisé (mode paper : toujours autorisé,
// sans question). Sinon : le MOTIF du refus, à afficher avant exit(1).
inline std::optional<std::string> liveGateRefusal(const LiveGateInput& in,
                                                  std::istream& confirmation) {
    if (!in.liveFlag) return std::nullopt;   // paper : aucun gate

    if (!in.approvedInConfig)
        return "liveTradingApproved=false dans config/prod.json — le passage "
               "en argent réel exige la checklist pré-live du RUNBOOK "
               "(DoD d'edge atteinte, alertes testées, décision consignée)";

    if (!in.alertChannelReady)
        return "aucun canal d'alerte configuré (variables SWINGBOT_*) — "
               "pas de live sans alerting opérationnel";

    if (!in.stdinIsTty)
        return "stdin n'est pas un terminal — le mode réel ne démarre JAMAIS "
               "sans opérateur (conteneur détaché / redémarrage automatique "
               "refusé)";

    std::string reponse;
    if (!std::getline(confirmation, reponse))
        return "confirmation interrompue (EOF) — démarrage réel refusé";
    if (reponse != "OUI")
        return "confirmation invalide (attendu « OUI » exactement, reçu « "
               + reponse + " ») — démarrage réel refusé";

    return std::nullopt;
}

} // namespace trading
