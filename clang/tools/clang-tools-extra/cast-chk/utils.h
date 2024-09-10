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

#define CNS_LOG(sev, msg)\
    do{ FOUT << "[" << sev << "](" << __func__ << "(): " << __LINE__ << ") " << " " << msg << "\n"; }while(0)

#define CNS_LOG_LEVEL_DEBUG {}
#define CNS_LOG_LEVEL_INFO {}
#define CNS_LOG_LEVEL_WARN {}
#define CNS_LOG_LEVEL_ERROR {}

#ifdef CNS_LOG_LEVEL_DEBUG
#define CNS_DEBUG(msg) {CNS_LOG("DEBUG", msg);}
#else
#define CNS_DEBUG(msg) {}
#endif

#ifdef CNS_LOG_LEVEL_INFO
#define CNS_INFO(msg) {CNS_LOG("INFO ", msg);}
#else
#define CNS_INFO(msg) {}
#endif

#ifdef CNS_LOG_LEVEL_WARN
#define CNS_WARN(msg) {CNS_LOG("WARN ", msg);}
#else
#define CNS_WARN(msg) {}
#endif

#ifdef CNS_LOG_LEVEL_ERROR
#define CNS_ERROR(msg) {CNS_LOG("ERROR", msg);}
#else
#define CNS_ERROR(msg) {}
#endif


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
    CNS_DEBUG("<VarDecl>");
    auto const *init = fp.getInit();
    if(!init) {
       CNS_ERROR("FP Init expr == nullptr");
       CNS_DEBUG("<VarDecl> end.");
       return nullptr;
    }

    auto const *decl = init->getReferencedDeclOfCallee();
    if(!decl) {
        CNS_ERROR("FP ref'd decl == nullptr");
        CNS_DEBUG("<VarDecl> end.");
        return nullptr;
    }

    auto const *func = dyn_cast<FunctionDecl>(decl);
    if(!func) {
        CNS_ERROR("dyncast<FD> FP ref'd decl == nullptr");
        CNS_DEBUG("<VarDecl> end.");
        return nullptr;
    }
    CNS_DEBUG("<VarDecl> end.");
    return func;
}

clang::FunctionDecl const* getDeclFromFunctionPtr(clang::CallExpr const &call) {
    CNS_DEBUG("<CallExpr>");
    auto const *expr = call.getCallee();
    FunctionProtoType const * d {nullptr};
    if(!expr) {
        CNS_ERROR("Call expr == nullptr");

        auto const *e = call.getDirectCallee();
        if(!e) {
            CNS_ERROR("DirectCallee == nullptr");
            CNS_DEBUG("<CallExpr> end (1).");
            return nullptr;
        }

        d = e->getType()->getAs<FunctionProtoType>();

    }

    if(d) {
        CNS_DEBUG("Well, got function prototype!");
        CNS_DEBUG("<CallExpr> end (2).");
        return nullptr; //getDeclFromFunctionPtr(*d);
    }

    auto const *fpDecl = expr->getReferencedDeclOfCallee();
    if(!fpDecl) {
        CNS_ERROR("FP Decl == nullptr");
        CNS_DEBUG("<CallExpr> end (3).");
        return nullptr;
    }

    auto const *fp = dyn_cast<VarDecl>(fpDecl);
    if(!fp) {
        CNS_ERROR("dyncast<VarDecl> FP Decl == nullptr");
        CNS_DEBUG("<CallExpr> end (4).");
        return nullptr;
    }

    CNS_DEBUG("<CallExpr> end (5: last return i.e. Found FPtr).");
    return getDeclFromFunctionPtr(*fp);
}

clang::FunctionDecl const* getCalleeDecl(CallExpr const &call) {
    CNS_DEBUG("");
    auto const *fn = call.getDirectCallee();
    if(!fn) {
        // If direct callee is null, check if call is actually a callexpr/fnptr.
        // If not, return.
        /*
        // Doesn't work because input is CallExpr even for error case.
        if(dyn_cast<CallExpr const>(&call)) {
        }
        else {
            CNS_ERROR("input call is not CallExpr");
            return nullptr;
        }
        */
        CNS_INFO("call.DirectCallee == nullptr");
        CNS_INFO("Attempting getDeclFromFunctionPtr");
        fn = getDeclFromFunctionPtr(call);
        if(!fn) {
            CNS_ERROR("getDeclFromFunctionPtr == nullptr too");
            CNS_DEBUG("end.");
            return nullptr;
        }
        CNS_INFO("Found getDeclFromFunctionPtr");
    }

    if(fn->isVariadic()) {
        CNS_INFO("Callee is variadic and not supported yet for Census.");
        CNS_DEBUG("<callexpr> end.");
        return nullptr;
    }

    CNS_DEBUG("end.");
    return fn;
}

// Stringer to get source statement from Stmt.
std::string String(ASTContext const &context, Stmt const &stmt) {
    CNS_DEBUG("<Stmt>");
    clang::LangOptions defaultOps;
    std::string oStr;
    llvm::raw_string_ostream stream(oStr);
    //stmt->printPretty(stream, NULL, PrintingPolicy(defaultOps));
    auto policy = context.getLangOpts();
    stmt.printPretty(stream, NULL, policy);
    CNS_DEBUG("<Stmt> end.");
    return oStr;
}

std::string String(ASTContext const &context, DeclStmt const &decl) {
    CNS_DEBUG("<DeclStmt>");
    if (decl.isSingleDecl()) {
        auto const *d = decl.getSingleDecl();
        auto const &nd = static_cast<NamedDecl const*>(d);
        if(!nd)
            return "(Could not find name!)";
        return nd->getNameAsString();
    }
    CNS_DEBUG("<DeclStmt> end.");
    return "(Could not find name!)";
}

std::string String(ASTContext const &context, CallExpr const &call, unsigned parmPos) {
    CNS_DEBUG("<CallExpr, unsigned>");
    auto const *fn = getCalleeDecl(call);
    if(!fn)
        return "(Could not find function name!)";
    assert(fn);

    CNS_DEBUG("<CallExpr, unsigned> end.");
    return String(context, *fn, parmPos);
}

std::string String(ASTContext const &context, FunctionDecl const &fn, unsigned parmPos) {
    CNS_DEBUG("<FunctionDecl, unsigned>");
    std::stringstream ss;
    ss << "{" << fn.getNameAsString() << ".$" << parmPos << ": ";

    auto const *parm = fn.getParamDecl(parmPos);
    if(!parm) {
        ss << "(Cannot getParamDecl())";
        CNS_DEBUG("<FunctionDecl, unsigned> end.");
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
        CNS_DEBUG("<FunctionDecl, unsigned> end.");
        return ss.str();
    }
    assert(parmId);
    ss << parmId->getName().str();

    CNS_DEBUG("<FunctionDecl, unsigned> end.");
    return ss.str();
}

std::string String(ASTContext const &context, NamedDecl const &d) {
    CNS_DEBUG("<NamedDecl>");
    CNS_DEBUG("<NamedDecl> end.");
    return d.getNameAsString();
}

// Get type name of QualType
std::string Typename(ASTContext const &context, QualType qtype) {
    CNS_DEBUG("<QualType>");
    auto policy = context.getLangOpts();
    CNS_DEBUG("<QualType> end.");
    return qtype.getAsString(policy);
}

// Get type name of Expr.
std::string Typename(ASTContext const &context, Expr const &expr) {
    CNS_DEBUG("<Expr>");
    CNS_DEBUG("<Expr> end.");
    return Typename(context, expr.getType());
}

// Get type name of Type
std::string Typename(ASTContext const &context, clang::Type const *type) {
    CNS_DEBUG("<Type>");
    assert(type);

    std::string oStr;
    llvm::raw_string_ostream stream(oStr);

    type->dump(stream, context);
    CNS_DEBUG("<Type> end.");
    return oStr; 
}

// Get type name from VarDecl
std::string Typename(ASTContext const &context, clang::ValueDecl const &d) {
    CNS_DEBUG("<ValueDecl>");
    auto const qtype = d.getType();
    CNS_DEBUG("<ValueDecl> end.");
    return Typename(context, qtype);
}

std::string TypeCategory(QualType const &qtype) {
    CNS_DEBUG("<QualType>");
    auto const *type = qtype.getTypePtr();
    assert(type);
    if(type->isFunctionPointerType()){ // No type class for fptr in clang.
        CNS_INFO("Assinging FunctionPointer TypeCategory not defined in clang::Type::TypeClass.");
        CNS_DEBUG("<QualType> end.");
        return "FunctionPointer";
    }

    CNS_DEBUG("<QualType> end.");
    return type->getTypeClassName();
}

// Get type class of Expr (pointer, array)
std::string TypeCategory(ASTContext const &context, Expr const &expr) {
    CNS_DEBUG("<Expr>");
    auto qtype = expr.getType();
    CNS_DEBUG("<Expr> end.");
    return TypeCategory(qtype);
}

std::string TypeCategory(ASTContext const &context, ValueDecl const &d) {
    CNS_DEBUG("<ValueDecl>");
    auto qtype = d.getType();
    CNS_DEBUG("<ValueDecl> end.");
    return TypeCategory(qtype);
}

// Get containing function decl
template<typename T>
clang::FunctionDecl const* getContainerFunctionDecl(ASTContext &context, T const &node) {
    CNS_DEBUG("<T>");
    auto parents = context.getParents(node);
    if (parents.size() == 0) {
        CNS_INFO("0 Parents found");
        CNS_DEBUG("<T> end.");
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
            CNS_INFO(" Cannot continue loop due to unknown parent type.");
            CNS_DEBUG("<T> end.");
            return nullptr;
        }
    }

    if(parents.size() == 0) return nullptr;
    auto const *fn = parents[0].template get<clang::FunctionDecl>();
    if(!fn) {
        CNS_INFO("Could not find container function");
        CNS_DEBUG("<T> end.");
    }
    CNS_DEBUG("<T> end.");
    return fn;
}

clang::FunctionDecl const* getContainerFunctionDecl(
        clang::DeclContext const *context,
        clang::VarDecl const &var) {

    CNS_DEBUG("<DeclContext, VarDecl>");
    while(context) {
        if(auto const *func = dyn_cast<FunctionDecl>(context)) {
            // Found the parent function
            FOUT << "[INFO](getContainerFunction<DC>) Found parent function: " << func->getNameAsString() << "\n";
    CNS_DEBUG("<DeclContext, VarDecl> end.");
            return func;
        }
        context = context->getParent();
    }

    CNS_INFO("Could not find parent function.");
    CNS_DEBUG("<DeclContext, VarDecl> end.");
    return nullptr;
}

clang::FunctionDecl const* getContainerFunctionDecl(ASTContext &context, VarDecl const &var) {
    CNS_DEBUG("<Context, VarDecl>");
    auto const *c2 = var.getDeclContext();
    CNS_DEBUG("<Context, VarDecl> end.");
    return getContainerFunctionDecl(c2, var);
}

// Get containing function for declaration
template<typename T>
std::string getContainerFunction(ASTContext &context, T const &node) {
    CNS_DEBUG("<T>");
    auto const *fn = getContainerFunctionDecl(context, node);
    if(!fn) {
        return "(Could not find container function)\n";
        CNS_DEBUG("<T> end.");
    }

    CNS_DEBUG("<T> end.");
    return fn->getNameAsString();
}

// Get containing translation unit for declaration
//

clang::Decl const* getParamDecl(ASTContext const &context, CallExpr const &call, unsigned parmPos) {
    CNS_DEBUG("<CallExpr, unsigned>");
    auto const *fn = getCalleeDecl(call);
    if(!fn) {
        CNS_DEBUG("<CallExpr, unsigned> end.");
        return nullptr;
    }
    assert(fn);

    auto const *parm = fn->getParamDecl(parmPos);
    assert(parm);   // not needed
    CNS_DEBUG("<CallExpr, unsigned> end.");
    return parm;
}

std::optional<unsigned> getParameterMatch(clang::FunctionDecl const &fn, clang::DeclarationName const &matchName) {
    CNS_DEBUG("<FunctionDecl, DeclarationName>");
    /*
    //assert(fn);
    //CNS_INFO("declarationname");
    if(!fn) {
        //CNS_INFO("fn == nullptr");
        return std::nullopt;
    }
    //FOUT << "CNS_INFO("fn != nullptr");
    */

    unsigned parmPos = 0;
    auto match = std::find_if(fn.param_begin(), fn.param_end(),
        [&] (auto const &parm) -> bool {
        parmPos++;
        FOUT << "[INFO](getParametermatch) parmPos == " << parmPos << "\n";
        CNS_DEBUG("<FunctionDecl, DeclarationName> end.");
        auto parmName = parm->getDeclName();
        return parmName == matchName;
    });

    if (match == fn.param_end() || parmPos > fn.getNumParams()) {
        CNS_INFO("parmPos is nullopt.");
        CNS_DEBUG("<FunctionDecl, DeclarationName> end.");
        return std::nullopt;
    }
    else {
        FOUT << "[INFO](getParametermatch) matched parmPos: " << parmPos - 1 << "\n";
        CNS_DEBUG("<FunctionDecl, DeclarationName> end.");
        return parmPos - 1;
    }
    CNS_DEBUG("<FunctionDecl, DeclarationName> end.");
}

std::optional<unsigned> getParameterMatch(clang::FunctionDecl const &fn, clang::DeclarationNameInfo const &matchInfo) {
    CNS_DEBUG("<FunctionDecl, DeclarationNameInfo>");
    //CNS_INFO("declarationnameINFO");
    CNS_DEBUG("<FunctionDecl, DeclarationNameInfo> end.");
    return getParameterMatch(fn, matchInfo.getName());
}

template<typename T>
std::string getLinkedParm(
        clang::ASTContext &context,
        T const &node,
        clang::DeclarationName const &name) {

    CNS_DEBUG("<T, DeclarationName>");
    //CNS_INFO("declname");
    auto const *fn = getContainerFunctionDecl(context, node);
    if(!fn) {
        CNS_INFO("fn == nullptr");
        CNS_DEBUG("<T, DeclarationName> end.");
        return "(Not a param)";
    }

    if(auto parmPos = getParameterMatch(*fn, name)) {
        FOUT << "[INFO](getLinkedParm) parmPos: " << parmPos.value() << "; " << String(context, *fn, *parmPos) << "\n";
        CNS_DEBUG("<T, DeclarationName> end.");
        return String(context, *fn, *parmPos);
    }

    CNS_WARN("parmPos nullopt.");
    CNS_DEBUG("<T, DeclarationName> end.");
    return "(Not a param)";
}

template<typename T>
std::string getLinkedParm(
        clang::ASTContext &context,
        T const &node,
        clang::DeclarationNameInfo const &nameInfo) {

    CNS_DEBUG("<T, DeclarationNameInfo>");
    //CNS_INFO("declnameinfo");
    CNS_DEBUG("<T, DeclarationNameInfo> end.");
    return getLinkedParm(context, node, nameInfo.getName());
}

std::string getLinkedParm(
        clang::DeclContext const *context,
        clang::VarDecl const &var) {

    CNS_DEBUG("<DeclContext, VarDecl>");
    auto const *func = getContainerFunctionDecl(context, var);
    if(!func) {
        CNS_ERROR("<DC>No Parent function found.");
        CNS_DEBUG("<DeclContext, VarDecl> end.");
        return "(Not a param)";
    }

    if(auto parmPos = getParameterMatch(*func, var.getDeclName())) {
        FOUT << "[INFO](getLinkedParm<DC>) parmPos: " << parmPos.value() << "; " << String(context->getParentASTContext(), *func, *parmPos) << "\n";
        CNS_DEBUG("<DeclContext, VarDecl> end.");
        return String(context->getParentASTContext(), *func, *parmPos);
    }

    CNS_ERROR("<DC>parmPos nullopt.");
    CNS_DEBUG("<DeclContext, VarDecl> end.");
    return "(Not a param)";
}

std::string getLinkedParm(
        clang::ASTContext &context,
        clang::VarDecl const &var) {

    CNS_DEBUG("<Context, VarDecl>");
    if(var.isLocalVarDecl()) {
        CNS_INFO("VarDecl is local var & not parm");
        CNS_DEBUG("<Context, VarDecl> end.");
        return "(Not a param)";
    }

    CNS_INFO("VarDecl is not local var");
    if(var.isLocalVarDeclOrParm()) {
        CNS_INFO("VarDecl is probably a parm");
        //return getLinkedParm(context, var, var.getDeclName());
        auto const *c2 = var.getDeclContext();
        if(!c2) {
            CNS_INFO("Using iContext.");
            CNS_DEBUG("<Context, VarDecl> end.");
            return getLinkedParm(context, var, var.getDeclName());
        }

        CNS_INFO("Trying with var.getDeclContext");
        CNS_DEBUG("<Context, VarDecl> end.");
        return getLinkedParm(c2, var);
    }

    CNS_DEBUG("<Context, VarDecl> end.");
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

template<typename T>
constexpr bool impl_false = false;

/*
template<typename T>
unsigned cnsHash(ASTContext &context, T const& node) {
    static_assert(impl_false<T>, "cnsHash not defined for this type");
    // For parameter/variables:
    //  - include declname, decl, type, container function, translation unit
    // For unary operand:
    //  - include declname, exprtype, container function, translation unit
}

unsigned cnsHash(ASTContext &context, clang::VarDecl const& var) {
    ODRHash h;
    h.AddFunctionDecl(getContainerFunctionDecl(context, var));
    h.AddDecl(&var);
    h.AddType(var.getType().getTypePtr());
    h.AddIdentifierInfo(var.getIdentifier());
    return h.CalculateHash();
}
*/

// VarDecl isA ValueDecl
unsigned cnsHash(clang::ASTContext &context, clang::ValueDecl const& var) {
    ODRHash h;
    auto const *f = getContainerFunctionDecl(context, var);
    if(f) {
        h.AddFunctionDecl(f);
    }
    h.AddDecl(&var);
    h.AddQualType(var.getType());
    auto const *ii = var.getIdentifier();
    if(ii) {
        h.AddIdentifierInfo(ii);
    }
    return h.CalculateHash();
}
unsigned cnsHash(clang::ASTContext &context, clang::Decl const& decl) {
    ODRHash h;
    auto const *f = getContainerFunctionDecl(context, decl);
    if(f) {
        h.AddFunctionDecl(f);
    }
    h.AddDecl(&decl);
    return h.CalculateHash();
}
unsigned cnsHash(clang::ASTContext &context, clang::Expr const& e) {
    ODRHash h;
    auto const *f = getContainerFunctionDecl(context, e);
    if(f) {
        h.AddFunctionDecl(f);
    }
    h.AddStmt(&e);
    h.AddQualType(e.getType());
    return h.CalculateHash();
}
unsigned cnsHash(clang::ASTContext &context, clang::Stmt const& s) {
    ODRHash h;
    auto const *f = getContainerFunctionDecl(context, s);
    if(f) {
        h.AddFunctionDecl(f);
    }
    h.AddStmt(&s);
    return h.CalculateHash();
}

unsigned cnsHash(clang::ASTContext &context, clang::DeclarationNameInfo const& n) {
    ODRHash h;
    h.AddDeclarationName(n.getName());
    auto const *t = n.getNamedTypeInfo();
    if(t) {
        h.AddQualType(t->getType());
    }
    return h.CalculateHash();
}
//--

