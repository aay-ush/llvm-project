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

    CNS_DEBUG(logKey, "Checking current doms for this dom [{}]", dom.from_.qn_);
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
    CNS_DEBUG(logKey, "History update using dom '{}'", domInfo.exprType_);

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
    fmt::print(fOUT, "      expr: [{}]{}\n", dom.exprType_, dom.expr_);
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

    DominatorData dom{
        lhs,
        String(context, castExpr),
        castExpr.getCastKindName(), //{}, //getCastExprType(dest),
        //{}, //getLinkedFunction(context, castExpr, dest)
    };

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

    DominatorData dom = buildVarDeclDom(context, lhs, dest);
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
            qualifiedName(context, call, arg)
        };
}

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


OpData buildArgOp(clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CallExpr const &call,
        clang::Expr const &arg) {

    auto const logKey = String(context, call) + "(): " + String(context, arg);
    CNS_DEBUG_MSG(logKey, "begin");
    auto const *e = arg.IgnoreImplicit();
    if(!e) {
        CNS_DEBUG_MSG(logKey, "No expr from arg.IgnoreImplicit()");
        CNS_DEBUG_MSG(logKey, "end");
        return buildLimitedArgOp(context, sm, call, arg);
    }

    /*
    auto const *dre = getDREChild(e);
    if(!dre) {
        CNS_WARN(logKey, "Null DRE from expr '{}'", String(context, *e));
        CNS_WARN_MSG(logKey, "end");
        return buildLimitedArgOp(context, sm, call, *e);
    }

    CNS_DEBUG_MSG(logKey, "Found dre from child expr");

    //processMidCall(context, sm, call, *dre, *e);
    */

    CNS_DEBUG(logKey, "Building arg op to return from '{}'", String(context, *e));
    OpData to = {
                cnsHash(context, *e),
                String(context, *e),
                Typename(context, *e),
                TypeCategory(context, *e),
                String(context, arg),
                getContainerFunction(context, *e),
                getLinkedRecord(*e),
                linkedTypeCategory(*e),
                call.getExprLoc().printToString(sm),
                //String(context, *e), //
                qualifiedName(context, call, *e)
            };

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
            DominatorData dom{
                lhs,
                String(context, *arg),
                "TODOArg", //getCastExprType(arg),
                //{} //getLinkedFunction(context, call, arg)
            };
            auto lhsce = dyn_cast<CastExpr>(arg);
            if(lhsce) {
                dom.exprType_ = lhsce->getCastKindName();
            }
            else {
                dom.exprType_ = "NotACast";
            }

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
                        : ("NullCallee")
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
                        qns
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
                        "Resolve Func from callexpr_.$" + std::to_string(pos)
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
            auto const argQn = qualifiedName(context, call, *a);

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

// CommonOptionsParser declares HelpMessage with a description of the common cli options
// related to the compilation db and input files. (Nice to have help)
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);

// Help message for this specific tool.
static cl::extrahelp Morehelp("\nMore help text...\n");

void buildIgnoreList();
void printCollection();
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
    printCollection();
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


void printCollection(std::FILE *fp) {
    LOG_FUNCTION_TIME;

    CastStat tcst("Total Cast Statistics");
    fmt::print(fp, "History collection:\n");
    std::for_each(begin(TypeSummaries), end(TypeSummaries),
        [&](auto const &s) {
            CastStat cst("Cast stats for " + s.first);
            fmt::print(fp, "History of ({}):\n", s.first);
            auto tsummary = s.second.summarize(cst, {SUMMARY_DEPTH});
            tcst.extend(cst);

            fmt::print(fp, "{}\n\n", tsummary);
            cst.print(fp);
            fmt::print(fp, "\n");
        });
    tcst.print(fp);
    std::fflush(fp);
}

void printCollection() {
    LOG_FUNCTION_TIME;

    elaborateHistories();
    /*
    std::for_each(begin(TypeTransforms), end(TypeTransforms),
        [&](auto &h) {
            elaborateHistory(h.second); //, {3});
        });
    */

    printCollection(fOUT);
    printCollection(stdout);
    /*
    std::for_each(begin(TypeTransforms), end(TypeTransforms),
        [&](auto &h) {
            elaborateHistory(h.second); //, {3});
        });

    CastStat tcst;
    fmt::print(fOUT, "History collection:\n");
    fmt::print(stdout, "History collection:\n");
    std::for_each(begin(TypeSummaries), end(TypeSummaries),
        [&](auto const &s) {
            CastStat cst;
            fmt::print(fOUT, "History of ({}):\n", s.first);
            fmt::print(stdout, "History of ({}):\n", s.first);
            //std::cout << "History of (" << s.first << "):\n";
            auto tsummary = s.second.summarize(cst, {SUMMARY_DEPTH});
            fmt::print(fOUT, "{}\n\n", tsummary);
            fmt::print(stdout, "{}\n\n", tsummary);
            cst.print(fOUT);
            cst.print(stdout);
            std::fflush(stdout);
            tcst.extend(cst);
        });
    fmt::print(fOUT, "end History collection\n");
    fmt::print(stdout, "end History collection\n");
    */
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
