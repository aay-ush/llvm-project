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
std::vector<std::string> ignoreFunctions;

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
    CNS_DEBUG_MSG("<UnaryOp>");
    CNS_DEBUG_MSG("<UnaryOp> end");
}

void preprocess(
        clang::ASTContext &context,
        clang::DeclRefExpr const &op) {
    CNS_DEBUG_MSG("<DeclRefExpr>");
    CNS_DEBUG_MSG("<DeclRefExpr> end");
}

bool isNodeDominatorNew(
        OpData const &node,
        DominatorData const &dom) {

    CNS_DEBUG_MSG("");
    auto &[_, doms_] = census[node.qn_];
    if(!doms_) {
        CNS_INFO_MSG("No doms present currently.");
        return true;
    }

    CNS_INFO("Checking current doms for this dom [{}]", dom.from_.qn_);
    auto doms = doms_.value();
    CNS_DEBUG_MSG(" end");
    return std::find(begin(doms), end(doms), dom) == end(doms);
}

void appendNodeDominator(
        OpData const &node,
        DominatorData const &dom) {

    CNS_DEBUG_MSG("");
    auto &[_, doms_] = census[node.qn_];
    if(!doms_) {
        CNS_INFO_MSG("Dominator Initialized.");
        census[node.qn_] = makeUseDefInfo(node, dom);
    } else {
        CNS_INFO_MSG("Appending to dominators.");
        auto &doms = doms_.value();
        doms.push_back(dom);
    }

    CNS_INFO_MSG("New Dominator Appended: {");
    fmt::print(fOUT, "{}\n\n", dump(dom));
    CNS_INFO_MSG("}");
    CNS_DEBUG_MSG("end");
}

void chkNodeDataForChange(OpData const &node) {
    CNS_DEBUG_MSG("");
    auto const& [old, _] = census[node.qn_];
    if(old != node) {
        CNS_WARN_MSG("Old node with different OpData:");
        CNS_WARN_MSG("Old data:");
        fmt::print(fOUT, "{}\n\n", dump(old));
        CNS_WARN_MSG("New data:");
        fmt::print(fOUT, "{}\n\n", dump(node));
    }
    CNS_DEBUG_MSG(" end");
}

void addDomNode(OpData const &dom) {
    CNS_DEBUG_MSG("");
    if(census.find(dom.qn_) == std::end(census)) {
        CNS_INFO_MSG("Inserting new node for 'dom (census 'from')'.");
        census.insert(makeCensusSourceNode(dom));
        return;
    }
    CNS_INFO_MSG("'dom (census 'from')' is already in census.");
    // If dominator is in census, do nothing
    // except warning of decl data change, if any.
    chkNodeDataForChange(dom);
    CNS_DEBUG_MSG(" end");
}

void updateHistory(
        OpData const &from,
        OpData const &to) {
    CNS_DEBUG_MSG("");

    // Ensure H(to) first.
    // Retrieve H(to)
    if(TypeTransforms.find(to.qn_) == std::end(TypeTransforms)) {
        // Add H(to)
        CNS_INFO(":to: New history started for {}", to.qn_);
        TypeTransforms.emplace(to.qn_, to.qn_);
        //TypeTransforms.insert({to.qn_, History(to.qn_)});
        // sanity check
        if(TypeTransforms.find(to.qn_) == std::end(TypeTransforms)) {
            CNS_ERROR(":to: Could not insert history for {}", to.qn_);
            CNS_DEBUG_MSG("end");
            return;
        }
    }
    else {
        CNS_INFO(":to: History of {} already on record. No action needed.", to.qn_);
    }

    // Retrieve H(from)
    if(TypeTransforms.find(from.qn_) == std::end(TypeTransforms)) {
        // Add H(from)
        CNS_INFO(":from: New history started for {}", from.qn_);
        TypeTransforms.emplace(from.qn_, from.qn_);
        //TypeTransforms.insert({from.qn_, History(from.qn_)});
        // sanity check
        if(TypeTransforms.find(from.qn_) == std::end(TypeTransforms)) {
            CNS_ERROR(":from: Could not insert history for {}", from.qn_);
            CNS_DEBUG_MSG("end");
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
        CNS_INFO(":toFP: Creating new context {{key, value}} = {{{},{}}}", to.qn_, from.qn_);
        HistoryContext hc;
        hc[to.qn_] = from.qn_;

        auto &foh = TypeTransforms.at(from.qn_);
        auto &toh = TypeTransforms.at(to.qn_);
        CNS_INFO(":toFP: Adding context to ToHistory: {}", toh.idversion());
        //auto ech = toh.addContext(hc);
        toh.addContext(hc);
        CNS_INFO(":toFP: New ToHistory version: {}", toh.idversion());
        CNS_INFO(":toFP: Extending history from {} with {}", foh.idversion(), toh.idversion());
        //TypeTransforms.at(from.qn_).extend(ech);
        //foh.extend({toh, hc}); //ech);
        foh.extend(toh);
        CNS_INFO(":toFP: New from version: {}", foh.idversion());
    }
    else {
        // Extend H(from) with H(to)
        // CNS_INFO(":from: Extending history of {} with {}", from.qn_, to.qn_);
        //TypeTransforms.at(from.qn_).extend(TypeTransforms.at(to.qn_));
        auto &foh = TypeTransforms.at(from.qn_);
        auto const &toh = TypeTransforms.at(to.qn_);
        CNS_INFO(":from: Extending history {} with {}", foh.idversion(), toh.idversion());
        CNS_INFO(":from: New from version: {}", foh.idversion());
        foh.extend(toh);
        CNS_INFO(":from: New from version: {}", foh.idversion());
    }

    CNS_DEBUG_MSG("end");
}

void updateCensus(
        OpData &from,
        OpData &to,
        DominatorData const &dom) {

    CNS_DEBUG_MSG("<from, to, dom>");

    addDomNode(from);
    auto it = census.find(to.qn_);
    if(it == std::end(census)) {
        // `to` not in census
        CNS_INFO_MSG("<0> Inserting new node for 'to'.");
        census.insert(makeCensusNode(to, dom));

        updateHistory(from, to);
        CNS_DEBUG_MSG("<from, to, dom> end");
        return;
    }

    CNS_INFO_MSG("<0> 'to' already in census.");
    chkNodeDataForChange(to);
    if(isNodeDominatorNew(to, dom)) {
        CNS_INFO_MSG("<0> 'to' has new dominator.");
        appendNodeDominator(to, dom);
    }

    updateHistory(from, to);
    CNS_DEBUG_MSG("<from, to, dom> end");
}

void updateCensusNoHistory(
        OpData &from,
        OpData &to,
        DominatorData const &dom) {

    CNS_DEBUG_MSG("");

    addDomNode(from);
    auto it = census.find(to.qn_);
    if(it == std::end(census)) {
        // `to` not in census
        CNS_INFO_MSG("Inserting new node for 'to'.");
        census.insert(makeCensusNode(to, dom));
        CNS_DEBUG_MSG("end");
        return;
    }

    CNS_INFO_MSG("'to' already in census.");
    chkNodeDataForChange(to);
    if(isNodeDominatorNew(to, dom)) {
        CNS_INFO_MSG("'to' has new dominator.");
        appendNodeDominator(to, dom);
    }

    //updateHistory(from, to);
    CNS_DEBUG_MSG("end");
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

    CNS_DEBUG_MSG("<T>");
    preprocess(context, dest);

    CNS_INFO_MSG("<Cast> building lhs data.");
    auto lhs = buildOpData(context, sm, castExpr, castSource);
    CNS_INFO_MSG("<Cast> building rhs data.");
    auto rhs = buildOpData<CS_t>(context, sm, castExpr, dest);

    DominatorData dom{
        lhs,
        String(context, castExpr),
        {}, //getCastExprType(dest),
        //{}, //getLinkedFunction(context, castExpr, dest)
    };

    updateCensus(lhs, rhs, dom);
    logCensusUpdate(lhs, rhs, dom);
    CNS_DEBUG_MSG("<T> end");
}

void processCast(MatchFinder::MatchResult const &result) {
    CNS_DEBUG_MSG("");
    auto *context = result.Context;
    if(!context) {
        CNS_ERROR_MSG("Null context");
        CNS_DEBUG_MSG("end.");
        return;
    }
    assert(context);
    auto const *castExpr = result.Nodes.getNodeAs<CastExpr>("cast");
    if(!castExpr) {
        CNS_ERROR_MSG("Null cast expr");
        CNS_DEBUG_MSG("end.");
        return;
    }
    assert(castExpr);

    CNS_DEBUG("Cast match at: '{}'", castExpr->getExprLoc().printToString(*result.SourceManager));
    CNS_DEBUG("Cast : '{}'", String(*context, *castExpr));

    // Source
    auto const *s_unaryCastee = result.Nodes.getNodeAs<DeclRefExpr>("unaryCastee");

    auto const *binOp = result.Nodes.getNodeAs<BinaryOperator>("binOp");

    // Target
    auto const *unaryOp = result.Nodes.getNodeAs<UnaryOperator>("unaryOp");

    if(!!unaryOp) {
        CNS_DEBUG_MSG("Processing cast: Unary operation");
        updateCensus<CastSourceType::UnaryOp>(*context, *result.SourceManager, *castExpr, *s_unaryCastee, *unaryOp);
    }
    else if(!!binOp) {
        CNS_DEBUG_MSG("Processing cast: Binary operation");
        auto const *bl = result.Nodes.getNodeAs<DeclRefExpr>("lhsref");
        auto const *br = result.Nodes.getNodeAs<DeclRefExpr>("rhsref");
        if(!bl) {
            CNS_ERROR_MSG("binop lHS == nullptr.");
            CNS_DEBUG_MSG(" end");
            return;
        }
        if(!br) {
            CNS_ERROR_MSG("binop RHS == nullptr.");
            CNS_DEBUG_MSG(" end");
            return;
        }
        updateCensus<CastSourceType::BinaryOp>(*context, *result.SourceManager, *castExpr, *br, *bl);
    }

    CNS_DEBUG_MSG(" end");
}

void updateCensus(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::DeclRefExpr const &src,
        clang::VarDecl const &dest) {

    CNS_DEBUG_MSG("<declrefexpr, varDecl>");
    auto const *lhsDecl = src.getDecl();
    assert(lhsDecl);
    CNS_INFO_MSG("<declrefexpr, varDecl> building lhs data.");
    auto lhs = buildOpData(context, sm, src, *lhsDecl);
    CNS_INFO_MSG("<declrefexpr, varDecl> building rhs data.");
    auto rhs = buildOpData(context, sm, dest);

    DominatorData dom{lhs, {}, {}};
    updateCensus(lhs, rhs, dom);
    logCensusUpdate(lhs, rhs, dom);
    CNS_DEBUG_MSG("<declrefexpr, varDecl> end");
}

void processVar(MatchFinder::MatchResult const &result) {
    CNS_DEBUG_MSG("");
    assert(result);
    auto *context = result.Context;
    assert(context);

    // [VarDecl]    [DeclRefExpr]
    // [int *pi2] = [pi]
    // Census: {pi -> p2}
    //          LHS   RHS

    auto const *rhs = result.Nodes.getNodeAs<clang::VarDecl>("varDecl");
    assert(rhs);

    CNS_DEBUG("VarDecl match at: '{}'", rhs->getLocation().printToString(*result.SourceManager));
    CNS_DEBUG("VarDecl : '{}'", String(*context, *rhs));

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
        CNS_INFO_MSG("Skipping VarDecl init with a literal.");
        return;
    }

    assert(lhsRef);
    updateCensus(*context, *result.SourceManager, *lhsRef, *rhs);
    CNS_DEBUG_MSG(" end");
}

OpData buildArgOp(clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CallExpr const &call,
        clang::Expr const &arg) {

    CNS_DEBUG_MSG("");
    auto const *e = arg.IgnoreImplicit();
    if(!e) {
        CNS_DEBUG_MSG("Null from IgnoreImplicit()");
        CNS_DEBUG_MSG(" end");
        // get declref expr for arg
        return {
            cnsHash(context, arg),
            String(context, arg),
            Typename(context, arg),
            TypeCategory(context, arg),
            String(context, arg),
            getContainerFunction(context, arg),
            call.getExprLoc().printToString(sm),
            qualifiedName(context, call, arg)
        };
    }
    auto const *dre = dyn_cast<DeclRefExpr>(e);
    if(!dre) {
        CNS_DEBUG_MSG("Null DRE");
        CNS_DEBUG_MSG(" end");
        return {
            cnsHash(context, arg),
            String(context, arg),
            Typename(context, arg),
            TypeCategory(context, arg),
            String(context, arg),
            getContainerFunction(context, arg),
            call.getExprLoc().printToString(sm),
            qualifiedName(context, call, arg)
        };
    }
    CNS_INFO_MSG("Got DRE from arg.");
    CNS_DEBUG_MSG(" end");
    return buildOpData(context, sm, arg, *dre);
}

auto buildOpDatas(clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CallExpr const &call) {

    CNS_INFO_MSG("");
    // for each arg
    unsigned pos = 0;
    std::for_each(call.arg_begin(), call.arg_end(),
        [&](auto const *arg) {
            CNS_INFO_MSG("Building lhs(Arg) opData.");
            OpData lhs, rhs;
            // create source(arg) op
            lhs = buildArgOp(context, sm, call, *arg);

            // create target(param) op
            CNS_INFO_MSG("Building rhs(Param) opData.");
            auto const *parmd = getParamDecl(context, call, pos);
            if(parmd) {
                CNS_INFO_MSG("Got ParamDecl.");
                auto const *parm = dyn_cast<clang::ParmVarDecl>(parmd);
                if(!parm) {
                    CNS_ERROR_MSG("Got ParamDecl but no ParmVarDecl.");
                    CNS_INFO_MSG("end");
                    return;
                }

                rhs = {
                    cnsHash(context, *parm),
                    parm->getNameAsString(),
                    Typename(context, *parm),
                    TypeCategory(context, *parm),
                    String(context, call, pos),
                    getContainerFunction(context, *arg),
                    call.getExprLoc().printToString(sm),
                    (call.getDirectCallee() != nullptr)
                        ? (call.getDirectCallee()->getQualifiedNameAsString() + ".$" + std::to_string(pos))
                        : ("NullCallee")
                };
            }

            else {
                CNS_INFO_MSG("No ParamDecl.");

                // getcalleedecl() will not work
                auto const *fn = call.getCallee();
                if(fn) {
                    CNS_INFO_MSG("Got Callee expr.");
                    // In case of fptr, it is likely that function decl is not available.
                    // Get the qn of fptr
                    std::string qns;
                    qns.reserve(64);
                    auto const *fptrp = call.IgnoreImplicit();
                    if(fptrp) {
                        CNS_INFO_MSG("Got fptr from call expr after implicitignore.");
                        auto const * dre = dyn_cast<clang::DeclRefExpr>(fptrp);
                        if(dre) {
                            CNS_INFO_MSG("Got dre from fptr.");
                            qns = qualifiedName(context, *dre);
                        }
                        else {
                            CNS_INFO_MSG("No dre from fptr.");
                            for(auto child: call.children()) {
                                auto const *ce = dyn_cast<clang::CastExpr>(child);
                                if(ce) {
                                    CNS_INFO_MSG("Got castexpr from fptr.");
                                    auto const *dre = dyn_cast<clang::DeclRefExpr>(ce->getSubExpr());
                                    if(dre) {
                                        CNS_INFO_MSG("Got dre from castexpr.");
                                        qns = qualifiedName(context, *dre);
                                        break;
                                    }
                                    else {
                                        CNS_INFO_MSG("No dre from castexpr.");
                                    }
                                }
                                else {
                                    CNS_INFO_MSG("No castexpr from fptr.");
                                    auto const *dre = dyn_cast<clang::DeclRefExpr>(child);
                                    if(dre) {
                                        CNS_INFO_MSG("Got dre from child.");
                                        qns = qualifiedName(context, *dre);
                                        break;
                                    }
                                    else {
                                        CNS_INFO_MSG("No dre from child.");
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
                        CNS_INFO_MSG("No fptr from call expr after implicitignore.");
                    }
                    if(qns.empty()) {
                        qns = String(context, *fn); // + ".$" + to_string(pos);
                    }
                    qns  += std::to_string(pos);
                    rhs = {
                        cnsHash(context, *arg),
                        qns,
                        "",//Typename(context, *arg),     // Since we can't get decl from callee expr.
                        TypeCategory(context, *arg),
                        String(context, call, pos),
                        getContainerFunction(context, *arg),
                        call.getExprLoc().printToString(sm),
                        qns
                    };
                }
                else {
                    CNS_INFO_MSG("No Callee either.");
                    rhs = {
                        cnsHash(context, *arg),
                        String(context, *arg),
                        "", //Typename(context, *arg),
                        TypeCategory(context, *arg),
                        String(context, call, pos),
                        getContainerFunction(context, *arg),
                        call.getExprLoc().printToString(sm),
                        "Resolve Func from callexpr_.$" + std::to_string(pos)
                    };
                }
            }

            // update census()
            DominatorData dom{
                lhs,
                String(context, *arg),
                {}, //getCastExprType(arg),
                //{} //getLinkedFunction(context, call, arg)
            };
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
                CNS_INFO(":from: New history started for {}", lhs.qn_);
                TypeTransforms.emplace(lhs.qn_, lhs.qn_);
                //TypeTransforms.insert({lhs.qn_, History(lhs.qn_)});
            }
            if(TypeTransforms.find(rhs.qn_) == std::end(TypeTransforms)) {
                // Add H(to)
                CNS_INFO(":to: New history started for {}", rhs.qn_);
                TypeTransforms.emplace(rhs.qn_, rhs.qn_);
                //TypeTransforms.insert({rhs.qn_, History(rhs.qn_)});
                // Why is this needed?
                //TypeTransforms.at(lhs.qn_).extend(TypeTransforms.at(rhs.qn_));
            }
            logCensusUpdate(lhs, rhs, dom);
            pos++;
        });
    CNS_INFO_MSG("end");
}

void addCallHistory(clang::ASTContext & context, clang::CallExpr const& call) {
    CNS_INFO_MSG("");
    auto const *calledFn = getCalleeDecl(call);
    assert(calledFn);
    std::string fn;
    if(!calledFn) {
        CNS_ERROR_MSG("Null callee decl. Maybe an fptr.");

        auto const *fptr = getFptrFromFptrCall(context, call);
        if(!fptr) {
            CNS_ERROR_MSG("No fptr either. end");
            return;
        }

        fn = qualifiedNameFromFptrCall(context, call);
        auto it = std::find(begin(TransformTemplates), end(TransformTemplates), fn);
        if(it == std::end(TransformTemplates)) {
            CNS_DEBUG("No template found for: {}()", fn);
            CNS_DEBUG("Adding new template for: {}()", fn);
            TransformTemplates.push_back({context, call, *fptr});
        }
    }
    else {
        fn = calledFn->getNameAsString();
    }
    auto it = std::find(begin(TransformTemplates), end(TransformTemplates), fn);
    if(it == std::end(TransformTemplates)) {
        CNS_DEBUG("No template found for: {}()", fn);
        CNS_DEBUG("Adding new template for: {}()", fn);
        // Likely to happen.
        // Create new template from calleeDecl and instantiate.
        TransformTemplates.push_back({*calledFn});
        auto it2 = std::find(begin(TransformTemplates), end(TransformTemplates), fn);
        if(it2 == std::end(TransformTemplates)) {
            CNS_ERROR("New template insertion failed for {}()", fn);
            CNS_DEBUG_MSG("end");
            return;
        }
        else {
            it = it2;
        }
    }

    // Instantiate template and add to history
    auto hs = it->instantiate(context, call);
    if(hs.size() != call.getNumArgs()) {
        CNS_ERROR_MSG("History count does not match arg count, cannot assign history to args.");
        CNS_DEBUG_MSG("end");
        return;
    }

    // For each arg operand opA, H(opA) is extended by H(A).
    unsigned i = 0;
    std::for_each(call.arg_begin(), call.arg_end(), [&](auto const *a) {
            CNS_DEBUG_MSG("for_each arg");
            // get key for a
            auto const argQn = qualifiedName(context, call, *a);

            // Contextualized history = local history; arg history gets extended by local contextual parm history
            // Search history of a
            if(TypeTransforms.find(argQn) != std::end(TypeTransforms)) {
                CNS_DEBUG("Found existing history for {}", argQn);
                // extend history
                if(i < hs.size()) {
                    CNS_INFO("Extending history for {} with {}", argQn, hs[i].first.get().opId());
                    TypeTransforms.at(argQn).extend(hs.at(i));
                }
                else {
                    CNS_ERROR_MSG("Out of bound history insert.");
                }
            }
            else {
                // add new history
                CNS_DEBUG("Adding new history for {}", argQn);
                //TypeTransforms.insert({hs[i].opId(), hs[i]});
                auto hqn = History(argQn);
                CNS_DEBUG("Extending history for {} with {}", argQn, hs[i].first.get().opId());
                hqn.extend(hs[i]);
                TypeTransforms.emplace(argQn, std::move(hqn));
                //TypeTransforms.insert({qn, hqn});
            }
            ++i;
        });
    CNS_DEBUG_MSG("end");
}

// For call expressions:
void preprocess(
        clang::ASTContext &context,
        clang::CallExpr const &call) {

    CNS_DEBUG_MSG("<CallExpr>");
    auto const *calledFn = getCalleeDecl(call);
    assert(calledFn);
    if(!calledFn) {
        CNS_ERROR_MSG("<CallExpr> null callee decl");
        CNS_DEBUG_MSG("<CallExpr> end");
        return;
    }

    auto const& fn = calledFn->getNameAsString();

    if(std::find(begin(ignoreFunctions), end(ignoreFunctions), fn) != end(ignoreFunctions)) {
        CNS_INFO("Skipping ignored function: {}", String(context, *calledFn));
        CNS_INFO_MSG("<CallExpr> Adding ignored function to seen functions.");
        CNS_DEBUG_MSG("<CallExpr> end");
        return; // ignore
    }

    auto h = cnsHash(context, *calledFn);
    if(std::find(begin(seenFunctions), end(seenFunctions), h) != end(seenFunctions)) {
        CNS_INFO("Skipping seen function: {}", String(context, *calledFn));
        CNS_DEBUG_MSG("<CallExpr> end");
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
        CNS_INFO("Adding template for function: {}()", fn);
        TransformTemplates.push_back({*calledFn});
    }
    else {
        CNS_INFO("Template for function: {}() exists", fn);
    }
    // TODO end

    CNS_DEBUG_MSG("<CallExpr> end");
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
    CNS_DEBUG_MSG("");

    assert(result);
    auto *context = result.Context;
    assert(context);

    auto const *call = result.Nodes.getNodeAs<CallExpr>("ce");
    assert(call);
    CNS_DEBUG("FunctionCall match at: '{}'", call->getExprLoc().printToString(*result.SourceManager));
    CNS_DEBUG("Call : '{}'", String(*context, *call));

    // - Preprocess call.
    preprocess(*context, *call);
    // update census
    buildOpDatas(*context, *result.SourceManager, *call);
    //
    addCallHistory(*context, *call);

    CNS_DEBUG_MSG(" end");
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

        CNS_INFO_MSG("# Census summary so far:");
        censusSummary();
        CNS_INFO_MSG("# end Census summary so far");

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
static llvm::cl::OptionCategory MyToolCategory("my-tool options");

// CommonOptionsParser declares HelpMessage with a description of the common cli options
// related to the compilation db and input files. (Nice to have help)
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);

// Help message for this specific tool.
static cl::extrahelp Morehelp("\nMore help text...\n");

void buildIgnoreList();
void printCollection();

int main(int argc, const char **argv) {
    if(argc > 1) {
        // Make sure last arg is summary depth
        SUMMARY_DEPTH = std::atoi(argv[argc - 1]);
        argc = argc - 1;
    }
    auto ExpectedParser = CommonOptionsParser::create(argc, argv, MyToolCategory);
    if(!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }

    CommonOptionsParser &OptionsParser = ExpectedParser.get();
    ClangTool Tool(OptionsParser.getCompilations(),
                   OptionsParser.getSourcePathList());

    CastMatchCallback historian;
    MatchFinder Finder;
    Finder.addMatcher(AssignMatcher, &historian);
    Finder.addMatcher(CallMatcher, &historian);
    Finder.addMatcher(CastMatcher, &historian);

    buildIgnoreList();
    //FOUT.open("census-dump.old.txt", std::ios::out);
    fOUT = fopen("census-dump.txt", "w");
    if(fOUT == nullptr) {
        fmt::print(stderr, "Error opening census-dump.txt\n");
        return 1;
    }
    //return Tool.run(newFrontendActionFactory<clang::SyntaxOnlyAction>().get());
    //return Tool.run(newFrontendActionFactory(&Finder).get());
    auto rc = Tool.run(newFrontendActionFactory(&Finder).get());
    printCollection();
    fclose(fOUT);
    return rc;
}

void printCollection() {
    LOG_FUNCTION_TIME;

    std::for_each(begin(TypeTransforms), end(TypeTransforms),
        [&](auto &h) {
            elaborateHistory(h.second); //, {3});
        });

    fmt::print(fOUT, "History collection:\n");
    fmt::print(stdout, "History collection:\n");
    std::for_each(begin(TypeSummaries), end(TypeSummaries),
        [&](auto const &s) {
            fmt::print(fOUT, "History of ({}):\n", s.first);
            fmt::print(stdout, "History of ({}):\n", s.first);
            //std::cout << "History of (" << s.first << "):\n";
            auto tsummary = s.second.summarize({SUMMARY_DEPTH});
            fmt::print(fOUT, "{}\n\n", tsummary);
            fmt::print(stdout, "{}\n\n", tsummary);
        });
    fmt::print(fOUT, "end History collection\n");
    fmt::print(stdout, "end History collection\n");
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
