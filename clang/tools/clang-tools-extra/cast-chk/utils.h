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

std::ofstream FOUT;

// Stringer to get source statement from Stmt.
std::string String(ASTContext const &context, Stmt const &stmt);
std::string String(ASTContext const &context, DeclStmt const &decl);
std::string String(ASTContext const &context, CallExpr const &call, unsigned parmPos);
std::string String(ASTContext const &context, FunctionDecl const &fn, unsigned parmPos);
std::string String(ASTContext const &context, NamedDecl const &d);
std::string String(ASTContext const &context, Decl const &decl);

// Get type name of QualType
std::string Typename(ASTContext const &context, QualType qtype);
// Get type name of Expr.
std::string Typename(ASTContext const &context, Expr const &expr);
// Get type name of Type
std::string Typename(ASTContext const &context, clang::Type const *type);
// Get type name from VarDecl
std::string Typename(ASTContext const &context, clang::ValueDecl const &d);
// Get type class of Expr (pointer, array)

std::string TypeCategory(QualType const &qtype);
std::string TypeCategory(ASTContext const &context, Expr const &expr);
std::string TypeCategory(ASTContext const &context, ValueDecl const &d);

//clang::FunctionDecl const* getContainerFunctionDecl(ASTContext &context, clang::Stmt const &stmt);
//std::string getContainerFunction(ASTContext &context, clang::Stmt const &stmt);

clang::Decl const* getParamDecl(ASTContext const &context, CallExpr const &call, unsigned parmPos);

std::optional<unsigned> getParameterMatch(clang::FunctionDecl const &fn, clang::DeclarationNameInfo const &matchInfo);

//---//
clang::FunctionDecl const* getDeclFromFunctionPtr(clang::VarDecl const &fp) {
    auto const *init = fp.getInit();
    if(!init) {
        FOUT << "[ERROR](getDeclFromFunctionPtr) FP Init expr == nullptr\n";
        return nullptr;
    }

    auto const *decl = init->getReferencedDeclOfCallee();
    if(!decl) {
        FOUT << "[ERROR](getDeclFromFunctionPtr) FP ref'd decl == nullptr\n";
        return nullptr;
    }

    auto const *func = dyn_cast<FunctionDecl>(decl);
    if(!func) {
        FOUT << "[ERROR](getDeclFromFunctionPtr) dyncast<FD> FP ref'd decl == nullptr\n";
        return nullptr;
    }
    return func;
}

clang::FunctionDecl const* getDeclFromFunctionPtr(clang::CallExpr const &call) {
    auto const *expr = call.getCallee();
    if(!expr) {
        FOUT << "[ERROR](getDeclFromFunctionPtr) Call expr == nullptr\n";
        return nullptr;
    }

    auto const *fpDecl = expr->getReferencedDeclOfCallee();
    if(!fpDecl) {
        FOUT << "[ERROR](getDeclFromFunctionPtr) FP Decl == nullptr\n";
        return nullptr;
    }

    auto const *fp = dyn_cast<VarDecl>(fpDecl);
    if(!fp) {
        FOUT << "[ERROR](getDeclFromFunctionPtr) dyncast<VarDecl> FP Decl == nullptr\n";
        return nullptr;
    }

    return getDeclFromFunctionPtr(*fp);
}

clang::FunctionDecl const* getCalleeDecl(CallExpr const &call) {
    auto const *fn = call.getDirectCallee();
    if(!fn) {
        FOUT << "[INFO](getFunctionDecl) call.DirectCallee == nullptr\n";
        FOUT << "[INFO](getFunctionDecl) Attempting getDeclFromFunctionPtr\n";
        fn = getDeclFromFunctionPtr(call);
        if(!fn) {
            FOUT << "[ERROR](getFunctionDecl) getDeclFromFunctionPtr == nullptr too\n";
            return nullptr;
        }
        FOUT << "[INFO](getFunctionDecl) Found getDeclFromFunctionPtr\n";
    }
    return fn;
}

// Stringer to get source statement from Stmt.
std::string String(ASTContext const &context, Stmt const &stmt) {
    clang::LangOptions defaultOps;
    std::string oStr;
    llvm::raw_string_ostream stream(oStr);
    //stmt->printPretty(stream, NULL, PrintingPolicy(defaultOps));
    auto policy = context.getLangOpts();
    stmt.printPretty(stream, NULL, policy);
    return oStr;
}

std::string String(ASTContext const &context, DeclStmt const &decl) {
    if (decl.isSingleDecl()) {
        auto const *d = decl.getSingleDecl();
        auto const &nd = static_cast<NamedDecl const*>(d);
        if(!nd)
            return "(Could not find name!)";
        return nd->getNameAsString();
    }
    return "(Could not find name!)";
}

std::string String(ASTContext const &context, CallExpr const &call, unsigned parmPos) {
    auto const *fn = getCalleeDecl(call);
    if(!fn)
        return "(Could not find function name!)";
    assert(fn);

    return String(context, *fn, parmPos);
}

std::string String(ASTContext const &context, FunctionDecl const &fn, unsigned parmPos) {
    std::stringstream ss;
    ss << "{" << fn.getNameAsString() << ".$" << parmPos << ": ";

    auto const *parm = fn.getParamDecl(parmPos);
    if(!parm) {
        ss << "(Cannot getParamDecl())";
        return ss.str();
    }
    assert(parm);

    // Get parm type
    auto const parmType = parm->getOriginalType(); 
    ss << Typename(context, parmType) << "} ";

    // Get parm id
    auto const *parmId = parm->getIdentifier();
    if(!parmId) {
        ss << "(Cannot get parameter ID)";
        return ss.str();
    }
    assert(parmId);
    ss << parmId->getName().str();

    return ss.str();
}

std::string String(ASTContext const &context, NamedDecl const &d) {
    return d.getNameAsString();
}

// Get type name of QualType
std::string Typename(ASTContext const &context, QualType qtype) {
    auto policy = context.getLangOpts();
    return qtype.getAsString(policy);
}

// Get type name of Expr.
std::string Typename(ASTContext const &context, Expr const &expr) {
    return Typename(context, expr.getType());
}

// Get type name of Type
std::string Typename(ASTContext const &context, clang::Type const *type) {
    assert(type);

    std::string oStr;
    llvm::raw_string_ostream stream(oStr);

    type->dump(stream, context);
    return oStr; 
}

// Get type name from VarDecl
std::string Typename(ASTContext const &context, clang::ValueDecl const &d) {
    auto const qtype = d.getType();
    return Typename(context, qtype);
}

std::string TypeCategory(QualType const &qtype) {
    auto const *type = qtype.getTypePtr();
    assert(type);
    return type->getTypeClassName();
}

// Get type class of Expr (pointer, array)
std::string TypeCategory(ASTContext const &context, Expr const &expr) {
    auto qtype = expr.getType();
    return TypeCategory(qtype);
}

std::string TypeCategory(ASTContext const &context, ValueDecl const &d) {
    auto qtype = d.getType();
    return TypeCategory(qtype);
}

// Get containing function decl
template<typename T>
clang::FunctionDecl const* getContainerFunctionDecl(ASTContext &context, T const &node) {
    auto parents = context.getParents(node);
    if (parents.size() == 0) {
        //FOUT << "[INFO](getContainerFunctionDecl) 0 Parents found\n";
        return nullptr;
    }

    while(parents.size() != 0 && parents[0].template get<clang::FunctionDecl>() == nullptr) {
        auto const *decl = parents[0].template get<clang::Decl>();
        auto const *node = parents[0].template get<clang::Stmt>();
        if(!decl && node) {
            parents = context.getParents(*node);
        }
        else if(!node && decl) {
            parents = context.getParents(*decl);
        }
        else {
            //FOUT << "[INFO](getContainerFunctionDecl)  Cannot continue loop due to unknown parent type.\n";
            return nullptr;
        }
    }

    if(parents.size() == 0) return nullptr;
    auto const *fn = parents[0].template get<clang::FunctionDecl>();
    if(!fn) {
        //FOUT << "[INFO](getContainerFunctionDecl) Could not find container function\n";
    }
    return fn;
}

clang::FunctionDecl const* getContainerFunctionDecl(
        clang::DeclContext const *context,
        clang::VarDecl const &var) {

    while(context) {
        if(auto const *func = dyn_cast<FunctionDecl>(context)) {
            // Found the parent function
            FOUT << "[INFO](getContainerFunction<DC>) Found parent function: " << func->getNameAsString() << "\n";
            return func;
        }
        context = context->getParent();
    }

    FOUT << "[INFO](getContainerFunction<DC>) Could not find parent function.\n";
    return nullptr;
}

clang::FunctionDecl const* getContainerFunctionDecl(ASTContext &context, VarDecl const &var) {
    auto const *c2 = var.getDeclContext();
    return getContainerFunctionDecl(c2, var);
}

// Get containing function for declaration
template<typename T>
std::string getContainerFunction(ASTContext &context, T const &node) {
    auto const *fn = getContainerFunctionDecl(context, node);
    if(!fn) {
        return "(Could not find container function)\n";
    }

    return fn->getNameAsString();
}

// Get containing translation unit for declaration
//

clang::Decl const* getParamDecl(ASTContext const &context, CallExpr const &call, unsigned parmPos) {
    auto const *fn = getCalleeDecl(call);
    if(!fn) {
        return nullptr;
    }
    assert(fn);

    auto const *parm = fn->getParamDecl(parmPos);
    assert(parm);   // not needed
    return parm;
}

std::optional<unsigned> getParameterMatch(clang::FunctionDecl const &fn, clang::DeclarationName const &matchName) {
    /*
    //assert(fn);
    //FOUT << "[INFO](getParametermatch) declarationname\n";
    if(!fn) {
        //FOUT << "[INFO](getParametermatch) fn == nullptr\n";
        return std::nullopt;
    }
    //FOUT << "[INFO](getParametermatch) fn != nullptr\n";
    */

    unsigned parmPos = 0;
    auto match = std::find_if(fn.param_begin(), fn.param_end(),
        [&] (auto const &parm) -> bool {
        parmPos++;
        //FOUT << "[INFO](getParametermatch) parmPos == " << parmPos << "\n";
        auto parmName = parm->getDeclName();
        return parmName == matchName;
    });

    if (match == fn.param_end() || parmPos > fn.getNumParams()) {
        //FOUT << "[INFO](getParametermatch) parmPos is nullopt.\n";
        return std::nullopt;
    }
    else {
        //FOUT << "[INFO](getParametermatch) matched parmPos: " << parmPos - 1 << "\n";
        return parmPos - 1;
    }
}

std::optional<unsigned> getParameterMatch(clang::FunctionDecl const &fn, clang::DeclarationNameInfo const &matchInfo) {
    //FOUT << "[INFO](getParametermatch) declarationnameINFO\n";
    return getParameterMatch(fn, matchInfo.getName());
}

template<typename T>
std::string getLinkedParm(
        clang::ASTContext &context,
        T const &node,
        clang::DeclarationName const &name) {

    //FOUT << "[INFO](getLinkedParm) declname\n";
    auto const *fn = getContainerFunctionDecl(context, node);
    if(!fn) {
        FOUT << "[INFO](getLinkedParm) fn == nullptr\n";
        return "(Not a param)";
    }

    if(auto parmPos = getParameterMatch(*fn, name)) {
        //FOUT << "[INFO](getLinkedParm) parmPos: " << parmPos.value() << "; " << String(context, *fn, *parmPos) << "\n";
        return String(context, *fn, *parmPos);
    }

    //FOUT << "[Warn](getLinkedParm) parmPos nullopt.\n";
    return "(Not a param)";
}

template<typename T>
std::string getLinkedParm(
        clang::ASTContext &context,
        T const &node,
        clang::DeclarationNameInfo const &nameInfo) {

    //FOUT << "[INFO](getLinkedParm) declnameinfo\n";
    return getLinkedParm(context, node, nameInfo.getName());
}

std::string getLinkedParm(
        clang::DeclContext const *context,
        clang::VarDecl const &var) {

    auto const *func = getContainerFunctionDecl(context, var);
    if(!func) {
        FOUT << "[ERROR](getLinkedParm<DC>) No Parent function found.\n";
        return "(Not a param)";
    }

    if(auto parmPos = getParameterMatch(*func, var.getDeclName())) {
        FOUT << "[INFO](getLinkedParm<DC>) parmPos: " << parmPos.value() << "; " << String(context->getParentASTContext(), *func, *parmPos) << "\n";
        return String(context->getParentASTContext(), *func, *parmPos);
    }

    FOUT << "[ERROR](getLinkedParm<DC>) parmPos nullopt.\n";
    return "(Not a param)";
}

std::string getLinkedParm(
        clang::ASTContext &context,
        clang::VarDecl const &var) {

    if(var.isLocalVarDecl()) {
        FOUT << "[INFO](getLinkedParm) VarDecl is local var & not parm\n";
        return "(Not a param)";
    }

    FOUT << "[INFO](getLinkedParm) VarDecl is not local var\n";
    if(var.isLocalVarDeclOrParm()) {
        FOUT << "[INFO](getLinkedParm) VarDecl is probably a parm\n";
        //return getLinkedParm(context, var, var.getDeclName());
        auto const *c2 = var.getDeclContext();
        if(!c2) {
            FOUT << "[INFO](getLinkedParm) Using iContext.\n";
            return getLinkedParm(context, var, var.getDeclName());
        }

        FOUT << "[INFO](getLinkedParm) Trying with var.getDeclContext\n";
        return getLinkedParm(c2, var);
    }

    return "(No_Impl_Yet!)";
}

//--
template <typename T, typename Parameter>
class NamedType {
public:
    explicit NamedType(T const& value):
        value_(value) {}
    explicit NamedType(T&& value):
        value_(std::move(value)) {}
    T& get() { return value_; }
    T const& get() const {return value_; }

    // callable (fluentcpp: https://github.com/joboccara/NamedType/blob/master/include/NamedType/underlying_functionalities.hpp)
    constexpr operator T const&() const {
        return value_;
    }
    constexpr operator T&() {
        return value_;
    }

    [[nodiscard]] constexpr std::remove_reference_t<T> const* operator->() const {
        return std::addressof(value_);
    }
    [[nodiscard]] constexpr std::remove_reference_t<T> * operator->()  {
        return std::addressof(value_);
    }


    [[nodiscard]] constexpr bool operator<(T const& other) const
    {
        return this->get() < other.get();
    }
    [[nodiscard]] constexpr bool operator>(T const& other) const
    {
        return other.get() < this->get();
    }
    [[nodiscard]] constexpr bool operator<=(T const& other) const
    {
        return !(other < *this);
    }
    [[nodiscard]] constexpr bool operator>=(T const& other) const
    {
        return !(*this < other);
    }
    [[nodiscard]] constexpr bool operator==(T const& other) const
    {
        return !(*this < other) && !(other < *this);
    }
    [[nodiscard]] constexpr bool operator!=(T const& other) const
    {
        return !(*this == other);
    }

private:
    T value_;
};
//--

