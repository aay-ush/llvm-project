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
#include "llvm/Support/Debug.h"
//#include "llvm/Support/raw_ostream.h"
#include <string>

using namespace clang::tooling;
using namespace llvm;

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::ento;

// Stringer to get source statement from Stmt.
std::string toString(ASTContext const& context, DeclStmt const& decl);
std::string toString(ASTContext const& context, CallExpr const& call, unsigned parmPos);
std::string toString(ASTContext const& context, Stmt const& stmt);
std::string toString(ASTContext const& context, FunctionDecl const& fn, unsigned parmPos);
std::string toString(ASTContext const& context, VarDecl const& var);
// Get type name of Expr.
std::string typeof(ASTContext const& context, QualType qtype);
// Get type name of QualType
std::string typeof(ASTContext const& context, Expr const& expr);
// Get type name of Type
std::string typeof(ASTContext const& context, clang::Type const *type);
// Get type name from VarDecl
std::string typeof(ASTContext const& context, clang::VarDecl const& var);
// Get type class of Expr (pointer, array)
std::string getTypeCategoryName(ASTContext const& context, Expr const& expr);
std::string getTypeCategoryName(ASTContext const& context, VarDecl const& var);

clang::FunctionDecl const* getContainerFunctionDecl(ASTContext & context, clang::Stmt const& stmt);
std::string getContainerFunction(ASTContext & context, clang::Stmt const& stmt);

// Stringer to get source statement from Stmt.
std::string toString(ASTContext const& context, Stmt const& stmt) {
    clang::LangOptions defaultOps;
    std::string oStr;
    llvm::raw_string_ostream stream(oStr);
    //stmt->printPretty(stream, NULL, PrintingPolicy(defaultOps));
    auto policy = context.getLangOpts();
    stmt.printPretty(stream, NULL, policy);
    return oStr;
}

std::string toString(ASTContext const& context, DeclStmt const& decl) {
    if (decl.isSingleDecl()) {
        auto const * d = decl.getSingleDecl();
        auto const & nd = static_cast<NamedDecl const*>(d);
        if(!nd)
            return "(Could not find name!)";
        return nd->getNameAsString();
    }
    return "(Could not find name!)";
}

clang::Decl const* getParamDecl(ASTContext const& context, CallExpr const& call, unsigned parmPos) {
    auto const * fn = call.getDirectCallee();
    assert(fn);

    auto const * parm = fn->getParamDecl(parmPos);
    assert(parm);   // not needed
    return parm;
    //return parm->getCanonicalDecl();
}

std::string toString(ASTContext const& context, CallExpr const& call, unsigned parmPos) {
    auto const * fn = call.getDirectCallee();
    if(!fn)
        return "(Could not find function name!)";
    assert(fn);

    return toString(context, *fn, parmPos);
    /*
    std::stringstream ss;
    ss << fn->getNameAsString() << "{$" << parmPos << ": ";

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
    */
}

std::string toString(ASTContext const& context, FunctionDecl const& fn, unsigned parmPos) {
    std::stringstream ss;
    ss << "{" << fn.getNameAsString() << ".$" << parmPos << ": ";

    auto const * parm = fn.getParamDecl(parmPos);
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

std::string toString(ASTContext const& context, VarDecl const& var) {
    return var.getNameAsString();
}

// Get type name of Expr.
std::string typeof(ASTContext const& context, Expr const& expr) {
    return typeof(context, expr.getType());
}

// Get type name of QualType
std::string typeof(ASTContext const& context, QualType qtype) {
    auto policy = context.getLangOpts();
    return qtype.getAsString(policy);
}

// Get type name of Type
std::string typeof(ASTContext const& context, clang::Type const *type) {
    assert(type);

    std::string oStr;
    llvm::raw_string_ostream stream(oStr);

    type->dump(stream, context);
    return oStr; 
}

// Get type name from VarDecl
std::string typeof(ASTContext const& context, clang::VarDecl const& var) {
    auto const qtype = var.getType();
    return typeof(context, qtype);
}

std::string getTypeCategoryName(QualType const& qtype) {
    auto const * type = qtype.getTypePtr();
    assert(type);
    return type->getTypeClassName();
}

// Get type class of Expr (pointer, array)
std::string getTypeCategoryName(ASTContext const& context, Expr const& expr) {
    auto qtype = expr.getType();
    return getTypeCategoryName(qtype);
}

std::string getTypeCategoryName(ASTContext const& context, VarDecl const& var) {
    auto qtype = var.getType();
    return getTypeCategoryName(qtype);
}

// Get containing function decl
clang::FunctionDecl const* getContainerFunctionDecl(ASTContext & context, clang::Stmt const& stmt) {
    auto parents = context.getParents(stmt);
    if (parents.size() == 0) {
        LLVM_DEBUG(dbgs() << "((getContainerFunctionDecl) 0 Parents found\n");
        return nullptr;
    }

    while(parents[0].get<clang::FunctionDecl>() == nullptr) {
        auto const * decl = parents[0].get<clang::Decl>();
        auto const * stmt = parents[0].get<clang::Stmt>();
        if(!decl && stmt) {
            parents = context.getParents(*stmt);
        }
        else if(!stmt && decl) {
            parents = context.getParents(*decl);
        }
        else {
            LLVM_DEBUG(dbgs() << "(getContainerFunctionDecl) Cannot continue loop due to unknown parent type\n");
            return nullptr;
        }
    }
    auto const *fn = parents[0].get<clang::FunctionDecl>();
    if(!fn) {
        LLVM_DEBUG(dbgs() << "(getContainerFunctionDecl) Could not find container function\n");
    }
    return fn;
}

// Get containing function for declaration
std::string getContainerFunction(ASTContext & context, clang::Stmt const& stmt) {
    auto const *fn = getContainerFunctionDecl(context, stmt);
    if(!fn) {
        return "(Could not find container function)";
    }
    
    return fn->getNameAsString();
}

// Get containing translation unit for declaration
//

std::ofstream FOUT;

std::optional<unsigned> getParameterMatch(clang::FunctionDecl const* fn, clang::DeclarationNameInfo const& matchInfo) {
    assert(fn);
    //FOUT << "[DEBUG](utils.h::getParameterMatch)" << "matchInfo: " << matchInfo.getAsString() << "\n";
    unsigned parmPos = 0;
    auto match = std::find_if(fn->param_begin(), fn->param_end(),
        [&] (auto const& parm) -> bool {
        parmPos++;
        auto parmName = parm->getDeclName();
        //FOUT << "[DEBUG](utils.h::getParameterMatch)" << "parmPos: " << parmPos << "; parmName: " << parmName.getAsString() << "\n";
        return parmName == matchInfo.getName();
    });
    //FOUT << "[DEBUG](utils.h::getParameterMatch)" << "parmPos: " << parmPos << "\n";
    //FOUT << "[DEBUG](utils.h::getParameterMatch)" << "nbParms: " << fn->getNumParams() << "\n";

    if (match == fn->param_end() || parmPos > fn->getNumParams()) {
        //FOUT << "[DEBUG](utils.h::getParameterMatch)" << "parmPos > nbParams\n";
        return std::nullopt;
    }
    else {
        //FOUT << "[DEBUG](utils.h::getParameterMatch)" << "parmPos: " << parmPos << "\n";
        return parmPos - 1;
    }
}
        
// get non cast node()
//{
// }
