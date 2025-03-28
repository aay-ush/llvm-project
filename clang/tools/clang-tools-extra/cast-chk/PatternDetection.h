#ifndef PATTERNDETECTION_H
#define PATTERNDETECTION_H

#include "Census.h"
#include "History.h"
#include "utils.h"

#include <algorithm>
#include <numeric>

class TypeScore {
public:
    unsigned inScore() const {
        return inTypes_.size();
    }
    unsigned outScore() const {
        return outTypes_.size();
    }

    std::string inTypes() const {
        return in_.types();
    }

    std::string outTypes() const {
        return out_.types();
    }

    void addInTypes(TypeScore const &ts) {
        inTypes_.insert(std::begin(ts.inTypes_), std::end(ts.inTypes_));
    }

    void addInType(std::string type) {
        CNS_DEBUG(id_, "Inserting inType: {}", type);
        auto st = inTypes_.insert(type);
        CNS_DEBUG(id_, "Insert status: {}", st.second);
    }

    void addOutType(std::string type) {
        CNS_DEBUG(id_, "Inserting outType: {}", type);
        auto st = outTypes_.insert(type);
        CNS_DEBUG(id_, "Insert status: {}", st.second);
    }

    TypeScore(CensusKey const& id): id_(id) {}

private:
    struct Score {
        std::unordered_set<std::string> types_;
        std::string types() const {
            if(types_.empty()) {
                return "";
            }

            return std::accumulate(next(begin(types_)), end(types_), *begin(types_),
                [](std::string a, std::string b) {
                    CNS_DEBUG("accumulating", "Accumulated: {}; adding: {}", a, b);
                    return std::move(a) + ", " + b;
                });
        }
    };

    CensusKey id_;
    Score in_;
    Score out_;

    decltype(Score::types_) & inTypes_ = in_.types_;
    decltype(Score::types_) & outTypes_ = out_.types_;
};

std::unordered_map<CensusKey, TypeScore> SummarizedCastScores;
std::unordered_map<CensusKey, TypeScore> SummarizedGenericScores;
std::unordered_map<CensusKey, TypeScore> SummarizedSubtypingScores;
std::unordered_map<CensusKey, TypeScore> SummarizedReinterpretScores;

void initScores() {
    std::for_each(begin(TypeSummaries), end(TypeSummaries),
        [](auto const &node) {
            SummarizedCastScores.emplace(node.first, node.first);
            SummarizedGenericScores.emplace(node.first, node.first);
            SummarizedSubtypingScores.emplace(node.first, node.first);
            SummarizedReinterpretScores.emplace(node.first, node.first);
        });
}

std::string cleanType(CensusKey const &opKey) {
    auto const &op = ops(opKey);
    auto const &typeInfo = op.td_;

    if(typeInfo.numericType_) {
        return typeInfo.numericType_.value();
    }

    if(typeInfo.pointeeType_) {
        return typeInfo.pointeeType_.value();
    }

    if(typeInfo.fptrType_) {
        return typeInfo.fptrType_.value();
    }

    return typeInfo.uqType_;
}

using Score_t = std::unordered_map<CensusKey, TypeScore>;

void recordEdgeScore(std::string const& label, CensusKey const &from, CensusKey const &to, Score_t scores, bool useCleanType = true) {
    auto const logKey = "label | " + from + " -> " + to;
    CNS_DEBUG_MSG(logKey, "begin");

    auto recordType = [&useCleanType](auto const &key) {
        if(useCleanType) {
            return cleanType(key);
        }
        return ops(key).type_;
    };

    scores.at(from).addOutType(recordType(to));
    CNS_DEBUG(logKey, "Added type '{}'; Updated out score for '{}': {}", recordType(to), from, scores.at(from).outScore());

    scores.at(to).addInType(recordType(from));
    CNS_DEBUG(logKey, "Added type '{}'; Updated in score for '{}': {}", recordType(from), to, scores.at(to).inScore());

    CNS_DEBUG_MSG(logKey, "end");
}

inline bool isTransformConditional(DominatorData const &linkInfo) {
    if(!String(linkInfo.parentCondition()).empty()
            && linkInfo.parentCondition().condition_ != "NoCond") {
        return true;
    }
    return false;
}

inline bool isTransformThroughMember(DominatorData const &linkInfo) {
    if(linkInfo.exprType().find("Member") != std::string::npos) {
        return true;
    }
    return false;
}

inline bool isSubtypingTransform(DominatorData const &linkInfo) {
    // If there is a member access with conditional cast, consider it as subtyping
    return isTransformConditional(linkInfo)
        && isTransformThroughMember(linkInfo);
    // Maybe potential upcasts if there is memeber access without conditional
}

inline bool isNumeric(CensusKey const &op) {
    return ops(op).td_.numericType_.has_value();
}

inline bool isCharacter(CensusKey const &op) {
    return ops(op).td_.charType_.has_value();
}

inline void propagateScore(TypeScore const &from, TypeScore &to) {
    to.addInTypes(from);
}

inline void propagateGenericScore(CensusKey const &from, CensusKey const &to) {
    propagateScore(SummarizedGenericScores.at(from), SummarizedGenericScores.at(to));
}

bool hasReinterpretCast(CensusKey const &from, CensusKey const &to, DominatorData const &linkInfo) {
    /*
    if(isTransformThroughMember(linkInfo)) {
        return false;
    }
    */

    if(linkInfo.castKind() != "BitCast") {
        return false;
    }

    auto isRelevantType = [](auto const &op) {
        return isCharacter(op.qn_)
            || (isNumeric(op.qn_) && (op.type_.find("int") != 0));
    };

    auto isFromRType = isRelevantType(ops(from));
    auto isFromPointer = ops(from).td_.isPointerType_;

    auto isToRType = isRelevantType(ops(to));
    auto isToPointer = ops(to).td_.isPointerType_;

    // true if cast is from number to pointer or vice versa only.
    if(isFromRType && !isFromPointer && isToPointer) {
        return true;
    }
    if(isToRType && !isToPointer && isFromPointer) {
        return true;    // Probably not a thing
    }

    return false;
}

void scoreSummary(TypeSummary const &ts) {
    LOG_FUNCTION_TIME;
    auto const logKey = ts.key();
    CNS_DEBUG_MSG(logKey, "begin");

    for(auto const &to: ts.nexts()) {
        auto const& linkInfo = to.linkInfo();
        if(linkInfo.castKind() == "BitCast") {
            recordEdgeScore("CastScore", ts.key(), to.key(), SummarizedCastScores);
        }

        if(isSubtypingTransform(linkInfo)) {
            if(isNumeric(to.key())) {
                CNS_DEBUG_MSG(logKey, "Skipping number edge");
                continue;
            }
            recordEdgeScore("Subtyping score", ts.key(), to.key(), SummarizedSubtypingScores);
        }

        if(!isTransformThroughMember(linkInfo)) {
            recordEdgeScore("Generic score", ts.key(), to.key(), SummarizedGenericScores);
        }

        auto const &from = ops(ts.key());
        //if(!from.td_.fptrType_ && from.td_.uqType_ == "void *") {
        if(from.td_.isVoidPointerType_) {
            // propagate only to void *
            if(ops(to.key()).td_.isVoidPointerType_) {
                propagateGenericScore(ts.key(), to.key());
            }
        }

        // reinterpret
        if(hasReinterpretCast(ts.key(), to.key(), linkInfo)) {
            recordEdgeScore("Reinterpret score", ts.key(), to.key(), SummarizedReinterpretScores, false);
        }

        scoreSummary(to);

    }
    CNS_DEBUG_MSG(logKey, "end");
}

bool isPotentiallyGeneric(CensusKey const &op) {
    auto const& opd = ops(op);
    // Not a function pointer and has more than one intype
    return !opd.td_.fptrType_ && SummarizedGenericScores.at(op).inScore() > 1;
}

bool isSingleUseVoid(CensusKey const &op) {
    auto isVoidPtr = ops(op).td_.isVoidPointerType_;
    auto genericScore = SummarizedGenericScores.at(op);
    return isVoidPtr
        && genericScore.inScore() == 1
        && genericScore.inScore() == genericScore.outScore();
}

bool isPotentiallySubtype(CensusKey const &op) {
    return SummarizedSubtypingScores.at(op).outScore() > 1;
}

bool isReinterpret(CensusKey const &op) {
    return SummarizedReinterpretScores.at(op).outScore() > 0;
}

bool isNotUsedInCasts(CensusKey const &op) {
    return SummarizedCastScores.at(op).inScore() == 0
        && SummarizedCastScores.at(op).outScore() == 0;
}
#endif // PATTERNDETECTION_H
