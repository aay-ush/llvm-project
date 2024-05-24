// Declares clang::SyntaxOnlyAction
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

#include <iostream>
#include <fstream>
#include <sstream>

#include "llvm/Support/raw_os_ostream.h"
//#include "llvm/Support/raw_ostream.h"
#include <string>

using namespace clang::tooling;
using namespace llvm;

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::ento;

// Stringer to get source statement from Stmt.
std::string toString(ASTContext *context, DeclStmt const *decl);
std::string toString(ASTContext *context, CallExpr const *call);
std::string toString(ASTContext *context, Stmt const *stmt);
// Get type name of Expr.
std::string typeof(ASTContext *context, QualType qtype);
// Get type name of QualType
std::string typeof(ASTContext *context, Expr const *expr);
// Get type name of Type
std::string typeof(ASTContext *context, clang::Type const *type);
// Get type class of Expr (pointer, array)
std::string getTypeCategoryName(ASTContext *context, Expr const *expr);

// Stringer to get source statement from Stmt.
std::string toString(ASTContext *context, Stmt const *stmt) {
    assert(context);
    if(!stmt) {
        return "Unknown Stmt Type (nullptr)";
    }

    clang::LangOptions defaultOps;
    std::string oStr;
    llvm::raw_string_ostream stream(oStr);
    //stmt->printPretty(stream, NULL, PrintingPolicy(defaultOps));
    auto policy = context->getLangOpts();
    stmt->printPretty(stream, NULL, policy);
    return oStr;
}

std::string toString(ASTContext *context, DeclStmt const *decl) {
    assert(context);
    assert(decl);
    if (decl->isSingleDecl()) {
        auto const * d = decl->getSingleDecl();
        auto const * nd = static_cast<NamedDecl const*>(d);
        if(!nd)
            return "(Could not find name!)";
        return nd->getNameAsString();
    }
    return "(Could not find name!)";
}

std::string toString(ASTContext *context, CallExpr const *call) {
    assert(context);
    assert(call);
    auto const * fn = call->getDirectCallee();
    if(!fn)
        return "(Could not find function name!)";
    return fn->getNameAsString();
}

// Get type name of Expr.
std::string typeof(ASTContext *context, Expr const *expr) {
    assert(context);
    assert(expr);
    auto qtype = expr->getType();
    return typeof(context, qtype);
}

// Get type name of QualType
std::string typeof(ASTContext *context, QualType qtype) {
    assert(context);

    auto policy = context->getLangOpts();
    return qtype.getAsString(policy);
}

// Get type name of Type
std::string typeof(ASTContext *context, clang::Type const *type) {
    assert(context);
    assert(type);

    std::string oStr;
    llvm::raw_string_ostream stream(oStr);

    type->dump(stream, *context);
    return oStr; 
}

// Get type class of Expr (pointer, array)
std::string getTypeCategoryName(ASTContext *context, Expr const *expr) {
    assert(context);
    assert(expr);
    auto qtype = expr->getType();
    auto const * type = qtype.getTypePtr();
    assert(type);
    return type->getTypeClassName();
}

// Get containing function for declaration

// Get containing translation unit for declaration
