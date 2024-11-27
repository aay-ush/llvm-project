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

#include <iostream>
#include <fstream>
#include <sstream>

#include "llvm/Support/raw_os_ostream.h"
//#include "llvm/Support/raw_ostream.h"
#include <string>
#include <unordered_map>
#include <utility>
#include <algorithm>
#include <optional>
#include <any>
#include <tuple>
#include <set>
#include <unordered_set>

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
    CNS_DEBUG("<UnaryOp>");
    CNS_DEBUG("<UnaryOp> end.");
}

void preprocess(
        clang::ASTContext &context,
        clang::DeclRefExpr const &op) {
    CNS_DEBUG("<DeclRefExpr>");
    CNS_DEBUG("<DeclRefExpr> end.");
}

bool isNodeDominatorNew(
        OpData const &node,
        DominatorData const &dom) {

    CNS_DEBUG("");
    auto &[_, doms_] = census[node.qn_];
    if(!doms_) {
        CNS_INFO("No doms present currently.");
        return true;
    }

    FOUT << "[INFO ](isNodeDominatorNew) Checking current doms for this dom[" << dom.from_.qn_ << "]\n";
    auto doms = doms_.value();
    CNS_DEBUG(" end.");
    return std::find(begin(doms), end(doms), dom)
        == end(doms);
}

void appendNodeDominator(
        OpData const &node,
        DominatorData const &dom) {

    CNS_DEBUG("");
    auto &[_, doms_] = census[node.qn_];
    if(!doms_) {
        CNS_INFO("Dominator Initialized.");
        census[node.qn_] = makeUseDefInfo(node, dom);
    } else {
        CNS_INFO("Appending to dominators.");
        auto &doms = doms_.value();
        doms.push_back(dom);
    }

    CNS_INFO("New Dominator Appended: {");
    dump(FOUT, dom);
    CNS_INFO("}");
    CNS_DEBUG("end.");
}

void chkNodeDataForChange(OpData const &node) {
    CNS_DEBUG("");
    auto const& [old, _] = census[node.qn_];
    if(old != node) {
        FOUT << "[WARN](chkNodeDataForChange) Old node with different OpData:\n"
             << "Old data:\n";
        dump(FOUT, old);
        FOUT << "\nNew data:\n";
        dump(FOUT, node);
        FOUT << "\n";
    }
    CNS_DEBUG(" end.");
}

void addDomNode(OpData const &dom) {
    CNS_DEBUG("");
    if(census.find(dom.qn_) == std::end(census)) {
        CNS_INFO("Inserting new node for 'dom (census 'from')'.");
        census.insert(makeCensusSourceNode(dom));
        return;
    }
    CNS_INFO("'dom (census 'from')' is already in census.");
    // If dominator is in census, do nothing
    // except warning of decl data change, if any.
    chkNodeDataForChange(dom);
    CNS_DEBUG(" end.");
}

void updateHistory(
        OpData const &from,
        OpData const &to) {
    CNS_DEBUG("");

    // Ensure H(to) first.
    // Retrieve H(to)
    if(TypeTransforms.find(to.qn_) == std::end(TypeTransforms)) {
        // Add H(to)
        FOUT << "[INFO ](updateHistory) :to: New history started for " << to.qn_ << "\n";
        TypeTransforms.emplace(to.qn_, to.qn_);
        //TypeTransforms.insert({to.qn_, History(to.qn_)});
        // sanity check
        if(TypeTransforms.find(to.qn_) == std::end(TypeTransforms)) {
            FOUT << "[ERROR](updateHistory) :to: Could not insert history for " << to.qn_ << "\n";
            CNS_DEBUG("end.");
            return;
        }
    }
    else {
        FOUT << "[INFO ](updateHistory) :to: History of " << to.qn_ << " already on record. No action needed.\n";
    }

    // Retrieve H(from)
    if(TypeTransforms.find(from.qn_) == std::end(TypeTransforms)) {
        // Add H(from)
        FOUT << "[INFO ](updateHistory) :from: New history started for " << from.qn_ << "\n";
        TypeTransforms.emplace(from.qn_, from.qn_);
        //TypeTransforms.insert({from.qn_, History(from.qn_)});
        // sanity check
        if(TypeTransforms.find(from.qn_) == std::end(TypeTransforms)) {
            FOUT << "[ERROR](updateHistory) :from: Could not insert history for " << from.qn_ << "\n";
            CNS_DEBUG("end.");
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
        FOUT << "[INFO ](updateHistory) :toFP: Creating new context {key, value} = {" << to.qn_ << ", " << from.qn_ << "}\n";
        HistoryContext hc;
        hc[to.qn_] = from.qn_;

        auto &foh = TypeTransforms.at(from.qn_);
        auto &toh = TypeTransforms.at(to.qn_);
        FOUT << "[INFO ](updateHistory) :toFP: Adding context to ToHistory: " << toh.idversion() << "\n";
        //auto ech = toh.addContext(hc);
        toh.addContext(hc);
        FOUT << "[INFO ](updateHistory) :toFP: New ToHistory version: " << toh.idversion() << "\n";
        FOUT << "[INFO ](updateHistory) :toFP: Extending history from: " << foh.idversion() << " with " << toh.idversion() << "\n";
        //TypeTransforms.at(from.qn_).extend(ech);
        //foh.extend({toh, hc}); //ech);
        foh.extend(toh);
        FOUT << "[INFO ](updateHistory) :toFP: New from version: " << foh.idversion() << "\n";
    }
    else {
        // Extend H(from) with H(to)
        //FOUT << "[INFO ](updateHistory) :from: Extending history of " << from.qn_ << " with " << to.qn_ << "\n";
        //TypeTransforms.at(from.qn_).extend(TypeTransforms.at(to.qn_));
        auto &foh = TypeTransforms.at(from.qn_);
        auto const &toh = TypeTransforms.at(to.qn_);
        FOUT << "[INFO ](updateHistory) :from: Extending history " << foh.idversion() << " with " << toh.idversion() << "\n";
        foh.extend(toh);
        FOUT << "[INFO ](updateHistory) :from: New from version: " << foh.idversion() << "\n";
    }

    CNS_DEBUG("end.");
}

void updateCensus(
        OpData &from,
        OpData &to,
        DominatorData const &dom) {

    CNS_DEBUG("<from, to, dom>");

    addDomNode(from);
    auto it = census.find(to.qn_);
    if(it == std::end(census)) {
        // `to` not in census
        CNS_INFO("<0> Inserting new node for 'to'.");
        census.insert(makeCensusNode(to, dom));

        updateHistory(from, to);
        CNS_DEBUG("<from, to, dom> end");
        return;
    }

    CNS_INFO("<0> 'to' already in census.");
    chkNodeDataForChange(to);
    if(isNodeDominatorNew(to, dom)) {
        CNS_INFO("<0> 'to' has new dominator.");
        appendNodeDominator(to, dom);
    }

    updateHistory(from, to);
    CNS_DEBUG("<from, to, dom> end.");
}

void updateCensusNoHistory(
        OpData &from,
        OpData &to,
        DominatorData const &dom) {

    CNS_DEBUG("");

    addDomNode(from);
    auto it = census.find(to.qn_);
    if(it == std::end(census)) {
        // `to` not in census
        CNS_INFO("Inserting new node for 'to'.");
        census.insert(makeCensusNode(to, dom));
        CNS_DEBUG("end.");
        return;
    }

    CNS_INFO("'to' already in census.");
    chkNodeDataForChange(to);
    if(isNodeDominatorNew(to, dom)) {
        CNS_INFO("'to' has new dominator.");
        appendNodeDominator(to, dom);
    }

    //updateHistory(from, to);
    CNS_DEBUG("end.");
}


void logCensusUpdate(
        OpData const &lhs,
        OpData const &rhs,
        DominatorData const &dom) {

    FOUT << "Match site: " << rhs.location_ << "\n"
         << "   Linking: [" << lhs.qn_ << "]"
         << lhs.expr_ << " {" << lhs.linkedParm_ << "} -> "
         << "[" << rhs.qn_ << "]"
         << rhs.expr_ << " {" << rhs.linkedParm_ << "}\n"
         << "       from: [" << lhs.category_ << "] " << lhs.type_ << "\n"
         << "         to: [" << rhs.category_ << "] " << rhs.type_ << "\n"
         << "       expr: " << "[" << dom.exprType_ << "] " << dom.expr_ << "\n"
         //<< "Funcslinked: " << lhs.container_ << "() -> " << dom.callee_.value_or("(n/a)") << "()\n"
         << "\n";
}

template<CastSourceType CS_t, typename T>
void updateCensus(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CastExpr const &castExpr,
        clang::DeclRefExpr const &castSource,
        T const &dest) {

    CNS_DEBUG("<T>");
    preprocess(context, dest);

    CNS_INFO("<Cast> building lhs data.");
    auto lhs = buildOpData(context, sm, castExpr, castSource);
    CNS_INFO("<Cast> building rhs data.");
    auto rhs = buildOpData<CS_t>(context, sm, castExpr, dest);

    DominatorData dom{
        lhs,
        String(context, castExpr),
        {}, //getCastExprType(dest),
        //{}, //getLinkedFunction(context, castExpr, dest)
    };

    updateCensus(lhs, rhs, dom);
    logCensusUpdate(lhs, rhs, dom);
    CNS_DEBUG("<T> end.");
}

void processCast(MatchFinder::MatchResult const &result) {
    CNS_DEBUG("");
    assert(result);
    auto *context = result.Context;
    assert(context);
    auto const *castExpr = result.Nodes.getNodeAs<CastExpr>("cast");

    // Source
    //auto const *s_fptrRef = result.Nodes.getNodeAs<DeclRefExpr>("callee");
    //auto const *s_callArg = result.Nodes.getNodeAs<DeclRefExpr>("arg");
    auto const *s_unaryCastee = result.Nodes.getNodeAs<DeclRefExpr>("unaryCastee");

    auto const *binOp = result.Nodes.getNodeAs<BinaryOperator>("binOp");
    //auto const *var = result.Nodes.getNodeAs<DeclStmt>("var");
    //auto const *gexpr = result.Nodes.getNodeAs<Expr>("gexpr");

    // Target
    //auto const *call = result.Nodes.getNodeAs<CallExpr>("call");
    //auto const *fptr = result.Nodes.getNodeAs<CallExpr>("fptr");
    auto const *unaryOp = result.Nodes.getNodeAs<UnaryOperator>("unaryOp");

    /*
    if(!!var) {
        updateCensus(*context, *castExpr, *castSource, var, location);
    }
    */
    if(!!unaryOp) {
        updateCensus<CastSourceType::UnaryOp>(*context, *result.SourceManager, *castExpr, *s_unaryCastee, *unaryOp);
    }
    /*
    else if (!!call) {
        //updateCensus<CastSourceType::FunctionArg>(*context, *result.SourceManager, *castExpr, *s_callArg, *call);
    }
    else if(!!fptr) {
        //updateCensus<CastSourceType::Function>(*context, *result.SourceManager, *castExpr, *s_fptrRef, *fptr);
    }
    */
    else if(!!binOp) {
        CNS_INFO("binop processing");
        auto const *bl = result.Nodes.getNodeAs<DeclRefExpr>("binLhs");
        auto const *br = result.Nodes.getNodeAs<DeclRefExpr>("binRhs");
        if(!bl) {
            CNS_ERROR("binop lHS == nullptr.");
            return;
        }
        if(!br) {
            CNS_ERROR("binop RHS == nullptr.");
            return;
        }
        updateCensus<CastSourceType::BinaryOp>(*context, *result.SourceManager, *castExpr, *bl, *br);
    }

    CNS_DEBUG(" end.");
}

void updateCensus(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::DeclRefExpr const &src,
        clang::VarDecl const &dest) {

    CNS_DEBUG("<declrefexpr, varDecl>");
    auto const *lhsDecl = src.getDecl();
    assert(lhsDecl);
    CNS_INFO("<declrefexpr, varDecl> building lhs data.");
    auto lhs = buildOpData(context, sm, src, *lhsDecl);
    CNS_INFO("<declrefexpr, varDecl> building rhs data.");
    auto rhs = buildOpData(context, sm, dest);

    DominatorData dom{lhs, {}, {}};
    updateCensus(lhs, rhs, dom);
    logCensusUpdate(lhs, rhs, dom);
    CNS_DEBUG("<declrefexpr, varDecl> end.");
}

void processVar(MatchFinder::MatchResult const &result) {
    CNS_DEBUG("");
    assert(result);
    auto *context = result.Context;
    assert(context);

    // [VarDecl]    [DeclRefExpr]
    // [int *pi2] = [pi]
    // Census: {pi -> p2}
    //          LHS   RHS

    auto const *rhs = result.Nodes.getNodeAs<clang::VarDecl>("varDecl");
    assert(rhs);

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
        return;
    }

    assert(lhsRef);
    //auto const *lhs = lhsRef->getDecl();

    // We can have FunctionDecl too?
    /*
    auto lhsDecl = dyn_cast<VarDecl>(lhsRef->getDecl());
    if(!lhsDecl) {
        CNS_INFO("dyncast LHS Decl == nullptr");
        return;
    }
    */
    updateCensus(*context, *result.SourceManager, *lhsRef, *rhs);
    CNS_DEBUG(" end.");
}

OpData buildArgOp(clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CallExpr const &call,
        clang::Expr const &arg) {

    CNS_DEBUG("");
    auto const *e = arg.IgnoreImplicit();
    if(!e) {
        CNS_DEBUG("Null from IgnoreImplicit()");
        CNS_DEBUG(" end.");
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
        CNS_DEBUG("Null DRE");
        CNS_DEBUG(" end.");
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
    CNS_INFO("Got DRE from arg.");
    CNS_DEBUG(" end.");
    return buildOpData(context, sm, arg, *dre);
}

auto buildOpDatas(clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CallExpr const &call) {

    CNS_INFO("");
    // for each arg
    unsigned pos = 0;
    std::for_each(call.arg_begin(), call.arg_end(),
        [&](auto const *arg) {
            CNS_INFO("Building lhs(Arg) opData.");
            OpData lhs, rhs;
            // create source(arg) op
            lhs = buildArgOp(context, sm, call, *arg);

            // create target(param) op
            CNS_INFO("Building rhs(Param) opData.");
            auto const *parmd = getParamDecl(context, call, pos);
            if(parmd) {
                CNS_INFO("Got ParamDecl.");
                auto const *parm = dyn_cast<clang::ParmVarDecl>(parmd);
                if(!parm) {
                    CNS_ERROR("Got ParamDecl but no ParmVarDecl.");
                    CNS_INFO("end.");
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
                CNS_INFO("No ParamDecl.");

                // getcalleedecl() will not work
                auto const *fn = call.getCallee();
                if(fn) {
                    CNS_INFO("Got Callee expr.");
                    // In case of fptr, it is likely that function decl is not available.
                    // Get the qn of fptr
                    std::stringstream ss;
                    auto const *fptrp = call.IgnoreImplicit();
                    if(fptrp) {
                        CNS_INFO("Got fptr from call expr after implicitignore.");
                        auto const * dre = dyn_cast<clang::DeclRefExpr>(fptrp);
                        if(dre) {
                            CNS_INFO("Got dre from fptr.");
                            ss << qualifiedName(context, *dre);
                        }
                        else {
                            CNS_INFO("No dre from fptr.");
                            for(auto child: call.children()) {
                                auto const *ce = dyn_cast<clang::CastExpr>(child);
                                if(ce) {
                                    CNS_INFO("Got castexpr from fptr.");
                                    auto const *dre = dyn_cast<clang::DeclRefExpr>(ce->getSubExpr());
                                    if(dre) {
                                        CNS_INFO("Got dre from castexpr.");
                                        ss << qualifiedName(context, *dre);
                                        break;
                                    }
                                    else {
                                        CNS_INFO("No dre from castexpr.");
                                    }
                                }
                                else {
                                    CNS_INFO("No castexpr from fptr.");
                                    auto const *dre = dyn_cast<clang::DeclRefExpr>(child);
                                    if(dre) {
                                        CNS_INFO("Got dre from child.");
                                        ss << qualifiedName(context, *dre);
                                        break;
                                    }
                                    else {
                                        CNS_INFO("No dre from child.");
                                    }
                                }
                            }
                            /*
                            auto cit = call.child_begin();
                            auto const * dre = dyn_cast<clang::DeclRefExpr>(*cit);
                            while(cit != call.child_end()) {
                                if(dre) {
                                    CNS_INFO("Got dre from cit.");
                                    ss << qualifiedName(context, *dre);
                                    break;
                                }
                                ++cit;
                            }
                            if(!dre) {
                                CNS_INFO("No dre from children.");
                                ss << qualifiedName(context, call, *fptrp);
                            }
                            */
                        }
                    }
                    else {
                        CNS_INFO("No fptr from call expr after implicitignore.");
                    }
                    if(ss.str().empty()) {
                        ss << String(context, *fn); // << ".$" << pos;
                    }
                    ss << ".$" << pos;
                    rhs = {
                        cnsHash(context, *arg),
                        ss.str(),
                        "",//Typename(context, *arg),     // Since we can't get decl from callee expr.
                        TypeCategory(context, *arg),
                        String(context, call, pos),
                        getContainerFunction(context, *arg),
                        call.getExprLoc().printToString(sm),
                        ss.str()
                    };
                }
                else {
                    CNS_INFO("No Callee either.");
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
                FOUT << "[INFO ](updateHistory) :from: New history started for " << lhs.qn_ << "\n";
                TypeTransforms.emplace(lhs.qn_, lhs.qn_);
                //TypeTransforms.insert({lhs.qn_, History(lhs.qn_)});
            }
            if(TypeTransforms.find(rhs.qn_) == std::end(TypeTransforms)) {
                // Add H(to)
                FOUT << "[INFO ](updateHistory) :to: New history started for " << rhs.qn_ << "\n";
                TypeTransforms.emplace(rhs.qn_, rhs.qn_);
                //TypeTransforms.insert({rhs.qn_, History(rhs.qn_)});
                // Why is this needed?
                //TypeTransforms.at(lhs.qn_).extend(TypeTransforms.at(rhs.qn_));
            }
            logCensusUpdate(lhs, rhs, dom);
            pos++;
        });
    CNS_INFO("end.");
}

void addCallHistory(clang::ASTContext & context, clang::CallExpr const& call) {
    CNS_INFO("");
    auto const *calledFn = getCalleeDecl(call);
    assert(calledFn);
    std::string fn;
    if(!calledFn) {
        CNS_ERROR("Null callee decl. Maybe an fptr.");

        auto const *fptr = getFptrFromFptrCall(context, call);
        if(!fptr) {
            CNS_ERROR("No fptr either. end.");
            return;
        }

        fn = qualifiedNameFromFptrCall(context, call);
        auto it = std::find(begin(TransformTemplates), end(TransformTemplates), fn);
        if(it == std::end(TransformTemplates)) {
            FOUT << "[DEBUG](addCallHistory) No template found for : " << fn << "()\n";
            FOUT << "[DEBUG](addCallHistory) Adding new template for : " << fn << "()\n";
            TransformTemplates.push_back({context, call, *fptr});
        }
    }
    else {
        fn = calledFn->getNameAsString();
    }
    auto it = std::find(begin(TransformTemplates), end(TransformTemplates), fn);
    if(it == std::end(TransformTemplates)) {
        FOUT << "[DEBUG](addCallHistory) No template found for : " << fn << "()\n";
        FOUT << "[DEBUG](addCallHistory) Adding new template for : " << fn << "()\n";
        // Likely to happen.
        // Create new template from calleeDecl and instantiate.
        TransformTemplates.push_back({*calledFn});
        auto it2 = std::find(begin(TransformTemplates), end(TransformTemplates), fn);
        if(it2 == std::end(TransformTemplates)) {
            FOUT << "[ERROR](addCallHistory) New template insert failed for: " << fn << "()\n";
            CNS_DEBUG("end.");
            return;
        }
        else {
            it = it2;
        }
    }

    // Instantiate template and add to history
    auto hs = it->instantiate(context, call);
    if(hs.size() != call.getNumArgs()) {
        CNS_ERROR("History count does not match arg count, cannot assign history to args.");
        CNS_DEBUG("end.");
        return;
    }

    // For each arg operand opA, H(opA) is extended by H(A).
    unsigned i = 0;
    std::for_each(call.arg_begin(), call.arg_end(), [&](auto const *a) {
            CNS_DEBUG("for_each arg");
            // get key for a
            auto const argQn = qualifiedName(context, call, *a);

            // Contextualized history = local history; arg history gets extended by local contextual parm history
            // Search history of a
            if(TypeTransforms.find(argQn) != std::end(TypeTransforms)) {
                FOUT << "[DEBUG](addCallHistory) Found existing history for " << argQn << "\n";
                // extend history
                if(i < hs.size()) {
                    FOUT << "[INFO ](addCallHistory) Extending history for " << argQn << " with " << hs[i].first.get().opId() << "\n";
                    TypeTransforms.at(argQn).extend(hs.at(i));
                }
                else {
                    CNS_ERROR("Out of bound history insert.");
                }
            }
            else {
                // add new history
                FOUT << "[DEBUG](addCallHistory) Adding new history for " << argQn << "\n";
                //TypeTransforms.insert({hs[i].opId(), hs[i]});
                auto hqn = History(argQn);
                FOUT << "[DEBUG](addCallHistory) Extending history for " << argQn << " with " << hs[i].first.get().opId() << "\n";
                hqn.extend(hs[i]);
                TypeTransforms.emplace(argQn, std::move(hqn));
                //TypeTransforms.insert({qn, hqn});
            }
            ++i;
        });
    CNS_DEBUG("end.");
}

/*
HistoryTemplate makeHistoryTemplate(
        clang::ASTContext &context,
        clang::FunctionDecl const &fn) {
    CNS_DEBUG("");

    auto const& fname = fn.getNameAsString();
    TransformTemplates.push_back({*calledFn});

    CNS_DEBUG("end.");
}
*/

// For call expressions:
void preprocess(
        clang::ASTContext &context,
        clang::CallExpr const &call) {

    CNS_DEBUG("<CallExpr>");
    auto const *calledFn = getCalleeDecl(call);
    assert(calledFn);
    if(!calledFn) {
        CNS_ERROR("<CallExpr> null callee decl");
        CNS_DEBUG("<CallExpr> end.");
        return;
    }

    auto const& fn = calledFn->getNameAsString();
    /*
    auto it = std::find(begin(TransformTemplates), end(TransformTemplates), fn);
    if(it != std::end(TransformTemplates)) {
        FOUT << "[INFO ](preprocess<CallExpr>) Skipping processed function: " << String(context, *calledFn) << "\n";
        CNS_DEBUG("<CallExpr> end.");
        return;
    }
    */

    if(std::find(begin(ignoreFunctions), end(ignoreFunctions), fn) != end(ignoreFunctions)) {
        FOUT << "[INFO ](preprocess<CallExpr>) Skipping ignored function: " << String(context, *calledFn) << "\n";
        CNS_INFO("<CallExpr> Adding ignored function to seen functions.");
        CNS_DEBUG("<CallExpr> end.");
        return; // ignore
    }

    auto h = cnsHash(context, *calledFn);
    if(std::find(begin(seenFunctions), end(seenFunctions), h) != end(seenFunctions)) {
        FOUT << "[INFO ](preprocess<CallExpr>) Skipping seen function: " << String(context, *calledFn) << "\n";
        CNS_DEBUG("<CallExpr> end.");
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
        FOUT << "[INFO ](preprocess<CallExpr> Adding template for function: " << fn << "()\n";
        TransformTemplates.push_back({*calledFn});
    }
    else {
        FOUT << "[INFO ](preprocess<CallExpr> Template for function: " << fn << "() exists.\n";
    }
    // TODO end

    CNS_DEBUG("<CallExpr> end.");
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
    CNS_INFO("");
    assert(result);
    auto *context = result.Context;
    assert(context);

    auto const *call = result.Nodes.getNodeAs<CallExpr>("ce");
    assert(call);

    // - Preprocess call.
    preprocess(*context, *call);
    // update census
    buildOpDatas(*context, *result.SourceManager, *call);
    //
    addCallHistory(*context, *call);

    CNS_INFO(" end.");
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
                        hasLHS(expr(declRefExpr().bind("lhsref")).bind("binLhs")),
                        hasRHS(expr(declRefExpr().bind("rhsref")).bind("binRhs"))
                        ).bind("binOp")))

                //hasDescendant(declRefExpr().bind("castee")))    // All castExprs will have this descendant, it is to just get the castee easily.
        ).bind("cast");


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
// TODO: Add missing cast dumps. For example in other cast types.(?).

//---
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

        if(!FOUT.is_open()) {
            std::cout << "File open error.\n";
            return;
        }

        FOUT << "# Census summary so far:\n";
        //censusSummary(FOUT);
        censusSummary();
        //evaluateHistory();
        /*
        for(auto const &[key, _]: census) {
           elaborateHistory(key);
        }
        */
        std::for_each(begin(TypeTransforms), end(TypeTransforms),
            [&](auto &h) {
                elaborateHistory(h.second, {3});
            });

        FOUT << "History collection:\n";
        std::cout << "History collection:\n";
        std::for_each(begin(TypeSummaries), end(TypeSummaries),
            [&](auto const &s) {
                FOUT << "History of (" << s.first << "):\n";
                std::cout << "History of (" << s.first << "):\n";
                FOUT << s.second << "\n";
                std::cout << s.second << "\n";
            });
        /*
        std::for_each(begin(Finality), end(Finality),
            [&](auto const &f) {
                FOUT << "History of (" << f.first << "):\n";
                std::cout << "History of (" << f.first << "):\n";
                FOUT << f << "\n";
                //dumpHistory(FOUT, h.second);
                std::cout << f << "\n";
            });
        */
        FOUT << "# Census summary end.\n";
        FOUT << "end History collection:\n";
        /*
        std::for_each(begin(TypeTransforms), end(TypeTransforms), [&](auto const &h) {
                FOUT << "History of (" << h.first << "):\n";
                std::cout << "History of (" << h.first << "):\n";
                FOUT << h.second << "\n";
                //dumpHistory(FOUT, h.second);
                std::cout << h.second << "\n";
            });
        FOUT << "# Census summary end.\n";
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

int main(int argc, const char **argv) {
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
    FOUT.open("census-dump.txt", std::ios::out);
    //return Tool.run(newFrontendActionFactory<clang::SyntaxOnlyAction>().get());
    return Tool.run(newFrontendActionFactory(&Finder).get());
}

void buildIgnoreList() {
    std::ifstream in;
    in.open("cstdlib.ignore", std::ios::in);
    for(std::string l; std::getline(in, l); ) {
        ignoreFunctions.push_back(l.substr(l.find_last_of(',') + 1));
    }
}

// TODO
// Add unary op handling when it is ancestor of cast.
// - Match assignments that are not inits.
//   - Fix BINOP
// - Due to the changes in computing container function and resolving function pointers to calls,
//   the old old problem (cf. CastMatcher2) of function pointers dominating var decls is back.
//   problem can be solved by not resolving fptr. But since both fptr(p) and the call fptr(p -> f.$0) are using arg match, there is no way to distinguish arg vs ptr (so far). If fptr is not resolved, than qsort(v1, v2, compare(a, b)) will not give v1->compare.$0 e.g.
//
// - Qsort: Infinite loop in bsearch header
//   - Checkout bsearch code to find the loop.
//   - Find a way to prevent going too deep in header.
// - Cast.c: No infinity but f1->f2->f1->f2->break instead of f1->f2->f1->break
