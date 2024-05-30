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
#include "clang/AST/ParentMapContext.h"

#include "clang/Basic/IdentifierTable.h"

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
std::string toString(ASTContext *context, CallExpr const *call, unsigned parmPos);
std::string toString(ASTContext *context, Stmt const *stmt);
// Get type name of Expr.
std::string typeof(ASTContext *context, QualType qtype);
// Get type name of QualType
std::string typeof(ASTContext *context, Expr const *expr);
// Get type name of Type
std::string typeof(ASTContext *context, clang::Type const *type);
// Get type class of Expr (pointer, array)
std::string getTypeCategoryName(ASTContext *context, Expr const *expr);

std::string getContainerFunction(ASTContext *context, clang::Stmt const *stmt);

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

std::string toString(ASTContext *context, CallExpr const *call, unsigned parmPos) {
    assert(context);
    assert(call);
    auto const * fn = call->getDirectCallee();
    if(!fn)
        return "(Could not find function name!)";

    std::stringstream ss;
    ss << fn->getNameAsString() << "{$" << parmPos << ": ";

    /*
    auto const * fnDecl = call->getDirectCallee();
    if(!fnDecl) {
        ss << "(Cannot getDirectCallee())";
        return ss.str();
    }
    assert(fnDecl);
    */
    auto const * parm = fn->getParamDecl(parmPos);
    if(!parm) {
        ss << "(Cannot getParamDecl())";
        return ss.str();
    }
    assert(parm);
    // Get parm type
    auto const parmType = parm->getOriginalType(); 
    ss << typeof(context, parmType) << "} ";

    // Get parm id
    auto const * parmId = parm->getIdentifier();
    if(!parmId) {
        ss << "(Cannot get parameter ID)";
        return ss.str();
    }
    assert(parmId);
    ss << parmId->getName().str();

    return ss.str();
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
std::string getContainerFunction(ASTContext *context, clang::Stmt const *stmt) {
    assert(context);
    assert(stmt);

    auto parents = context->getParents(*stmt);
    if (parents.size() == 0) {
        return "(0 Parents found)";
    }

    /*
    for(auto const& parent: parents) {
        auto const * fn = parent.get<clang::FunctionDecl>();
        if(fn && isa<clang::FunctionDecl>(fn)) {
            return fn->getNameAsString();
        }
    */
    while(parents[0].get<clang::FunctionDecl>() == nullptr) {
        auto const * decl = parents[0].get<clang::Decl>();
        auto const * stmt = parents[0].get<clang::Stmt>();
        if(!decl && stmt) {
            parents = context->getParents(*stmt);
        }
        else if(!stmt && decl) {
            parents = context->getParents(*decl);
        }
        else
            return "(Can't loop forever, gfy)";
    }
    auto const *fn = parents[0].get<clang::FunctionDecl>();
    if(!fn) {
        return "(Could not find container function)";
    }
    
    return fn->getNameAsString();
}

// Get containing translation unit for declaration
