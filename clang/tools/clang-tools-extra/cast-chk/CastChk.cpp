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


// TODO: use TypeInfo from context to detect incompatible size casts.
// TODO: &i -> trace to int i;

// If the LHS is fn ptr, consider skipping the census update.
//  -> Can't, since fptr can be passed as arg which are needed in census.
//
// TODO TODO: resolve fnptr call to (*fnptr) call. 
//  (*comp)(a, b) -> should link a -> (*comp).$0
//  Currently, since *comp is not resolved, parm info is not available.

/*/--
using DeclExpr = NamedType<std::string, struct Expression>;
using ExprType = NamedType<std::string, struct ExpressionType>;
using ExprCategory = NamedType<std::string, struct ExpressionCategory>;
using LinkedParm = NamedType<std::string,struct LinkedParameter>;
using FunctionName = NamedType<std::string, struct FunctionNameType>;
using Location = NamedType<std::string, struct LocationType>;
using ExprHash = NamedType<unsigned, struct DeclHash>;
using LinkingExpr = NamedType<std::string, struct LinkingExpression>;

//--*/
/*
// Function: collection of name, and parameter OpData.
struct ParameterData {
    unsigned argPos_;
    std::string argType_;
    std::string argName_;
};

struct FunctionInfo {
    clang::DeclarationNameInfo name_;
    std::vector<ParameterData> params_;
};
*/

//---

/*
void buildHistory(OpData const& op) {
    CNS_DEBUG("");
    for(auto const& d_: census) {
        auto const& d = ops(d_);
        auto it = std::find_if(begin(d.use_), end(d.use_), [&](auto const& uhash) {
                auto const& u = ops(uhash);
                if(u.qn_ == op.qn_) {
                    return true;
                }
                if(u.linkedParm_ == op.linkedParm_ && u.location_ == op.location_) {
                    return true;
                }
                return false;
            });
        //if(std::find(begin(d.use_), end(d.use_), op.hash_) != std::end(d.use_)) {
        if(it != std::end(d.use_)) {
            CNS_DEBUG("Node history match found.");
            op.history_.insert(d.qn_);
        }
    }
    CNS_DEBUG("end.");
}

void elaborateHistory(OpData const &op, std::optional<int> level) {
    CNS_DEBUG("");
    for(;level && level.value() > 0; level = level.value() - 1) {
        auto const th = op.history_;
        for(auto const& i: th) {
            std::copy(begin(ops(i).history_), end(ops(i).history_), inserter(op.history_, op.history_.begin()));
        }
    }
    CNS_DEBUG("end.");
}
*/

//---
/*
void preprocess(
        clang::ASTContext &context,
        clang::DeclStmt const &decl) {}
*/

// TODO TODO
// Before preprocessing, check if function is meant to be filtered.
// Filter seen functions too.
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

/*
void addFunction(
        clang::ASTContext &context,
        clang::FunctionDecl const &f) {

    CNS_DEBUG("");
    auto const nameinfo = f.getNameInfo();
    std::vector<ParameterData> params;
    unsigned pos = 0;
    std::for_each(f.param_begin(), f.param_end(), [&](auto const *p) {
            // append param data to params.
            params.push_back({pos++, Typename(context, *p), String(context, *p)});
        });
    Functions.insert({cnsHash(context, nameinfo), {nameinfo, params}});

    // TODO: maintain fp->f links such that fp can be resolved to f for f in Functions and valid fp.
    CNS_DEBUG("end");
}
*/

std::vector<unsigned> seenFunctions;
std::vector<std::string> ignoreFunctions;

void OBSOLETEpreprocess(
        clang::ASTContext &context,
        clang::CallExpr const &call) {

    CNS_DEBUG("<CallExpr>");
    // Trigger cast check for the called function.
    auto const *calledFn = getCalleeDecl(call);
    // assert still throws error for qsort. WHY?
    assert(calledFn);
    if(!calledFn) {
        CNS_INFO("<CallExpr> null callee decl");
        CNS_DEBUG("<CallExpr> end.");
        return;
    }

    auto h = cnsHash(context, *calledFn);
    if(std::find(begin(seenFunctions), end(seenFunctions), h) != end(seenFunctions)) {
        FOUT << "[INFO](preprocess<CallExpr>) Skipping processed function: " << String(context, *calledFn) << "\n";
        CNS_DEBUG("<CallExpr> end.");
        return; // already seen
    }
    if(std::find(begin(ignoreFunctions), end(ignoreFunctions), calledFn->getNameAsString()) != end(ignoreFunctions)) {
        FOUT << "[INFO](preprocess<CallExpr>) Skipping ignored function: " << String(context, *calledFn) << "\n";
        CNS_DEBUG("<CallExpr> Adding ignored function to seen functions.");
        //seenFunctions.push_back(cnsHash(context, *calledFn));
        CNS_DEBUG("<CallExpr> end.");
        return; // ignore
    }
    /*
    if(calledFn) {
        FOUT << "[INFO](preprocess) Adding function : " << String(context, *calledFn) << "\n";
        addFunction(context, *calledFn);
    }
    */

    ////if(!calledFn) {
        //// Couldn't find function decl from callexpr
        //// But maybe the call args have a function pointer?
        //// If yes, now's the time to check it.
        //CNS_DEBUG("Checking for Fptr args in function.");
        //
        //std::for_each(call.arg_begin(), call.arg_end(),
            //[&] (auto const *arg) -> void{
            //CNS_DEBUG("start fparg finder.");
            //FOUT << "[DEBUG](preprocess<call>) Arg: " << String(context, *arg) << "\n";
            //auto const *argType = arg->getType().getTypePtrOrNull();
            //if(!argType) {
                //CNS_DEBUG("Arg Type could not be retrieved.");
                //CNS_DEBUG("end fparg finder.");
                //return;
            //}
            //
            //if(argType->isFunctionPointerType()) {
                //CNS_DEBUG("Found FPtr arg.");
            //
                ///*
                //auto const* dt = static_cast<clang::DecltypeType const*>(argType);
                //if(dt) {
                    //CNS_DEBUG("DecltypeType not null");
                    //auto const* d = dt->getAs<clang::NamedDecl>();
                    //if(d) {
                        //CNS_DEBUG("Really?!!!>>>>");
                    //}
                    //else {
                        //CNS_DEBUG("Unsurprising. <<<< ");
                    //}
                //}
                //else {
                    //CNS_DEBUG("DecltypeType null");
                //}
                //auto fh = cnsHash(context, *arg);
                //FOUT << "[DEBUG](preprocess<call> fpargfinder: cnshash(fparg) = " << fh << "\n";
                //auto it = Functions.find(fh);
                //auto it = std::find_if(Functions.begin(), Functions.end()
                //if (it != Functions.end()) {
                    //CNS_DEBUG(">>> Found fp arg match in fdb");
                //}
                //else {
                    //CNS_DEBUG("<<< Didn't find fp arg match in fdb");
                //}
                //*/
                //// FP Type arg.Get the decl out of it and preprocess it.
                //auto const *dre = dyn_cast<DeclRefExpr>(arg);
                //if(dre) {
                    //CNS_DEBUG(">>>DeclRefExpr Arg for Fptr. Find Dom to resolve.");
                //// at this point census probably has the dominator for this fptr.
                //// The dominator is the functionproto or the callee decl.
                //// Get the callee decl from functioncensus.
                ///*
                    //if(auto it = census.find(cnsHash(context, dre)); it != end(census)) {
                            //// 1. get the dominator function.
                            //// 2. find Functions(cnsHash(context, function.name))
                            //// 3. from Functions, find the link between function args and params.
                    //}
                    //else {
                        //CNS_INFO("Could not find function.");
                        //CNS_DEBUG("end fparg finder.");
                        //return;
                    //}
                    //*/
                //}
                //else {
                    //CNS_DEBUG("<<>DeclRefExpr Arg not found for Fptr");
                //}
            //}
            //else {
                //CNS_DEBUG("<<<Fptr arg not found.");
            //}
            //
            ////CNS_DEBUG("Arg is not a function pointer.");
            //CNS_DEBUG("end fparg finder.");
        //});
        //
        ////CNS_DEBUG("<CallExpr> end.");
        ////return;
    ////}

    if(calledFn->hasBody()) {
        auto const *body = calledFn->getBody();
        assert(body);
        MatchFinder m;
        m.match(*body, context);
    }
    seenFunctions.push_back(h);
    CNS_DEBUG("<CallExpr> end.");
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

    FOUT << "[INFO](isNodeDominatorNew) Checking current doms for this dom[" << dom.from_.qn_ << "]\n";
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
        census[node.qn_] = makeNodeInfo(node, dom);
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

bool isFptrOp(OpData const &op) {
    CNS_INFO("");
    FOUT << "[INFO](isFptrOp for [" << op.qn_ << "]{" << op.category_ << "} = " << (op.category_ == "FunctionPointer" || op.category_ == "FunctionProto") << "\n";
    CNS_INFO("end.");
    return op.category_ ==  "FunctionPointer" || op.category_ == "FunctionProto";
}

bool isSpuriousFPDom(OpData const &from, OpData const &to) {
    // If from: FP and to: func.$0: (not void*/fp)
    // then return false
    // -> handle cases where fp is mistakenly matched to f.$0

    if(isFptrOp(from)) {
        return false;
    }

    if(isFptrOp(to)) { // || to.type_ == "void *") {
        return false;
    }

    return true;
}

/*
void resolveFptrInCensus(
        OpData &from,
        OpData &to) {

    CNS_DEBUG("");
    auto isFarg = isFptrOp(from);
    auto isFparm = isFptrOp(to);
    if(isFarg || isFparm) {
        // g -> f.$1
        // L : list of operands with f.$1 prefix
        std::vector<CensusKey> opsToUpdate;
        auto getRelatedOps = [&](auto const& cn) {
            auto const &[k, _] = cn;
            if(k.find(to.qn_) != std::string::npos) {
                opsToUpdate.push_back(k);
            }
        };
        std::for_each(begin(census), end(census), getRelatedOps);
        FOUT << "[INFO](resolveFptrInCensus) Operands to update: " << opsToUpdate.size() << "\n";

        // Update to.qn_ to g. g is available from 'from'.
        std::for_each(begin(opsToUpdate), end(opsToUpdate),
                [&](auto const &key) {
                    auto &[op, ds_] = census[key];
                    auto const &n = op.qn_.find(to.qn_);
                    if(n != std::string::npos) {
                        FOUT << "[INFO](resolveFptrInCensus) Updating: " << op.qn_ << "\n";
                        op.qn_.replace(n, from.qn_.length() + 3, from.qn_);
                        FOUT << "[INFO](resolveFptrInCensus) to: " << op.qn_ << "\n";
                    }
                    if(ds_) {
                        auto &ds = ds_.value();
                        std::for_each(begin(ds), end(ds), [&](auto & d) -> void{
                            auto const &dn = key.find(to.qn_);
                                if(dn != std::string::npos) {
                                    FOUT << "[INFO](resolveFptrInCensus)(Domupdate) Updating: " << d.from_.qn_ << "\n";
                                    d.from_.qn_.replace(dn, from.qn_.length() + 3, from.qn_);
                                    FOUT << "[INFO](resolveFptrInCensus)(Domupdate) to: " << d.from_.qn_ << "\n";
                                }
                            });
                    }
                    auto const &nn = op.linkedParm_.find(to.qn_);
                    if(nn != std::string::npos) {
                        FOUT << "[INFO](resolveFptrInCensus) Updating: " << op.linkedParm_ << "\n";
                        op.linkedParm_.replace(nn, from.qn_.length() + 3, from.qn_);
                        FOUT << "[INFO](resolveFptrInCensus) to: " << op.linkedParm_ << "\n";
                        // TODO update usechain algorithm to ensure qn is used instead of key.
                    }
                });

        // Update prefix to g.
        to.qn_ = from.qn_;
        if(to.linkedParm_.find(from.qn_) != std::string::npos) {
            to.linkedParm_.replace(to.linkedParm_.find(from.qn_), from.qn_.length(), from.qn_);
        }
    }
    CNS_DEBUG("end");
}
*/

void updateCensus(
        OpData &from,
        OpData &to,
        DominatorData const &dom) {

    CNS_DEBUG("<from, to, dom>");

    /*
    if(isSpuriousFPDom(from, to)) {
        CNS_WARN("Skipping spurious fptr link to non-fptr.");
        CNS_DEBUG("<from, to, dom> end");
        return;
    }
    */

    //resolveFptrInCensus(from, to);
    addDomNode(from);
    auto it = census.find(to.qn_);
    if(it == std::end(census)) {
        // `to` not in census
        CNS_INFO("<0> Inserting new node for 'to'.");
        census.insert(makeCensusNode(to, dom));
        CNS_DEBUG("<from, to, dom> end");
        return;
    }

    CNS_INFO("<0> 'to' already in census.");
    chkNodeDataForChange(to);
    if(isNodeDominatorNew(to, dom)) {
        CNS_INFO("<0> 'to' has new dominator.");
        appendNodeDominator(to, dom);
    }
    CNS_DEBUG("<from, to, dom> end.");
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
         << "Funcslinked: " << lhs.container_ << "() -> " << dom.callee_.value_or("(n/a)") << "()\n"
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
        getLinkedFunction(context, castExpr, dest)
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
    auto const *call = result.Nodes.getNodeAs<CallExpr>("call");
    auto const *fptr = result.Nodes.getNodeAs<CallExpr>("fptr");
    auto const *unaryOp = result.Nodes.getNodeAs<UnaryOperator>("unaryOp");

    /*
    if(!!var) {
        updateCensus(*context, *castExpr, *castSource, var, location);
    }
    */
    if(!!unaryOp) {
        updateCensus<CastSourceType::UnaryOp>(*context, *result.SourceManager, *castExpr, *s_unaryCastee, *unaryOp);
    }
    else if (!!call) {
        //updateCensus<CastSourceType::FunctionArg>(*context, *result.SourceManager, *castExpr, *s_callArg, *call);
    }
    else if(!!fptr) {
        //updateCensus<CastSourceType::Function>(*context, *result.SourceManager, *castExpr, *s_fptrRef, *fptr);
    }
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

    /*
    } else if(!!binop) {
        callee = containerFn;
        cast.second = binop;
        castExprType = "binop";
        dest = String(context, binop);
    */
    /*
    } else if(!!gexpr) {
        cast.second = gexpr;
    }
    */

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

    DominatorData dom{lhs, {}, {}, {}};
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
            getLinkedParmQn(context, call, arg)
        };
    }
    auto const *dre = dyn_cast<DeclRefExpr>(e);
    if(!dre) {
        CNS_DEBUG(" end.");
        return {
            cnsHash(context, arg),
            String(context, arg),
            Typename(context, arg),
            TypeCategory(context, arg),
            String(context, arg),
            getContainerFunction(context, arg),
            call.getExprLoc().printToString(sm),
            getLinkedParmQn(context, call, arg)
        };
    }
    CNS_DEBUG(" end.");
    return buildOpData(context, sm, arg, *dre);
}

auto buildOpDatas(clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CallExpr const &call) {

    // for each arg
    unsigned pos = 0;
    OpData lhs, rhs;
    std::for_each(call.arg_begin(), call.arg_end(),
        [&](auto const *arg) {
            // create source(arg) op
            lhs = buildArgOp(context, sm, call, *arg);

            // create target(param) op
            auto const *parmd = getParamDecl(context, call, pos);
            if(parmd) {
                auto const *parm = dyn_cast<clang::ParmVarDecl>(parmd);
                rhs = {
                    cnsHash(context, *parm),
                    parm->getNameAsString(),
                    Typename(context, *parm),
                    TypeCategory(context, *parm),
                    String(context, call, pos),
                    getContainerFunction(context, *arg),
                    call.getExprLoc().printToString(sm),
                    call.getDirectCallee()->getQualifiedNameAsString() + ".$" + std::to_string(pos)
                };
            }
            else {
                auto const *fn = call.getCallee();
                if(fn) {
                    std::stringstream ss;
                    ss << String(context, *fn) << ".$" << pos;
                    //cnsHash(context, arg),
                    //ss.str(),
                    //"(T)" or typename(context, arg)
                    //"(?)" or typecategory(context, arg)
                    //String(context, call, pos)
                    //getContainerFunction(context, arg)
                    //call.getExprLoc().printToString(sm)
                    //ss.str()
                }
                else {
                    //cnsHash(context, arg),
                    //String(context, call),
                    //"(T')" or typename(context, arg)
                    //"(?)" or typecategory(context, arg)
                    //String(context, call, pos)
                    //call.getExprLoc().printToString(sm)
                    //("UnkFn_" + String(context, call)
                }
            }

            // update census()
            DominatorData dom{
                lhs,
                String(context, *arg),
                {}, //getCastExprType(arg),
                {} //getLinkedFunction(context, call, arg)
            };
            updateCensus(lhs, rhs, dom);
        });
}

OpData buildOpDataHofArg(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::DeclRefExpr const &e) {

    CNS_DEBUG("");
    FOUT << "[INFO](buildOpDataHofArg) ref: \n"
         << String(context, e) << "; type: "
         << Typename(context, e) << "\n";

    return {
        cnsHash(context, e),
        String(context, e),
        Typename(context, e),
        TypeCategory(context, e),
        "(No Param match tried due to declrefexpr)",
        getContainerFunction(context, e),
        e.getExprLoc().printToString(sm),
        String(context, e)
    };
    CNS_DEBUG("end");
}

OpData buildOpDataHofCall(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::DeclRefExpr const& farg,
        clang::CallExpr const &e) {

    CNS_DEBUG("");
    FOUT << "[INFO](buildOpDataHofCall) call: \n"
         << String(context, e) << "\n";

    unsigned argPos = 0;
    auto match = std::find_if(e.arg_begin(), e.arg_end(),
        [&] (auto const &arg) {
        argPos++;
        FOUT << "[DEBUG](buildOpData<call>) Argpos: " << argPos << "\n";
        return clang::Expr::isSameComparisonOperand(arg, &farg);
    });
    FOUT << "[DEBUG](buildOpData<call>) final argpos: " << argPos << "\n";

    std::string parmId;
    clang::ParmVarDecl const *parm = nullptr;
    llvm::raw_string_ostream stream(parmId);

    if (match != e.arg_end() && argPos <= e.getNumArgs()) {
        auto const *parmd = getParamDecl(context, e, argPos - 1);
        if(parmd) {
            parm = dyn_cast<clang::ParmVarDecl>(parmd);
            if(parm) {
                parm->printQualifiedName(stream);
            }
        }
    }
    assert(parm);
    if(!parm) {
        // Couldn't find parameter info but we can still use info from call expr.
        auto const *c = e.getCallee();
        //auto const *c = e.getCalleeDecl();
        if(c) {
        std::stringstream ss;
        ss << String(context, *c) << ".$" << argPos - 1;

        return {
            cnsHash(context, e),
            ss.str(),
            "(T)", //Typename(context, castExpr),
            "(?)", //TypeCategory(context, castExpr),
            (argPos > e.getNumArgs()
                ? ("(Failed to match arg)")
                : (String(context, e, argPos - 1))),
            getContainerFunction(context, e),
            e.getExprLoc().printToString(sm),
            ss.str()
        };
        }
        else {
        return {
            cnsHash(context, e),
            String(context, e),
            "(T')", //Typename(context, castExpr),
            "(?)", //TypeCategory(context, castExpr),
            (argPos > e.getNumArgs()
                ? ("(Failed to match arg)")
                : (String(context, e, argPos - 1))),
            getContainerFunction(context, e),
            e.getExprLoc().printToString(sm),
            String(context, e)
        };
        }
    }

    // CHECK TODO what is container function taken from e?
    return {
        cnsHash(context, *parm),
        parmId,
        Typename(context, *parm),
        TypeCategory(context, *parm),
        (argPos > e.getNumArgs()
            ? ("(Failed to match arg)")
            : (String(context, e, argPos - 1))),
        getContainerFunction(context, e),
        e.getExprLoc().printToString(sm),
        e.getDirectCallee()
            ? (e.getDirectCallee()->getQualifiedNameAsString() + ".$" + std::to_string(argPos - 1))
            : (String(context, e))
    };
    CNS_DEBUG("end");
}

void updateCensusHof(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::DeclRefExpr const &arg,
        clang::CallExpr const &call) {

    CNS_DEBUG("");
    // lhs (arg)
    //
    auto lhs = buildOpDataHofArg(context, sm, arg);
    auto rhs = buildOpDataHofCall(context, sm, arg, call);
    DominatorData dom{
        lhs,
        String(context, call),
        "call",
        "TODO callee name"
    };
    updateCensus(lhs, rhs, dom);
    logCensusUpdate(lhs, rhs, dom);
    CNS_DEBUG(" end.");
}

std::vector<HistoryTemplate> TransformTemplates;
std::vector<History> TypeTransforms;

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
    auto it = std::find(begin(TransformTemplates), end(TransformTemplates), fn);
    if(it != std::end(TransformTemplates)) {
        FOUT << "[INFO](preprocess<CallExpr>) Skipping processed function: " << String(context, *calledFn) << "\n";
        CNS_DEBUG("<CallExpr> end.");
        return;
    }

    if(std::find(begin(ignoreFunctions), end(ignoreFunctions), fn) != end(ignoreFunctions)) {
        FOUT << "[INFO](preprocess<CallExpr>) Skipping ignored function: " << String(context, *calledFn) << "\n";
        CNS_INFO("<CallExpr> Adding ignored function to seen functions.");
        CNS_DEBUG("<CallExpr> end.");
        return; // ignore
    }

    auto h = cnsHash(context, *calledFn);
    if(std::find(begin(seenFunctions), end(seenFunctions), h) != end(seenFunctions)) {
        FOUT << "[INFO](preprocess<CallExpr>) Skipping seen function: " << String(context, *calledFn) << "\n";
        CNS_INFO("<CallExpr> Adding seen function to seen functions.");
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
    TransformTemplates.push_back({*calledFn});
    // TODO end

    CNS_DEBUG("<CallExpr> end.");
}

void addCallHistory(clang::ASTContext & context, clang::CallExpr const& call) {
    CNS_DEBUG("");
    auto const *calledFn = getCalleeDecl(call);
    assert(calledFn);
    if(!calledFn) {
        CNS_ERROR("Null callee decl.");
        CNS_DEBUG("end.");
        return;
    }

    auto const& fn = calledFn->getNameAsString();
    auto it = std::find(begin(TransformTemplates), end(TransformTemplates), fn);
    if(it == std::end(TransformTemplates)) {
        FOUT << "[ERROR](addCallHistory) No template found for : " << fn << "()\n";
        CNS_DEBUG("end.");
        return;
    }

    // Instantiate template and add to history
    auto const hs = it->instantiate(context, call);
    // For each arg operand opA, H(opA) is extended by H(A).
    unsigned i = 0;
    std::for_each(call.arg_begin(), call.arg_end(), [&](auto const *a) {
            CNS_DEBUG("for_each arg");
            // get key for a
            auto const& qn = getLinkedParmQn(context, call, *a);
            // Get H(a)
            auto it = std::find(begin(TypeTransforms), end(TypeTransforms), qn);
            if( it != std::end(TypeTransforms)) {
                FOUT << "[DEBUG](addCallHistory) Found existing history for" << qn << "\n";
                // extend history
                if(i < hs.size()) {
                    (*it).extend(hs[i]);
                }
                else {
                    CNS_ERROR("Out of bound history insert.");
                }
            }
            else {
                FOUT << "[DEBUG](addCallHistory) Adding history for" << qn << "\n";
                TypeTransforms.push_back(hs[i]);
            }
            ++i;
        });
    CNS_DEBUG("end.");
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
    CNS_DEBUG("");
    assert(result);
    auto *context = result.Context;
    assert(context);

    auto const *call = result.Nodes.getNodeAs<CallExpr>("ce");
    assert(call);

    // - Preprocess call.
    preprocess(*context, *call);
    // update census
    buildOpDatas(*context, *result.SourceManager, *call);
    addCallHistory(*context, *call);

    CNS_DEBUG(" end.");
}
/*
void processHof(MatchFinder::MatchResult const &result) {
    CNS_DEBUG("");
    assert(result);
    auto *context = result.Context;
    assert(context);

    auto const *fnarg = result.Nodes.getNodeAs<DeclRefExpr>("ceFnArg");
    auto const *call = result.Nodes.getNodeAs<CallExpr>("ce");
    assert(fnarg);
    assert(call);
    updateCensusHof(*context, *result.SourceManager, *fnarg, *call);
    CNS_DEBUG(" end.");
}
*/

//----------------------------------------------------------------------------
// MATCHERS

// similar construct can match a function ptr. {VarDecl, DeclRefExpr}
DeclarationMatcher AssignMatcher =
    varDecl(
            anyOf(
                hasDescendant(declRefExpr().bind("assignee")),
                hasDescendant(expr().bind("literal")))
            ).bind("varDecl");

auto CallMatcher = 
    callExpr().bind("ce");
            //hasDescendant(
            //    unaryOperator(
            //        hasDescendant(declRefExpr().bind("ceFnArg"))
            //        ))).bind("ce");
//  todo
// Function matcher = 
//    void f(int i);
//    f(x); 
//    matcher: Functions.push_back({f: void, i.1 -> CANCEL
//  NO todo 
StatementMatcher CastMatcher =
    castExpr(
            anyOf(
                //hasAncestor(declStmt().bind("var")), // Check if needed since varDecl is also present
                /*
                 * Now handled through call matcher
                allOf(
                    unless(hasCastKind(CK_FunctionToPointerDecay)),
                    //hasCastKind(CK_BitCast),
                    //hasCastKind(CK_LValueToRValue),
                    hasAncestor(
                        callExpr().bind("call")),
                    hasDescendant(declRefExpr().bind("arg"))),

                allOf(
                    //hasCastKind(CK_FunctionToPointerDecay),//LValueToRValue),
                    hasAncestor(
                        callExpr().bind("fptr")),
                    hasCastKind(CK_LValueToRValue),
                    hasDescendant(
                        declRefExpr(hasType(pointerType(pointee(functionType())))).bind("callee"))),
                 * Now handled through call matcher
                */

                hasDescendant(
                    unaryOperator(
                        hasDescendant(declRefExpr().bind("unaryCastee"))
                        ).bind("unaryOp")),

                // lhs: declrefexpr or expr(hasDescendant(declrefexpr))
                // rhs: declrefexpr or expr(hasDescendant(declrefexpr)) or literal
                hasParent(
                    binaryOperator(
                        isAssignmentOperator(),
                        hasLHS(expr(declRefExpr().bind("lhsref")).bind("binLhs")),
                        hasRHS(expr(declRefExpr().bind("rhsref")).bind("binRhs"))).bind("binOp")))

                //hasDescendant(declRefExpr().bind("castee")))    // All castExprs will have this descendant, it is to just get the castee easily.
        ).bind("cast");


StatementMatcher CastMatcher2 =
    castExpr(
        allOf(
            /*
            anyOf(
                hasCastKind(CK_BitCast),
                hasCastKind(CK_LValueToRValue)),
            */
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
        // Calls for fn args
        auto const *ce = result.Nodes.getNodeAs<clang::CallExpr>("ce");

        if(castExpr) {
            processCast(result);
        }

        if(varDecl) {
            processVar(result);
        }

        if(ce) {
            //processHof(result);
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
        FOUT << "# Census summary end.\n";

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
    Finder.addMatcher(CastMatcher, &historian);
    Finder.addMatcher(AssignMatcher, &historian);
    Finder.addMatcher(CallMatcher, &historian);

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
