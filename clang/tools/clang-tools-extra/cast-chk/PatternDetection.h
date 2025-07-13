#ifndef PATTERNDETECTION_H
#define PATTERNDETECTION_H

#include "Census.h"
#include "History.h"
#include "utils.h"

#include <algorithm>
#include <numeric>
#include <stack>

class TypeScore {
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

public:
    using Typeset = decltype(Score::types_);

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

    Typeset inTypeset() const {
        return inTypes_;
    }

    Typeset outTypeset() const {
        return outTypes_;
    }

    void addInTypes(TypeScore const &ts) {
        inTypes_.insert(std::begin(ts.inTypes_), std::end(ts.inTypes_));
    }

    void addOutTypes(TypeScore const &ts) {
        outTypes_.insert(std::begin(ts.outTypes_), std::end(ts.outTypes_));
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
    CensusKey id_;
    Score in_;
    Score out_;

    Typeset & inTypes_ = in_.types_;
    Typeset & outTypes_ = out_.types_;
};

std::unordered_map<CensusKey, TypeScore> SummarizedCastScores;
std::unordered_map<CensusKey, TypeScore> SummarizedGenericScores;
std::unordered_map<CensusKey, TypeScore> SummarizedSubtypingScores;
std::unordered_map<CensusKey, TypeScore> SummarizedReinterpretScores;
std::unordered_map<CensusKey, TypeScore> SummarizedFunctionPointerScores;
std::unordered_map<CensusKey, TypeScore> SummarizedVoidProvenance;
std::unordered_map<CensusKey, TypeScore> SummarizedCastProvenance;
std::unordered_map<CensusKey, TypeScore> SummarizedCVProvenance;

void initScores() {
    std::for_each(begin(TypeSummaries), end(TypeSummaries),
        [](auto const &node) {
            SummarizedCastScores.emplace(node.first, node.first);
            SummarizedGenericScores.emplace(node.first, node.first);
            SummarizedSubtypingScores.emplace(node.first, node.first);
            SummarizedReinterpretScores.emplace(node.first, node.first);
            SummarizedFunctionPointerScores.emplace(node.first, node.first);
            SummarizedVoidProvenance.emplace(node.first, node.first);
            SummarizedCastProvenance.emplace(node.first, node.first);
            SummarizedCVProvenance.emplace(node.first, node.first);
        });
}

struct VariantField {
    std::string name_;
    std::string value_;
    std::string location_;
};

struct VariantData {
    std::string name_;
    std::string location_;
    std::unordered_map<std::string, VariantField> attrs_;    // attr: <enum field> = <enum field's value>
};
std::unordered_map<std::string, VariantData> Variants;

std::string cleanType(CensusKey const &opKey) {
    auto const &op = ops(opKey);
    auto const &typeInfo = op.td_;

    if(typeInfo.elementType_) {
        return typeInfo.elementType_.value();
    }

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

void recordEdgeScore(std::string const& label, CensusKey const &from, CensusKey const &to, Score_t &scores, bool useCleanType = true) {
    auto const logKey = "label | " + from + " -> " + to;
    CNS_DEBUG_MSG(logKey, "begin");

    auto recordType = [&useCleanType](auto const &key) {
        if(useCleanType) {
            return cleanType(key);
        }
        return ops(key).type_;
    };

    auto const toty = recordType(to);
    if(!toty.empty()) {
        scores.at(from).addOutType(toty);
        CNS_DEBUG(logKey, "Added type '{}'; Updated out score for '{}': {}", toty, from, scores.at(from).outScore());
    }

    auto const foty = recordType(from);
    if(!foty.empty()) {
        scores.at(to).addInType(foty);
        CNS_DEBUG(logKey, "Added type '{}'; Updated in score for '{}': {}", foty, to, scores.at(to).inScore());
    }

    CNS_DEBUG_MSG(logKey, "end");
}

inline bool isTransformConditional(DominatorData const &linkInfo) {
    if(!String(linkInfo.parentCondition()).empty()
            && linkInfo.parentCondition().condition() != "NoCond") {
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

inline bool isVariantLikeTransform(DominatorData const &linkInfo, OpData const &to) {
    auto const &condition = linkInfo.parentCondition();
    auto isSwitchCondtion = isTransformConditional(linkInfo) && condition.isSwitch_;

    if(!condition.lhsqn_) {
        return false;
    }

    // TODO: Check doms and ancestors for match
    std::vector<CensusKey> domchain;
    std::stack<std::string> tochk;
    tochk.push(condition.lhsqn_.value());
    while(!tochk.empty()) {
        auto top = tochk.top();
        tochk.pop();
        if(auto const &[_, d] = census[top]; d.has_value() && !d->empty()) {
            for(auto const &dd: d.value()) {
                auto dk = dd.op().qn_;
                if(std::find(begin(domchain), end(domchain), dk) == std::end(domchain)) {
                    tochk.push(dk);
                }
            }
        }
        domchain.push_back(top);
    }

    auto isFromSwitchPointer = (std::find(begin(domchain), end(domchain),
                linkInfo.op().qn_) != std::end(domchain));
    auto isToKnownType = !(to.type_.empty() || to.type_ == "T");

    return isSwitchCondtion && isFromSwitchPointer && isToKnownType;
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

void recordEdgeDom(CensusKey const &from, CensusKey const &to, DominatorData const &linkInfo) {
    auto fop = ops(from);
    auto top = ops(to);

    // Void ancestry
    auto const &fromVoid = SummarizedVoidProvenance.at(from);
    if(fromVoid.inScore() > 0) {// from a void descendant
        SummarizedVoidProvenance.at(to).addInTypes(fromVoid);
    }

    if(fop.td_.isVoidPointerType_) {
        SummarizedVoidProvenance.at(to).addInType(from); // Add void dom qn
    }

    if(top.td_.isVoidPointerType_) {
        SummarizedVoidProvenance.at(from).addOutType(to); // Add dom'd qn
    }

    // Cast ancestry
    auto const &fromCasts = SummarizedCastProvenance.at(from);
    if(fromCasts.inScore() > 0) { // from a cast descendant
        SummarizedCastProvenance.at(to).addInTypes(fromCasts);
        SummarizedCastProvenance.at(from).addOutType(to);
    }
    if(linkInfo.castKind() == "BitCast") { // from a bit cast
        SummarizedCastProvenance.at(to).addInType(linkInfo.linkExpr());
        SummarizedCastProvenance.at(from).addOutType(to);
    }

    if(SummarizedVoidProvenance.at(to).inScore() > 0
            || SummarizedCastProvenance.at(to).inScore() > 0) {
        SummarizedCVProvenance.at(to).addInType(from);
        SummarizedCVProvenance.at(from).addOutType(to);
    }
}

TypeScore recordLeafScore(TypeSummary const &ts) {
    auto const &logKey = ts.key();
    CNS_DEBUG_MSG(logKey, "begin");

    std::stack<std::reference_wrapper<const TypeSummary>> stack;
    std::unordered_map<CensusKey, bool> seen;
    auto pushNexts = [&](auto const &t) {
        for(auto const &n: t.nexts()) {
            if(seen.find(n.key()) == std::end(seen)) {
                CNS_DEBUG(logKey, "Pushing nexts to stack: {}", n.key());
                stack.emplace(std::cref(n));
            }
            else {
                CNS_DEBUG(logKey, "Skipping seen node: {}", n.key());
            }
        }
    };

    TypeScore score(ts.key());
    pushNexts(ts);

    //auto extracount = 0;
    while(!stack.empty()) {
        auto const &top = stack.top().get();
        auto const &topk = top.key();
        CNS_DEBUG(logKey, "stack top: {}\n", topk);
        seen[topk] = true;
        stack.pop();

        score.addOutType(cleanType(top.key())); //ops(top.key()).type_);

        // We need to reach the leaf regardless of type score.
        // To reach the leaf:
        //  - push nexts of top to stack
        pushNexts(top);
        auto isVoidLeaf = top.nexts().empty() && ops(topk).td_.isVoidPointerType_;

        //  - if nexts is empty => leaf
        //     - if leaf is void*, fetch top's history from TS and push on stack
        if(isVoidLeaf) {
            if(TypeSummaries.find(topk) != std::end(TypeSummaries)) {
                CNS_DEBUG_MSG(topk, "Pushing summary for void* leaf from TypeSummaries\n");
                pushNexts(TypeSummaries.at(topk));
            }
            else {
                CNS_DEBUG_MSG(topk, "No summary found for void* leaf in TypeSummaries\n");
            }
        }
        //     - else, noop
    }

    CNS_DEBUG_MSG(logKey, "end");
    return score;
}

void scoreSummary(TypeSummary const &ts) {
    LOG_FUNCTION_TIME;
    auto const logKey = ts.key();
    CNS_DEBUG_MSG(logKey, "begin");

    for(auto const &to: ts.nexts()) {
        auto const& linkInfo = to.linkInfo();

        recordEdgeDom(ts.key(), to.key(), linkInfo);

        if(linkInfo.castKind() == "BitCast") {
            recordEdgeScore("CastScore", ts.key(), to.key(), SummarizedCastScores, false);
        }

        if(isSubtypingTransform(linkInfo)) {
            if(isNumeric(to.key())) {
                CNS_DEBUG_MSG(logKey, "Skipping number edge");
                continue;
            }
            recordEdgeScore("Subtyping score", ts.key(), to.key(), SummarizedSubtypingScores);
        }

        if(isVariantLikeTransform(linkInfo, ops(to.key()))) {
            //recordEdgeScore("Variant score", ts.key(), to.key(), SummarizedVariantScores);

        //  - Is variant?
        //     - if no, skip
        //     - if yes, get the condition from dominfo:
        //        - enum name = ptr type from lhs (non-switch condition is ignored)
        //        - enum attr = type of ptr
        //        - attr literal value = rhs value collected from variant.
        //        e.g. given:
        //        *ps: Shape*
        //        *pr: Rectangle*
        //        *pc: Circle*
        //        switch(ps->type) { case "RECT": ...; case "CIRC": ...;}
        //
        //        yields
        //          enum Shape { "RECT"(Rectangle*), "CIRC"(Circle*)} 
        //
        // TODO
        //  - Store dre of condition (e.g. op(ps) in ps->type) with the variant/condition (TBD)
        //    so that variant fields can be filtered. If the cast is not from ps, then we can avoid adding the rhs as a field.

            auto condition = linkInfo.parentCondition();
            auto vdname = condition.type_.value_or(condition.lhs_ + ": " + condition.location_); // Condition + location to help with diagnostic
            auto topd = ops(to.key());
            std::string vdattr;
            //vdattr = topd.td_.uqType_;
            if(topd.td_.isPointerType_) {
                vdattr = topd.td_.pointeeType_.value_or("BadPointee_t for " + condition.rhs_ + "<" + condition.location_ + ">");
                //vdattr = topd.td_.elementType_.value_or("BadPtrElement_t for " + condition.rhs_ + "<" + condition.location_ + ">");
            }
            else {
                vdattr = topd.td_.uqType_;
            }
            auto vdval = condition.rhs_;
            auto vdloc = topd.location_;

            // Add or update variant data
            auto vd = VariantData{vdname, condition.location_, {}};
            if(Variants.find(vdname) != std::end(Variants)) {
                vd = Variants[vdname];
            }

            // update this attr
            vd.attrs_[vdattr] = {vdattr, vdval, vdloc};
            // Update variant data;
            Variants[vdname] = vd;
        }

        if(!isTransformThroughMember(linkInfo)) {
            recordEdgeScore("Generic score", ts.key(), to.key(), SummarizedGenericScores);
        }

        auto const &from = ops(ts.key());
        if(from.td_.isVoidPointerType_) {
            // propagate only to void *
            if(ops(to.key()).td_.isVoidPointerType_) {
                propagateGenericScore(ts.key(), to.key());
            }
        }

        if(ops(to.key()).td_.isVoidPointerType_) {
            CNS_DEBUG(logKey, "TO({}) is voidptr, checking till leaves; current out types: [{}]\n", to.key(), SummarizedGenericScores.at(to.key()).outTypes());
            auto leafScores = recordLeafScore(to);
            CNS_DEBUG(logKey,"TO({}) Adding out types: {}\n", to.key(), leafScores.outTypes());
            SummarizedGenericScores.at(to.key()).addOutTypes(leafScores);
            CNS_DEBUG(logKey, "TO({}) Updated out types: {}\n", to.key(), SummarizedGenericScores.at(to.key()).outTypes());
        }

        // reinterpret
        if(hasReinterpretCast(ts.key(), to.key(), linkInfo)) {
            recordEdgeScore("Reinterpret score", ts.key(), to.key(), SummarizedReinterpretScores, false);
        }

        // function pointers
        if(from.td_.fptrType_) {
            recordEdgeScore("FunctionPointer score", ts.key(), to.key(), SummarizedFunctionPointerScores, false);
        }

        scoreSummary(to);

    }
    CNS_DEBUG_MSG(logKey, "end");
}

bool isPotentiallyGeneric(CensusKey const &op) {
    auto const& opd = ops(op);
    auto isVoidPointer = opd.td_.isVoidPointerType_;
    auto isFunctionPointer = opd.td_.fptrType_;
    auto score = SummarizedGenericScores.at(op);

    if(isFunctionPointer || !isVoidPointer) {
        // Function pointers or pointers that are only void* cannot be established as generic.
        return false;
    }

    // Expected generics:
    //      qsort.$0, qsort.$4.$0, qsort.$4.$1, qsort.vt, qsort.vl, qsort.vr
    // Not generic:
    //      cmpstr.$0,$1, cmpnum.$0,$1

    // Eliminate void* difference
    score.addInType("void");
    score.addInType("void *");
    score.addOutType("void");
    score.addOutType("void *");

    if(score.inScore() <= 3) {
        // Genericity propagation ensures that aliased void pointer has more than one intype
        return false;
    }
    /*
    TypeScore::Typeset ins, outs;
    auto voidFilter = [](auto const &type) {
        return type != "void *"
            && type != "void";
    };

    std::copy_if(begin(score.inTypeset()), end(score.inTypeset()), inserter(ins), voidFilter);
    std::copy_if(begin(score.outTypeset()), end(score.outTypeset()), outserter(outs), voidFilter);
    */

    //return !opd.td_.fptrType_ && score.inScore() > 1
    return score.inTypeset() == score.outTypeset();
}

bool isSingleUseVoid(CensusKey const &op) {
    auto isVoidPtr = ops(op).td_.isVoidPointerType_;

    auto genericScore = SummarizedGenericScores.at(op);
    // Eliminate void* difference
    genericScore.addInType("void");
    genericScore.addInType("void *");
    genericScore.addOutType("void");
    genericScore.addOutType("void *");

    return isVoidPtr
        && genericScore.inScore() == 3
        //&& genericScore.inTypes() != "void *"
        && genericScore.inTypes() == genericScore.outTypes();
}

bool isPotentiallySubtype(CensusKey const &op) {
    return SummarizedSubtypingScores.at(op).outScore() > 1
        && !isPotentiallyGeneric(op);
}

bool isReinterpret(CensusKey const &op) {
    return SummarizedReinterpretScores.at(op).outScore() > 0;
}

bool isFunctionPointer(CensusKey const &op) {
    auto const &score = SummarizedFunctionPointerScores.at(op);
    return score.inScore() > 0
        && (score.outScore() == 0 || score.inTypes() == score.outTypes());
}

bool isSink(CensusKey const &op) {
    return SummarizedCastScores.at(op).inScore() > 0
        && SummarizedCastScores.at(op).outScore() == 0;
}

bool isMissingSource(CensusKey const &op) {
    return SummarizedCastScores.at(op).inScore() == 0
        && SummarizedCastScores.at(op).outScore() > 0;
}

bool isNotUsedInCasts(CensusKey const &op) {
    return SummarizedCastScores.at(op).inScore() == 0
        && SummarizedCastScores.at(op).outScore() == 0;
}

bool isVoidDescendant(CensusKey const &op) {
    return SummarizedVoidProvenance.at(op).inScore() > 0;
}

bool isCastDescendant(CensusKey const &op) {
    return SummarizedCastProvenance.at(op).inScore() > 0;
}

bool isCVDescendant(CensusKey const &op) {
    return SummarizedCVProvenance.at(op).inScore() > 0;
}

#endif // PATTERNDETECTION_H
