#ifndef UTILS_H
#define UTILS_H

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

#include "llvm/Support/raw_os_ostream.h"
#include "llvm/Support/Debug.h"
#include <string>

using namespace clang::tooling;
using namespace llvm;

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::ento;

#include <cstdio>
FILE * fOUT = nullptr;

/*
#define CNS_LOG(sev, msg)\
    do{ FOUT << "[" << sev << "](" << __func__ << "(): " << __LINE__ << ") " << " " << msg << "\n"; }while(0)
*/

#include <fmt/core.h>
#include <fmt/format.h>

void vlog(const char * severity, const char * func, int line, fmt::string_view fmt, fmt::format_args args) {
    fmt::print(fOUT, "[{}] {}():{}: {}\n" , severity, func, line, fmt::vformat(fmt, args));
}

template <typename S, typename... Args>
void log(const char * severity, const char * func, int line, const S& format, Args&&... args) {
    vlog(severity, func, line, format, fmt::internal::make_args_checked<Args...>(format, args...));
}

/*
template <typename... T>
void log(const char * severity, const char * func, int line, fmt::format_string<T...> fmt, T&&... args) {
    vlog(severity, func, line, fmt, fmt::make_format_args(args...));
}
*/

void logm(const char * severity, const char * func, int line, fmt::string_view msg) {
    fmt::print(fOUT, "[{}] {}():{}: {}\n" , severity, func, line, msg);
}

#define CNS_LOG_LEVEL_DEBUG {}
#define CNS_LOG_LEVEL_INFO {}
#define CNS_LOG_LEVEL_WARN {}
#define CNS_LOG_LEVEL_ERROR {}

#ifdef CNS_LOG_LEVEL_DEBUG
#define CNS_DEBUG(fmt, ...) {log("DEBUG", __FUNCTION__, __LINE__, fmt, __VA_ARGS__);}
#define CNS_DEBUG_MSG(msg) {logm("DEBUG", __FUNCTION__, __LINE__, msg);}
#else
#define CNS_DEBUG(fmt, ...) {}
#define CNS_DEBUG_MSG(msg) {}
#endif

#ifdef CNS_LOG_LEVEL_INFO
#define CNS_INFO(fmt, ...) {log(" INFO", __FUNCTION__, __LINE__, fmt, __VA_ARGS__);}
#define CNS_INFO_MSG(msg) {logm(" INFO", __FUNCTION__, __LINE__, msg);}
#else
#define CNS_INFO(fmt, ...) {}
#define CNS_INFO_MSG(msg) {}
#endif

#ifdef CNS_LOG_LEVEL_WARN
#define CNS_WARN(fmt, ...) {log(" WARN", __FUNCTION__, __LINE__, fmt, __VA_ARGS__);}
#define CNS_WARN_MSG(msg) {logm(" WARN", __FUNCTION__, __LINE__, msg);}
#else
#define CNS_WARN(fmt, ...) {}
#define CNS_WARN_MSG(msg) {}
#endif

#ifdef CNS_LOG_LEVEL_ERROR
#define CNS_ERROR(fmt, ...) {log("ERROR", __FUNCTION__, __LINE__, fmt, __VA_ARGS__);}
#define CNS_ERROR_MSG(msg) {logm("ERROR", __FUNCTION__, __LINE__, msg);}
#else
#define CNS_ERROR(fmt, ...) {}
#define CNS_ERROR_MSG(msg) {}
#endif

#include <chrono>

class LogTime {
    public:
        LogTime(std::string const& subject):
            subject_(subject),
            start_(std::chrono::steady_clock::now()) {}

        ~LogTime() {
            auto const duration = std::chrono::steady_clock::now() - start_;
            fmt::print(fOUT, "[ INFO] :TIME TRACE: {} took {}μs\n",
                    subject_,
                    std::chrono::duration_cast<std::chrono::microseconds>(duration).count());
        }

    private:
        std::string const subject_;
        std::chrono::steady_clock::time_point start_;
};

//#define LOG_FUNCTION_TIME_ENABLE
#ifdef LOG_FUNCTION_TIME_ENABLE
#define LOG_FUNCTION_TIME LogTime const lt___(__FUNCTION__);
#else
#define LOG_FUNCTION_TIME {}
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

std::string getLinkedRecord(clang::QualType const &qt);
std::string getLinkedRecord(clang::Expr const &expr);
std::string getLinkedRecord(clang::ValueDecl const &decl);
std::string getLinkedRecord(clang::Type const &type);

//clang::FunctionDecl const* getContainerFunctionDecl(ASTContext &context, clang::Stmt const &stmt);
//std::string getContainerFunction(ASTContext &context, clang::Stmt const &stmt);

clang::Decl const* getParamDecl(ASTContext const &context, CallExpr const &call, unsigned parmPos);

std::optional<unsigned> getParameterMatch(clang::FunctionDecl const &fn, clang::DeclarationNameInfo const &matchInfo);

//---//
clang::FunctionDecl const* getDeclFromFunctionPtr(clang::VarDecl const &fp) {
    CNS_DEBUG_MSG("<VarDecl>");
    auto const *init = fp.getInit();
    if(!init) {
       CNS_ERROR_MSG("FP Init expr == nullptr");
       CNS_DEBUG_MSG("<VarDecl> end");
       return nullptr;
    }

    auto const *decl = init->getReferencedDeclOfCallee();
    if(!decl) {
        CNS_ERROR_MSG("FP ref'd decl == nullptr");
        CNS_DEBUG_MSG("<VarDecl> end");
        return nullptr;
    }

    auto const *func = dyn_cast<FunctionDecl>(decl);
    if(!func) {
        CNS_ERROR_MSG("dyncast<FD> FP ref'd decl == nullptr");
        CNS_DEBUG_MSG("<VarDecl> end");
        return nullptr;
    }
    CNS_DEBUG_MSG("<VarDecl> end");
    return func;
}

clang::FunctionDecl const* getDeclFromFunctionPtr(clang::CallExpr const &call) {
    CNS_DEBUG_MSG("<CallExpr>");
    auto const *expr = call.getCallee();
    FunctionProtoType const * d {nullptr};
    if(!expr) {
        CNS_ERROR_MSG("Call expr == nullptr");

        auto const *e = call.getDirectCallee();
        if(!e) {
            CNS_ERROR_MSG("DirectCallee == nullptr");
            CNS_DEBUG_MSG("<CallExpr> end 1.");
            return nullptr;
        }

        d = e->getType()->getAs<FunctionProtoType>();

    }

    if(d) {
        CNS_DEBUG_MSG("Well, got function prototype!");
        CNS_DEBUG_MSG("<CallExpr> end 2.");
        return nullptr; //getDeclFromFunctionPtr(*d);
    }

    auto const *fpDecl = expr->getReferencedDeclOfCallee();
    if(!fpDecl) {
        CNS_ERROR_MSG("FP Decl == nullptr");
        CNS_DEBUG_MSG("<CallExpr> end 3.");
        return nullptr;
    }

    auto const *fp = dyn_cast<VarDecl>(fpDecl);
    if(!fp) {
        CNS_ERROR_MSG("dyncast<VarDecl> FP Decl == nullptr");
        CNS_DEBUG_MSG("<CallExpr> end 4.");
        return nullptr;
    }

    CNS_DEBUG_MSG("<CallExpr> end 5: last return i.e. Found FPtr.");
    return getDeclFromFunctionPtr(*fp);
}

clang::FunctionDecl const* getCalleeDecl(CallExpr const &call) {
    CNS_DEBUG_MSG("");
    auto const *fn = call.getDirectCallee();
    if(!fn) {
        // If direct callee is null, check if call is actually a callexpr/fnptr.
        // If not, return.
        /*
        // Doesn't work because input is CallExpr even for error case.
        if(dyn_cast<CallExpr const>(&call)) {
        }
        else {
            CNS_ERROR_MSG("input call is not CallExpr");
            return nullptr;
        }
        */
        CNS_INFO_MSG("call.DirectCallee == nullptr");
        CNS_INFO_MSG("Attempting getDeclFromFunctionPtr");
        fn = getDeclFromFunctionPtr(call);
        if(!fn) {
            CNS_ERROR_MSG("getDeclFromFunctionPtr == nullptr too");
            CNS_DEBUG_MSG("end");
            return nullptr;
        }
        CNS_INFO_MSG("Found getDeclFromFunctionPtr");
    }

    if(fn->isVariadic()) {
        CNS_INFO_MSG("Callee is variadic and not supported yet for Census.");
        CNS_DEBUG_MSG("<callexpr> end");
        return nullptr;
    }

    CNS_DEBUG_MSG("end");
    return fn;
}

// Stringer to get source statement from Stmt.
std::string String(ASTContext const &context, Stmt const &stmt) {
    CNS_DEBUG_MSG("<Stmt>");
    clang::LangOptions defaultOps;
    std::string oStr;
    llvm::raw_string_ostream stream(oStr);
    //stmt->printPretty(stream, NULL, PrintingPolicy(defaultOps));
    auto policy = context.getLangOpts();
    stmt.printPretty(stream, NULL, policy);
    CNS_DEBUG_MSG("<Stmt> end");
    return oStr;
}

std::string String(ASTContext const &context, DeclStmt const &decl) {
    CNS_DEBUG_MSG("<DeclStmt>");
    if (decl.isSingleDecl()) {
        auto const *d = decl.getSingleDecl();
        auto const &nd = static_cast<NamedDecl const*>(d);
        if(!nd)
            return "(Could not find name!)";
        return nd->getNameAsString();
    }
    CNS_DEBUG_MSG("<DeclStmt> end");
    return "(Could not find name!)";
}

std::string String(ASTContext const &context, CallExpr const &call, unsigned parmPos) {
    CNS_DEBUG_MSG("<CallExpr, unsigned>");
    auto const *fn = getCalleeDecl(call);
    if(!fn) {
        CNS_DEBUG_MSG("<CallExpr, unsigned> end");
        //return "(Could not find function name!)";
        return "(@Unk)";
    }
    assert(fn);

    CNS_DEBUG_MSG("<CallExpr, unsigned> end");
    return String(context, *fn, parmPos);
}

std::string parmqn(ASTContext const &context, FunctionDecl const &fn, unsigned parmPos) {
    CNS_DEBUG_MSG("");
    std::string qn;
    qn.reserve(64);
    qn = fn.getNameAsString() + ".$" + std::to_string(parmPos);
    CNS_DEBUG_MSG("end");
    return qn;
}

std::string String(ASTContext const &context, FunctionDecl const &fn, unsigned parmPos) {
    CNS_DEBUG_MSG("<FunctionDecl, unsigned>");
    std::string ret;
    ret.reserve(64);
    ret = fn.getNameAsString() + ".$" + std::to_string(parmPos) + ": ";

    if(fn.getNumParams() == 0) {
        CNS_WARN_MSG("No parameters defined for function.");
        ret.append("()");
        return ret;
    }

    auto const *parm = fn.getParamDecl(parmPos);
    if(!parm) {
        CNS_DEBUG_MSG("Cannot getParamDecl()");
        ret.append("(@Unk)");
        CNS_DEBUG_MSG("<FunctionDecl, unsigned> end");
        return ret;
    }
    assert(parm);

    // Get parm type
    auto const parmType = parm->getOriginalType(); 
    ret.append(Typename(context, parmType));

    // Get parm id
    auto const *parmId = parm->getIdentifier();
    if(!parmId) {
        CNS_DEBUG_MSG("(Cannot get parameter ID)");
        ret.append("(@UID)");
        CNS_DEBUG_MSG("<FunctionDecl, unsigned> end");
        return ret;
    }
    assert(parmId);
    ret.append(parmId->getName().str());

    CNS_DEBUG_MSG("<FunctionDecl, unsigned> end");
    return ret;
}

std::string String(ASTContext const &context, NamedDecl const &d) {
    CNS_DEBUG_MSG("<NamedDecl>");
    CNS_DEBUG_MSG("<NamedDecl> end");
    return d.getNameAsString();
}

std::string String(ASTContext const &context, Decl const &decl) {
    CNS_DEBUG_MSG("<Decl>");
    clang::LangOptions defaultOps;
    std::string oStr;
    llvm::raw_string_ostream stream(oStr);
    auto policy = context.getLangOpts();
    decl.print(stream, policy, 0, true);
    CNS_DEBUG_MSG("<Decl> end");
    return stream.str();
}

std::string getLinkedRecord(clang::QualType const &qt) {
    CNS_DEBUG_MSG("<QualType>");
    CNS_DEBUG_MSG("end <QualType>");
    auto const *type = qt.getTypePtrOrNull();
    if(!type) {
        CNS_DEBUG_MSG("end <QualType>");
        return "";
    }
    return getLinkedRecord(*type);
}

std::string getLinkedRecord(clang::Expr const &expr) {
    CNS_DEBUG_MSG("<Expr>");
    CNS_DEBUG_MSG("end <Expr>");
    return getLinkedRecord(expr.getType());
}

std::string getLinkedRecord(clang::ValueDecl const &decl) {
    CNS_DEBUG_MSG("<ValueDecl>");
    CNS_DEBUG_MSG("end <ValueDecl>");
    return getLinkedRecord(decl.getType());
}

std::string getLinkedRecord(clang::Type const &type) {
    CNS_DEBUG_MSG("<Type>");
    auto const *stype = type.getAsStructureType();
    auto const *utype = type.getAsUnionType();

    RecordDecl const *decl = nullptr;
    if(stype) {
        decl = stype->getDecl();
        CNS_DEBUG_MSG("end <Type>");
        return type.getTypeClassName();
    }
    if(utype) {
        decl = utype->getDecl();
        CNS_DEBUG_MSG("end <Type>");
        return type.getTypeClassName();
    }

    if(!decl) {
        CNS_DEBUG_MSG("end <Type>");
        return "";
        // Check if this type is a memeber type
    }

    auto const *declType = decl->getTypeForDecl();
    if(!declType) {
        CNS_DEBUG_MSG("end <Type>");
        return "";
    }

    CNS_DEBUG_MSG("end <Type>");
    return declType->getTypeClassName();
}

// Get type name of QualType
std::string Typename(ASTContext const &context, QualType qtype) {
    CNS_DEBUG_MSG("<QualType>");
    auto policy = context.getLangOpts();
    CNS_DEBUG_MSG("<QualType> end");
    return qtype.getAsString(policy);
}

// Get type name of Expr.
std::string Typename(ASTContext const &context, Expr const &expr) {
    CNS_DEBUG_MSG("<Expr>");
    CNS_DEBUG_MSG("<Expr> end");
    return Typename(context, expr.getType());
}

// Get type name of Type
std::string Typename(ASTContext const &context, clang::Type const *type) {
    CNS_DEBUG_MSG("<Type>");
    assert(type);

    std::string oStr;
    llvm::raw_string_ostream stream(oStr);

    type->dump(stream, context);
    CNS_DEBUG_MSG("<Type> end");
    return oStr; 
}

// Get type name from VarDecl
std::string Typename(ASTContext const &context, clang::ValueDecl const &d) {
    CNS_DEBUG_MSG("<ValueDecl>");
    auto const qtype = d.getType();
    CNS_DEBUG_MSG("<ValueDecl> end");
    return Typename(context, qtype);
}

std::string TypeCategory(QualType const &qtype) {
    CNS_DEBUG_MSG("<QualType>");
    auto const *type = qtype.getTypePtr();
    assert(type);
    if(type->isFunctionPointerType()){ // No type class for fptr in clang.
        CNS_INFO_MSG("Assinging FunctionPointer TypeCategory not defined in clang::Type::TypeClass.");
        CNS_DEBUG_MSG("<QualType> end");
        return "FunctionPointer";
    }

    CNS_DEBUG_MSG("<QualType> end");
    return type->getTypeClassName();
}

// Get type class of Expr (pointer, array)
std::string TypeCategory(ASTContext const &context, Expr const &expr) {
    CNS_DEBUG_MSG("<Expr>");
    auto qtype = expr.getType();
    CNS_DEBUG_MSG("<Expr> end");
    return TypeCategory(qtype);
}

std::string TypeCategory(ASTContext const &context, ValueDecl const &d) {
    CNS_DEBUG_MSG("<ValueDecl>");
    auto qtype = d.getType();
    CNS_DEBUG_MSG("<ValueDecl> end");
    return TypeCategory(qtype);
}

std::string linkedTypeCategory(QualType const &qtype) {
    CNS_DEBUG_MSG("<QualType>");
    auto *type = qtype.getTypePtrOrNull();
    if(!type) {
        CNS_DEBUG_MSG("<QualType> end");
        return "";
    }

    auto const *stype = type->getAsStructureType();
    auto const *utype = type->getAsUnionType();

    if(stype) {
        CNS_DEBUG_MSG("<QualType> end");
        return "Struct";
    }
    if(utype) {
        CNS_DEBUG_MSG("<QualType> end");
        return "Union";
    }

    CNS_DEBUG_MSG("<QualType> end");
    return "";
}

std::string linkedTypeCategory(Expr const &e) {
    CNS_DEBUG_MSG("<Expr>");
    CNS_DEBUG_MSG("<Expr> end ");
    return linkedTypeCategory(e.getType());
}

std::string linkedTypeCategory(ValueDecl const &decl) {
    CNS_DEBUG_MSG("<ValueDecl>");
    CNS_DEBUG_MSG("<ValueDecl> end");
    return linkedTypeCategory(decl.getType());
}

// TODO Fix:
// for declarations, get the associated definition
// for expressions, get the declrefexpr
// Get containing function decl
template<typename T>
clang::FunctionDecl const* getContainerFunctionDecl(ASTContext &context, T const &node) {
    CNS_DEBUG_MSG("<T>");
    auto parents = context.getParents(node);
    if (parents.size() == 0) {
        CNS_INFO_MSG("0 Parents found");
        CNS_DEBUG_MSG("<T> end");
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
            CNS_INFO_MSG(" Cannot continue loop due to unknown parent type.");
            CNS_DEBUG_MSG("<T> end");
            return nullptr;
        }
    }

    if(parents.size() == 0) return nullptr;
    auto const *fn = parents[0].template get<clang::FunctionDecl>();
    if(!fn) {
        CNS_INFO_MSG("Could not find container function");
        CNS_DEBUG_MSG("<T> end");
    }
    CNS_DEBUG_MSG("<T> end");
    return fn;
}

clang::FunctionDecl const* getContainerFunctionDecl(
        clang::DeclContext const *context,
        clang::VarDecl const &var) {

    CNS_DEBUG_MSG("<DeclContext, VarDecl>");
    while(context) {
        if(auto const *func = dyn_cast<FunctionDecl>(context)) {
            // Found the parent function
            CNS_INFO("<DC> Found parent function: {}", func->getNameAsString());
            CNS_DEBUG_MSG("<DeclContext, VarDecl> end");
            return func;
        }
        context = context->getParent();
    }
    CNS_INFO_MSG("Could not find parent function.");
    CNS_DEBUG_MSG("<DeclContext, VarDecl> end");
    return nullptr;
}

clang::FunctionDecl const* getContainerFunctionDecl(ASTContext &context, VarDecl const &var) {
    CNS_DEBUG_MSG("<Context, VarDecl>");
    auto const *c2 = var.getDeclContext();
    CNS_DEBUG_MSG("<Context, VarDecl> end");
    return getContainerFunctionDecl(c2, var);
}

/*
clang::FunctionDecl const* getContainerFunctionDecl(ASTContext &context, clang::Decl const &decl) {
    CNS_DEBUG_MSG("<Context, Decl>");
    auto const * dc = decl.getParentFunctionOrMethod();
    while(dc && !(dc->isFunctionOrMethod())) { // getParentFunctionOrMethod returns function/method/block
        CNS_DEBUG_MSG("declContext is not a function");
        dc = dc->getParent();
    }
    //auto const *c2 = var.getDeclContext();
    CNS_DEBUG_MSG("<Context, Decl> end");
    return dyn_cast<FunctionDecl>(dc);
    //return getContainerFunctionDecl(c2, var);
}
*/

// Get containing function for declaration
template<typename T>
std::string getContainerFunction(ASTContext &context, T const &node) {
    CNS_DEBUG_MSG("<T>");
    auto const *fn = getContainerFunctionDecl(context, node);
    if(!fn) {
        return "(Could not find container function)\n";
        CNS_DEBUG_MSG("<T> end");
    }

    CNS_DEBUG_MSG("<T> end");
    return fn->getNameAsString();
}

std::string getContainerFucntion(ASTContext &context, clang::Decl const &decl) {
    CNS_DEBUG_MSG("<context, decl>");
    auto const *fn = getContainerFunctionDecl(context, decl);
    if(fn) {
        CNS_DEBUG_MSG("<context, decl> end");
        return fn->getNameAsString();
    }

    CNS_INFO("Container for '{}' is null, maybe global", String(context, decl));
    CNS_DEBUG_MSG("<context, decl> end");
    return "";
}

// Get containing translation unit for declaration
//

clang::Decl const* getParamDecl(ASTContext const &context, CallExpr const &call, unsigned parmPos) {
    CNS_DEBUG_MSG("<CallExpr, unsigned>");
    auto const *fn = getCalleeDecl(call);
    if(!fn) {
        CNS_DEBUG_MSG("<CallExpr, unsigned> end");
        return nullptr;
    }
    assert(fn);

    if(fn->getNumParams() == 0) {
        CNS_WARN_MSG("No parameters defined for function.");
        CNS_DEBUG_MSG("<CallExpr, unsigned> end");
        return nullptr;
    }

    auto const *parm = fn->getParamDecl(parmPos);
    assert(parm);   // not needed
    CNS_DEBUG_MSG("<CallExpr, unsigned> end");
    return parm;
}

std::optional<unsigned> getParameterMatch(clang::FunctionDecl const &fn, clang::DeclarationName const &matchName) {
    CNS_DEBUG_MSG("<FunctionDecl, DeclarationName>");
    /*
    //assert(fn);
    //CNS_INFO_MSG("declarationname");
    if(!fn) {
        //CNS_INFO_MSG("fn == nullptr");
        return std::nullopt;
    }
    //FOUT << "CNS_INFO_MSG("fn != nullptr");
    */

    unsigned parmPos = 0;
    auto match = std::find_if(fn.param_begin(), fn.param_end(),
        [&] (auto const &parm) -> bool {
        parmPos++;
        CNS_DEBUG("parmPos == {}", parmPos);
        auto parmName = parm->getDeclName();
        return parmName == matchName;
    });

    if (match == fn.param_end() || parmPos > fn.getNumParams()) {
        CNS_INFO_MSG("parmPos is nullopt.");
        CNS_DEBUG_MSG("<FunctionDecl, DeclarationName> end");
        return std::nullopt;
    }
    else {
        CNS_INFO("matched parmPos: {}", parmPos - 1);
        CNS_DEBUG_MSG("<FunctionDecl, DeclarationName> end");
        return parmPos - 1;
    }
    CNS_DEBUG_MSG("<FunctionDecl, DeclarationName> end");
}

std::optional<unsigned> getParameterMatch(clang::FunctionDecl const &fn, clang::DeclarationNameInfo const &matchInfo) {
    CNS_DEBUG_MSG("<FunctionDecl, DeclarationNameInfo>");
    //CNS_INFO_MSG("declarationnameINFO");
    CNS_DEBUG_MSG("<FunctionDecl, DeclarationNameInfo> end");
    return getParameterMatch(fn, matchInfo.getName());
}

template<typename T>
std::string getLinkedParm(
        clang::ASTContext &context,
        T const &node,
        clang::DeclarationName const &name) {

    CNS_DEBUG_MSG("<T, DeclarationName>");
    //CNS_INFO_MSG("declname");
    auto const *fn = getContainerFunctionDecl(context, node);
    if(!fn) {
        CNS_INFO_MSG("Container fn == nullptr");
        CNS_DEBUG_MSG("<T, DeclarationName> end");
        return "{n/a}";
    }

    if(auto parmPos = getParameterMatch(*fn, name)) {
        CNS_INFO("parmPos: {}; {}", parmPos.value(), String(context, *fn, *parmPos));
        CNS_DEBUG_MSG("<T, DeclarationName> end");
        //return String(context, *fn, *parmPos);
        return parmqn(context, *fn, *parmPos);
    }

    CNS_WARN_MSG("parmPos nullopt.");
    CNS_DEBUG_MSG("<T, DeclarationName> end");
    return "{local}";
}

template<typename T>
std::string getLinkedParm(
        clang::ASTContext &context,
        T const &node,
        clang::DeclarationNameInfo const &nameInfo) {

    CNS_DEBUG_MSG("<T, DeclarationNameInfo>");
    //CNS_INFO_MSG("declnameinfo");
    CNS_DEBUG_MSG("<T, DeclarationNameInfo> end");
    return getLinkedParm(context, node, nameInfo.getName());
}

/*
std::string qualifiedName(
        clang::ASTContext &context,
        clang::Stmt const &node,
        clang::DeclarationName const &name) {
    CNS_INFO_MSG("");

    // Check if node is function type, if yes just return name as string
    auto const *tty = name.getCXXNameType().getTypePtr();
    if(tty && tty->isFunctionType()) {
    //if(TypeCategory(context, node) == "FunctionProto") {
        CNS_INFO_MSG("Function type.");
        return name.getAsString();
    }

    CNS_INFO_MSG("Not a function type.");

    CNS_INFO_MSG("end");
}
std::string qualifiedName(
        clang::ASTContext &context,
        clang::ValueDecl const &node,
        clang::DeclarationName const &name) {
    CNS_INFO_MSG("");

    if(node.getFunctionType() != nullptr) {
        CNS_INFO_MSG("ValueDecl is a Function type.");
        return name.getAsString();
    }

    CNS_INFO_MSG("ValueDecl is not a Function type. Using template for qn");
    CNS_INFO_MSG("end");
    return qualifiedName<>(context, node, name);
}

*/

template<typename T>
std::string qualifiedName(
        clang::ASTContext &context,
        T const &node,
        clang::DeclarationName const &name) {

    CNS_DEBUG_MSG("<T, DeclarationName>");
    //CNS_INFO_MSG("declname");
    auto const *fn = getContainerFunctionDecl(context, node);
    if(!fn) {
        CNS_INFO_MSG("Container fn == nullptr");
        CNS_DEBUG_MSG("<T, DeclarationName> end");
        return name.getAsString();
    }

    if(auto parmPos = getParameterMatch(*fn, name)) {
        CNS_INFO("parmPos: {}; {}", parmPos.value(), parmqn(context, *fn, *parmPos));
        CNS_DEBUG_MSG("<T, DeclarationName> end");
        return parmqn(context, *fn, *parmPos);
    }

    CNS_WARN_MSG("parmPos nullopt.");

    CNS_DEBUG_MSG("<T, DeclarationName> end");
    return getContainerFunction(context, node) + "." + name.getAsString();
}

template<typename T>
std::string qualifiedName(
        clang::ASTContext &context,
        T const &node,
        clang::DeclarationNameInfo const &nameInfo) {

    CNS_DEBUG_MSG("<T, DeclarationNameInfo>");
    //CNS_INFO_MSG("declnameinfo");
    CNS_DEBUG_MSG("<T, DeclarationNameInfo> end");
    return qualifiedName(context, node, nameInfo.getName());
}

std::string getLinkedParm(
        clang::DeclContext const *context,
        clang::VarDecl const &var) {

    CNS_DEBUG_MSG("<DeclContext, VarDecl>");
    auto const *func = getContainerFunctionDecl(context->getParentASTContext(), var);
    if(!func) {
        CNS_ERROR_MSG("<DC>No Parent function found.");
        CNS_DEBUG_MSG("<DeclContext, VarDecl> end");
        return "{n/a}";
    }

    if(auto parmPos = getParameterMatch(*func, var.getDeclName())) {
        CNS_INFO("parmPos: {}; {}", parmPos.value(), parmqn(context->getParentASTContext(), *func, *parmPos));
        CNS_DEBUG_MSG("<DeclContext, VarDecl> end");
        return parmqn(context->getParentASTContext(), *func, *parmPos);
    }

    CNS_ERROR_MSG("<DC>parmPos nullopt.");
    CNS_DEBUG_MSG("<DeclContext, VarDecl> end");
    return "{local}";
}

std::string qualifiedName(
        clang::DeclContext const *context,
        clang::VarDecl const &var) {

    CNS_DEBUG_MSG("<DeclContext, VarDecl>");
    auto const *func = getContainerFunctionDecl(context->getParentASTContext(), var);
    if(!func) {
        CNS_ERROR_MSG("<DC>No Parent function found.");
        CNS_DEBUG_MSG("<DeclContext, VarDecl> end");
        return "{n/a}";
    }

    if(auto parmPos = getParameterMatch(*func, var.getDeclName())) {
        CNS_INFO("parmPos: {}; {}", parmPos.value(), parmqn(context->getParentASTContext(), *func, *parmPos));
        CNS_DEBUG_MSG("<DeclContext, VarDecl> end");
        return parmqn(context->getParentASTContext(), *func, *parmPos);
    }

    CNS_ERROR_MSG("<DC>parmPos nullopt.");
    CNS_DEBUG_MSG("<DeclContext, VarDecl> end");
    return  getContainerFunction(context->getParentASTContext(), var) + "." + String(context->getParentASTContext(), var);
}

std::string qualifiedName(
        clang::ASTContext &context,
        clang::VarDecl const &var) {
    CNS_DEBUG_MSG("");
    if(var.hasGlobalStorage()) {
        CNS_INFO_MSG("VarDecl is global");
        CNS_DEBUG_MSG("end");
        return "global." + String(context, var);
    }
    if(var.isLocalVarDecl()) {
        CNS_INFO_MSG("VarDecl is local var & not parm");
        CNS_DEBUG_MSG("end");
        return getContainerFunction(context, var) + "." + String(context, var);
    }
    if(var.isLocalVarDeclOrParm()) {
        CNS_INFO_MSG("VarDecl is probably a parm");

        auto const *func = getContainerFunctionDecl(context, var);
        if(!func) {
            CNS_ERROR_MSG("No Parent function found.");
            CNS_DEBUG_MSG("end");
            return qualifiedName(var.getDeclContext(), var);
            //return "{n/a}";
        }

        if(auto parmPos = getParameterMatch(*func, var.getDeclName())) {
            CNS_INFO("parmPos: {}; {}", parmPos.value(), parmqn(context, *func, *parmPos));
            CNS_DEBUG_MSG("end");
            return parmqn(context, *func, *parmPos);
        }

        CNS_ERROR_MSG("parmPos nullopt.");
        CNS_DEBUG_MSG("end");
        return getContainerFunction(context, var) + "." + String(context, var);
    }
    CNS_DEBUG_MSG("end");
    return "(whatisit?)";
}

std::string qualifiedName(
        clang::ASTContext &context,
        clang::DeclRefExpr const& dre) {

    CNS_DEBUG_MSG("<dre>");
    auto const *refd = dre.getReferencedDeclOfCallee();
    if(refd) {
        CNS_DEBUG_MSG("refd");
        auto const *vd = dyn_cast<VarDecl>(refd);
        if(vd) {
            CNS_INFO_MSG("ReferencedDecl is VarDecl");
            return qualifiedName(context, *vd);
        }
    }

    auto const *stmt = dre.getExprStmt();
    auto const *decl = dre.getDecl();
    if(!!decl) {
        CNS_INFO_MSG("DeclRefExpr is a decl.");
        auto const *var = dyn_cast<clang::VarDecl>(decl);
        if(var) {
            CNS_INFO_MSG("Found VarDecl from DeclRefExpr");
            return qualifiedName(context, *var);
        }
        CNS_INFO_MSG("No VarDecl from DeclRefExpr");
        if(decl->getFunctionType() != nullptr) {
            CNS_INFO_MSG("DeclRefExpr.decl is a Function type.");
            CNS_INFO_MSG("Using just the function name is sufficient.");
            return String(context, *decl);
        }
        CNS_WARN_MSG("DeclRefExpr.decl is not a Function type either.");
        return getContainerFunction(context, *decl) + "." + String(context, dre);
    }

    if(!!stmt) {
        CNS_INFO_MSG("DeclRefExpr is a stmt.");
        CNS_INFO_MSG("Building data from Expr stmt from DeclRefExpr");
        return getContainerFunction(context, *stmt) + "." + String(context, dre);
    }

    CNS_ERROR_MSG("DeclRefExpr has no decl or stmt.");
    CNS_DEBUG_MSG("<dre> end");
    return getContainerFunction(context, dre) + "." + String(context, dre);
}

namespace {
    inline clang::Expr const* getSubExpr_(clang::UnaryOperator const& e) {
        return e.getSubExpr();
    }
    inline clang::Expr const* getSubExpr_(clang::MemberExpr const& e){
        return e.getBase(); // Also checkout getMemberDecl
    }
    inline clang::Expr const* getSubExpr_(clang::ArraySubscriptExpr const&e) {
        return e.getBase();
    }
    inline clang::Expr const* getSubExpr_(clang::CastExpr const &e) {
        return e.getSubExpr();
    }

    inline clang::UnaryOperator const* unaryExpr_(clang::Expr const *e) {
        return dyn_cast<clang::UnaryOperator>(e);
    }

    inline clang::MemberExpr const* memberExpr_(clang::Expr const *e) {
        return dyn_cast<clang::MemberExpr>(e);
    }

    inline clang::ArraySubscriptExpr const* arraySubscriptExpr_(clang::Expr const *e) {
        return dyn_cast<clang::ArraySubscriptExpr>(e);
    }

    inline clang::CastExpr const* castExpr_(clang::Expr const *e) {
        return dyn_cast<clang::CastExpr>(e);
    }

    clang::DeclRefExpr const* getDREChild(clang::Expr const *e);

    template<typename T>
    clang::DeclRefExpr const* getDREChild(T const *e) {
        if(!e) {
            return nullptr;
        }
        auto const * sub = getSubExpr_(*e);
        return getDREChild(sub);
    }

    clang::DeclRefExpr const* getDREChild(clang::Expr const *e) {
        if(unaryExpr_(e)) {
            return getDREChild(unaryExpr_(e));
        }
        if(memberExpr_(e)) {
            return getDREChild(memberExpr_(e));
        }
        if(arraySubscriptExpr_(e)) {
            return getDREChild(arraySubscriptExpr_(e));
        }
        if(castExpr_(e)) {
            return getDREChild(castExpr_(e));
        }

        auto const *dre = dyn_cast<clang::DeclRefExpr>(e);
        if(dre) {
            return dre;
        }

        return nullptr;
    }
}

clang::DeclRefExpr const* getSubExprDRE(
        clang::ASTContext &context,
        clang::Expr const &e) {

    auto watch = "<" + String(context, e) + ">";
    CNS_DEBUG("{}: start", watch);

    auto const * uop = dyn_cast<clang::UnaryOperator>(&e);
    auto const * mem = dyn_cast<clang::MemberExpr>(&e);
    auto const * asubs = dyn_cast<clang::ArraySubscriptExpr>(&e);
    auto const * calle = dyn_cast<clang::CallExpr>(&e);
    auto const * caste = dyn_cast<clang::CastExpr>(&e);

    if(uop) {
        return getDREChild(uop);
    }
    if(mem) {
        return getDREChild(mem);
    }
    if(asubs) {
        return getDREChild(asubs);
    }
    if(caste) {
        return getDREChild(caste);
    }
    /*
    if(calle) {
        return getDREChild(calle);
    }
    auto cbegin = e.child_begin();
    auto cend = e.child_begin();
    ConstStmtIterator it = cbegin;
    unsigned pos = 0;
    while(it != cend) {
        CNS_DEBUG("{} pos = {}", watch, pos);
        auto const *dre = dyn_cast<clang::DeclRefExpr>(*it);
        if(dre) {
            CNS_DEBUG("{}: found dre", watch);
            CNS_DEBUG("{}: end", watch);
            return dre;
        }
        CNS_DEBUG("{}: expr = {}", watch, String(context, **it));
        it = (*it)->child_begin();
        cend = (*it)->child_end();
        pos++;
    }
    */

    CNS_DEBUG("{}: no dre found", watch);
    CNS_DEBUG("{}: end", watch);
    return nullptr;
}

std::string qualifiedName(
        clang::ASTContext &context,
        clang::CallExpr const& call,
        clang::Expr const &e) {

    std::string watch = "<" + String(context, call) + "|" + String(context, e) + ">";
    CNS_INFO("qn {}", watch);
    auto const *fn = getContainerFunctionDecl(context, e);
    if(!fn) {
        CNS_INFO("{}: Container fn == nullptr", watch);
        CNS_INFO("{}: end", watch);
        return String(context, e);
    }

    auto const * dre = dyn_cast<clang::DeclRefExpr>(&e);
    if(dre) {
        CNS_INFO("{}: Got DRE", watch);
        CNS_INFO("{}: end", watch);
        return qualifiedName(context, *dre);
    }

    auto const *ce = dyn_cast<clang::CastExpr>(&e);
    if(ce) {
        CNS_INFO("{}: Got castexpr.", watch);
        CNS_INFO("{}: <call, e> Cast subexpr: {}", watch, String(context, *(ce->getSubExpr())));
        auto const *dre2 = dyn_cast<clang::DeclRefExpr>(ce->getSubExpr());
        if(dre) {
            CNS_INFO("{}: Got dre from castexpr.", watch);
            CNS_INFO("{}: end", watch);
            return qualifiedName(context, *dre2);
        }
        else {
            CNS_INFO("{}: No dre from castexpr.", watch);
            auto const * ces = ce->getSubExpr();
            auto const * ed = ces->getReferencedDeclOfCallee();
            if(ed) {
                CNS_INFO("{}: Found callee decl from cast expr.", watch);
                auto const * edv = dyn_cast<VarDecl>(ed);
                auto const * edf = dyn_cast<FunctionDecl>(ed);
                if(edv) {
                    CNS_INFO("{}: Found var decl from callee decl.", watch);
                    return qualifiedName(context, *edv);
                }
                else if(edf) {
                    CNS_INFO("{}: Found function decl from callee decl.", watch);
                    return edf->getNameAsString();
                }
                CNS_INFO("{}: No value decl from callee decl. Stringifying decl", watch);
                return String(context, e);
            }
            CNS_INFO("{}: No callee decl from cast expr.", watch);

            // Check if subexpression has any dre child
            auto const *sdre = getSubExprDRE(context, *ces);
            if(sdre) {
                CNS_INFO("{}: Got dre from subexpr: '{}'.", watch, String(context, *sdre));
                CNS_INFO("{}: end", watch);
                return qualifiedName(context, *sdre);
            }

            // See if expr matches any of the fn parameters
            unsigned pos = 0;
            for(auto const* p: fn->parameters()) {
                if(p->getNameAsString() == String(context, e)) {
                    CNS_INFO("<call, e> Param match found at pos: {}", pos);
                    break;
                }
                pos++;
            }
            if(pos < fn->getNumParams()) {
                return fn->getNameAsString() + ".$" + std::to_string(pos);
            }
            else {
                CNS_INFO("{}: No param matched.", watch);
            }

        }
    }
    else {
        auto const * ed = e.getReferencedDeclOfCallee();
        if(ed) {
            CNS_INFO("{}: Found callee decl from expr.", watch);
            auto const * edv = dyn_cast<VarDecl>(ed);
            auto const * edf = dyn_cast<FunctionDecl>(ed);
            if(edv) {
                CNS_INFO("{}: Found var decl from callee decl.", watch);
                return qualifiedName(context, *edv);
            }
            else if(edf) {
                CNS_INFO("{}: Found function decl from callee decl.", watch);
                return edf->getNameAsString();
            }
            CNS_INFO("{}: No value decl from callee decl. Stringifying decl", watch);
            return String(context, e);
        }
    }

    CNS_INFO("{}: Found container fn but no dre.", watch);
    CNS_INFO("{}: end", watch);
    // Search for a declrefexpr in expr and qualifiedName on declref
    // or just return string
    return fn->getNameAsString() + "." + String(context, e);
}

std::string getLinkedParm(
        clang::ASTContext &context,
        clang::VarDecl const &var) {

    CNS_DEBUG_MSG("<Context, VarDecl>");
    if(var.isLocalVarDecl()) {
        CNS_INFO_MSG("VarDecl is local var & not parm");
        CNS_DEBUG_MSG("<Context, VarDecl> end");
        return "{local}";
    }

    CNS_INFO_MSG("VarDecl is not local var");
    if(var.isLocalVarDeclOrParm()) {
        CNS_INFO_MSG("VarDecl is probably a parm");

        auto const *func = getContainerFunctionDecl(context, var);
        if(!func) {
            CNS_ERROR_MSG("No Parent function found.");

            auto const *dc = var.getDeclContext();
            CNS_DEBUG_MSG("<Context, VarDecl> end");
            return getLinkedParm(dc, var);
            //return getLinkedParm(context, var, var.getDeclName());
            //return "{n/a}";
        }

        if(auto parmPos = getParameterMatch(*func, var.getDeclName())) {
            CNS_INFO("parmPos: {}; {}", parmPos.value(), parmqn(context, *func, *parmPos));
            CNS_DEBUG_MSG("<Context, VarDecl> end");
            return parmqn(context, *func, *parmPos);
        }

        CNS_ERROR_MSG("<DC>parmPos nullopt.");
        CNS_DEBUG_MSG("<Context, VarDecl> end");
        return "{local}";
    }

    CNS_DEBUG_MSG("<Context, VarDecl> end");
    return "{No_Impl_Yet!}";
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

unsigned cnsHash(clang::ASTContext &context, clang::FunctionDecl const& func) {
    ODRHash h;
    h.AddFunctionDecl(&func);
    //h.AddQualType(func.getType());
    //h.AddIdentifierInfo(func.getIdentifier());
    return h.CalculateHash();
}
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

clang::DeclRefExpr const* getFptrFromFptrCall(clang::ASTContext &context, clang::CallExpr const &call) {
    auto const *fptrp = call.IgnoreImplicit();
    if(fptrp) {
        CNS_INFO_MSG("Got fptr from call expr after implicitignore.");
        auto const * dre = dyn_cast<clang::DeclRefExpr>(fptrp);
        if(dre) {
            CNS_INFO_MSG("Got dre from fptr.");
            return dre;
        }
        else {
            CNS_INFO_MSG("No dre from fptr.");
        }
    }
    CNS_INFO_MSG("No fptr. Yet.");
    for(auto child: call.children()) {
        auto const *ce = dyn_cast<clang::CastExpr>(child);
        if(ce) {
            CNS_INFO_MSG("Got castexpr from fptr.");
            auto const *dre = dyn_cast<clang::DeclRefExpr>(ce->getSubExpr());
            if(dre) {
                CNS_INFO_MSG("Got dre from castexpr.");
                return dre;
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
                return dre;
            }
            else {
                CNS_INFO_MSG("No dre from child.");
            }
        }
    }

    return nullptr;
}

std::string qualifiedNameFromFptrCall(clang::ASTContext &context, clang::CallExpr const &call) {
    CNS_DEBUG_MSG("");
    auto const *fptr = getFptrFromFptrCall(context, call);
    if(!fptr) {
        CNS_DEBUG_MSG("No Fptr found. Using callee expr.");
        auto const *fn = call.getCallee();
        if(!fn) {
            CNS_ERROR_MSG("No callee expr found. Stringifying call.");
            return String(context, call);
        }
        CNS_DEBUG_MSG("end");
        return String(context, *fn);
    }
    CNS_DEBUG_MSG("end");
    return qualifiedName(context, *fptr);
}
//--

#endif // UTILS_H
