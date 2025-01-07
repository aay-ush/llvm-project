#ifndef OPDATA_H
#define OPDATA_H

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

#include <string>
#include <unordered_map>
#include <utility>
#include <algorithm>
#include <optional>
#include <any>
#include <tuple>
#include <set>
#include <unordered_set>
#include <memory>

#include "utils.h"

enum class CastSourceType {
    UnaryOp,        // &i -> i
    BinaryOp,       // a = b
    Function,       // void (*)() -> void f()
    FunctionArg     // f((void*)&i) -> f.$0
};

// OpData is built for both operands of cast/assignment for every match.
// Using OpData we can establish dominator relationships between the operands.
// We build 'dominated-by' information for every operand.
// int *pi = (int*) pv
//    -> lhs/dominator: pv
//    -> rhs/dominee  : pi
//    pi (<- pv) is stored in Census.
// The dominator data is actually kept as a vector.
// If there are multiple dominators (like for a function parameter), we update the dominator
// vector to include all the dominators seen so far.
//
// Using dominator data, build a history tree.
// Challenge: Handling N-N relationships.
// Fix: Use a placeholder history when history is not available. Update the history in second pass.
//
using OpID = std::string;

// We need the map key to be pluggable:
// hof(void *pv, fptr pf)
// pv: key = hof.$0
// pf: key = hof.$1
//
// hof(pi, f)
// => pi -> hof.$0 (pv)
//     f -> hof.$1 (pf)
//
// hof(void *pv, fptr pf) {
//   pf(pv);    // => pv -> pf.$0
// }
// We don't know what pf.$0 is, we can extrapolate the usechain though:
// pf -> hof.$1
// => pv -> hof.$1.$0
// To plug values from context above we need the key to be pluggable:
//  pi -> hof.$0
//  f -> hof.$1
//  => pv -> [hof.$1].$0 = f.$0

// Pluggable key:
// Key has two components: prefix key + id
//  - prefix key: everything before id; it is also a key.
//  - id: $<param number> | <local variable name> | <expr>
// Either complete key or Prefix can be plugged into but id is not pluggable.
// i.e. P(hof.$0, pi) = pi (correct)
//      P(hof.$0, pi) = pi.$0 (correct)
//      P(hof.$1, pi) = hof.$pi (incorrect)
//
// What can be plugged? Another key
//
// K1{{{}, 'hof'},'$0'}
// K2{{},'pi'}
// K3{{{{},'hof'}, '$1'}, '$0'}
// K4{{}, 'f'}
// P(K1, K2) => K2
// P(K3, K4) => {{{}, 'f'}, '$0'}
// P(K1, K4) => K4
// P(K1, K4.prefix) => {{{}, 'f'}, '$0'}
//
// What does it mean to plug a key?
//  - Key is pluggable only if the OpData is incomplete.
//  - Plugging implies the plugged key is instantiated as the plug.
//    Since instantiation is temporary, it is kept with the plug.
//    We are only interested in the history.
//    if P(k1, k2) = k2 => K1 is plugged with k2
//    then H(k1) = H(k2) and {P(k1, k2), H(k1)} is stored locally 
//
struct OpData {
    unsigned hash_ {0};
    std::string expr_;
    std::string type_;
    std::string category_;
    std::string linkedParm_;
    std::string container_;
    std::string location_;
    std::string qn_;
    std::string castKind_ {};
    mutable std::vector<OpID> use_ {};
    //bool isComplete {true};
    //mutable std::unordered_set<OpID> history_ {};
    //mutable std::weak_ptr<History> history_ {};
};

bool operator==(OpData const &lhs, OpData const &rhs) {
    return lhs.qn_ == rhs.qn_;
}

bool operator!=(OpData const &lhs, OpData const &rhs) {
    return !(lhs == rhs);
}

//---

template<CastSourceType s_type, typename T>
OpData buildOpData(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CastExpr const &castExpr,
        T const &e) {
    static_assert(impl_false<s_type>, "Unknown cast source type used");
}

// Build operand from ParamDecl
OpData buildOpData(
        clang::ParmVarDecl const &parm) {

    auto &context = parm.getASTContext();
    auto const &sm = context.getSourceManager();

    CNS_DEBUG("<ParmVarDecl> parm: {}; type: {}", String(context, parm), Typename(context, parm));

    return {
        cnsHash(context, parm),
        String(context, parm),
        Typename(context, parm),
        TypeCategory(context, parm),
        getLinkedParm(context, parm),
        getContainerFunction(context, parm),
        parm.getLocation().printToString(sm),
        qualifiedName(context, parm)
    };
}

// Build operand from argexpr
// May need improvements to use associated declref instead
OpData buildOpData(
        clang::ASTContext &context,
        clang::Expr const &arg) {

    //auto &context = arg.getASTContext();
    auto const &sm = context.getSourceManager();

    CNS_DEBUG("<Arg expr> arg: {}; type: {}", String(context, arg), Typename(context, arg));

    return {
        cnsHash(context, arg),
        String(context, arg),
        Typename(context, arg),
        TypeCategory(context, arg),
        String(context, arg), //getLinkedParm(context, arg),
        getContainerFunction(context, arg),
        arg.getExprLoc().printToString(sm),
        String(context, arg), //qualifiedName(context, arg),
    };
}

// Build operand from VarDecl
OpData buildOpData(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::VarDecl const &var) {

    CNS_DEBUG("<VarDecl> var: {}; type: {}", String(context, var), Typename(context, var));

    return {
        cnsHash(context, var),
        String(context, var),
        Typename(context, var),
        TypeCategory(context, var),
        getLinkedParm(context, var),
        getContainerFunction(context, var),
        var.getLocation().printToString(sm),
        qualifiedName(context, var)
    };
}

// Build operand from ValueDecl
OpData buildOpData(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::DeclRefExpr const &e,
        clang::ValueDecl const &decl) {

    CNS_DEBUG("<ValueDecl> decl: {}; type: {}", String(context, decl), Typename(context, decl));

    return {
        cnsHash(context, decl),
        String(context, decl),
        Typename(context, decl),
        TypeCategory(context, decl),
        getLinkedParm(context, e, decl.getDeclName()),
        getContainerFunction(context, e),
        decl.getLocation().printToString(sm),
        qualifiedName(context, e) //, decl.getDeclName())
    };
}

// Build operand from DeclRef decl
OpData buildOpData(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::Expr const &castExpr,
        clang::DeclRefExpr const &e,
        clang::ValueDecl const& decl) {

    CNS_DEBUG("<Decl> decl: {}; type: {}", String(context, e), Typename(context, e));

    return {
        cnsHash(context, decl),
        String(context, decl),
        Typename(context, decl),
        TypeCategory(context, decl),
        getLinkedParm(context, decl, decl.getDeclName()),
        getContainerFunction(context, castExpr),
        decl.getLocation().printToString(sm),
        qualifiedName(context, decl, decl.getDeclName())
    };
}

// Build OpData for DeclRef Statement.
OpData buildOpData(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::Expr const &castExpr,
        clang::DeclRefExpr const &e,
        clang::Stmt const& s) {

    CNS_DEBUG("<Stmt> stmt: {}; type: {}", String(context, s), Typename(context, e));

    return {
        cnsHash(context, s),
        String(context, e),
        Typename(context, e),
        TypeCategory(context, e),
        getLinkedParm(context, s, e.getNameInfo()),
        getContainerFunction(context, castExpr),
        castExpr.getExprLoc().printToString(sm),
        qualifiedName(context, s, e.getNameInfo())
    };
}

// buildOpDataBinOpLHS
OpData buildOpData(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::Expr const &castExpr,
        clang::DeclRefExpr const &e) {

    CNS_DEBUG_MSG("<DeclRef>");
    CNS_DEBUG("<DeclRef> ref: {}; type: {}", String(context, e), Typename(context, e));

    auto const *refd = e.getReferencedDeclOfCallee();
    if(refd) {
        CNS_INFO_MSG("Building data from ReferencedDecl");
        auto const *vd = dyn_cast<VarDecl>(refd);
        if(vd) {
            CNS_INFO_MSG("ReferencedDecl is VarDecl");
            return buildOpData(context, sm, *vd);
        }
    }

    auto const *stmt = e.getExprStmt();
    auto const *decl = e.getDecl();
    if(!!decl) {
        CNS_INFO_MSG("Building data from Decl from DeclRefExpr");
        auto const *var = dyn_cast<clang::VarDecl>(decl);
        if(var) {
            CNS_INFO_MSG("Found VarDecl from DeclRefExpr");
            return buildOpData(context, sm, *var);
        }
        CNS_INFO_MSG("NO VarDecl from DeclRefExpr");
        /*
        auto const *nd = e.getFoundDecl();
        if(!!nd) {
            CNS_INFO_MSG("Found getFoundDecl");
            if(nd->isFunctionPointerType()) {
                CNS_INFO_MSG("nd: Found function pointer decl");
                auto const *fp = nd->getAsFunction();
                if(!!fp) {
                    CNS_INFO_MSG("nd: Got function pointer");
                    return buildOpData(context, sm, castExpr, e, *fp);
                }
                CNS_INFO_MSG("nd: Could not extract function pointer");
            }
            CNS_INFO_MSG("nd: Not function pointer type");
        }
        else {
            CNS_INFO_MSG("No getFoundDecl");
        }
        if(decl->isFunctionPointerType()) {
            CNS_INFO_MSG("Found function pointer decl");
            auto const *fp = decl->getAsFunction();
            if(!!fp) {
                CNS_INFO_MSG("Got function pointer");
                return buildOpData(context, sm, castExpr, e, *fp);
            }
            CNS_INFO_MSG("Could not extract function pointer");
        }
        CNS_INFO_MSG("Not function pointer type");
        */
        auto const *fp = decl->getAsFunction();
        if(!!fp) {
            CNS_INFO_MSG("Got function pointer");
            return buildOpData(context, sm, castExpr, e, *fp);
        }
        CNS_INFO_MSG("Not function pointer type");
        return buildOpData(context, sm, castExpr, e, *decl);
    }

    if(!!stmt) {
        CNS_INFO_MSG("Building data from Expr stmt from DeclRefExpr");
        return buildOpData(context, sm, castExpr, e, *stmt);
    }

    CNS_ERROR_MSG("DeclRefExpr has no decl or stmt.");
    CNS_DEBUG_MSG("<DeclRef> end");

    return {
        cnsHash(context, e),
        String(context, e),
        Typename(context, e),
        TypeCategory(context, e),
        "(No Param match due to declrefexpr error)",
        getContainerFunction(context, castExpr),
        castExpr.getExprLoc().printToString(sm),
        String(context, e)
    };
}

// Build OpData for Function Call Arg. (Difference in getLnkedParmqn, no cast expression involved.)
OpData buildOpDataArg(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::Expr const &arg,
        clang::DeclRefExpr const &e) {

    CNS_DEBUG("<DeclRef2> ref: {}; type: {}", String(context, e), Typename(context, e));

    auto const *refd = e.getReferencedDeclOfCallee();
    if(refd) {
        CNS_INFO_MSG("Building data from ReferencedDecl");
        auto const *vd = dyn_cast<VarDecl>(refd);
        if(vd) {
            CNS_INFO_MSG("ReferencedDecl is VarDecl");
            return buildOpData(context, sm, *vd);
        }
    }

    auto const *stmt = e.getExprStmt();
    auto const *decl = e.getDecl();
    if(!!decl) {
        CNS_INFO_MSG("Building data from Decl from DeclRefExpr");
        auto const *var = dyn_cast<clang::VarDecl>(decl);
        if(var) {
            CNS_INFO_MSG("Found VarDecl from DeclRefExpr");
            return buildOpData(context, sm, *var);
        }
        CNS_WARN_MSG("NO VarDecl from DeclRefExpr");
        return {
            cnsHash(context, *decl),
            String(context, e),
            Typename(context, e),
            TypeCategory(context, e),
            getLinkedParm(context, *decl, e.getNameInfo()),
            getContainerFunction(context, arg),
            arg.getExprLoc().printToString(sm),
            qualifiedName(context, *decl, e.getNameInfo())
        };
    }

    if(!!stmt) {
        CNS_INFO_MSG("Building data from Expr stmt from DeclRefExpr");
        //return buildOpData(context, sm, arg, e, *stmt);
        return {
            cnsHash(context, *stmt),
            String(context, e),
            Typename(context, e),
            TypeCategory(context, e),
            getLinkedParm(context, *stmt, e.getNameInfo()),
            getContainerFunction(context, arg),
            arg.getExprLoc().printToString(sm),
            qualifiedName(context, *stmt, e.getNameInfo())
        };
    }

    CNS_ERROR_MSG("DeclRefExpr has no decl or stmt.");

    return {
        cnsHash(context, e),
        String(context, e),
        Typename(context, e),
        TypeCategory(context, e),
        "(No Param match due to declrefexpr error)",
        getContainerFunction(context, arg),
        arg.getExprLoc().printToString(sm),
        String(context, e)
    };
}

template<>
OpData buildOpData<CastSourceType::UnaryOp>(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CastExpr const &castExpr,
        clang::UnaryOperator const &op) {

    CNS_DEBUG("<UnaryOp> op: {}; type: {}", String(context, op), Typename(context, op));

    return {
        cnsHash(context, op),
        String(context, op),
        Typename(context, op),
        TypeCategory(context, op),
        "(TODO param_check)",
        getContainerFunction(context, castExpr),
        castExpr.getExprLoc().printToString(sm),
        String(context, op)
    };
}


/*
template<>
OpData buildOpData<CastSourceType::Function>(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CastExpr const &castExpr,
        clang::CallExpr const &e) {

    FOUT << "[INFO ](buildOpData<callExpr:fptr>) call: \n"
         << String(context, e) << "\n";

    return {
        cnsHash(context, e),
        String(context, e),
        Typename(context, e),
        TypeCategory(context, e),
        "(Not parm)",
        getContainerFunction(context, e),
        e.getExprLoc().printToString(sm),
        e.getDirectCallee()
            ? (getContainerFunction(context, e) + "." + e.getDirectCallee()->getQualifiedNameAsString())
            : (getContainerFunction(context, e) + "." + String(context, e))
    };
}
*/

/*
template<>
OpData buildOpData<CastSourceType::FunctionArg>(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CastExpr const &castExpr,
        clang::CallExpr const &e) {

    FOUT << "[INFO ](buildOpData<callExpr:farg>) call: \n"
         << String(context, e) << "\n"
         << "; Cast expr: " << String(context, castExpr) << "\n";

    // TODO TODO
    // Check if e.args is fptr, if yes, then preprocess fptr
    // and link other args with *fptr.

    // Get cast expressions position in call argument list.
    unsigned argPos = 0;
    auto match = std::find_if(e.arg_begin(), e.arg_end(),
        [&] (auto const &arg) {
        argPos++;
        FOUT << "[DEBUG](buildOpData<call>) Argpos: " << argPos << "\n";
        return clang::Expr::isSameComparisonOperand(arg, &castExpr);
    });
    FOUT << "[DEBUG](buildOpData<call>) final argpos: " << argPos << "\n";
    //for(auto arg = e.arg_begin(); arg != e.arg_end(); argPos++, ++arg) {
    //    FOUT << "[DEBUG](buildOpData<call>) Argpos: " << argPos << "\n";
    //    if(clang::Expr::isSameComparisonOperand(*arg, &castExpr))
    //        break;
    //}
    //argPos += 1;

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
            cnsHash(context, castExpr),
            ss.str(),
            "(T)", //Typename(context, castExpr),
            "(?)", //TypeCategory(context, castExpr),
            (argPos > e.getNumArgs()
                ? ("(Failed to match arg)")
                : (String(context, e, argPos - 1))),
            getContainerFunction(context, castExpr),
            e.getExprLoc().printToString(sm),
            ss.str()
        };
        }
        else {
        return {
            cnsHash(context, castExpr),
            String(context, e),
            "(T')", //Typename(context, castExpr),
            "(?)", //TypeCategory(context, castExpr),
            (argPos > e.getNumArgs()
                ? ("(Failed to match arg)")
                : (String(context, e, argPos - 1))),
            getContainerFunction(context, castExpr),
            e.getExprLoc().printToString(sm),
            e.getDirectCallee()
                ? (e.getDirectCallee()->getQualifiedNameAsString() + ".$" + std::to_string(argPos - 1))
                : ("UnkFn_" + String(context, e))
        };
        }
    }

    return {
        cnsHash(context, *parm),
        parmId,
        Typename(context, *parm),
        TypeCategory(context, *parm),
        (argPos > e.getNumArgs()
            ? ("(Failed to match arg)")
            : (String(context, e, argPos - 1))),
        getContainerFunction(context, castExpr),
        e.getExprLoc().printToString(sm),
        e.getDirectCallee()
            ? (e.getDirectCallee()->getQualifiedNameAsString() + ".$" + std::to_string(argPos - 1))
            : (String(context, e))
    };
}
*/

template<>
OpData buildOpData<CastSourceType::BinaryOp>(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CastExpr const &castExpr,
        clang::DeclRefExpr const &e) {

    CNS_INFO_MSG("<BinaryOp, DeclRefExpr>");
    return buildOpData(context, sm, castExpr, e);
}

#endif

