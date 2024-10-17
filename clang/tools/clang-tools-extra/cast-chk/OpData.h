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
#include <memory>

#include "utils.h"

enum class CastSourceType {
    UnaryOp,        // &i -> i
    BinaryOp,       // a = b
    Function,       // void (*)() -> void f()
    FunctionArg     // f((void*)&i) -> f.$0
};

using OpID = std::string;
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

class History;

struct OpData {
    unsigned hash_ {0};
    std::string expr_;
    std::string type_;
    std::string category_;
    std::string linkedParm_;
    std::string container_;
    std::string location_;
    std::string qn_;
    mutable std::vector<OpID> use_ {};
    //mutable std::unordered_set<OpID> history_ {};
    mutable std::weak_ptr<History> history_ {};
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

OpData buildOpData(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::VarDecl const &var) {

    FOUT << "[INFO](buildOpData<VarDecl>) var: \n"
         << String(context, var) << "; type: "
         << Typename(context, var) << "\n";

    return {
        cnsHash(context, var),
        String(context, var),
        Typename(context, var),
        TypeCategory(context, var),
        getLinkedParm(context, var),
        getContainerFunction(context, var),
        var.getLocation().printToString(sm),
        //getContainerFunction(context, var) + "_" +
        getLinkedParmQn(context, var)
        //std::to_string(var.getID())
    };
}

OpData buildOpData(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::DeclRefExpr const &e,
        clang::ValueDecl const &d) {

    FOUT << "[INFO](buildOpData<ValueDecl>) decl: \n"
         << String(context, d) << "; type: "
         << Typename(context, d) << "\n";

    return {
        cnsHash(context, d),
        String(context, d),
        Typename(context, d),
        TypeCategory(context, d),
        getLinkedParm(context, e, d.getDeclName()),
        getContainerFunction(context, e),
        d.getLocation().printToString(sm),
        //getContainerFunction(context, d) + "_" +
        getLinkedParmQn(context, e, d.getDeclName())
        //std::to_string(d.getID())
    };
}

template<>
OpData buildOpData<CastSourceType::UnaryOp>(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CastExpr const &castExpr,
        clang::UnaryOperator const &op) {

    FOUT << "[INFO](buildOpData<UnaryOp>) op: \n"
         << String(context, op) << "; type: "
         << Typename(context, op) << "\n";

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

OpData buildOpData(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::Expr const &castExpr,
        clang::DeclRefExpr const &e,
        clang::Decl const& d) {

    FOUT << "[INFO](buildOpData<Decl>) decl: \n"
         << String(context, e) << "; type: "
         << Typename(context, e) << "\n";

    return {
        cnsHash(context, d),
        String(context, e),
        Typename(context, e),
        TypeCategory(context, e),
        getLinkedParm(context, d, e.getNameInfo()),
        getContainerFunction(context, castExpr),
        castExpr.getExprLoc().printToString(sm),
        //getContainerFunction(context, d) + "_" +
        getLinkedParmQn(context, d, e.getNameInfo())
        //std::to_string(d.getID())
    };
}

OpData buildOpData(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::Expr const &castExpr,
        clang::DeclRefExpr const &e,
        clang::Stmt const& s) {

    FOUT << "[INFO](buildOpData<Stmt>) stmt: \n"
         << String(context, s) << "; type: "
         << Typename(context, e) << "\n";

    return {
        cnsHash(context, s),
        String(context, e),
        Typename(context, e),
        TypeCategory(context, e),
        getLinkedParm(context, s, e.getNameInfo()),
        getContainerFunction(context, castExpr),
        castExpr.getExprLoc().printToString(sm),
        //String(context, e) + "_" +
        getLinkedParmQn(context, s, e.getNameInfo())
    };
}

// TODO: probably remove/rename
OpData buildOpData(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::Expr const &castExpr,
        clang::DeclRefExpr const &e) {

    FOUT << "[INFO](buildOpData<DeclRef2>) ref: \n"
         << String(context, e) << "; type: "
         << Typename(context, e) << "\n";

    auto const *refd = e.getReferencedDeclOfCallee();
    if(refd) {
        CNS_INFO("Building data from ReferencedDecl");
        auto const *vd = dyn_cast<VarDecl>(refd);
        if(vd) {
            CNS_INFO("ReferencedDecl is VarDecl");
            return buildOpData(context, sm, *vd);
        }
    }

    auto const *stmt = e.getExprStmt();
    auto const *decl = e.getDecl();
    if(!!decl) {
        CNS_INFO("Building data from Decl from DeclRefExpr");
        auto const *var = dyn_cast<clang::VarDecl>(decl);
        if(var) {
            CNS_INFO("Found VarDecl from DeclRefExpr");
            return buildOpData(context, sm, *var);
        }
        CNS_WARN("NO VarDecl from DeclRefExpr");
        return buildOpData(context, sm, castExpr, e, *decl);
    }

    if(!!stmt) {
        CNS_INFO("Building data from Expr stmt from DeclRefExpr");
        return buildOpData(context, sm, castExpr, e, *stmt);
    }

    CNS_ERROR("DeclRefExpr has no decl or stmt.");

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

OpData buildOpDataArg(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::Expr const &arg,
        clang::DeclRefExpr const &e) {

    FOUT << "[INFO](buildOpData<DeclRef2>) ref: \n"
         << String(context, e) << "; type: "
         << Typename(context, e) << "\n";

    auto const *refd = e.getReferencedDeclOfCallee();
    if(refd) {
        CNS_INFO("Building data from ReferencedDecl");
        auto const *vd = dyn_cast<VarDecl>(refd);
        if(vd) {
            CNS_INFO("ReferencedDecl is VarDecl");
            return buildOpData(context, sm, *vd);
        }
    }

    auto const *stmt = e.getExprStmt();
    auto const *decl = e.getDecl();
    if(!!decl) {
        CNS_INFO("Building data from Decl from DeclRefExpr");
        auto const *var = dyn_cast<clang::VarDecl>(decl);
        if(var) {
            CNS_INFO("Found VarDecl from DeclRefExpr");
            return buildOpData(context, sm, *var);
        }
        CNS_WARN("NO VarDecl from DeclRefExpr");
        return {
            cnsHash(context, *decl),
            String(context, e),
            Typename(context, e),
            TypeCategory(context, e),
            getLinkedParm(context, *decl, e.getNameInfo()),
            getContainerFunction(context, arg),
            arg.getExprLoc().printToString(sm),
            //String(context, e) + "_" +
            getLinkedParmQn(context, *decl, e.getNameInfo())
        };
    }

    if(!!stmt) {
        CNS_INFO("Building data from Expr stmt from DeclRefExpr");
        //return buildOpData(context, sm, arg, e, *stmt);
        return {
            cnsHash(context, *stmt),
            String(context, e),
            Typename(context, e),
            TypeCategory(context, e),
            getLinkedParm(context, *stmt, e.getNameInfo()),
            getContainerFunction(context, arg),
            arg.getExprLoc().printToString(sm),
            //String(context, e) + "_" +
            getLinkedParmQn(context, *stmt, e.getNameInfo())
        };
    }

    CNS_ERROR("DeclRefExpr has no decl or stmt.");

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

/*
OpData buildOpData(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CastExpr const &castExpr,
        clang::DeclStmt const &e) {

    FOUT << "[INFO](buildOpData<Declstmt>) ds: \n"
         << String(context, e) << "; type: "
         << Typename(context, castExpr) << "\n";

    return {
        cnsHash(context, e),
        String(context, e),
        Typename(context, castExpr),
        TypeCategory(context, castExpr),
        "(Not a param)",
        getContainerFunction(context, castExpr),
        e.getEndLoc().printToString(sm),
    };
}
*/

template<>
OpData buildOpData<CastSourceType::Function>(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CastExpr const &castExpr,
        clang::CallExpr const &e) {

    FOUT << "[INFO](buildOpData<callExpr:fptr>) call: \n"
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
        /*
        e.getDirectCallee()
            ? (std::to_string(e.getDirectCallee()->getID()))
            : (getContainerFunction(context, e) + "." + String(context, e))
        */
    };
}

template<>
OpData buildOpData<CastSourceType::FunctionArg>(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CastExpr const &castExpr,
        clang::CallExpr const &e) {

    FOUT << "[INFO](buildOpData<callExpr:farg>) call: \n"
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
    /*
    for(auto arg = e.arg_begin(); arg != e.arg_end(); argPos++, ++arg) {
        FOUT << "[DEBUG](buildOpData<call>) Argpos: " << argPos << "\n";
        if(clang::Expr::isSameComparisonOperand(*arg, &castExpr))
            break;
    }
    argPos += 1;
    */

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

template<>
OpData buildOpData<CastSourceType::BinaryOp>(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CastExpr const &castExpr,
        clang::DeclRefExpr const &e) {

    CNS_INFO("<BinaryOp, DeclRefExpr>");
    return buildOpData(context, sm, castExpr, e);
}

std::string getLinkedFunction(
        clang::ASTContext const &context,
        clang::CastExpr const &castExpr,
        clang::UnaryOperator const&) {

    CNS_DEBUG("<unaryop>");
    CNS_DEBUG("<unaryop> end.");
    return "N/A";
}

std::string getLinkedFunction(
        clang::ASTContext const &context,
        clang::CastExpr const &castExpr,
        clang::DeclRefExpr const&) {

    CNS_DEBUG("<declrefexpr>");
    CNS_DEBUG("<declrefe> end.");
    return "N/A";
}


std::string getLinkedFunction(
        clang::ASTContext &context,
        clang::CastExpr const &castExpr,
        clang::CallExpr const &call) {

    CNS_DEBUG("<callexpr>");
    auto const *calledFn = getCalleeDecl(call);
    assert(calledFn);
    if(!calledFn) {
        return "";
    }

    CNS_DEBUG("<callexpr> end.");
    return calledFn->getNameAsString();
}
#endif

