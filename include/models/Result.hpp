#pragma once
// ============================================================
//  Result.hpp  —  Canal d'erreur explicite (Sprint 2, item 10)
//
//  Avant : optional/vecteur vide signifiait à la fois « erreur
//  réseau » et « pas de donnée » dans IDataFeed/IBroker. La
//  réconciliation de TradingBot réinitialisait donc l'état sur
//  une simple panne réseau (rétrospective Sprint 1).
//
//  Result<T> sépare les deux mondes :
//    Result<T>::Ok(v)    → réponse obtenue (v peut être « vide »)
//    Result<T>::Err(msg) → panne (réseau, parsing…) : la valeur
//                          est INCONNUE, ne pas agir dessus
//
//  Exemples :
//    Result<std::optional<Position>>::Ok(std::nullopt)
//        → le broker a répondu : AUCUNE position (certitude)
//    Result<std::optional<Position>>::Err("timeout")
//        → on ne SAIT PAS s'il y a une position
// ============================================================
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace trading {

template <typename T>
class Result {
public:
    static Result Ok(T value) {
        Result r;
        r.value_ = std::move(value);
        return r;
    }

    static Result Err(std::string error) {
        Result r;
        r.error_ = std::move(error);
        return r;
    }

    bool ok() const { return value_.has_value(); }
    explicit operator bool() const { return ok(); }

    // Précondition : ok() — lance sinon (jamais d'accès silencieux à
    // une valeur inexistante)
    const T& value() const& {
        if (!value_) throw std::logic_error("Result::value() sur erreur : " + *error_);
        return *value_;
    }
    T&& value() && {
        if (!value_) throw std::logic_error("Result::value() sur erreur : " + *error_);
        return std::move(*value_);
    }

    // Précondition : !ok()
    const std::string& error() const {
        static const std::string kNone = "";
        return error_ ? *error_ : kNone;
    }

private:
    Result() = default;
    std::optional<T>           value_;
    std::optional<std::string> error_;
};

} // namespace trading
