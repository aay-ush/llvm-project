#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"

// Declares llvm::cl::extrahelp
#include "llvm/Support/CommandLine.h"

// AST Matchers
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"

// Source locating
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/Analysis/AnalysisDeclContext.h"

#include "clang/AST/Type.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Stmt.h"

#include "clang/AST/ODRHash.h"
#include "llvm/ADT/ArrayRef.h"

#include <fstream>

#include "llvm/Support/raw_os_ostream.h"
#include <string>
#include <unordered_map>
#include <utility>
#include <algorithm>
#include <optional>
#include <any>
#include <tuple>
#include <set>
#include <unordered_set>
#include <cstdlib>

#include "utils.h"
#include "Census.h"
#include "History.h"
#include "PatternDetection.h"
#include "OpData.h"

using namespace clang::tooling;
using namespace llvm;

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::ento;

std::vector<unsigned> seenFunctions;

// Tool workflow
// Matcher invokes Callback with match result
//                                      |
//                                      v
//                                  Cast Operands -> lhs, rhs
// 
// For every case/match type:
// 1. preprocess(expression) -> e.g. run matcher on called function for call expressions
// 2. Build operand data
// 3. Add {lhs, {[]}} & {rhs, {[lhs]}} to census
// 4. Add H(lhs).extend(H(rhs)) to TransformHistory; Create HistoryTemplate for functions
//

void preprocess(
        clang::ASTContext &context,
        clang::UnaryOperator const &op) {
    auto const logKey = String(context, op);
    CNS_DEBUG_MSG(logKey, "begin");
    CNS_DEBUG_MSG(logKey, "end");
}

void preprocess(
        clang::ASTContext &context,
        clang::DeclRefExpr const &op) {
    auto const logKey = String(context, op);
    CNS_DEBUG_MSG(logKey, "begin");
    CNS_DEBUG_MSG(logKey, "end");
}

bool isNodeDominatorNew(
        OpData const &node,
        DominatorData const &dom) {

    auto const& logKey = node.qn_ + "| dom(" + String(dom) + ")";
    CNS_DEBUG_MSG(logKey, "begin");
    auto &[_, doms_] = census[node.qn_];
    if(!doms_) {
        CNS_DEBUG_MSG(logKey, "No doms present currently.");
        return true;
    }

    CNS_DEBUG(logKey, "Checking current doms for this dom [{}]", dom.op().qn_);
    auto doms = doms_.value();
    CNS_DEBUG_MSG(logKey, "end");
    return std::find(begin(doms), end(doms), dom) == end(doms);
}

void appendNodeDominator(
        OpData const &node,
        DominatorData const &dom) {

    auto const& logKey = node.qn_ + "| dom(" + String(dom) + ")";
    CNS_DEBUG_MSG(logKey, "begin");
    auto &[_, doms_] = census[node.qn_];
    if(!doms_) {
        CNS_INFO_MSG(logKey, "Dominator Initialized.");
        census[node.qn_] = makeUseDefInfo(node, dom);
    } else {
        CNS_INFO_MSG(logKey, "Appending to dominators.");
        auto &doms = doms_.value();
        doms.push_back(dom);
    }

    CNS_INFO_MSG(logKey, "New Dominator Appended: {");
    if(SEVERITY_FILTER & cns::logging::severity::Info) {
        fmt::print(fOUT, "{}\n\n", dump(dom));
    }
    CNS_INFO_MSG(logKey, "}");
    CNS_DEBUG_MSG(logKey, "end");
}

void chkNodeDataForChange(OpData const &node) {
    auto const logKey = node.qn_;
    CNS_DEBUG_MSG(logKey, "begin");
    auto const& [old, _] = census[node.qn_];
    if(old != node) {
        CNS_WARN(logKey, "Old node with different OpData '{}':", String(old));
        if(SEVERITY_FILTER & cns::logging::severity::Warn) {
            fmt::print(fOUT, "{}\n\n", dump(old));
        }
        CNS_WARN(logKey, "New OpData '{}':", String(node));
        if(SEVERITY_FILTER & cns::logging::severity::Warn) {
            fmt::print(fOUT, "{}\n\n", dump(node));
        }
    }
    CNS_DEBUG_MSG(logKey, "end");
}

void addDomNode(OpData const &dom) {
    auto const logKey = String(dom);
    CNS_DEBUG_MSG(logKey, "begin");
    if(census.find(dom.qn_) == std::end(census)) {
        CNS_INFO_MSG(logKey, "Inserting new node for 'dom (census 'from')'.");
        census.insert(makeCensusSourceNode(dom));
        return;
    }
    CNS_INFO_MSG(logKey, "'dom (census 'from')' is already in census.");
    // If dominator is in census, do nothing
    // except warning of decl data change, if any.
    chkNodeDataForChange(dom);
    CNS_DEBUG_MSG(logKey, "end");
}

void updateHistory(
        OpData const &from,
        OpData const &to,
        DominatorData const& domInfo) {

    auto const logKey = from.qn_ + "->" + to.qn_;

    CNS_DEBUG_MSG(logKey, "begin");
    CNS_DEBUG(logKey, "History update using dom '{}'", String(domInfo));

    // Ensure H(to) first.
    // Retrieve H(to)
    if(TypeTransforms.find(to.qn_) == std::end(TypeTransforms)) {
        // Add H(to)
        CNS_INFO(logKey, ":to: New history started for '{}'", to.qn_);
        TypeTransforms.emplace(to.qn_, to.qn_);
        //TypeTransforms.insert({to.qn_, History(to.qn_)});
        // sanity check
        if(TypeTransforms.find(to.qn_) == std::end(TypeTransforms)) {
            CNS_ERROR(logKey, ":to: Could not insert history for '{}'", to.qn_);
            CNS_DEBUG_MSG(logKey, "end");
            return;
        }
    }
    else {
        CNS_INFO(logKey, ":to: History of '{}' already on record. No action needed.", to.qn_);
    }

    // Retrieve H(from)
    if(TypeTransforms.find(from.qn_) == std::end(TypeTransforms)) {
        // Add H(from)
        CNS_INFO(logKey, ":from: New history started for '{}'", from.qn_);
        TypeTransforms.emplace(from.qn_, from.qn_);
        //TypeTransforms.insert({from.qn_, History(from.qn_)});
        // sanity check
        if(TypeTransforms.find(from.qn_) == std::end(TypeTransforms)) {
            CNS_ERROR(logKey, ":from: Could not insert history for '{}'", from.qn_);
            CNS_DEBUG_MSG(logKey, "end");
            return;
        }
    }

    if(to.category_ == "FunctionPointer") {
        // Why ech context is not part of extended context.
        // To history is already locally instantiated in some TT.
        // Adding the context doesn't work as the local instantiations are not updated.
        //  -> Update context on each branch history
        //  -> Or in instantiation, keep a reference to global history with local context.
        //     -> i.e. branch will keep references instead of copies.
        //     -> seems like a better approach.
        CNS_INFO(logKey, ":toFP: Creating new context {{key, value}} = {{'{}','{}'}}", to.qn_, from.qn_);
        HistoryContext hc;
        hc[to.qn_] = from.qn_;

        auto &foh = TypeTransforms.at(from.qn_);
        auto &toh = TypeTransforms.at(to.qn_);
        CNS_INFO(logKey, ":toFP: Adding context to ToHistory: '{}'", toh.idversion());
        //auto ech = toh.addContext(hc);
        toh.addContext(hc);
        CNS_INFO(logKey, ":toFP: New ToHistory version: '{}'", toh.idversion());
        CNS_INFO(logKey, ":toFP: Extending history from '{}' with '{}'", foh.idversion(), toh.idversion());
        //TypeTransforms.at(from.qn_).extend(ech);
        //foh.extend({toh, hc}); //ech);
        foh.extend(toh, domInfo);
        CNS_INFO(logKey, ":toFP: New from version: '{}'", foh.idversion());
    }
    else {
        // Extend H(from) with H(to)
        // CNS_INFO(":from: Extending history of '{}' with '{}'", from.qn_, to.qn_);
        //TypeTransforms.at(from.qn_).extend(TypeTransforms.at(to.qn_));
        auto &foh = TypeTransforms.at(from.qn_);
        auto const &toh = TypeTransforms.at(to.qn_);
        CNS_INFO(logKey, ":from: Extending history '{}' with '{}'", foh.idversion(), toh.idversion());
        CNS_INFO(logKey, ":from: New from version: '{}'", foh.idversion());
        foh.extend(toh, domInfo);
        CNS_INFO(logKey, ":from: New from version: '{}'", foh.idversion());
    }

    CNS_DEBUG_MSG(logKey, "end");
}

void updateCensus(
        OpData &from,
        OpData &to,
        DominatorData const &dom) {

    auto const logKey = from.qn_ + "->" + to.qn_;
    CNS_DEBUG_MSG(logKey, "begin");

    addDomNode(from);
    auto it = census.find(to.qn_);
    if(it == std::end(census)) {
        // `to` not in census
        CNS_INFO(logKey, "<0> Inserting new node for 'to'('{}')", String(to));
        census.insert(makeCensusNode(to, dom));

        CNS_INFO(logKey, "Updating history with dom '{}'", String(dom));
        updateHistory(from, to, dom);
        CNS_DEBUG_MSG(logKey, "end");
        return;
    }

    CNS_INFO_MSG(logKey, "<0> 'to' already in census");
    chkNodeDataForChange(to);
    if(isNodeDominatorNew(to, dom)) {
        CNS_INFO(logKey, "<0> 'to' has new dominator '{}'", String(dom));
        appendNodeDominator(to, dom);
    }

    CNS_INFO(logKey, "Updating history with dom '{}'", String(dom));
    updateHistory(from, to, dom);
    CNS_DEBUG_MSG(logKey, "end");
}

void updateCensusNoHistory(
        OpData &from,
        OpData &to,
        DominatorData const &dom) {

    auto const logKey = from.qn_ + "->" + to.qn_;
    CNS_DEBUG_MSG(logKey, "begin");

    addDomNode(from);
    auto it = census.find(to.qn_);
    if(it == std::end(census)) {
        // `to` not in census
        CNS_INFO(logKey, "Inserting new node for 'to'('{}')", String(to));
        census.insert(makeCensusNode(to, dom));
        CNS_DEBUG_MSG(logKey, "end");
        return;
    }

    CNS_INFO_MSG(logKey, "'to' already in census");
    chkNodeDataForChange(to);
    if(isNodeDominatorNew(to, dom)) {
        CNS_INFO(logKey, "'to' has new dominator '{}'", String(dom));
        appendNodeDominator(to, dom);
    }

    //updateHistory(from, to);
    CNS_DEBUG_MSG(logKey, "end");
//}
}


void logCensusUpdate(
        OpData const &lhs,
        OpData const &rhs,
        DominatorData const &dom) {

    fmt::print(fOUT, "Match site: {}\n", rhs.location_);
    fmt::print(fOUT, "   Linking: [{}]{}{{{}}} -> [{}]{}{{{}}}\n",
            lhs.qn_, lhs.expr_, lhs.linkedParm_,
            rhs.qn_, rhs.expr_, rhs.linkedParm_);
    fmt::print(fOUT, "      from: [{}]{}\n", lhs.category_, lhs.type_);
    fmt::print(fOUT, "        to: [{}]{}\n", rhs.category_, rhs.type_);
    fmt::print(fOUT, "      expr: [{}]{}\n", dom.linkType(), dom.linkExpr());
    fmt::print(fOUT, "      origin condition: {}\n", String(dom.parentCondition()));
    //fmt::print(fOUT, "FuncsLinked: {}() -> {}()\n", lhs.container_, dom.callee_.value_or("(n/a)"));

}

template<CastSourceType CS_t, typename T>
void updateCensus(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CastExpr const &castExpr,
        clang::DeclRefExpr const &castSource,
        T const &dest) {

    auto const logKey = String(context, castExpr);
    CNS_DEBUG_MSG(logKey, "<T> begin");
    preprocess(context, dest);

    CNS_INFO_MSG(logKey, "<Cast> building lhs data.");
    auto lhs = buildOpData(context, sm, castExpr, castSource);
    CNS_INFO_MSG(logKey, "<Cast> building rhs data.");
    auto rhs = buildOpData<CS_t>(context, sm, castExpr, dest);
    // Cast kind update
    //rhs.castKind_ = castExpr.getCastKindName();

    auto dom = makeDominatorData(context, lhs, castExpr);

    updateCensus(lhs, rhs, dom);
    if(SEVERITY_FILTER & cns::logging::severity::Info) {
        logCensusUpdate(lhs, rhs, dom);
    }
    CNS_DEBUG_MSG(logKey, "<T> end");
}

void processCast(MatchFinder::MatchResult const &result) {
    constexpr auto logKey = "<CastMatch>";
    CNS_DEBUG_MSG(logKey, "begin");
    auto *context = result.Context;
    if(!context) {
        CNS_ERROR_MSG(logKey, "Null context");
        CNS_DEBUG_MSG(logKey, "end.");
        return;
    }
    assert(context);
    auto const *castExpr = result.Nodes.getNodeAs<CastExpr>("cast");
    if(!castExpr) {
        CNS_ERROR_MSG(logKey, "Null cast expr");
        CNS_DEBUG_MSG(logKey, "end.");
        return;
    }
    assert(castExpr);

    CNS_DEBUG(logKey, "Cast match at: '{}'", castExpr->getExprLoc().printToString(*result.SourceManager));
    CNS_DEBUG(logKey, "Cast : '{}'", String(*context, *castExpr));

    // Source
    auto const *s_unaryCastee = result.Nodes.getNodeAs<DeclRefExpr>("unaryCastee");

    auto const *binOp = result.Nodes.getNodeAs<BinaryOperator>("binOp");

    // Target
    auto const *unaryOp = result.Nodes.getNodeAs<UnaryOperator>("unaryOp");

    if(!!unaryOp) {
        CNS_DEBUG_MSG(logKey, "Processing cast: Unary operation");
        updateCensus<CastSourceType::UnaryOp>(*context, *result.SourceManager, *castExpr, *s_unaryCastee, *unaryOp);
    }
    else if(!!binOp) {
        CNS_DEBUG_MSG(logKey, "Processing cast: Binary operation");
        auto const *bl = result.Nodes.getNodeAs<DeclRefExpr>("lhsref");
        auto const *br = result.Nodes.getNodeAs<DeclRefExpr>("rhsref");
        if(!bl) {
            CNS_ERROR_MSG(logKey, "binop lHS == nullptr.");
            CNS_DEBUG_MSG(logKey, "end");
            return;
        }
        if(!br) {
            CNS_ERROR_MSG(logKey, "binop RHS == nullptr.");
            CNS_DEBUG_MSG(logKey, "end");
            return;
        }
        updateCensus<CastSourceType::BinaryOp>(*context, *result.SourceManager, *castExpr, *br, *bl);
    }

    CNS_DEBUG_MSG(logKey, "end");
}

void updateCensus(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::DeclRefExpr const &src,
        clang::VarDecl const &dest) {

    auto const logKey = String(context, src) + "->" + String(context, dest);
    CNS_DEBUG_MSG(logKey, "<declrefexpr, varDecl> begin");
    auto const *lhsDecl = src.getDecl();
    assert(lhsDecl);
    CNS_INFO_MSG(logKey, "<declrefexpr, varDecl> building lhs data.");
    auto lhs = buildOpData(context, sm, src, *lhsDecl);
    CNS_INFO_MSG(logKey, "<declrefexpr, varDecl> building rhs data.");
    auto rhs = buildOpData(context, sm, dest);

    DominatorData dom = makeDominatorData(context, lhs, dest);
    updateCensus(lhs, rhs, dom);
    if(SEVERITY_FILTER & cns::logging::severity::Info) {
        logCensusUpdate(lhs, rhs, dom);
    }
    CNS_DEBUG_MSG(logKey, "<declrefexpr, varDecl> end");
}

void processVar(MatchFinder::MatchResult const &result) {
    constexpr auto logKey = "<VarMatch>";
    CNS_DEBUG_MSG(logKey, "begin");
    assert(result);
    auto *context = result.Context;
    assert(context);

    // [VarDecl]    [DeclRefExpr]
    // [int *pi2] = [pi]
    // Census: {pi -> p2}
    //          LHS   RHS

    auto const *rhs = result.Nodes.getNodeAs<clang::VarDecl>("varDecl");
    assert(rhs);

    CNS_DEBUG(logKey, "VarDecl match at: '{}'", rhs->getLocation().printToString(*result.SourceManager));
    CNS_DEBUG(logKey, "VarDecl : '{}'", String(*context, *rhs));

    auto const *lhsRef = result.Nodes.getNodeAs<clang::DeclRefExpr>("assignee");
    auto const *lhsLit = result.Nodes.getNodeAs<clang::Expr>("literal");
    if(!lhsRef || lhsLit) {
        auto rhsData = buildOpData(*context, *result.SourceManager, *rhs);
        //auto const &lhsData = buildOpData(*context, *result.SourceManager, *lhsLit);
        census.insert(makeCensusSourceNode(rhsData));
        /*
        auto const &rhsData = buildOpData(*context, *result.SourceManager, *lhsLit);
        census.insert(makeCensusNode(rhsData));
        FOUT << "Match site: " << rhsData.location_ << "\n"
             << "   Linking: [literal] -> [" << rhsData.hash_ << "]"
             << rhsData.expr_ << "{" << rhsData.linkedParm_ << "}\n"
             << "       rhs: [" << rhsData.category_ << "] " << rhsData.type_ << "\n"
             << "          : inside " << rhsData.container_ << "()\n"
             << "\n";
        */
        CNS_INFO_MSG(logKey, "Skipping VarDecl init with a literal.");
        return;
    }

    assert(lhsRef);
    updateCensus(*context, *result.SourceManager, *lhsRef, *rhs);
    CNS_DEBUG_MSG(logKey, "end");
}

inline OpData buildLimitedArgOp(clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CallExpr const &call,
        clang::Expr const &arg) {

        return {
            cnsHash(context, arg),
            String(context, arg),
            Typename(context, arg),
            TypeCategory(context, arg),
            linkedParmPos(context, call, arg),
            getContainerFunction(context, arg),
            getLinkedRecord(arg),
            linkedTypeCategory(arg),
            call.getExprLoc().printToString(sm),
            qualifiedName(context, arg), //qualifiedName(context, call, arg)
            makeTypeDataExtra(context, sm, arg)
        };
}

/*
void processMidCall(clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CallExpr const &call,
        clang::DeclRefExpr const &src,
        clang::Expr const &dest) {

    auto const logKey = String(context, call) + "(): " + String(context, dest);
    CNS_DEBUG_MSG(logKey, "begin");

    CNS_DEBUG(logKey, "Building src op: '{}", String(context, src));
    auto from = buildOpData(context, sm, dest, src);

    CNS_DEBUG(logKey, "Building dest op: '{}", String(context, dest));
    OpData to = { cnsHash(context, dest),
                  String(context, dest),
                  Typename(context, dest),
                  TypeCategory(context, dest),
                  String(context, dest),
                  getContainerFunction(context, dest),
                  getLinkedRecord(dest),
                  linkedTypeCategory(dest),
                  call.getExprLoc().printToString(sm),
                  String(context, dest)}; //qualifiedName(context, call, dest) };

    // TODO: check if cast is involved
    DominatorData dom = {from, {}, {}};

    // Update census with this pair
    // This takes care of history as well so that later on it is sufficient to just use the expr as key instead of fetching declrefexpr from the arg.
    CNS_DEBUG_MSG(logKey, "updating census");
    updateCensus(from, to, dom);
    CNS_DEBUG_MSG(logKey, "end");
}
*/


OpData buildArgOp(clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CallExpr const &call,
        clang::Expr const &arg) {

    auto const logKey = String(context, call) + "(): " + String(context, arg);
    CNS_DEBUG_MSG(logKey, "begin");
    /*
    auto const *e = arg.IgnoreImplicit();
    if(!e) {
        CNS_DEBUG_MSG(logKey, "No expr from arg.IgnoreImplicit()");
        CNS_DEBUG_MSG(logKey, "end");
        return buildLimitedArgOp(context, sm, call, arg);
    }

    auto const *dre = getDREChild(e);
    if(!dre) {
        CNS_WARN(logKey, "Null DRE from expr '{}'", String(context, *e));
        CNS_WARN_MSG(logKey, "end");
        return buildLimitedArgOp(context, sm, call, *e);
    }

    CNS_DEBUG_MSG(logKey, "Found dre from child expr");

    //processMidCall(context, sm, call, *dre, *e);
    */
    auto const * vd = getArgDecl(context, arg);
    OpData to;

    if(vd) {
        CNS_DEBUG(logKey, "Found decl for arg '{}'", String(context, arg));
        to = {
                cnsHash(context, *vd),
                String(context, *vd),
                Typename(context, *vd),
                TypeCategory(context, *vd),
                linkedParmPos(context, call, arg),
                getContainerFunction(context, *vd),
                getLinkedRecord(*vd),
                linkedTypeCategory(*vd),
                call.getExprLoc().printToString(sm),
                qualifiedName(context, *vd, vd->getDeclName()),
                makeTypeDataExtra(context, sm, *vd)
            };
    }
    else {
        CNS_DEBUG(logKey, "Decl not found for arg '{}'; building OpData from arg expr", String(context, arg));
        to = {
                cnsHash(context, arg),
                String(context, arg),
                Typename(context, arg),
                TypeCategory(context, arg),
                //String(context, arg),
                linkedParmPos(context, call, arg),
                getContainerFunction(context, arg),
                getLinkedRecord(arg),
                linkedTypeCategory(arg),
                call.getExprLoc().printToString(sm),
                //String(context, arg), //
                qualifiedName(context, arg), //qualifiedName(context, call, arg)
                makeTypeDataExtra(context, sm, arg)
            };
    }

    CNS_DEBUG_MSG(logKey, "end");
    // 'to' will be the dom for the function param
    return to;
}

void buildOpDatas(clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CallExpr const &call) {

    auto const logKey = String(context, call);
    CNS_INFO_MSG(logKey, "begin");

    // for each arg
    unsigned pos = 0;
    std::for_each(call.arg_begin(), call.arg_end(),
        [&](auto const *arg) {
            CNS_INFO(logKey, "Building lhs(Arg) opData for '{}'", String(context, *arg));
            OpData lhs, rhs;
            // create source(arg) op
            lhs = buildArgOp(context, sm, call, *arg);

            DominatorData dom = makeDominatorData(context, lhs, *arg);

            // create target(param) op
            CNS_INFO_MSG(logKey, "Building rhs(Param) opData");
            auto const *parmd = getParamDecl(context, call, pos);
            if(parmd) {
                CNS_INFO(logKey, "Got ParamDecl '{}'", String(context, *parmd));
                auto const *parm = dyn_cast<clang::ParmVarDecl>(parmd);
                if(!parm) {
                    CNS_ERROR_MSG(logKey, "Got ParamDecl but no ParmVarDecl");
                    CNS_INFO_MSG(logKey, "end");
                    return;
                }

                rhs = {
                    cnsHash(context, *parm),
                    parm->getNameAsString(),
                    Typename(context, *parm),
                    TypeCategory(context, *parm),
                    String(context, call, pos),
                    getContainerFunction(context, *arg),
                    getLinkedRecord(*arg),
                    linkedTypeCategory(*arg),
                    call.getExprLoc().printToString(sm),
                    (call.getDirectCallee() != nullptr)
                        ? (call.getDirectCallee()->getQualifiedNameAsString() + ".$" + std::to_string(pos))
                        : ("NullCallee"),
                    makeTypeDataExtra(context, sm, *parm)
                };
            }

            else {
                CNS_INFO_MSG(logKey, "No ParamDecl.");

                // getcalleedecl() will not work
                auto const *fn = call.getCallee();
                if(fn) {
                    CNS_INFO_MSG(logKey, "Got Callee expr.");
                    // In case of fptr, it is likely that function decl is not available.
                    // Get the qn of fptr
                    std::string qns;
                    qns.reserve(64);
                    auto const *fptrp = call.IgnoreImplicit();
                    if(fptrp) {
                        CNS_INFO_MSG(logKey, "Got fptr from call expr after implicitignore.");
                        auto const * dre = dyn_cast<clang::DeclRefExpr>(fptrp);
                        if(dre) {
                            CNS_INFO_MSG(logKey, "Got dre from fptr.");
                            qns = qualifiedName(context, *dre);
                        }
                        else {
                            CNS_INFO_MSG(logKey, "No dre from fptr.");
                            for(auto child: call.children()) {
                                auto const *ce = dyn_cast<clang::CastExpr>(child);
                                if(ce) {
                                    CNS_INFO_MSG(logKey, "Got castexpr from fptr.");
                                    auto const *dre = dyn_cast<clang::DeclRefExpr>(ce->getSubExpr());
                                    if(dre) {
                                        CNS_INFO_MSG(logKey, "Got dre from castexpr.");
                                        qns = qualifiedName(context, *dre);
                                        break;
                                    }
                                    else {
                                        CNS_INFO_MSG(logKey, "No dre from castexpr.");
                                    }
                                }
                                else {
                                    CNS_INFO_MSG(logKey, "No castexpr from fptr.");
                                    auto const *dre = dyn_cast<clang::DeclRefExpr>(child);
                                    if(dre) {
                                        CNS_INFO_MSG(logKey, "Got dre from child.");
                                        qns = qualifiedName(context, *dre);
                                        break;
                                    }
                                    else {
                                        CNS_INFO_MSG(logKey, "No dre from child.");
                                    }
                                }
                            }
                            /*
                            auto cit = call.child_begin();
                            auto const * dre = dyn_cast<clang::DeclRefExpr>(*cit);
                            while(cit != call.child_end()) {
                                if(dre) {
                                    CNS_INFO_MSG("Got dre from cit.");
                                    ss << qualifiedName(context, *dre);
                                    break;
                                }
                                ++cit;
                            }
                            if(!dre) {
                                CNS_INFO_MSG("No dre from children.");
                                ss << qualifiedName(context, call, *fptrp);
                            }
                            */
                        }
                    }
                    else {
                        CNS_INFO_MSG(logKey, "No fptr from call expr after implicitignore.");
                    }
                    if(qns.empty()) {
                        qns = String(context, *fn); // + ".$" + to_string(pos);
                    }
                    qns  += ".$" + std::to_string(pos);
                    rhs = {
                        cnsHash(context, *arg),
                        qns,
                        "",//Typename(context, *arg),     // Since we can't get decl from callee expr.
                        "",// TypeCategory(context, *arg),
                        String(context, call, pos),
                        getContainerFunction(context, *arg),
                        getLinkedRecord(*arg),
                        linkedTypeCategory(*arg),
                        call.getExprLoc().printToString(sm),
                        qns,
                        makeTypeDataExtra(context, sm, *arg)
                    };
                }
                else {
                    CNS_INFO_MSG(logKey, "No Callee either.");
                    rhs = {
                        cnsHash(context, *arg),
                        String(context, *arg),
                        "", //Typename(context, *arg),
                        "", //TypeCategory(context, *arg),
                        String(context, call, pos),
                        getContainerFunction(context, *arg),
                        getLinkedRecord(*arg),
                        linkedTypeCategory(*arg),
                        call.getExprLoc().printToString(sm),
                        "Resolve Func from callexpr_.$" + std::to_string(pos),
                        makeTypeDataExtra(context, sm, *arg)
                    };
                }
            }

            // update census()
            updateCensusNoHistory(lhs, rhs, dom);
            // H(to) will be added through addCallHistory()
            //updateHistory(lhs, rhs);
            /*
            auto itTo = std::find(begin(TypeTransforms), end(TypeTransforms), rhs.qn_);
            if(itTo == std::end(TypeTransforms)) {
                // Add H(to)
                FOUT << "[INFO ](updateHistory) :to: New history started for " << rhs.qn_ << "\n";
                TypeTransforms.emplace_back(rhs.qn_);
            }
            */
            if(TypeTransforms.find(lhs.qn_) == std::end(TypeTransforms)) {
                // Add H(from)
                CNS_INFO(logKey, ":from: New history started for {}", lhs.qn_);
                TypeTransforms.emplace(lhs.qn_, lhs.qn_);
                //TypeTransforms.insert({lhs.qn_, History(lhs.qn_)});
            }
            if(TypeTransforms.find(rhs.qn_) == std::end(TypeTransforms)) {
                // Add H(to)
                CNS_INFO(logKey, ":to: New history started for {}", rhs.qn_);
                TypeTransforms.emplace(rhs.qn_, rhs.qn_);
                //TypeTransforms.insert({rhs.qn_, History(rhs.qn_)});
                // Why is this needed?
                //TypeTransforms.at(lhs.qn_).extend(TypeTransforms.at(rhs.qn_));
            }
            if(SEVERITY_FILTER & cns::logging::severity::Info) {
                logCensusUpdate(lhs, rhs, dom);
            }
            pos++;
        });
    CNS_INFO_MSG(logKey, "end");
}

void addCallHistory(clang::ASTContext & context, clang::CallExpr const& call) {
    auto const logKey = String(context, call) + "()";
    CNS_INFO_MSG(logKey, "begin");
    auto const *calledFn = getCalleeDecl(context, call);
    assert(calledFn);
    std::string fn;
    if(!calledFn) {
        CNS_ERROR_MSG(logKey, "Null callee decl. Maybe an fptr.");

        auto const *fptr = getFptrFromFptrCall(context, call);
        if(!fptr) {
            CNS_ERROR_MSG(logKey, "No fptr either. end");
            return;
        }

        fn = qualifiedNameFromFptrCall(context, call);
        auto it = std::find(begin(TransformTemplates), end(TransformTemplates), fn);
        if(it == std::end(TransformTemplates)) {
            CNS_DEBUG(logKey, "No template found for: {}()", fn);
            CNS_DEBUG(logKey, "Adding new template for: {}()", fn);
            TransformTemplates.push_back({context, call, *fptr});
        }
    }
    else {
        fn = calledFn->getNameAsString();
    }
    auto it = std::find(begin(TransformTemplates), end(TransformTemplates), fn);
    if(it == std::end(TransformTemplates)) {
        CNS_DEBUG(logKey, "No template found for: {}()", fn);
        CNS_DEBUG(logKey, "Adding new template for: {}()", fn);
        // Likely to happen.
        // Create new template from calleeDecl and instantiate.
        TransformTemplates.push_back({*calledFn});
        auto it2 = std::find(begin(TransformTemplates), end(TransformTemplates), fn);
        if(it2 == std::end(TransformTemplates)) {
            CNS_ERROR(logKey, "New template insertion failed for {}()", fn);
            CNS_DEBUG_MSG(logKey, "end");
            return;
        }
        else {
            it = it2;
        }
    }

    // Instantiate template and add to history
    auto hs = it->instantiate(context, call);
    if(hs.size() != call.getNumArgs()) {
        CNS_ERROR_MSG(logKey, "History count does not match arg count, cannot assign history to args.");
        CNS_DEBUG_MSG(logKey, "end");
        return;
    }

    // For each arg operand opA, H(opA) is extended by H(A).
    unsigned i = 0;
    std::for_each(call.arg_begin(), call.arg_end(), [&](auto const *a) {
            CNS_DEBUG_MSG(logKey, "for_each arg");
            // get key for a
            auto const argQn = qualifiedName(context, *a); //qualifiedName(context, call, *a);

            // Contextualized history = local history; arg history gets extended by local contextual parm history
            // Search history of a
            if(TypeTransforms.find(argQn) != std::end(TypeTransforms)) {
                CNS_DEBUG(logKey, "Found existing history for {}", argQn);
                // extend history
                if(i < hs.size()) {
                    CNS_INFO(logKey, "Extending history for {} with {}", argQn, hs[i].id());
                    TypeTransforms.at(argQn).extend(hs.at(i));
                }
                else {
                    CNS_ERROR_MSG(logKey, "Out of bound history insert.");
                }
            }
            else {
                // add new history
                CNS_DEBUG(logKey, "Adding new history for {}", argQn);
                //TypeTransforms.insert({hs[i].opId(), hs[i]});
                auto hqn = History(argQn);
                CNS_DEBUG(logKey, "Extending history for {} with {}", argQn, hs[i].id());
                hqn.extend(hs[i]);
                TypeTransforms.emplace(argQn, std::move(hqn));
                //TypeTransforms.insert({qn, hqn});
            }
            ++i;
        });
    CNS_DEBUG_MSG(logKey, "end");
}

// For call expressions:
void preprocess(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CallExpr const &call) {

    auto const logKey = String(context, call);
    CNS_DEBUG_MSG(logKey, "begin");
    auto const *calledFn = getCalleeDecl(context, call);
    assert(calledFn);
    if(!calledFn) {
        CNS_ERROR_MSG(logKey, "Null callee decl");
        CNS_DEBUG_MSG(logKey, "end");
        return;
    }

    auto const& fn = calledFn->getNameAsString();

    //if(sm.isInSystemHeader(call.getExprLoc()) || sm.isInExternCSystemHeader(call.getExprLoc())) {
    auto const fcs = sm.getFileCharacteristic(call.getExprLoc());
    if(clang::SrcMgr::isSystem(fcs)) {
        ignoreFunctions.push_back(fn);
        CNS_INFO(logKey, "Ignoring system function: {}", String(context, *calledFn));
        CNS_DEBUG_MSG(logKey, "end");
        return; // ignore
    }
    if(std::find(begin(ignoreFunctions), end(ignoreFunctions), fn) != end(ignoreFunctions)) {
        CNS_INFO(logKey, "Skipping ignored function: {}", String(context, *calledFn));
        CNS_DEBUG_MSG(logKey, "end");
        return; // ignore
    }

    auto h = cnsHash(context, *calledFn);
    if(std::find(begin(seenFunctions), end(seenFunctions), h) != end(seenFunctions)) {
        CNS_INFO(logKey, "Skipping seen function: {}", String(context, *calledFn));
        CNS_DEBUG_MSG(logKey, "end");
        return; // seen
    }

    // - Process function will
    //      - create operands for all args and params (new)
    //      - populate census with the new operands (exists)
    if(calledFn->hasBody()) {
        auto const *body = calledFn->getBody();
        assert(body);
        MatchFinder m;
        m.match(*body, context);
    }
    seenFunctions.push_back(h);

    //      - create histories, templates and contexts (partially exists)
    // TODO: make template by matching function signature through matcher
    // At this point, function is processed, so census should have operands for its parameters.
    //auto const& ht = HistoryTemplate(*calledFn);
    // Perhaps that's not true TODO Check
    auto it = std::find(begin(TransformTemplates), end(TransformTemplates), fn);
    if(it == std::end(TransformTemplates)) {
        CNS_INFO(logKey, "Adding template for function: {}()", fn);
        TransformTemplates.push_back({*calledFn});
    }
    else {
        CNS_INFO(logKey, "Template for function: {}() exists", fn);
    }
    // TODO end

    CNS_DEBUG_MSG(logKey, "end");
}

// Earlier, all process functions would take ASTContext and match result as input
// And populate Census with operands
//
// Now all process functions additionally populate HistoryRecords with History(operand + context)
// For var/cast/assignment expressions:
// - Process function will
//      - create operands for both sides (exists)
//      - populate census with the new operands (exists)
//      - create histories and contexts (partially exists)
//
//
void processFunctionCall(MatchFinder::MatchResult const &result) {
    constexpr auto logKey = "<FunctionCallMatch>";
    CNS_DEBUG_MSG(logKey, "begin");

    assert(result);
    auto *context = result.Context;
    assert(context);

    auto const *call = result.Nodes.getNodeAs<CallExpr>("ce");
    assert(call);
    CNS_DEBUG(logKey, "Match at: '{}'", call->getExprLoc().printToString(*result.SourceManager));
    CNS_DEBUG(logKey, "Call : '{}'", String(*context, *call));

    auto const *calledFn = getCalleeDecl(*context, *call);
    if(calledFn) {
        auto const& fn = calledFn->getNameAsString();
        auto const fcs = result.SourceManager->getFileCharacteristic(call->getExprLoc());
        if(clang::SrcMgr::isSystem(fcs)) {
            ignoreFunctions.push_back(fn);
            CNS_INFO(logKey, "Ignoring system function: {}", fn);
            CNS_DEBUG_MSG(logKey, "end");
            return; // ignore
        }
    }
    /*
    else {
        CNS_INFO(logKey, "Cannot find decl for call '{}', skipping", String(*context, *call));
        CNS_DEBUG_MSG(logKey, "end");
        return;
    }
    */

    // - Preprocess call.
    preprocess(*context, *result.SourceManager, *call);
    // update census
    buildOpDatas(*context, *result.SourceManager, *call);
    //
    addCallHistory(*context, *call);

    CNS_DEBUG_MSG(logKey, "end");
}

//----------------------------------------------------------------------------
// MATCHERS

// similar construct can match a function ptr. {VarDecl, DeclRefExpr}
// to support binary operator assignment, both lhs/rhs dre in binop should be linked with lhs of '='
DeclarationMatcher AssignMatcher =
    //anyOf(
        varDecl(
            anyOf(
                hasDescendant(declRefExpr().bind("assignee")),
                hasDescendant(expr().bind("literal")))
            ).bind("varDecl");
        //);

auto CallMatcher = callExpr().bind("ce");
            //hasDescendant(
            //    unaryOperator(
            //        hasDescendant(declRefExpr().bind("ceFnArg"))
            //        ))).bind("ce");

StatementMatcher CastMatcher =
    castExpr(
            anyOf(
                hasDescendant(
                    unaryOperator(
                        hasDescendant(declRefExpr().bind("unaryCastee"))
                        ).bind("unaryOp")),

                // lhs: declrefexpr or expr(hasDescendant(declrefexpr))
                // rhs: declrefexpr or expr(hasDescendant(declrefexpr)) or literal
                // Assignment involves ltor cast unless a literal is used.
                hasParent(
                    binaryOperator(
                        isAssignmentOperator(),
                        hasLHS(expr(
                                anyOf(
                                    declRefExpr().bind("lhsref"),
                                    hasDescendant(declRefExpr().bind("lhsref"))
                                )).bind("binLhs")),
                        hasRHS(expr(
                                anyOf(
                                    declRefExpr().bind("rhsref"),
                                    hasDescendant(declRefExpr().bind("rhsref"))
                                )).bind("binRhs"))
                    ).bind("binOp")))

    ).bind("cast");

auto StatCastMatcher = castExpr(hasCastKind(CK_BitCast)).bind("statCast");
// TODO TODO Doesn't include fptr params
auto StatPointerMatcher = varDecl(hasType(pointerType())).bind("statPointer");

class StatMatchCallback: public MatchFinder::MatchCallback {
public:
    void run(MatchFinder::MatchResult const &result) override {
        // Cast expression
        auto const *castExpr = result.Nodes.getNodeAs<clang::CastExpr>("statCast");
        // Pointer
        auto const *ptr = result.Nodes.getNodeAs<clang::VarDecl>("statPointer");

        auto *context = result.Context;

        if(ptr) {
            auto const &key = qualifiedName(*context, *ptr);
            auto idPattern = detectedPattern(key);

            auto qt = ptr->getType();

            // If void*, update voidpointers
            if(qt->isVoidPointerType()) {
                voidPointers_.emplace(key, idPattern);
            }

            // If non-function pointers, update pointers
            if(!qt->isFunctionPointerType()) {
                pointers_.emplace(key, idPattern);
            }
            else {
                // Get the parameters from fptr
                auto const pointee= qt->getPointeeType();

                auto const * fpt = pointee->getAs<clang::FunctionProtoType>();
                if(!fpt) {
                    CNS_ERROR("statfp", "Cannot get function type for fptr: {}", key);
                }
                else {
                    CNS_ERROR("statfp", "Found function proto type for fptr: {}", key);
                    unsigned pos = 0;
                    std::for_each(fpt->param_type_begin(), fpt->param_type_end(),
                        [&](auto const &parmType) {
                            auto pkey = key + ".$" + std::to_string(pos);
                            auto pPattern = detectedPattern(pkey);

                            // If void*, update voidpointers
                            if(parmType->isVoidPointerType()) {
                                voidPointers_.emplace(pkey, pPattern);
                            }

                            // If non-function pointers, update pointers
                            if(parmType->isPointerType() && !parmType->isFunctionPointerType()) {
                                pointers_.emplace(pkey, pPattern);
                            }
                            pos++;
                        });
                }
            }
        }

        if(castExpr) {
            auto const &key = qualifiedName(*context, *castExpr);
            casts_.emplace(key, detectedPattern(key));
            nbCasts_++;
        }

        castExpr = nullptr;
        ptr = nullptr;
    }

    // TODO - for each pointer or void pointer
    // see if the pointer is typed with a pattern in ScoreSummary
    // see if the void pinter is typed
    // collect casts of void pointer with the pointer
    void print() {
        constexpr auto logKey = "StatSource";
        fmt::print(fOUT, "[{}] Total Stat'd BitCasts: {}\n", logKey, casts_.size());
        fmt::print(fOUT, "[{}] Census Patterned BitCasts: {}\n", logKey, casts_.size());
        fmt::print(fOUT, "[{}] Total Pointers: {}\n", logKey, pointers_.size());
        fmt::print(fOUT, "[{}] Total void *: {}\n", logKey, voidPointers_.size());

        auto countPattern = [](Stat const &collection, auto pattern) {
            return std::count_if(std::execution::par, begin(collection), end(collection),
                    [&](auto const &node) {
                        return node.second == pattern;
                    });
        };
        auto generics = countPattern(voidPointers_, Pattern::generic);
        auto subtypes = countPattern(voidPointers_, Pattern::subtyping);
        auto reinterpret = countPattern(voidPointers_, Pattern::reinterpret);
        auto wild = countPattern(voidPointers_, Pattern::wild);
        auto unchecked = countPattern(voidPointers_, Pattern::unchecked);

        fmt::print(fOUT, "[{}] Total typed pointers: {}\n", logKey, generics + subtypes + reinterpret);
        fmt::print(fOUT, "[{}] Wild (untyped) pointers: {}\n", logKey, wild);
        fmt::print(fOUT, "[{}] Unchecked pointers (not found in Census): {}\n", logKey, unchecked);
        fmt::print(fOUT, "[{}] Generics: {}\n", logKey, generics);
        fmt::print(fOUT, "[{}] Subtypes: {}\n", logKey, subtypes);
        fmt::print(fOUT, "[{}] Reinterpret: {}\n", logKey, reinterpret);

        auto printPattern = [](Stat const &collection, auto const &label, auto pattern) {
            constexpr auto logKey = ">---";
            fmt::print(fOUT, "{} {}:\n", logKey, label);
            std::for_each(begin(collection), end(collection),
                    [&](auto const &node) {
                        if(node.second == pattern) {
                            fmt::print(fOUT, "{}, ", node.first);
                        }
                    });
            fmt::print(fOUT, "END <--\n");
        };

        printPattern(voidPointers_, "Generics found", Pattern::generic);
        printPattern(voidPointers_, "Unchecked list", Pattern::unchecked);
        printPattern(voidPointers_, "Wild list", Pattern::wild);
    }

private:
    enum class Pattern {
        generic = 0,
        subtyping,
        reinterpret,
        wild,
        unchecked
    };

    Pattern detectedPattern(CensusKey const &key) {
        if(TypeSummaries.find(key) == std::end(TypeSummaries)) {
            return Pattern::unchecked;
        }

        if(isPotentiallyGeneric(key)) {
            return Pattern::generic;
        }
        if(isPotentiallySubtype(key)) {
            return Pattern::subtyping;
        }
        if(isReinterpret(key)) {
            return Pattern::reinterpret;
        }
        return Pattern::wild;
    }

    using Stat = std::unordered_map<CensusKey, Pattern>;

    unsigned nbCasts_ = 0;
    Stat casts_;
    Stat pointers_;
    Stat voidPointers_;

};

/*
StatementMatcher CastMatcher2 =
    castExpr(
        allOf(
            hasCastKind(CK_LValueToRValue),
            anyOf( // technically just any of expr or decl is needed.
                hasAncestor(declStmt().bind("var")),
                hasAncestor(binaryOperator().bind("binop")),
                hasAncestor(callExpr().bind("call")),
                hasAncestor(expr().bind("gexpr"))),
                hasDescendant(declRefExpr().bind("castee")))
        ).bind("cast");
*/
// TODO: Add missing cast dumps. For example in other cast types.(?).

//---
unsigned SUMMARY_DEPTH = 0;

class CastMatchCallback: public MatchFinder::MatchCallback {
public:
    void run(MatchFinder::MatchResult const &result) override {
        assert(result);

        // Cast expression
        auto const *castExpr = result.Nodes.getNodeAs<clang::CastExpr>("cast");
        // Decl with/without cast
        auto const *varDecl = result.Nodes.getNodeAs<clang::VarDecl>("varDecl");
        // Calls for fn calls
        auto const *ce = result.Nodes.getNodeAs<clang::CallExpr>("ce");

        if(castExpr) {
            processCast(result);
        }

        if(varDecl) {
            processVar(result);
        }

        if(ce) {
            processFunctionCall(result);
        }

        /* Dumps the whole AST!
        std::cout << "TUD:\n";
        auto *tud = context->getTranslationUnitDecl();
        tud->dumpAsDecl();
        */

        /*
        if(!FOUT.is_open()) {
            std::cout << "File open error.\n";
            return;
        }
        */

        constexpr auto logKey = "<summary>";
        CNS_INFO_MSG(logKey, "# Census summary so far:");
        censusSummary();
        CNS_INFO_MSG(logKey, "# end Census summary so far");

        /*
        std::for_each(begin(TypeTransforms), end(TypeTransforms),
            [&](auto &h) {
                elaborateHistory(h.second); //, {3});
            });

        FOUT << "History collection:\n";
        std::cout << "History collection:\n";
        std::for_each(begin(TypeSummaries), end(TypeSummaries),
            [&](auto const &s) {
                FOUT << "History of (" << s.first << "):\n";
                std::cout << "History of (" << s.first << "):\n";
                summarize(FOUT, s.second, SUMMARY_DEPTH);
                summarize(std::cout, s.second, SUMMARY_DEPTH);
                FOUT << "\n";
                std::cout << "\n";
                //FOUT << s.second << "\n";
                //std::cout << s.second << "\n";
            });
        FOUT << "# Census summary end\n";
        FOUT << "end History collection:\n";
        */
        /*
        std::for_each(begin(TypeTransforms), end(TypeTransforms), [&](auto const &h) {
                FOUT << "History of (" << h.first << "):\n";
                std::cout << "History of (" << h.first << "):\n";
                FOUT << h.second << "\n";
                //dumpHistory(FOUT, h.second);
                std::cout << h.second << "\n";
            });
        FOUT << "# Census summary end\n";
        FOUT << "end History collection:\n";
        */

        // TODO: Emit error when a cast destination is incompatible with source/parent types.
    }
};

// Matcher code end
//-----------------------------------------------------------------------------------------


///////////////////////////////////////////////////////////////////////////////////////////
// Build:
//  cd <llvm dir>/build
//  ninja cast-chk
//
// Steps to execute:
//  bin/cast-chk <path/to>/qsort.c
//
//  (dump will be created in <exe dir>/census-dump.txt)
//-----------------------------------------------------------------------------------------

// Apply a custom category to all cli options so that they are the only ones displayed
static llvm::cl::OptionCategory tccCategory("tcc run options");

static cl::opt<int> optSummaryDepth(
        "summary-depth",
        cl::desc("Control depth of summary tree in output"),
        cl::init(4), cl::cat(tccCategory));

static cl::opt<unsigned> optVerbosity(
        "v",
        cl::desc("Control output log level: 0(None), 1(Errors), 2(Warnings), 3(Info), 4(Debug)"),
        cl::init(0), cl::cat(tccCategory));

static cl::opt<bool> optIgnoreCDB(
        "no-cdb",
        cl::desc("Ignore compile db and use input c filenames"),
        cl::init(false), cl::cat(tccCategory));

static cl::opt<bool> optDumpJSON(
        "json",
        cl::desc("Dump summary to a json file"),
        cl::init(false), cl::cat(tccCategory));

static cl::opt<bool> optTimeTrace(
        "time-trace",
        cl::desc("Enable time traces"),
        cl::init(false), cl::cat(tccCategory));

static cl::opt<bool> optIntentDiscovery(
        "hlid",
        cl::desc("Enable high-level intent or pattern detection for pointers"),
        cl::init(true), cl::cat(tccCategory));

// CommonOptionsParser declares HelpMessage with a description of the common cli options
// related to the compilation db and input files. (Nice to have help)
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);

// Help message for this specific tool.
static cl::extrahelp Morehelp("\nMore help text...\n");

void buildIgnoreList();
void regularizeCensusTypes();
void printCollection();
void printSummaryToJson();
void printScores();

std::vector<std::string> filterC(CompilationDatabase const& cdb);
std::vector<std::string> filterC(std::vector<std::string> input);

int main(int argc, const char **argv) {
    fOUT = fopen("census-dump.txt", "w");
    if(fOUT == nullptr) {
        fmt::print(stderr, "Error opening census-dump.txt\n");
        return 1;
    }

    auto ExpectedParser = CommonOptionsParser::create(argc, argv, tccCategory);
    if(!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }

    CommonOptionsParser &OptionsParser = ExpectedParser.get();

    SUMMARY_DEPTH = optSummaryDepth;
    switch(optVerbosity) {
        case 0: // None
            SEVERITY_FILTER = 0; break;
        case 1: // Errors
            SEVERITY_FILTER = 8; break;
        case 2: // Warnings
            SEVERITY_FILTER = 12; break;
        case 3: // Info
            SEVERITY_FILTER = 14; break;
        case 4: // Debug
            SEVERITY_FILTER = 15; break;
        default: // Turn on errors
            SEVERITY_FILTER = 8; break;
    }
    TIME_TRACE = optTimeTrace;

    std::vector<std::string> cfiles;
    if(!optIgnoreCDB) {
        auto &cdb = OptionsParser.getCompilations();
        cfiles = filterC(cdb);
        if(cfiles.empty()) {
            // No files to process
            fmt::print("Compile DB has no C files to process!");
            return 1;
        }
    }
    else {
        cfiles = filterC(OptionsParser.getSourcePathList());
        if(cfiles.empty()) {
            fmt::print("No C file in input!");
            return 1;
        }
    }

    ClangTool Tool(OptionsParser.getCompilations(),
                   cfiles);

    CastMatchCallback historian;
    MatchFinder Finder;
    Finder.addMatcher(AssignMatcher, &historian);
    Finder.addMatcher(CallMatcher, &historian);
    Finder.addMatcher(CastMatcher, &historian);

    buildIgnoreList();
    //return Tool.run(newFrontendActionFactory<clang::SyntaxOnlyAction>().get());
    //return Tool.run(newFrontendActionFactory(&Finder).get());
    auto rc = Tool.run(newFrontendActionFactory(&Finder).get());
    regularizeCensusTypes();
    elaborateHistories();
    if(optDumpJSON) {
        printSummaryToJson();
    }
    // Printing collection seems to add default (blank) entries to census which pollutes
    // the json output. Unclear why it is happening. There is no explicit census insertion in printCollection();
    printCollection();

    if(optIntentDiscovery) {
        printScores();
    }

    StatMatchCallback statistician;
    MatchFinder statFinder;
    statFinder.addMatcher(StatCastMatcher, &statistician);
    statFinder.addMatcher(StatPointerMatcher, &statistician);
    //statFinder.addMatcher(StatVoidPointerMatcher, &statistician);
    rc = Tool.run(newFrontendActionFactory(&statFinder).get());
    statistician.print();

    fclose(fOUT);
    return rc;
}

std::vector<std::string> filterC(std::vector<std::string> input) {
    std::vector<std::string> verified_sources;
    for(auto const& s: input) {
        // Check that source has a valid path
        SmallString<255> AbsPath;
        if(s.substr(s.size()-2) == ".c") {
            if(!(llvm::sys::fs::real_path(s, AbsPath))) {
                verified_sources.push_back(s);
            }
        }
    }
    return verified_sources;
}

std::vector<std::string> filterC(CompilationDatabase const& cdb) {
    auto sources = cdb.getAllFiles();
    return filterC(sources);
}

// Tee data on all output streams
inline void tprint(std::string const& data) {
    std::fprintf(fOUT, "%s", data.c_str());
    std::printf("%s", data.c_str());
}

// From nlohman json
std::size_t getEscapesSize(std::string const &s)
{
    std::size_t result = 0;

    for (const auto& c : s)
    {
        switch (c)
        {
            case '"':
            case '\\':
            case '\b':
            case '\f':
            case '\n':
            case '\r':
            case '\t':
            {
                // from c (1 byte) to \x (2 bytes)
                result += 1;
                break;
            }

            default:
            {
                if (c >= 0x00 and c <= 0x1f)
                {
                    // from c (1 byte) to \uxxxx (6 bytes)
                    result += 5;
                }
                break;
            }
        }
    }

    return result;
}

std::string json_escape(std::string const &s) {
    // create a result string of necessary size
    const auto nbEscapes = getEscapesSize(s);
    if(nbEscapes == 0) {
        return s;
    }

    std::string result(s.size() + nbEscapes, '\\');
    std::size_t pos = 0;

    for (const auto& c : s)
    {
        switch (c)
        {
            // quotation mark (0x22)
            case '"':
            {
                result[pos + 1] = '"';
                pos += 2;
                break;
            }

            // reverse solidus (0x5c)
            case '\\':
            {
                // nothing to change
                pos += 2;
                break;
            }

            // backspace (0x08)
            case '\b':
            {
                result[pos + 1] = 'b';
                pos += 2;
                break;
            }

            // formfeed (0x0c)
            case '\f':
            {
                result[pos + 1] = 'f';
                pos += 2;
                break;
            }

            // newline (0x0a)
            case '\n':
            {
                result[pos + 1] = 'n';
                pos += 2;
                break;
            }

            // carriage return (0x0d)
            case '\r':
            {
                result[pos + 1] = 'r';
                pos += 2;
                break;
            }

            // horizontal tab (0x09)
            case '\t':
            {
                result[pos + 1] = 't';
                pos += 2;
                break;
            }

            default:
            {
                if (c >= 0x00 and c <= 0x1f)
                {
                    // print character c as \uxxxx
                    sprintf(&result[pos + 1], "u%04x", int(c));
                    pos += 6;
                    // overwrite trailing null character
                    result[pos] = '\\';
                }
                else
                {
                    // all other characters are added as-is
                    result[pos++] = c;
                }
                break;
            }
        }
    }

    return result;
}

void printOpDataToJson(FILE *fp) {
    tprint("START OpData JSON Dump\n");
    fmt::print(fp, "\"OpDatas\": \n[\n");

    auto printOpJson = [&](auto const &op, bool delim = true) {
        fmt::print(fp, "{{");
        fmt::print(fp, "\"id\": \"{}\", ", json_escape(op.qn_));
        fmt::print(fp, "\"type\": \"{}\", ", json_escape(op.type_));
        fmt::print(fp, "\"category\": \"{}\", ", json_escape(op.category_));
        fmt::print(fp, "\"location\": \"{}\"", json_escape(op.location_));
        if(delim) {
            fmt::print(fp, "}},\n");
        }
        else {
            fmt::print(fp, "}}\n");
        }
    };

    auto csize = census.size();
    fmt::print(fOUT, "Census size: {}\n", csize);

    decltype(census)::size_type pos = 0;
    for(auto const &node: census) {
        auto const &op = ops(node);
        if(pos++ == csize - 1) {
            printOpJson(op, false);
        }
        else {
            printOpJson(op);
        }
    }

    fmt::print(fp, "]\n");
    tprint("END OpData JSON Dump\n");
}

std::string getSummaryJson(TypeSummary const &ts, unsigned indent = 0) {
    std::string vertices;
    vertices.reserve(1024);
    auto const& nexts = ts.nexts();
    if(!nexts.empty()) {
        auto vertexStr = [&](auto const &nextTs, bool delim = true) {
            vertices.append("\n" + space(indent));
            vertices.append(getSummaryJson(nextTs, indent + 4));
            if(delim) {
                vertices.append(",");
            }
            else {
                vertices.append("\n");
            }
        };

        std::for_each(begin(nexts), end(nexts) - 1,
                [&](auto const &s) {
                    vertexStr(s, true);
                });
        vertexStr(nexts[nexts.size() - 1], false);
        vertices.append(space(indent ? indent - 4 : 0));
    }

    auto const &linkInfo = ts.linkInfo();
    std::string summary = "{\"SummaryID\": \"" + json_escape(ts.key())
        + "\", \"CastKind\": \"" + json_escape(linkInfo.castKind())
        + "\", \"ExprType\": \"" + json_escape(linkInfo.exprType())
        + "\", \"Expr\": \"" + json_escape(linkInfo.linkExpr())
        + "\", \"Condition\": \"" + json_escape(String(linkInfo.parentCondition()))
        + "\", \"Nexts\": [" + std::move(vertices) + "]}";

    return summary;
}

void printSummaryToJson() {
    auto fjOut = fopen("census-summary.json", "w");
    if(fjOut == nullptr) {
        fmt::print(stderr, "Error opening census-summary.json\n");
        return;
    }

    fmt::print(fjOut, "{{");
    printOpDataToJson(fjOut);
    tprint("START Summary JSON Dump\n");
    fmt::print(fjOut, ", \"TypeSummaries\":\n[\n ");
    decltype(TypeSummaries)::size_type pos = 0;
    auto tsSize = TypeSummaries.size();
    for(auto const &[_, ts]: TypeSummaries) {
        if(pos++ == tsSize - 1) {
            fmt::print(fjOut, "{}\n", getSummaryJson(ts, 4));
        }
        else {
            fmt::print(fjOut, "{},\n", getSummaryJson(ts, 4));
        }
    }

    fmt::print(fjOut, "]\n}}");
    fclose(fjOut);
    tprint("END Summary JSON Dump\n");
}

void printCollection() {
    LOG_FUNCTION_TIME;

    auto teeStat = [](auto const &stat) {
        stat.print(fOUT);
        stat.print(stdout);
    };

    CastStat tcst("Total Cast Statistics");
    tprint("History collection:\n");
    std::for_each(begin(TypeSummaries), end(TypeSummaries),
        [&](auto const &s) {
            CastStat cst("Cast stats for " + s.first);
            tprint(fmt::format("History of ({}) [{}]:\n", s.first, ops(s.first).location_));
            auto tsummary = s.second.summarize(cst, {SUMMARY_DEPTH});
            tcst.extend(cst);

            tprint(tsummary);
            tprint("\n\n");
            teeStat(cst);
            tprint("\n");

            std::fflush(fOUT);
            std::fflush(stdout);
        });
    teeStat(tcst);
}

inline void printScore(CensusKey const &op, unsigned score, std::string const &types) {
    tprint(fmt::format("{} [{}] <{}>: {}\n", op, ops(op).location_, score, types));
}

inline void printInScore(CensusKey const &op, Score_t scores) {
    printScore(op, scores.at(op).inScore(), scores.at(op).inTypes());
}

inline void printOutScore(CensusKey const &op, Score_t scores) {
    printScore(op, scores.at(op).outScore(), scores.at(op).outTypes());
}

void printPatternFinds(std::string label,
        Score_t scores,
        bool (*checker)(CensusKey const&),
        void (*printer)(CensusKey const&, Score_t)) {

    tprint(label + ":\n");
    unsigned count = 0;
    std::for_each(begin(scores), end(scores),
        [&](auto const &node) {
            if(checker(node.first)) {
                printer(node.first, scores);
                count++;
            }
        });
    tprint(fmt::format("Total ({}): {}\n", label, count));
}

void printScores() {
    LOG_FUNCTION_TIME;
    initScores();
    {
        LogTime scoreTime("Complete Score Summary");
        std::for_each(std::execution::par, begin(TypeSummaries), end(TypeSummaries),
            [](auto const &node) {
                scoreSummary(node.second);
            });
    }

    /*
    tprint("Summarized scores:\n");
    std::for_each(begin(SummarizedScores), end(SummarizedScores),
        [](auto const &node) {
            tprint(fmt::format("{}: in({}), out({})\n", node.first, node.second.inScore(), node.second.outScore()));
        });
    tprint("\n");
    */

    printPatternFinds("Possible generic uses", SummarizedGenericScores,
            isPotentiallyGeneric, printInScore);
    printPatternFinds("Possible subtype uses", SummarizedSubtypingScores,
            isPotentiallySubtype, printOutScore);
    printPatternFinds("Possible reinterpret casts", SummarizedReinterpretScores,
            isReinterpret, printOutScore);
}

void regularizeCensusTypes() {
    std::for_each(std::execution::par, begin(census), end(census),
        [](auto &node) {
            auto &op = ops(node);
            if(op.type_.empty()) {
                op.type_ = "T";
            }
        });
}

void buildIgnoreList() {
    std::ifstream in;
    in.open("cstdlib.ignore", std::ios::in);
    for(std::string l; std::getline(in, l); ) {
        ignoreFunctions.push_back(l.substr(l.find_last_of(',') + 1));
    }
}

// TODO
// - Qsort: Infinite loop in bsearch header
//   - Checkout bsearch code to find the loop.
// - Cast.c: No infinity but f1->f2->f1->f2->break instead of f1->f2->f1->break
