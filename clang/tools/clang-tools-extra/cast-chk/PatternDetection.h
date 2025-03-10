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

std::unordered_map<CensusKey, TypeScore> SummarizedScores;
std::unordered_map<CensusKey, TypeScore> SummarizedSubtypingScores;

void scoreEdge(CensusKey const &from, CensusKey const &to) {
    auto const logKey = from + " -> " + to;
    CNS_DEBUG_MSG(logKey, "begin");

    auto cleanType = [](CensusKey const &op) -> auto {
        auto const &op_ = ops(op);
        auto const &typeInfo = op_.td_;
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
    };

    SummarizedScores.at(from).addOutType(cleanType(to));
    CNS_DEBUG(logKey, "Updated out score for '{}': {}", from, SummarizedScores.at(from).outScore());

    SummarizedScores.at(to).addInType(cleanType(from));
    CNS_DEBUG(logKey, "Updated in score for '{}': {}", to, SummarizedScores.at(to).inScore());

    CNS_DEBUG_MSG(logKey, "end");
}

void initScores() {
    std::for_each(begin(TypeSummaries), end(TypeSummaries),
        [](auto const &node) {
            SummarizedScores.emplace(node.first, node.first);
            SummarizedSubtypingScores.emplace(node.first, node.first);
        });
}

void scoreSubtypingEdge(CensusKey const &from, CensusKey const &to) {
    auto const logKey = from + " -> " + to;
    CNS_DEBUG_MSG(logKey, "begin");

    auto cleanType = [](CensusKey const &op) -> auto {
        auto const &op_ = ops(op);
        auto const &typeInfo = op_.td_;
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
    };

    SummarizedSubtypingScores.at(from).addOutType(cleanType(to));
    CNS_DEBUG(logKey, "Updated out score for '{}': {}", from, SummarizedSubtypingScores.at(from).outScore());

    SummarizedSubtypingScores.at(to).addInType(cleanType(from));
    CNS_DEBUG(logKey, "Updated in score for '{}': {}", to, SummarizedSubtypingScores.at(to).inScore());

    CNS_DEBUG_MSG(logKey, "end");
}

bool isConditionalTransform(DominatorData const &linkInfo) {
    if(!String(linkInfo.parentCondition()).empty()
            && linkInfo.parentCondition().condition_ != "NoCond") {
        return true;
    }
    return false;
}

void scoreSummary(TypeSummary const &ts) {
    auto const logKey = ts.key();
    CNS_DEBUG_MSG(logKey, "begin");
    for(auto const &to: ts.nexts()) {
        // If there is a member access or conditional cast, consider it as subtyping
        if((to.linkInfo().exprType().find("Member") != std::string::npos)
                && isConditionalTransform(to.linkInfo())) {
            // Maybe potential upcasts
            //CNS_DEBUG_MSG(logKey, "Skipping member edge");
            auto const &op = ops(to.key());
            if(op.td_.numericType_) {
                CNS_DEBUG_MSG(logKey, "Skipping number edge");
                continue;
            }
            scoreSubtypingEdge(ts.key(), to.key());
        }

        if(to.linkInfo().exprType().find("Member") == std::string::npos) {
            scoreEdge(ts.key(), to.key());
        }

        auto const &from = ops(ts.key());
        if(from.type_ == "void *") {
            SummarizedScores.at(to.key())
                .addInTypes(SummarizedScores.at(ts.key()));
        }

        scoreSummary(to);
    }
    CNS_DEBUG_MSG(logKey, "end");
}

bool isPotentiallyGeneric(CensusKey const &op) {
    return SummarizedScores.at(op).inScore() > 1;
}

bool isPotentiallySubtype(CensusKey const &op) {
    return SummarizedSubtypingScores.at(op).outScore() > 1;
}

#endif // PATTERNDETECTION_H
