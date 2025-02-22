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

namespace cns {
    namespace logging {
        enum severity: unsigned {
            None = 0,
            Debug = 1 << 0,
            Info = 1 << 1,
            Warn = 1 << 2,
            Error = 1 << 3,
        };
    }
}

template<>
struct fmt::formatter<cns::logging::severity>: formatter<string_view> {
    format_context::iterator format(cns::logging::severity s, format_context& ctx) const {
        string_view ret = "None";
        switch(s) {
            case cns::logging::severity::None:
                ret = "None"; break;
            case cns::logging::severity::Debug:
                ret = "DEBUG"; break;
            case cns::logging::severity::Info:
                ret = "INFO"; break;
            case cns::logging::severity::Warn:
                ret = "WARN"; break;
            case cns::logging::severity::Error:
                ret = "ERROR"; break;
        }
        return formatter<string_view>::format(ret, ctx);
    }
};


void vlog(cns::logging::severity severity, char const * func, int line, std::string const& key, fmt::string_view fmt, fmt::format_args args) {
    fmt::print(fOUT, "[{:<5}] {}():{}: {} | {}\n" , severity, func, line, key, fmt::vformat(fmt, args));
}

unsigned SEVERITY_FILTER = 1<<4;

template <typename S, typename... Args>
void log(cns::logging::severity severity, char const * func, int line, std::string const& key, S const & format, Args&&... args) {
    if(SEVERITY_FILTER & severity) {
        vlog(severity, func, line, key, format, fmt::make_args_checked<Args...>(format, args...));
    }
}

/*
template <typename... T>
void log(char const * severity, char const * func, int line, fmt::format_string<T...> fmt, T&&... args) {
    vlog(severity, func, line, fmt, fmt::make_format_args(args...));
}
*/

//void logm(char const * severity, char const * func, int line, std::string const& key, fmt::string_view msg) {
void logm(cns::logging::severity severity, char const * func, int line, std::string const& key, fmt::string_view msg) {
    if(SEVERITY_FILTER & severity) {
        fmt::print(fOUT, "[{:<5}] {}():{}: {} | {}\n" , severity, func, line, key, msg);
    }
}

#define CNS_LOG_LEVEL_DEBUG {}
#define CNS_LOG_LEVEL_INFO {}
#define CNS_LOG_LEVEL_WARN {}
#define CNS_LOG_LEVEL_ERROR {}

#ifdef CNS_LOG_LEVEL_DEBUG
#define CNS_DEBUG(key, fmt, ...) {log(cns::logging::severity::Debug, __FUNCTION__, __LINE__, key, fmt, __VA_ARGS__);}
#define CNS_DEBUG_MSG(key, msg) {logm(cns::logging::severity::Debug, __FUNCTION__, __LINE__, key, msg);}
#else
#define CNS_DEBUG(key, fmt, ...) {}
#define CNS_DEBUG_MSG(key, msg) {}
#endif

#ifdef CNS_LOG_LEVEL_INFO
#define CNS_INFO(key, fmt, ...) {log(cns::logging::severity::Info, __FUNCTION__, __LINE__, key, fmt, __VA_ARGS__);}
#define CNS_INFO_MSG(key, msg) {logm(cns::logging::severity::Info, __FUNCTION__, __LINE__, key, msg);}
#else
#define CNS_INFO(key, fmt, ...) {}
#define CNS_INFO_MSG(key, msg) {}
#endif

#ifdef CNS_LOG_LEVEL_WARN
#define CNS_WARN(key, fmt, ...) {log(cns::logging::severity::Warn, __FUNCTION__, __LINE__, key, fmt, __VA_ARGS__);}
#define CNS_WARN_MSG(key, msg) {logm(cns::logging::severity::Warn, __FUNCTION__, __LINE__, key, msg);}
#else
#define CNS_WARN(key, fmt, ...) {}
#define CNS_WARN_MSG(key, msg) {}
#endif

#ifdef CNS_LOG_LEVEL_ERROR
#define CNS_ERROR(key, fmt, ...) {log(cns::logging::severity::Error, __FUNCTION__, __LINE__, key, fmt, __VA_ARGS__);}
#define CNS_ERROR_MSG(key, msg) {logm(cns::logging::severity::Error, __FUNCTION__, __LINE__, key, msg);}
#else
#define CNS_ERROR(key, fmt, ...) {}
#define CNS_ERROR_MSG(key, msg) {}
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
namespace {
    inline clang::Expr const* getSubExpr_(clang::MemberExpr const& e){
        return e.getBase(); // Also checkout getMemberDecl
    }
    inline clang::Expr const* getSubExpr_(clang::ArraySubscriptExpr const&e) {
        return e.getBase();
    }
    inline clang::Expr const* getSubExpr_(clang::UnaryExprOrTypeTraitExpr const&e) {
        return e.getArgumentExpr();
    }
    template<typename T>
    clang::Expr const* getSubExpr_(T const& expr) {
        return expr.getSubExpr();
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

    inline clang::DeclRefExpr const* declRefExpr_(clang::Expr const *e) {
        return dyn_cast<clang::DeclRefExpr>(e);
    }

    inline clang::ParenExpr const* parenExpr_(clang::Expr const *e) {
        return dyn_cast<clang::ParenExpr>(e);
    }

    // No subexpr
    /*
    inline clang::PredefinedExpr const* predefinedExpr_(clang::Expr const *e) {
        return dyn_cast<clang::PredefinedExpr>(e);
    }
    inline clang::SourceLocExpr const* sourceLocExpr_(clang::Expr const *e) {
        return dyn_cast<clang::SourceLocExpr>(e);
    }
    */
    inline clang::UnaryExprOrTypeTraitExpr const* unaryExprOrTypeTraitExpr_(clang::Expr const *e) {
        return dyn_cast<clang::UnaryExprOrTypeTraitExpr>(e);
    }
    inline clang::VAArgExpr const* vaArgExpr_(clang::Expr const *e) {
        return dyn_cast<clang::VAArgExpr>(e);
    }

    clang::DeclRefExpr const* getDREChild(clang::ASTContext &context, clang::Expr const *e);

    template<typename T>
    clang::DeclRefExpr const* getDREChild(clang::ASTContext &context, T const *e) {
        constexpr auto logKey = "<T>";
        if(!e) {
            CNS_DEBUG_MSG(logKey, "Null expr");
            return nullptr;
        }
        CNS_DEBUG_MSG(logKey, "Working out subexpr");
        auto const * sub = getSubExpr_(*e);
        return getDREChild(context, sub);
    }

    clang::DeclRefExpr const* getDREChild(clang::ASTContext &context, clang::Expr const *e) {
        auto const logKey = String(context, *e);
        if(unaryExpr_(e)) {
            CNS_DEBUG_MSG(logKey, "Getting dre child from unary expr");
            return getDREChild(context, unaryExpr_(e));
        }
        if(memberExpr_(e)) {
            CNS_DEBUG_MSG(logKey, "Getting child from member expr");
            return getDREChild(context, memberExpr_(e));
        }
        if(arraySubscriptExpr_(e)) {
            CNS_DEBUG_MSG(logKey, "Getting child from array subscript expr");
            return getDREChild(context, arraySubscriptExpr_(e));
        }
        if(castExpr_(e)) {
            CNS_DEBUG_MSG(logKey, "Getting child from cast expr");
            return getDREChild(context, castExpr_(e));
        }
        if(parenExpr_(e)) {
            CNS_DEBUG_MSG(logKey, "Getting child from paren expr");
            return getDREChild(context, parenExpr_(e));
        }

        auto const *dre = dyn_cast<clang::DeclRefExpr>(e);
        if(dre) {
            CNS_DEBUG_MSG(logKey, "Found DRE");
            return dre;
        }

        CNS_DEBUG_MSG(logKey, "No DRE found");
        return nullptr;
    }
}

clang::DeclRefExpr const* getSubExprDRE(
        clang::ASTContext &context,
        clang::Expr const &e) {

    auto logKey = String(context, e);
    CNS_DEBUG_MSG(logKey, "begin");

    auto const * uop = unaryExpr_(&e);
    auto const * mem = memberExpr_(&e);
    auto const * asubs = arraySubscriptExpr_(&e);
    auto const * caste = castExpr_(&e);
    auto const * parene = parenExpr_(&e);
    //auto const * calle = callExpr_(&e);
    //auto const * varg = vaArgExpr_(&e);
    //auto const * uort = unaryExprOrTypeTraitExpr_(&e);

    if(uop) {
        CNS_DEBUG_MSG(logKey, "Found subexpr unaryOp");
        CNS_DEBUG_MSG(logKey, "end");
        return getDREChild(context, uop);
    }
    if(mem) {
        CNS_DEBUG_MSG(logKey, "Found subexpr memberExpr");
        CNS_DEBUG_MSG(logKey, "end");
        return getDREChild(context, mem);
    }
    if(asubs) {
        CNS_DEBUG_MSG(logKey, "Found subexpr arraySubscript");
        CNS_DEBUG_MSG(logKey, "end");
        return getDREChild(context, asubs);
    }
    if(caste) {
        CNS_DEBUG_MSG(logKey, "Found subexpr castExpr");
        CNS_DEBUG_MSG(logKey, "end");
        return getDREChild(context, caste);
    }
    if(parene) {
        CNS_DEBUG_MSG(logKey, "Found subexpr parenExpr");
        CNS_DEBUG_MSG(logKey, "end");
        return getDREChild(context, parene);
    }
    /*
    if(calle) {
        return getDREChild(calle);
    }

    if(!e.children().empty()) {
        // TODO
        CNS_WARN_MSG(logKey, "No DRE found; expr has children that may not have been visited");
        CNS_DEBUG_MSG(logKey, "end");
        return nullptr;
    }
    */

    CNS_DEBUG_MSG(logKey, "no dre found");
    CNS_DEBUG_MSG(logKey, "end");
    return nullptr;
}
clang::FunctionDecl const* getDeclFromFunctionPtr(clang::ASTContext const &context, clang::VarDecl const &fp) {
    auto const logKey = String(context, fp);
    CNS_DEBUG_MSG(logKey, "<VarDecl>");
    auto const *init = fp.getInit();
    if(!init) {
       CNS_ERROR_MSG(logKey, "FP Init expr == nullptr");
       CNS_DEBUG_MSG(logKey, "<VarDecl> end");
       return nullptr;
    }

    auto const *decl = init->getReferencedDeclOfCallee();
    if(!decl) {
        CNS_ERROR_MSG(logKey, "FP ref'd decl == nullptr");
        CNS_DEBUG_MSG(logKey, "<VarDecl> end");
        return nullptr;
    }

    auto const *func = dyn_cast<FunctionDecl>(decl);
    if(!func) {
        CNS_ERROR_MSG(logKey, "dyncast<FD> FP ref'd decl == nullptr");
        CNS_DEBUG_MSG(logKey, "<VarDecl> end");
        return nullptr;
    }
    CNS_DEBUG_MSG(logKey, "<VarDecl> end");
    return func;
}
//---//

clang::FunctionDecl const* getDeclFromFunctionPtr(clang::ASTContext const &context, clang::CallExpr const &call) {
    auto const logKey = String(context, call);
    CNS_DEBUG_MSG(logKey, "<CallExpr>");
    auto const *expr = call.getCallee();
    FunctionProtoType const * d {nullptr};
    if(!expr) {
        CNS_ERROR_MSG(logKey, "Call expr == nullptr");

        auto const *e = call.getDirectCallee();
        if(!e) {
            CNS_ERROR_MSG(logKey, "DirectCallee == nullptr");
            CNS_DEBUG_MSG(logKey, "<CallExpr> end 1.");
            return nullptr;
        }

        d = e->getType()->getAs<FunctionProtoType>();

    }

    if(d) {
        CNS_DEBUG_MSG(logKey, "Well, got function prototype!");
        CNS_DEBUG_MSG(logKey, "<CallExpr> end 2.");
        return nullptr; //getDeclFromFunctionPtr(*d);
    }

    auto const *fpDecl = expr->getReferencedDeclOfCallee();
    if(!fpDecl) {
        CNS_ERROR_MSG(logKey, "FP Decl == nullptr");
        CNS_DEBUG_MSG(logKey, "<CallExpr> end 3.");
        return nullptr;
    }

    auto const *fp = dyn_cast<VarDecl>(fpDecl);
    if(!fp) {
        CNS_ERROR_MSG(logKey, "dyncast<VarDecl> FP Decl == nullptr");
        CNS_DEBUG_MSG(logKey, "<CallExpr> end 4.");
        return nullptr;
    }

    CNS_DEBUG_MSG(logKey, "<CallExpr> end 5: last return i.e. Found FPtr.");
    return getDeclFromFunctionPtr(context, *fp);
}

clang::FunctionDecl const* getCalleeDecl(clang::ASTContext const &context, CallExpr const &call) {
    auto const logKey = String(context, call);
    CNS_DEBUG_MSG(logKey, "begin");
    auto const *fn = call.getDirectCallee();
    if(!fn) {
        // If direct callee is null, check if call is actually a callexpr/fnptr.
        // If not, return.
        /*
        // Doesn't work because input is CallExpr even for error case.
        if(dyn_cast<CallExpr const>(&call)) {
        }
        else {
            CNS_ERROR_MSG(logKey, "input call is not CallExpr");
            return nullptr;
        }
        */
        CNS_INFO_MSG(logKey, "call.DirectCallee == nullptr");
        CNS_INFO_MSG(logKey, "Attempting getDeclFromFunctionPtr");
        fn = getDeclFromFunctionPtr(context, call);
        if(!fn) {
            CNS_ERROR_MSG(logKey, "getDeclFromFunctionPtr == nullptr too");
            CNS_DEBUG_MSG(logKey, "end");
            return nullptr;
        }
        CNS_INFO_MSG(logKey, "Found getDeclFromFunctionPtr");
    }

    if(fn->isVariadic()) {
        CNS_INFO_MSG(logKey, "Callee is variadic and not supported yet for Census.");
        CNS_DEBUG_MSG(logKey, "<callexpr> end");
        return nullptr;
    }

    CNS_DEBUG_MSG(logKey, "end");
    return fn;
}

// Stringer to get source statement from Stmt.
std::string String(ASTContext const &context, Stmt const &stmt) {
    constexpr auto logKey = "<Stmt>";
    CNS_DEBUG_MSG(logKey, "begin");
    clang::LangOptions defaultOps;
    std::string oStr;
    llvm::raw_string_ostream stream(oStr);
    //stmt->printPretty(stream, NULL, PrintingPolicy(defaultOps));
    auto policy = context.getLangOpts();
    stmt.printPretty(stream, NULL, policy);
    CNS_DEBUG_MSG(logKey, "end");
    return oStr;
}

std::string String(ASTContext const &context, DeclStmt const &decl) {
    constexpr auto logKey = "<DeclStmt>";
    CNS_DEBUG_MSG(logKey, "begin");
    if (decl.isSingleDecl()) {
        auto const *d = decl.getSingleDecl();
        auto const &nd = static_cast<NamedDecl const*>(d);
        if(nd) {
            CNS_DEBUG_MSG(logKey, "end");
            return nd->getNameAsString();
        }
    }
    CNS_DEBUG_MSG(logKey, "end");
    return "(Could not find name!)";
}

std::string String(ASTContext const &context, CallExpr const &call, unsigned parmPos) {
    auto const logKey = "<CallExpr>.$" + std::to_string(parmPos);
    CNS_DEBUG_MSG(logKey, "begin");
    auto const *fn = getCalleeDecl(context, call);
    if(!fn) {
        CNS_DEBUG_MSG(logKey, "end");
        //return "(Could not find function name!)";
        return "(@Unk)";
    }
    assert(fn);

    CNS_DEBUG_MSG(logKey, "end");
    return String(context, *fn, parmPos);
}

std::string parmqn(ASTContext const &context, FunctionDecl const &fn, unsigned parmPos) {
    auto const logKey = String(context, fn) + ".$" + std::to_string(parmPos);
    CNS_DEBUG_MSG(logKey, "");
    std::string qn;
    qn.reserve(64);
    qn = fn.getNameAsString() + ".$" + std::to_string(parmPos);
    CNS_DEBUG_MSG(logKey, "end");
    return qn;
}

std::string String(ASTContext const &context, FunctionDecl const &fn, unsigned parmPos) {
    auto const logKey = "<FunctionDecl, unsigned>";
    CNS_DEBUG_MSG(logKey, "begin");
    std::string ret;
    ret.reserve(64);
    ret = fn.getNameAsString() + ".$" + std::to_string(parmPos) + ": ";

    if(fn.getNumParams() == 0) {
        CNS_WARN_MSG(logKey, "No parameters defined for function.");
        ret.append("()");
        return ret;
    }

    auto const *parm = fn.getParamDecl(parmPos);
    if(!parm) {
        CNS_DEBUG_MSG(logKey, "Cannot get ParamDecl");
        ret.append("(@Unk)");
        CNS_DEBUG_MSG(logKey, "end");
        return ret;
    }
    assert(parm);

    // Get parm type
    auto const parmType = parm->getOriginalType(); 
    ret.append(Typename(context, parmType));

    // Get parm id
    auto const *parmId = parm->getIdentifier();
    if(!parmId) {
        CNS_DEBUG_MSG(logKey, "Cannot get parameter ID");
        ret.append("(@UID)");
        CNS_DEBUG_MSG(logKey, "<FunctionDecl, unsigned> end");
        return ret;
    }
    assert(parmId);
    ret.append(parmId->getName().str());

    CNS_DEBUG_MSG(logKey, "<FunctionDecl, unsigned> end");
    return ret;
}

std::string String(ASTContext const &context, NamedDecl const &d) {
    return d.getNameAsString();
}

std::string String(ASTContext const &context, Decl const &decl) {
    constexpr auto logKey = "<Decl>";
    CNS_DEBUG_MSG(logKey, "begin");
    clang::LangOptions defaultOps;
    std::string oStr;
    llvm::raw_string_ostream stream(oStr);
    auto policy = context.getLangOpts();
    decl.print(stream, policy, 0, true);
    CNS_DEBUG_MSG(logKey, "end");
    return stream.str();
}

std::string getLinkedRecord(clang::QualType const &qt) {
    constexpr auto logKey = "<QualType>";
    CNS_DEBUG_MSG(logKey, "begin");
    auto const *type = qt.getTypePtrOrNull();
    if(!type) {
        CNS_DEBUG_MSG(logKey, "end");
        return "";
    }
    CNS_DEBUG_MSG(logKey, "end");
    return getLinkedRecord(*type);
}

std::string getLinkedRecord(clang::Expr const &expr) {
    constexpr auto logKey = "<Expr>";
    CNS_DEBUG_MSG(logKey, "begin");
    CNS_DEBUG_MSG(logKey, "end");
    return getLinkedRecord(expr.getType());
}

std::string getLinkedRecord(clang::ValueDecl const &decl) {
    constexpr auto logKey = "<ValueDecl>";
    CNS_DEBUG_MSG(logKey, "begin");
    CNS_DEBUG_MSG(logKey, "end");
    return getLinkedRecord(decl.getType());
}

std::string getLinkedRecord(clang::Type const &type) {
    constexpr auto logKey = "<Type>";
    CNS_DEBUG_MSG(logKey, "begin");
    auto const *stype = type.getAsStructureType();
    auto const *utype = type.getAsUnionType();

    RecordDecl const *decl = nullptr;
    if(stype) {
        decl = stype->getDecl();
        CNS_DEBUG_MSG(logKey, "end");
        return type.getTypeClassName();
    }
    if(utype) {
        decl = utype->getDecl();
        CNS_DEBUG_MSG(logKey, "end");
        return type.getTypeClassName();
    }

    if(!decl) {
        CNS_DEBUG_MSG(logKey, "end");
        return "";
        // Check if this type is a memeber type
    }

    auto const *declType = decl->getTypeForDecl();
    if(!declType) {
        CNS_DEBUG_MSG(logKey, "end");
        return "";
    }

    CNS_DEBUG_MSG(logKey, "end");
    return declType->getTypeClassName();
}

// Get type name of QualType
std::string Typename(ASTContext const &context, QualType qtype) {
    constexpr auto logKey = "<QualType>";
    CNS_DEBUG_MSG(logKey, "begin");
    auto policy = context.getLangOpts();
    CNS_DEBUG_MSG(logKey, "end");
    return qtype.getAsString(policy);
}

// Get type name of Expr.
std::string Typename(ASTContext const &context, Expr const &expr) {
    constexpr auto logKey = "<Expr>";
    CNS_DEBUG_MSG(logKey, "begin");
    CNS_DEBUG_MSG(logKey, "end");
    return Typename(context, expr.getType());
}

// Get type name of Type
std::string Typename(ASTContext const &context, clang::Type const *type) {
    constexpr auto logKey = "<Type>";
    CNS_DEBUG_MSG(logKey, "begin");
    assert(type);

    std::string oStr;
    llvm::raw_string_ostream stream(oStr);

    type->dump(stream, context);
    CNS_DEBUG_MSG(logKey, "end");
    return oStr; 
}

// Get type name from VarDecl
std::string Typename(ASTContext const &context, clang::ValueDecl const &d) {
    constexpr auto logKey = "<ValueDecl>";
    CNS_DEBUG_MSG(logKey, "begin");
    auto const qtype = d.getType();
    CNS_DEBUG_MSG(logKey, "end");
    return Typename(context, qtype);
}

std::string TypeCategory(QualType const &qtype) {
    constexpr auto logKey = "<QualType>";
    CNS_DEBUG_MSG(logKey, "begin");
    auto const *type = qtype.getTypePtr();
    assert(type);
    if(type->isFunctionPointerType()){ // No type class for fptr in clang.
        CNS_INFO_MSG(logKey, "Assinging FunctionPointer TypeCategory not defined in clang::Type::TypeClass.");
        CNS_DEBUG_MSG(logKey, "end");
        return "FunctionPointer";
    }

    CNS_DEBUG_MSG(logKey, "end");
    return type->getTypeClassName();
}

// Get type class of Expr (pointer, array)
std::string TypeCategory(ASTContext const &context, Expr const &expr) {
    constexpr auto logKey = "<Expr>";
    CNS_DEBUG_MSG(logKey, "begin");
    auto qtype = expr.getType();
    CNS_DEBUG_MSG(logKey, "end");
    return TypeCategory(qtype);
}

std::string TypeCategory(ASTContext const &context, ValueDecl const &d) {
    constexpr auto logKey = "<ValueDecl>";
    CNS_DEBUG_MSG(logKey, "begin");
    auto qtype = d.getType();
    CNS_DEBUG_MSG(logKey, "end");
    return TypeCategory(qtype);
}

std::string linkedTypeCategory(QualType const &qtype) {
    constexpr auto logKey = "<QualType>";
    CNS_DEBUG_MSG(logKey, "begin");
    auto *type = qtype.getTypePtrOrNull();
    if(!type) {
        CNS_DEBUG_MSG(logKey, "end");
        return "";
    }

    auto const *stype = type->getAsStructureType();
    auto const *utype = type->getAsUnionType();

    if(stype) {
        CNS_DEBUG_MSG(logKey, "end");
        return "Struct";
    }
    if(utype) {
        CNS_DEBUG_MSG(logKey, "end");
        return "Union";
    }

    CNS_DEBUG_MSG(logKey, "end");
    return "";
}

std::string linkedTypeCategory(Expr const &e) {
    constexpr auto logKey = "<Expr>";
    CNS_DEBUG_MSG(logKey, "begin");
    CNS_DEBUG_MSG(logKey, "end ");
    return linkedTypeCategory(e.getType());
}

std::string linkedTypeCategory(ValueDecl const &decl) {
    constexpr auto logKey = "<ValueDecl>";
    CNS_DEBUG_MSG(logKey, "begin");
    CNS_DEBUG_MSG(logKey, "end");
    return linkedTypeCategory(decl.getType());
}

// TODO Fix:
// for declarations, get the associated definition
// for expressions, get the declrefexpr
// Get containing function decl
template<typename T>
clang::FunctionDecl const* getContainerFunctionDecl(ASTContext &context, T const &node) {
    auto const logKey = String(context, node);
    CNS_DEBUG_MSG(logKey, "<T>");
    auto parents = context.getParents(node);
    if (parents.size() == 0) {
        CNS_INFO_MSG(logKey, "0 Parents found");
        CNS_DEBUG_MSG(logKey, "<T> end");
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
            CNS_INFO_MSG(logKey, "Cannot continue loop due to unknown parent type.");
            CNS_DEBUG_MSG(logKey, "<T> end");
            return nullptr;
        }
    }

    if(parents.size() == 0) return nullptr;
    auto const *fn = parents[0].template get<clang::FunctionDecl>();
    if(!fn) {
        CNS_INFO_MSG(logKey, "Could not find container function");
        CNS_DEBUG_MSG(logKey, "<T> end");
    }
    CNS_DEBUG_MSG(logKey, "<T> end");
    return fn;
}

clang::FunctionDecl const* getContainerFunctionDecl(
        clang::DeclContext const *context,
        clang::VarDecl const &var) {

    auto const logKey = String(context->getParentASTContext(), var);
    CNS_DEBUG_MSG(logKey, "<DeclContext, VarDecl>");
    while(context) {
        if(auto const *func = dyn_cast<FunctionDecl>(context)) {
            // Found the parent function
            CNS_INFO(logKey, "<DC> Found parent function: {}", func->getNameAsString());
            CNS_DEBUG_MSG(logKey, "<DeclContext, VarDecl> end");
            return func;
        }
        context = context->getParent();
    }
    CNS_INFO_MSG(logKey, "Could not find parent function.");
    CNS_DEBUG_MSG(logKey, "<DeclContext, VarDecl> end");
    return nullptr;
}

clang::FunctionDecl const* getContainerFunctionDecl(ASTContext &context, VarDecl const &var) {
    auto const logKey = String(context, var);
    CNS_DEBUG_MSG(logKey, "<Context, VarDecl>");
    auto const *c2 = var.getDeclContext();
    CNS_DEBUG_MSG(logKey, "<Context, VarDecl> end");
    return getContainerFunctionDecl(c2, var);
}

/*
clang::FunctionDecl const* getContainerFunctionDecl(ASTContext &context, clang::Decl const &decl) {
    CNS_DEBUG_MSG(logKey, "<Context, Decl>");
    auto const * dc = decl.getParentFunctionOrMethod();
    while(dc && !(dc->isFunctionOrMethod())) { // getParentFunctionOrMethod returns function/method/block
        CNS_DEBUG_MSG(logKey, "declContext is not a function");
        dc = dc->getParent();
    }
    //auto const *c2 = var.getDeclContext();
    CNS_DEBUG_MSG(logKey, "<Context, Decl> end");
    return dyn_cast<FunctionDecl>(dc);
    //return getContainerFunctionDecl(c2, var);
}
*/

// Get containing function for declaration
template<typename T>
std::string getContainerFunction(ASTContext &context, T const &node) {
    auto const logKey = String(context, node);
    CNS_DEBUG_MSG(logKey, "<T>");
    auto const *fn = getContainerFunctionDecl(context, node);
    if(!fn) {
        return "(Could not find container function)\n";
        CNS_DEBUG_MSG(logKey, "<T> end");
    }

    CNS_DEBUG_MSG(logKey, "<T> end");
    return fn->getNameAsString();
}

std::string getContainerFucntion(ASTContext &context, clang::Decl const &decl) {
    auto const logKey = String(context, decl);
    CNS_DEBUG_MSG(logKey, "begin");
    auto const *fn = getContainerFunctionDecl(context, decl);
    if(fn) {
        CNS_DEBUG_MSG(logKey, "end");
        return fn->getNameAsString();
    }

    CNS_INFO(logKey, "Container for '{}' is null, maybe global", String(context, decl));
    CNS_DEBUG_MSG(logKey, "end");
    return "";
}

// Get containing translation unit for declaration
//

clang::Decl const* getParamDecl(ASTContext const &context, CallExpr const &call, unsigned parmPos) {
    auto const logKey = String(context, call) + ".$" + std::to_string(parmPos);
    CNS_DEBUG_MSG(logKey, "begin");
    auto const *fn = getCalleeDecl(context, call);
    if(!fn) {
        CNS_DEBUG_MSG(logKey, "end");
        return nullptr;
    }
    assert(fn);

    if(fn->getNumParams() == 0) {
        CNS_WARN_MSG(logKey, "No parameters defined for function.");
        CNS_DEBUG_MSG(logKey, "end");
        return nullptr;
    }

    auto const *parm = fn->getParamDecl(parmPos);
    assert(parm);   // not needed
    CNS_DEBUG_MSG(logKey, "end");
    return parm;
}

std::optional<unsigned> getParameterMatch(clang::FunctionDecl const &fn, clang::DeclarationName const &matchName) {
    auto const logKey = fn.getNameAsString();
    CNS_DEBUG_MSG(logKey, "begin");

    unsigned parmPos = 0;
    auto match = std::find_if(fn.param_begin(), fn.param_end(),
        [&] (auto const &parm) -> bool {
        parmPos++;
        CNS_DEBUG(logKey, "parmPos == {}", parmPos);
        auto parmName = parm->getDeclName();
        return parmName == matchName;
    });

    if (match == fn.param_end() || parmPos > fn.getNumParams()) {
        CNS_INFO_MSG(logKey, "parmPos is nullopt.");
        CNS_DEBUG_MSG(logKey, "end");
        return std::nullopt;
    }
    else {
        CNS_INFO(logKey, "matched parmPos: {}", parmPos - 1);
        CNS_DEBUG_MSG(logKey, "end");
        return parmPos - 1;
    }
    CNS_DEBUG_MSG(logKey, "end");
}

std::optional<unsigned> getParameterMatch(clang::FunctionDecl const &fn, clang::DeclarationNameInfo const &matchInfo) {
    auto const logKey = fn.getNameAsString();
    CNS_DEBUG_MSG(logKey, "<FunctionDecl, DeclarationNameInfo> begin");
    CNS_DEBUG_MSG(logKey, "<FunctionDecl, DeclarationNameInfo> end");
    return getParameterMatch(fn, matchInfo.getName());
}

template<typename T>
std::string getLinkedParm(
        clang::ASTContext &context,
        T const &node,
        clang::DeclarationName const &name) {

    auto const logKey = String(context, node) + "(" + name.getAsString() + ")";
    CNS_DEBUG_MSG(logKey, "begin");
    //CNS_INFO_MSG(logKey, "declname");
    auto const *fn = getContainerFunctionDecl(context, node);
    if(!fn) {
        CNS_INFO_MSG(logKey, "Container fn == nullptr");
        CNS_DEBUG_MSG(logKey, "end");
        return "{n/a}";
    }

    if(auto parmPos = getParameterMatch(*fn, name)) {
        CNS_INFO(logKey, "parmPos: {}; {}", parmPos.value(), String(context, *fn, *parmPos));
        CNS_DEBUG_MSG(logKey, "end");
        //return String(context, *fn, *parmPos);
        return parmqn(context, *fn, *parmPos);
    }

    CNS_WARN_MSG(logKey, "parmPos nullopt.");
    CNS_DEBUG_MSG(logKey, "end");
    return "{local}";
}

template<typename T>
std::string getLinkedParm(
        clang::ASTContext &context,
        T const &node,
        clang::DeclarationNameInfo const &nameInfo) {

    auto const logKey = String(context, node) + "(" + nameInfo.getAsString() + ")";
    CNS_DEBUG_MSG(logKey, "<T, DeclarationNameInfo> begin");
    //CNS_INFO_MSG(logKey, "declnameinfo");
    CNS_DEBUG_MSG(logKey, "<T, DeclarationNameInfo> end");
    return getLinkedParm(context, node, nameInfo.getName());
}

std::string getLinkedParm(
        clang::ASTContext &context,
        clang::UnaryOperator const &e) {

    auto const logKey = String(context, e);
    CNS_DEBUG_MSG(logKey, "begin");
    auto const * dre = getDREChild(context, &e);
    if(dre) {
        CNS_DEBUG(logKey, "Found DREchild '{}'", String(context, *dre));
        CNS_DEBUG_MSG(logKey, "end");
        return getLinkedParm(context, e, dre->getNameInfo());
    }

    // Highly unlikely
    CNS_DEBUG_MSG(logKey, "No DRE in unary op => no linked parm");
    CNS_DEBUG_MSG(logKey, "end");
    return "{nolp}";
}

std::string qualifiedName(clang::ASTContext &context, clang::DeclRefExpr const& dre);
std::string qualifiedName(clang::ASTContext &context, clang::VarDecl const &var);

std::string qualifiedName(
        clang::ASTContext &context,
        clang::ValueDecl const &node,
        clang::DeclarationName const &name) {

    auto const logKey = String(context, node) + "(" + name.getAsString() + ")";
    CNS_DEBUG_MSG(logKey, "begin");
    auto const *fn = getContainerFunctionDecl(context, node);
    if(!fn) {
        CNS_INFO_MSG(logKey, "Container fn == nullptr");
        CNS_DEBUG_MSG(logKey, "end");
        return name.getAsString();
    }

    if(auto parmPos = getParameterMatch(*fn, name)) {
        CNS_INFO(logKey, "parmPos: {}; {}", parmPos.value(), parmqn(context, *fn, *parmPos));
        CNS_DEBUG_MSG(logKey, "end");
        return parmqn(context, *fn, *parmPos);
    }

    CNS_WARN_MSG(logKey, "parmPos nullopt.");

    CNS_DEBUG_MSG(logKey, "end");
    return getContainerFunction(context, node) + "." + name.getAsString();
}

std::string qualifiedName(
        clang::ASTContext &context,
        clang::ValueDecl const &node,
        clang::DeclarationNameInfo const &nameInfo) {

    auto const logKey = String(context, node) + "(" + nameInfo.getAsString() + ")";
    CNS_DEBUG_MSG(logKey, "<T, DeclarationNameInfo> begin");
    CNS_DEBUG_MSG(logKey, "<T, DeclarationNameInfo> end");
    return qualifiedName(context, node, nameInfo.getName());
}

std::string qualifiedName(clang::ASTContext &context,
        clang::Expr const& e) {

    auto logKey = String(context, e);
    CNS_DEBUG_MSG(logKey, "begin");
    /*
    if(e.children().empty()) {
        CNS_DEBUG_MSG(logKey, "No children found for expression");
        CNS_DEBUG_MSG(logKey, "end");
        return String(context, e);
    }
    */

    auto const *dre = getSubExprDRE(context, e);
    if(!dre) {
        CNS_DEBUG_MSG(logKey, "No DRE found for expression");
        CNS_DEBUG_MSG(logKey, "end");
        return String(context, e);
    }

    CNS_DEBUG(logKey, "Found DRE for expression: '{}'", String(context, *dre));

    if(auto const * vd = dre->getDecl()) {
        CNS_DEBUG(logKey, "Got decl from DRE: '{}'", String(context, *vd));
        if(vd->isFunctionPointerType() || vd->isFunctionOrFunctionTemplate()) {
            CNS_DEBUG(logKey, "DRE is fptr type: '{}'", String(context, *dre));
            CNS_DEBUG_MSG(logKey, "end");
            // Any operations on function/fptr are non-type changing
            return qualifiedName(context, *dre);
        }
        else {
            CNS_DEBUG(logKey, "DRE is not fptr type: '{}'", String(context, *dre));
        }
    }

    CNS_DEBUG(logKey, "No decl in dre: '{}'", String(context, *dre));

    auto dreqn = qualifiedName(context, *dre);
    CNS_DEBUG(logKey, "DRE qn: '{}'", dreqn);
    auto dres = String(context, *dre);
    CNS_DEBUG(logKey, "DRE string: '{}'", dres);
    // replace dre name in expr string with dreqn
    auto eqn = String(context, e);
    if(auto pos = eqn.find(dres); pos != std::string::npos) {
        CNS_DEBUG(logKey, "Found identifier '{}' in expr", dres);
        eqn.replace(pos, dres.size(), dreqn);
        CNS_DEBUG(logKey, "Replaced identifier '{}' with qn '{}'", dres, dreqn);
    }

    CNS_DEBUG(logKey, "Expr qn: '{}'", eqn);
    CNS_DEBUG_MSG(logKey, "end");
    return eqn;
}

std::string getLinkedParm(
        clang::DeclContext const *context,
        clang::VarDecl const &var) {

    auto & astContext = context->getParentASTContext();
    auto const logKey = String(astContext, var);
    CNS_DEBUG_MSG(logKey, "begin");
    auto const *func = getContainerFunctionDecl(astContext, var);
    if(!func) {
        CNS_ERROR_MSG(logKey, "<DC>No Parent function found.");
        CNS_DEBUG_MSG(logKey, "end");
        return "{n/a}";
    }

    if(auto parmPos = getParameterMatch(*func, var.getDeclName())) {
        CNS_INFO(logKey, "parmPos: {}; {}", parmPos.value(), parmqn(astContext, *func, *parmPos));
        CNS_DEBUG_MSG(logKey, "end");
        return parmqn(astContext, *func, *parmPos);
    }

    CNS_ERROR_MSG(logKey, "<DC>parmPos nullopt.");
    CNS_DEBUG_MSG(logKey, "end");
    return "{local}";
}

std::string qualifiedName(
        clang::ASTContext &context,
        clang::VarDecl const &var) {

    auto const logKey = String(context, var);
    CNS_DEBUG_MSG(logKey, "begin");
    if(var.hasGlobalStorage()) {
        CNS_INFO_MSG(logKey, "VarDecl is global");
        CNS_DEBUG_MSG(logKey, "end");
        return "::" + String(context, var);
    }
    if(var.isLocalVarDecl()) {
        CNS_INFO_MSG(logKey, "VarDecl is local var & not parm");
        CNS_DEBUG_MSG(logKey, "end");
        return getContainerFunction(context, var) + "." + String(context, var);
    }
    if(var.isLocalVarDeclOrParm()) {
        CNS_INFO_MSG(logKey, "VarDecl is probably a parm");

        auto const *func = getContainerFunctionDecl(context, var);
        if(!func) {
            CNS_ERROR_MSG(logKey, "Cannot find container function decl for Local VarDecl");
            CNS_DEBUG_MSG(logKey, "end");
            return String(context, var);
            //return qualifiedName(var.getDeclContext(), var);
            //return "{n/a}";
        }

        if(auto parmPos = getParameterMatch(*func, var.getDeclName())) {
            CNS_INFO(logKey, "parmPos: {}; {}", parmPos.value(), parmqn(context, *func, *parmPos));
            CNS_DEBUG_MSG(logKey, "end");
            return parmqn(context, *func, *parmPos);
        }

        CNS_ERROR_MSG(logKey, "parmPos nullopt.");
        CNS_DEBUG_MSG(logKey, "end");
        return getContainerFunction(context, var) + "." + String(context, var);
    }
    CNS_DEBUG_MSG(logKey, "end");
    return "(whatisit?)";
}

std::string qualifiedName(
        clang::ASTContext &context,
        clang::DeclRefExpr const& dre) {

    auto const logKey = String(context, dre);
    CNS_DEBUG_MSG(logKey, "begin");
    auto const *refd = dre.getReferencedDeclOfCallee();
    if(refd) {
        CNS_DEBUG_MSG(logKey, "refd");
        auto const *vd = dyn_cast<VarDecl>(refd);
        if(vd) {
            CNS_INFO_MSG(logKey, "ReferencedDecl is VarDecl");
            return qualifiedName(context, *vd);
        }
    }

    auto const *stmt = dre.getExprStmt();
    auto const *decl = dre.getDecl();
    if(!!decl) {
        CNS_INFO_MSG(logKey, "DeclRefExpr is a decl.");
        auto const *var = dyn_cast<clang::VarDecl>(decl);
        if(var) {
            CNS_INFO_MSG(logKey, "Found VarDecl from DeclRefExpr");
            return qualifiedName(context, *var);
        }
        CNS_INFO_MSG(logKey, "No VarDecl from DeclRefExpr");
        if(decl->getFunctionType() != nullptr) {
            CNS_INFO_MSG(logKey, "DeclRefExpr.decl is a Function type.");
            CNS_INFO_MSG(logKey, "Using just the function name is sufficient.");
            return String(context, *decl);
        }
        CNS_WARN_MSG(logKey, "DeclRefExpr.decl is not a Function type either.");
        return getContainerFunction(context, *decl) + "." + String(context, dre);
    }

    if(!!stmt) {
        CNS_INFO_MSG(logKey, "DeclRefExpr is a stmt.");
        CNS_INFO_MSG(logKey, "Building data from Expr stmt from DeclRefExpr");
        return getContainerFunction(context, *stmt) + "." + String(context, dre);
    }

    CNS_ERROR_MSG(logKey, "DeclRefExpr has no decl or stmt.");
    CNS_DEBUG_MSG(logKey, "end");
    return getContainerFunction(context, dre) + "." + String(context, dre);
}

std::string qualifiedNameX(
        clang::ASTContext &context,
        clang::CallExpr const& call,
        clang::Expr const &e) {
    std::string logKey = "(" + String(context, call) + "|" + String(context, e) + ")";
    CNS_INFO_MSG(logKey, "begin");
    auto const *fn = getContainerFunctionDecl(context, e);
    if(!fn) {
        CNS_INFO_MSG(logKey, "Container fn == nullptr");
        CNS_INFO_MSG(logKey, "end");
        return String(context, e);
    }

    auto const * dre = dyn_cast<clang::DeclRefExpr>(&e);
    if(dre) {
        CNS_INFO_MSG(logKey, "Got DRE");
        CNS_INFO_MSG(logKey, "end");
        return qualifiedName(context, *dre);
    }

    // For any other complex exprs, return func name + expr
    CNS_INFO_MSG(logKey, "Found container fn but no dre");
    CNS_INFO_MSG(logKey, "end");
    return  String(context, e);
}

std::string getLinkedParm(
        clang::ASTContext &context,
        clang::VarDecl const &var) {

    auto const logKey = String(context, var);
    CNS_DEBUG_MSG(logKey, "begin");
    if(var.isLocalVarDecl()) {
        CNS_INFO_MSG(logKey, "VarDecl is local var & not parm");
        CNS_DEBUG_MSG(logKey, "end");
        return "{local}";
    }

    CNS_INFO_MSG(logKey, "VarDecl is not local var");
    if(var.isLocalVarDeclOrParm()) {
        CNS_INFO_MSG(logKey, "VarDecl is probably a parm");

        auto const *func = getContainerFunctionDecl(context, var);
        if(!func) {
            CNS_ERROR_MSG(logKey, "No Parent function found.");

            auto const *dc = var.getDeclContext();
            CNS_DEBUG_MSG(logKey, "end");
            return getLinkedParm(dc, var);
            //return getLinkedParm(context, var, var.getDeclName());
            //return "{n/a}";
        }

        if(auto parmPos = getParameterMatch(*func, var.getDeclName())) {
            CNS_INFO(logKey, "parmPos: {}; {}", parmPos.value(), parmqn(context, *func, *parmPos));
            CNS_DEBUG_MSG(logKey, "end");
            return parmqn(context, *func, *parmPos);
        }

        CNS_ERROR_MSG(logKey, "<DC>parmPos nullopt.");
        CNS_DEBUG_MSG(logKey, "end");
        return "{local}";
    }

    CNS_INFO_MSG(logKey, "VarDecl is not local var or parm");
    if(var.hasGlobalStorage()) {
        CNS_INFO_MSG(logKey, "VarDecl has global storage => no linked parm");
        CNS_DEBUG_MSG(logKey, "end");
        return "{global}";
    }

    CNS_DEBUG_MSG(logKey, "end");
    return "{No_Impl_Yet!}";
}

std::string  linkedParmPos(
        clang::ASTContext &context,
        clang::CallExpr const &call,
        clang::Expr const &arg) {

    auto const logKey = String(context, call) + "()." + String(context, arg);
    CNS_DEBUG_MSG(logKey, "begin");
    auto const *fn = call.getDirectCallee();
    if(!fn) {
        CNS_INFO_MSG(logKey, "Callee fn == nullptr");
        CNS_DEBUG_MSG(logKey, "end");
        return String(context, call) + "." + String(context, arg);
    }

    unsigned pos = 0;
    for(auto const * aexpr: call.arguments()) {
        if(clang::Expr::isSameComparisonOperand(aexpr, &arg)) {
            pos++;
        }
        else {
            break;
        }
    }
    CNS_INFO(logKey, "argPos = {}", pos);
    // if pos > call.arguments() TODO

    CNS_DEBUG_MSG(logKey, "end");
    return fn->getNameAsString() + ".$" + std::to_string(pos);
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

template<class>
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
    auto const logKey = String(context, call);
    CNS_DEBUG_MSG(logKey, "begin");
    auto const *fptrp = call.IgnoreImplicit();
    if(fptrp) {
        CNS_INFO_MSG(logKey, "Got fptr from call expr after implicitignore");
        auto const * dre = dyn_cast<clang::DeclRefExpr>(fptrp);
        if(dre) {
            CNS_INFO_MSG(logKey, "Got dre from fptr");
            CNS_DEBUG_MSG(logKey, "end");
            return dre;
        }
        else {
            CNS_INFO_MSG(logKey, "No dre from fptr");
        }
    }
    CNS_INFO_MSG(logKey, "No fptr. Yet.");
    for(auto child: call.children()) {
        auto const *ce = dyn_cast<clang::CastExpr>(child);
        if(ce) {
            CNS_INFO_MSG(logKey, "Got castexpr from fptr");
            auto const *dre = dyn_cast<clang::DeclRefExpr>(ce->getSubExpr());
            if(dre) {
                CNS_INFO_MSG(logKey, "Got dre from castexpr");
                CNS_DEBUG_MSG(logKey, "end");
                return dre;
            }
            else {
                CNS_INFO_MSG(logKey, "No dre from castexpr");
            }
        }
        else {
            CNS_INFO_MSG(logKey, "No castexpr from fptr");
            auto const *dre = dyn_cast<clang::DeclRefExpr>(child);
            if(dre) {
                CNS_INFO_MSG(logKey, "Got dre from child");
                CNS_DEBUG_MSG(logKey, "end");
                return dre;
            }
            else {
                CNS_INFO_MSG(logKey, "No dre from child");
            }
        }
    }

    CNS_DEBUG_MSG(logKey, "end");
    return nullptr;
}

std::string qualifiedNameFromFptrCall(clang::ASTContext &context, clang::CallExpr const &call) {
    auto const logKey = String(context, call);
    CNS_DEBUG_MSG(logKey, "begin");
    auto const *fptr = getFptrFromFptrCall(context, call);
    if(!fptr) {
        CNS_DEBUG_MSG(logKey, "No Fptr found. Using callee expr.");
        auto const *fn = call.getCallee();
        if(!fn) {
            CNS_ERROR_MSG(logKey, "No callee expr found. Stringifying call.");
            return String(context, call);
        }
        CNS_DEBUG_MSG(logKey, "end");
        //return String(context, *fn);
        return qualifiedName(context, *fn);
    }
    CNS_DEBUG_MSG(logKey, "end");
    return qualifiedName(context, *fptr);
}
//--

#endif // UTILS_H
