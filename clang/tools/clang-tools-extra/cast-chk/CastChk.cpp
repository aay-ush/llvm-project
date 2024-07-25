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

#include "utils.h"

using namespace clang::tooling;
using namespace llvm;

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::ento;


// TODO: use TypeInfo from context to detect incompatible size casts.
// TODO: &i -> trace to int i;

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
using CensusDecl = clang::Decl;

enum class CastExprType{
    Assignment,
    FunctionCall,
    UnaryOp,
    BinaryOp
};

template <CastExprType t>
std::string getCastExprType() {
    switch(t) {
        case CastExprType::Assignment:
            return "assignment";
        case CastExprType::FunctionCall:
            return "call";
        case CastExprType::UnaryOp:
            return "unaryop";
        case CastExprType::BinaryOp:
            return "binaryop";
    };
}

/*
std::string getCastExprType(clang::DeclStmt const&) {
    return getCastExprType<CastExprType::Assignment>();
}
*/
std::string getCastExprType(clang::CallExpr const&) {
    return getCastExprType<CastExprType::FunctionCall>();
}
std::string getCastExprType(clang::UnaryOperator const&) {
    return getCastExprType<CastExprType::UnaryOp>();
}
std::string getCastExprType(clang::DeclRefExpr const&) {
    return getCastExprType<CastExprType::BinaryOp>();
}
//--
enum class CastSourceType {
    UnaryOp,        // &i -> i
    BinaryOp,       // a = b
    Function,       // void (*)() -> void f()
    FunctionArg     // f((void*)&i) -> f.$0
};
//--


/*
struct DeclData {
    unsigned dd_hash;
    std::string dd_expr;
    std::string type;
    std::string category;
    std::string linkedParameter;
    std::string linkedFunction;
    std::string location;
};
bool operator==(DeclData const &lhs, DeclData const &rhs) {
    return lhs.dd_hash == rhs.dd_hash
        && lhs.dd_expr == rhs.dd_expr
        && lhs.type == rhs.type
        && lhs.linkedParameter == rhs.linkedParameter;
}
bool operator!=(DeclData const &lhs, DeclData const &rhs) {
    return !(lhs == rhs);
}
*/

struct DeclData {
    unsigned hash_ {0};
    std::string expr_;
    std::string type_;
    std::string cat_;
    std::string parm_;
    std::string container_;
    std::string loc_;
};
bool operator==(DeclData const &lhs, DeclData const &rhs) {
    return lhs.hash_ == rhs.hash_;
}

bool operator!=(DeclData const &lhs, DeclData const &rhs) {
    return !(lhs == rhs);
}

//--

/*
template<>
struct std::hash<CensusDecl>
{
    unsigned operator()(CensusDecl const *decl) const noexcept {
        ODRHash hasher;
        hasher.AddDecl(decl);
        return hasher.CalculateHash();
    }
};
std::hash<CensusDecl> HASH;
*/

/*
 * Dominator data should be associated with target only. Why?
 *  - Source participates in the cast, it is not created or initialized by it.
 *  - Target is at least initialized by the cast/dominator.
 *  => Source can be involved in multiple casts which may not have same DominatorData.
 *  => Target can be involved in one cast only, unless it is also a source.
struct DominatorData {
    DeclData from;
    std::string linkExpr;
    std::string linkExprType;
    std::optional<std::string> linkedFunction;
};
*/

struct DominatorData {
    DeclData from_;
    std::string expr_;
    std::string etype_;
    std::optional<std::string> callee_;
};

bool operator==(DominatorData const &lhs, DominatorData const &rhs) {
    return lhs.from_ == rhs.from_;
}
bool operator!=(DominatorData const &lhs, DominatorData const &rhs) {
    return !(lhs == rhs);
}
//--

using Dominators = std::vector<DominatorData>;
using DeclInfo = std::pair<DeclData, std::optional<Dominators>>;

// Census stores decl & dominator data for every decl.
// DeclData is not necessarily the key for census.
// DeclData is computed based on information available from context at the time.
//   It can change with expression. For example, `int i` has same hash for `i` and `&i` but different data types, hence technically different decl data. If decldata is used as key, this difference will be missed.
using Census = std::unordered_map<unsigned, DeclInfo>;
Census census;
using CensusNode = decltype(census)::value_type;

std::pair<unsigned, DeclInfo> makeCensusNode(
        DeclData const &node,
        DominatorData const &dom) {
    return {node.hash_, {node, {{dom}}}};
}

std::pair<unsigned, DeclInfo> makeCensusNode(DeclData const &node) {
    return {node.hash_, {node, {}}};
}

DeclInfo makeDeclInfo(
        DeclData const &node,
        DominatorData const &dom) {
    return {node, {{dom}}};
}


std::ostream& dump(std::ostream &os, DeclInfo const &d);
std::ostream& dump(std::ostream &os, DeclData const &info);
std::ostream& dump(std::ostream &os, Dominators const &doms);
std::ostream& dump(std::ostream &os, DominatorData const &info);

void censusSummary(std::ostream &os);
void censusSummary(std::ostream &os, DeclData const& data, int indent = 0);
void declSummary(std::ostream &os, DeclData const &data);
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
unsigned cnsHash(ASTContext &context, clang::ValueDecl const& var) {
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
unsigned cnsHash(ASTContext &context, clang::Decl const& decl) {
    ODRHash h;
    auto const *f = getContainerFunctionDecl(context, decl);
    if(f) {
        h.AddFunctionDecl(f);
    }
    h.AddDecl(&decl);
    return h.CalculateHash();
}
unsigned cnsHash(ASTContext &context, clang::Expr const& e) {
    ODRHash h;
    auto const *f = getContainerFunctionDecl(context, e);
    if(f) {
        h.AddFunctionDecl(f);
    }
    h.AddStmt(&e);
    h.AddQualType(e.getType());
    return h.CalculateHash();
}
unsigned cnsHash(ASTContext &context, clang::Stmt const& s) {
    ODRHash h;
    auto const *f = getContainerFunctionDecl(context, s);
    if(f) {
        h.AddFunctionDecl(f);
    }
    h.AddStmt(&s);
    return h.CalculateHash();
}
//---

template<CastSourceType s_type, typename T>
DeclData buildDeclData(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CastExpr const &castExpr,
        T const &e) {
    static_assert(impl_false<s_type>, "Unknown cast source type used");
}

DeclData buildDeclData(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::VarDecl const &var) {

    FOUT << "[INFO](buildDeclData<VarDecl>) var: "
         << String(context, var) << "; type: "
         << Typename(context, var) << "\n";

    return {
        cnsHash(context, var),
        String(context, var),
        Typename(context, var),
        TypeCategory(context, var),
        getLinkedParm(context, var),
        getContainerFunction(context, var),
        var.getLocation().printToString(sm)
    };
}

DeclData buildDeclData(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::DeclRefExpr const &e,
        clang::ValueDecl const &d) {

    FOUT << "[INFO](buildDeclData<ValueDecl>) decl: "
         << String(context, d) << "; type: "
         << Typename(context, d) << "\n";

    return {
        cnsHash(context, d),
        String(context, d),
        Typename(context, d),
        TypeCategory(context, d),
        getLinkedParm(context, e, d.getDeclName()),
        getContainerFunction(context, e),
        d.getLocation().printToString(sm)
    };
}

template<>
DeclData buildDeclData<CastSourceType::UnaryOp>(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CastExpr const &castExpr,
        clang::UnaryOperator const &op) {

    FOUT << "[INFO](buildDeclData<UnaryOp>) op: "
         << String(context, op) << "; type: "
         << Typename(context, op) << "\n";

    return {
        cnsHash(context, op),
        String(context, op),
        Typename(context, op),
        TypeCategory(context, op),
        "(TODO param_check)",
        getContainerFunction(context, castExpr),
        castExpr.getExprLoc().printToString(sm)
    };

    /*
    switch(op.getOpcode()) {
        case UO_Deref:
            FOUT << "[INFO](buildDeclData<UnaryOp>) op: *\n";
            break;
        case UO_AddrOf:
            FOUT << "[INFO](buildDeclData<UnaryOp>) op: &\n";
            break;
        default:
            FOUT << "[INFO](buildDeclData<UnaryOp>) op: -\n";
    };
    */
}

/*
DeclData buildDeclData(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CastExpr const &castExpr,
        clang::DeclRefExpr const &e) {

    FOUT << "[INFO](buildDeclData<DeclRef>) ref: "
         << String(context, e) << "; type: "
         << Typename(context, e) << "\n";

    return {
        String(context, e),                                       // dd_expr
        Typename(context, e),                                         // type
        TypeCategory(context, e),                            // category
        getLinkedParm(context, castExpr, e.getNameInfo()), // linkedParameter
        getContainerFunction(context, castExpr),
        castExpr.getExprLoc().printToString(sm)
    };
}
*/

/*
DeclData buildDeclData(
        clang::ASTContext &context,
        clang::CastExpr const &castExpr,
        clang::UnaryOperator const &e) {
}
*/
DeclData buildDeclData(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CastExpr const &castExpr,
        clang::DeclRefExpr const &e,
        clang::Decl const& d) {

    FOUT << "[INFO](buildDeclData<Decl>) decl: "
         << String(context, e) << "; type: "
         << Typename(context, e) << "\n";

    return {
        cnsHash(context, d),
        String(context, e),
        Typename(context, e),
        TypeCategory(context, e),
        getLinkedParm(context, d, e.getNameInfo()),
        getContainerFunction(context, castExpr),
        castExpr.getExprLoc().printToString(sm)
    };
}

DeclData buildDeclData(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CastExpr const &castExpr,
        clang::DeclRefExpr const &e,
        clang::Stmt const& s) {

    FOUT << "[INFO](buildDeclData<Stmt>) stmt: "
         << String(context, s) << "; type: "
         << Typename(context, e) << "\n";

    return {
        cnsHash(context, s),
        String(context, e),
        Typename(context, e),
        TypeCategory(context, e),
        getLinkedParm(context, s, e.getNameInfo()),
        getContainerFunction(context, castExpr),
        castExpr.getExprLoc().printToString(sm)
    };
}

// TODO: probably remove/rename
DeclData buildDeclData(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CastExpr const &castExpr,
        clang::DeclRefExpr const &e) {

    FOUT << "[INFO](buildDeclData<DeclRef2>) ref: "
         << String(context, e) << "; type: "
         << Typename(context, e) << "\n";

    auto const *refd = e.getReferencedDeclOfCallee();
    if(refd) {
        FOUT << "[INFO](buildDeclData) Building data from ReferencedDecl\n";
        auto const *vd = dyn_cast<VarDecl>(refd);
        if(vd) {
            FOUT << "[INFO](buildDeclData) ReferencedDecl is VarDecl\n";
            return buildDeclData(context, sm, *vd);
        }
    }

    auto const *stmt = e.getExprStmt();
    auto const *decl = e.getDecl();
    if(!!decl) {
        FOUT << "[INFO](buildDeclData) Building data from Decl from DeclRefExpr\n";
        auto const *var = dyn_cast<clang::VarDecl>(decl);
        if(var) {
            FOUT << "[INFO](buildDeclData) Found VarDecl from DeclRefExpr\n";
            return buildDeclData(context, sm, *var);
        }
        FOUT << "[WARN](buildDeclData) NO VarDecl from DeclRefExpr\n";
        return buildDeclData(context, sm, castExpr, e, *decl);
    }

    if(!!stmt) {
        FOUT << "[INFO](buildDeclData) Building data from Expr stmt from DeclRefExpr\n";
        return buildDeclData(context, sm, castExpr, e, *stmt);
    }

    FOUT << "[ERROR](buildDeclData) DeclRefExpr has no decl or stmt.\n";

    return {
        cnsHash(context, e),
        String(context, e),
        Typename(context, e),
        TypeCategory(context, e),
        "(No Param match due to declrefexpr error)",
        getContainerFunction(context, castExpr),
        castExpr.getExprLoc().printToString(sm),
    };
}

/*
DeclData buildDeclData(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CastExpr const &castExpr,
        clang::DeclStmt const &e) {

    FOUT << "[INFO](buildDeclData<Declstmt>) ds: "
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
DeclData buildDeclData<CastSourceType::Function>(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CastExpr const &castExpr,
        clang::CallExpr const &e) {

    FOUT << "[INFO](buildDeclData<callExpr:fptr>) call: "
         << String(context, e) << "\n";

    return {
        cnsHash(context, e),
        String(context, e),
        Typename(context, e),
        TypeCategory(context, e),
        "(Not parm)",
        getContainerFunction(context, e),
        e.getExprLoc().printToString(sm)
    };
}

template<>
DeclData buildDeclData<CastSourceType::FunctionArg>(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CastExpr const &castExpr,
        clang::CallExpr const &e) {

    FOUT << "[INFO](buildDeclData<callExpr:farg>) call: "
         << String(context, e) << "\n";

    // Get cast expressions position in call argument list.
    unsigned argPos = 0;
    auto match = std::find_if(e.arg_begin(), e.arg_end(),
        [&] (auto const &arg) {
        argPos++;
        FOUT << "[DEBUG](buildDeclData<call>) Argpos: " << argPos << "\n";
        return clang::Expr::isSameComparisonOperand(arg, &castExpr);
    });
    FOUT << "[DEBUG](buildDeclData<call>) final argpos: " << argPos << "\n";

    std::string parmId;
    clang::ParmVarDecl const *parm = nullptr;
    llvm::raw_string_ostream stream(parmId);
    if (argPos <= e.getNumArgs()) {
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
        return {
            0,
            "(no parm found)",
            Typename(context, castExpr),
            TypeCategory(context, castExpr),
            (argPos > e.getNumArgs()
                ? ("(Failed to match arg)")
                : (String(context, e, argPos - 1))),
            getContainerFunction(context, castExpr),
            e.getExprLoc().printToString(sm),
        };
    }

    return {
        cnsHash(context, *parm),
        parmId,
        Typename(context, *parm),
        TypeCategory(context, castExpr),
        (argPos > e.getNumArgs()
            ? ("(Failed to match arg)")
            : (String(context, e, argPos - 1))),
        getContainerFunction(context, castExpr),
        e.getExprLoc().printToString(sm),
    };
}

template<>
DeclData buildDeclData<CastSourceType::BinaryOp>(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CastExpr const &castExpr,
        clang::DeclRefExpr const &e) {

    FOUT << "[INFO](buildDeclData<BinaryOp, DeclRefExpr>\n";
    return buildDeclData(context, sm, castExpr, e);
}

/*
std::string getLinkedFunction(
        clang::ASTContext const &context,
        clang::CastExpr const &castExpr,
        clang::DeclStmt const&) {

    return "N/A";
}
*/

std::string getLinkedFunction(
        clang::ASTContext const &context,
        clang::CastExpr const &castExpr,
        clang::UnaryOperator const&) {

    return "N/A";
}

std::string getLinkedFunction(
        clang::ASTContext const &context,
        clang::CastExpr const &castExpr,
        clang::DeclRefExpr const&) {

    return "N/A";
}


std::string getLinkedFunction(
        clang::ASTContext &context,
        clang::CastExpr const &castExpr,
        clang::CallExpr const &call) {

    auto const *calledFn = getCalleeDecl(call);
    assert(calledFn);

    return calledFn->getNameAsString();
}

template<CastSourceType s_type>
CensusDecl const* getSourceDecl(
        clang::ASTContext const &context,
        clang::DeclRefExpr const &source) {
    static_assert(impl_false<s_type>, "Unknown cast source type used");
}

template<>
CensusDecl const* getSourceDecl<CastSourceType::UnaryOp>(
        clang::ASTContext const &context,
        clang::DeclRefExpr const &source) {

    FOUT << "[INFO](getSourceDecl<UnaryOp>)\n";
    auto const *decl = source.getDecl();
    return dyn_cast<clang::VarDecl>(decl);
}

template<>
CensusDecl const* getSourceDecl<CastSourceType::BinaryOp>(
        clang::ASTContext const &context,
        clang::DeclRefExpr const &source) {

    FOUT << "[INFO](getSourceDecl<BinaryOp>)\n";
    return source.getDecl();
}

template<>
CensusDecl const* getSourceDecl<CastSourceType::Function>(
        clang::ASTContext const &context,
        clang::DeclRefExpr const &source) {

    FOUT << "[INFO](getSourceDecl<Function>)\n";
    auto const *decl = source.getDecl();
    return dyn_cast<clang::FunctionDecl>(decl);
}

template<>
CensusDecl const* getSourceDecl<CastSourceType::FunctionArg>(
        clang::ASTContext const &context,
        clang::DeclRefExpr const &source) {

    FOUT << "[INFO](getSourceDecl<FunctionArg>)\n";
    auto const *decl = source.getDecl();
    return dyn_cast<clang::ParmVarDecl>(decl);
}

CensusDecl const* getTargetDecl(
        clang::ASTContext const &context,
        clang::CastExpr const &castExpr,
        clang::UnaryOperator const &op) {

    //auto const *t1 = op.getReferencedDeclOfCallee();
    //auto const *t2 = op.getAsBuiltinConstantDeclRef(context);
    auto const *t3 = castExpr.getReferencedDeclOfCallee();
    auto const *t4 = castExpr.getAsBuiltinConstantDeclRef(context);

    /*
    if(!!t1) {
        FOUT << "[INFO](getTargetDecl<Unary>) t1\n";
        return t1;
    }
    if(!!t2) {
        FOUT << "[INFO](getTargetDecl<Unary>) t2\n";
        return t2;
    }
    */
    if(!!t3) {
        FOUT << "[INFO](getTargetDecl<Unary>) t3\n";
        return t3;
    }
    if(!!t4) {
        FOUT << "[INFO](getTargetDecl<Unary>) t4\n";
        return t4;
    }

    // TODO
    // Not having a unary declaration seems fine. *pi or &i e.g.
    FOUT << "[ERROR](getTargetDecl<Unary>) nullptr\n";
    return nullptr;
}

/*
CensusDecl const* getTargetDecl(
        clang::ASTContext const &context,
        clang::CastExpr const &castExpr,
        clang::DeclStmt const &t) {

    return t.getSingleDecl();  // TODO examples where decls() or DeclGroup() may be needed.
}
*/

CensusDecl const* getTargetDecl(
        clang::ASTContext const &context,
        clang::CastExpr const &castExpr,
        clang::CallExpr const &t) {

    // TODO CHK: If castexpr is a bitCast, then we have arg match
    //         otherwise it could just be fptr decay or l->r value.

    FOUT << "[INFO](getTargetDecl<callExpr>) call: "
         << String(context, t) << "\n";

    FOUT << "[INFO](getTargetDecl<callExpr>) castExpr: "
         << String(context, castExpr) << "\n";

    // Get cast expressions position in call argument list.
    unsigned argPos = 0;
    auto match = std::find_if(t.arg_begin(), t.arg_end(),
        [&] (auto const &arg) {
        argPos++;
        //FOUT << "[DEBUG](getTargetDecl<callExpr>) Argpos: " << argPos << "\n";
        return clang::Expr::isSameComparisonOperand(arg, &castExpr);
    });
    FOUT << "[DEBUG](getTargetDecl<callExpr>) final argpos: " << argPos << "\n";

    std::string parmId;
    clang::ParmVarDecl const *parm = nullptr;
    llvm::raw_string_ostream stream(parmId);
    if (argPos <= t.getNumArgs()) {
        auto const *parmd = getParamDecl(context, t, argPos - 1);
        if(parmd) {
            parm = dyn_cast<clang::ParmVarDecl>(parmd);
            if(parm) {
                parm->printQualifiedName(stream);
                FOUT << "[DEBUG](getTargetDecl<callExpr>) Found Parm: " << parmId << "\n";
            }
        }
    }
    assert(parm);
    if(!parm) {
        return nullptr;
    }

    return parm;

    /*
    // Get cast expressions position in call argument list.
    unsigned argPos = 0;
    auto match = std::find_if(t.arg_begin(), t.arg_end(),
        [&] (auto const &arg) {
            argPos++;
            return clang::Expr::isSameComparisonOperand(arg, &castExpr);
    });

    //if (match != t.arg_end()) {
    if (argPos <= t.getNumArgs()) {
        return getParamDecl(context, t, argPos - 1);
    }
    return nullptr;
    */
}

//---
std::ostream& dump(std::ostream &os, DeclInfo const &d) {
    auto const &[data, dominators] = d;
    dump(os, data);
    if(!dominators) {
        return os;
    }
    dump(os, dominators.value());
    os << " -> ";
    dump(os, data);
    return os;
}

std::ostream& dump(std::ostream &os, DeclData const &info) {
    os << "{hash: '" << info.hash_
       << "', expr: '" << info.expr_
       << "', type: '" << info.type_
       << "', category: '" << info.cat_
       << "', linkedParameter: '" << info.parm_
       << "', containerFunction: '" << info.container_
       << "', location: '" << info.loc_
       << "'}\n";
    return os;
}

std::ostream& dump(std::ostream &os, Dominators const &doms) {
    for(auto const &d: doms) {
        dump(os, d);
    }
    return os;
}

std::ostream& dump(std::ostream &os, DominatorData const &domInfo) {
    os << "{from: ";
    dump(os, domInfo.from_);
    os << "', LinkingExpr: '" << domInfo.expr_
       << "', ExprType: '" << domInfo.etype_
       << "', CalledFunction: '" << domInfo.callee_.value_or("(N/A)")
       << "}\n";

    return os;
}

void censusSummary(std::ostream &os) {
    for(auto const &[_, info]: census) {
        auto const &[data, __] = info;

        censusSummary(os, data);
        os << "\n";
    }
}

std::ostream& space(std::ostream &os, int indent) {
    for(int i = 0; i != indent; i++) {
        os << " ";
    }
    return os;
}

void censusSummary(std::ostream &os, DeclData const &node, int indent) {
    bool pushed = false;
    if(!node.hash_) {
        os << "<end>}\n";
        return;
    }

    static std::vector<unsigned> seenNodes;
    auto it = std::find(std::begin(seenNodes), std::end(seenNodes), node.hash_);
    if(it != std::end(seenNodes)) {
        os << "{ (revisiting) ";
        declSummary(os, node);
        os << "\n"; space(os, indent) << "-> <end>}\n";
        return;
    }

    pushed = true;
    //os << "<Pushing " << node.hash_ << ">\n";
    seenNodes.push_back(node.hash_);
    os << "{ (first visit) ";
    declSummary(os, node);

    auto doesNodeDominate = [&node](CensusNode const &n) {
        auto const &[_, info] = n;
        auto const &[__, doms_] = info;
        if(!doms_)
            return false;
        auto const &doms = doms_.value();
        return std::find_if(std::begin(doms), std::end(doms),
                [&node](auto const &d) {
                    return node == d.from_;
                }) != std::end(doms);
    };

    std::vector<CensusNode> dominatedNodes;
    std::copy_if(std::begin(census), std::end(census), std::back_inserter(dominatedNodes), doesNodeDominate);

    if(dominatedNodes.empty()) {
        if(!seenNodes.empty() && pushed) {
            pushed = false;
            //os << "<Popping (emptydoms) " << seenNodes.back() << ">\n";
            seenNodes.pop_back();
        }
        os << "\n"; space(os, indent) << "-> <end>}\n";
        return;
    }

    for(auto const &[_, rinfo]: dominatedNodes) {
        os << "\n"; space(os, indent) << "-> ";
        auto const &[rdata, __] = rinfo;
        censusSummary(os, rdata, indent+2);
        if(!seenNodes.empty()) {
            //os << "<Popping (domloop)" << seenNodes.back() << ">\n";
            seenNodes.pop_back();
        }
    }
    space(os, indent) << "}\n";
}

void declSummary(std::ostream &os, DeclData const &data) {
    os << "[" << data.cat_ << "](" << data.hash_ << ") " << data.expr_ << ": '" << data.type_ << "'"
       << " " << data.parm_ << " in " << data.container_ << "()";
}
//---
/*
void preprocess(
        clang::ASTContext &context,
        clang::DeclStmt const &decl) {}
*/

void preprocess(
        clang::ASTContext &context,
        clang::UnaryOperator const &op) {}

void preprocess(
        clang::ASTContext &context,
        clang::DeclRefExpr const &op) {}

void preprocess(
        clang::ASTContext &context,
        clang::CallExpr const &call) {

    // Trigger cast check for the called function.
    auto const *calledFn = getCalleeDecl(call);
    assert(calledFn);

    if(calledFn->hasBody()) {
        auto const *body = calledFn->getBody();
        assert(body);
        MatchFinder m;
        m.match(*body, context);
    }
}

bool isNodeDominatorNew(
        DeclData const &node,
        DominatorData const &dom) {

    auto &[_, doms_] = census[node.hash_];
    if(!doms_) {
        FOUT << "[INFO](isNodeDominatorNew) No doms present currently.\n";
        return true;
    }

    FOUT << "[INFO](isNodeDominatorNew) Checking current doms for this dom[" << dom.from_.hash_ << "]\n";
    auto doms = doms_.value();
    return std::find(begin(doms), end(doms), dom)
        == end(doms);
}

void appendNodeDominator(
        DeclData const &node,
        DominatorData const &dom) {

    auto &[_, doms_] = census[node.hash_];
    if(!doms_) {
        FOUT << "[INFO](appendNodeDominator) Dominator Initialized.\n";
        census[node.hash_] = makeDeclInfo(node, dom);
    } else {
        FOUT << "[INFO](appendNodeDominator) Appending to dominators.\n";
        auto &doms = doms_.value();
        doms.push_back(dom);
    }

    FOUT << "[INFO](appendNodeDominator) New Dominator Appended: {\n";
    dump(FOUT, dom);
    FOUT << "[INFO](appendNodeDominator) }\n";
}

void chkNodeDataForChange(DeclData const &node) {
    auto const& [old, _] = census[node.hash_];
    if(old != node) {
        FOUT << "[WARN]() Old node with different DeclData:\n"
             << "Old data:\n";
        dump(FOUT, old);
        FOUT << "\nNew data:\n";
        dump(FOUT, node);
        FOUT << "\n";
    }
}

void addDomNode(DeclData const &dom) {
    if(census.find(dom.hash_) == std::end(census)) {
        FOUT << "[INFO](addDomNode) Inserting new node for 'dom (census 'from')'.\n";
        census.insert(makeCensusNode(dom));
        return;
    }
    FOUT << "[INFO](addDomNode) 'dom (census 'from')' is already in census.\n";
    // If dominator is in census, do nothing
    // except warning of decl data change, if any.
    chkNodeDataForChange(dom);
}

void updateCensus(
        DeclData const &from,
        DeclData const &to,
        DominatorData const &dom) {

    addDomNode(from);
    auto it = census.find(to.hash_);
    if(it == std::end(census)) {
        // `to` not in census
        FOUT << "[INFO](updateCensus<0>) Inserting new node for 'to'.\n";
        census.insert(makeCensusNode(to, dom));
        return;
    }

    FOUT << "[INFO](updateCensus<0>) 'to' already in census.\n";
    chkNodeDataForChange(to);
    if(isNodeDominatorNew(to, dom)) {
        FOUT << "[INFO](updateCensus<0>) 'to' has new dominator.\n";
        appendNodeDominator(to, dom);
    }
}

void logCensusUpdate(
        DeclData const &lhs,
        DeclData const &rhs,
        DominatorData const &dom) {

    FOUT << "Match site: " << rhs.loc_ << "\n"
         << "   Linking: [" << lhs.hash_ << "]"
         << lhs.expr_ << " {" << lhs.parm_ << "} -> "
         << "[" << rhs.hash_ << "]"
         << rhs.expr_ << " {" << rhs.parm_ << "}\n"
         << "       from: [" << lhs.cat_ << "] " << lhs.type_ << "\n"
         << "         to: [" << rhs.cat_ << "] " << rhs.type_ << "\n"
         << "       expr: " << "[" << dom.etype_ << "] " << dom.expr_ << "\n"
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

    preprocess(context, dest);

    FOUT << "[INFO](updateCensus<Cast> building lhs data.\n";
    auto lhs = buildDeclData(context, sm, castExpr, castSource);
    FOUT << "[INFO](updateCensus<Cast> building rhs data.\n";
    auto rhs = buildDeclData<CS_t>(context, sm, castExpr, dest);

    DominatorData dom{
        lhs,
        String(context, castExpr),
        getCastExprType(dest),
        getLinkedFunction(context, castExpr, dest)
    };

    updateCensus(lhs, rhs, dom);
    logCensusUpdate(lhs, rhs, dom);
}

void processCast(MatchFinder::MatchResult const &result) {
    assert(result);
    auto *context = result.Context;
    assert(context);
    auto const *castExpr = result.Nodes.getNodeAs<CastExpr>("cast");

    // Source
    auto const *s_fptrRef = result.Nodes.getNodeAs<DeclRefExpr>("callee");
    auto const *s_callArg = result.Nodes.getNodeAs<DeclRefExpr>("arg");
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
        updateCensus<CastSourceType::FunctionArg>(*context, *result.SourceManager, *castExpr, *s_callArg, *call);
    }
    else if(!!fptr) {
        updateCensus<CastSourceType::Function>(*context, *result.SourceManager, *castExpr, *s_fptrRef, *fptr);
    }
    else if(!!binOp) {
        auto const *bl = result.Nodes.getNodeAs<DeclRefExpr>("binLhs");
        auto const *br = result.Nodes.getNodeAs<DeclRefExpr>("binRhs");
        if(!bl) {
            FOUT << "[ERROR](processCast) binop lHS == nullptr.\n";
            return;
        }
        if(!br) {
            FOUT << "[ERROR](processCast) binop RHS == nullptr.\n";
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

}

void updateCensus(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::DeclRefExpr const &src,
        clang::VarDecl const &dest) {

    auto const *lhsDecl = src.getDecl();
    assert(lhsDecl);
    FOUT << "[INFO](updateCensus<Var> building lhs data.\n";
    auto lhs = buildDeclData(context, sm, src, *lhsDecl);
    FOUT << "[INFO](updateCensus<Var> building rhs data.\n";
    auto rhs = buildDeclData(context, sm, dest);

    DominatorData dom{lhs, {}, {}, {}};
    updateCensus(lhs, rhs, dom);
    logCensusUpdate(lhs, rhs, dom);
}

void processVar(MatchFinder::MatchResult const &result) {
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
        auto const &rhsData = buildDeclData(*context, *result.SourceManager, *lhsLit);
        census.insert(makeCensusNode(rhsData));
        FOUT << "Match site: " << rhsData.loc_ << "\n"
             << "   Linking: [literal] -> [" << rhsData.hash_ << "]"
             << rhsData.expr_ << "{" << rhsData.parm_ << "}\n"
             << "       rhs: [" << rhsData.cat_ << "] " << rhsData.type_ << "\n"
             << "          : inside " << rhsData.container_ << "()\n"
             << "\n";
        */
        return;
    }

    assert(lhsRef);
    auto const *lhs = lhsRef->getDecl();

    // We can have FunctionDecl too?
    /*
    auto lhsDecl = dyn_cast<VarDecl>(lhsRef->getDecl());
    if(!lhsDecl) {
        FOUT << "[INFO](processVar) dyncast LHS Decl == nullptr\n";
        return;
    }
    */
    updateCensus(*context, *result.SourceManager, *lhsRef, *rhs);
}

//----------------------------------------------------------------------------
// MATCHERS

// similar construct can match a function ptr. {VarDecl, DeclRefExpr}
DeclarationMatcher AssignMatcher =
    varDecl(
            anyOf(
                hasDescendant(declRefExpr().bind("assignee")),
                hasDescendant(expr().bind("literal")))
            ).bind("varDecl");

StatementMatcher CastMatcher =
    castExpr(
            anyOf(
                //hasAncestor(declStmt().bind("var")), // Check if needed since varDecl is also present
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
                    hasDescendant(
                        declRefExpr(hasType(pointerType(pointee(functionType())))).bind("callee"))),

                hasDescendant(
                    unaryOperator(
                        hasDescendant(declRefExpr().bind("unaryCastee"))
                        ).bind("unaryOp")),

                hasParent(
                    binaryOperator(
                        /*
                        isAssignmentOperator(),
                        hasLHS(expr().bind("binLhs")),
                        hasRHS(expr().bind("binRhs"))).bind("binOp")))
                        */
                        isAssignmentOperator())))

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

        if(castExpr) {
            processCast(result);
        }

        if(varDecl) {
            processVar(result);
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
        censusSummary(FOUT);
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

int main(int argc, const char **argv) {
    auto ExpectedParser = CommonOptionsParser::create(argc, argv, MyToolCategory);
    if(!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }

    CommonOptionsParser &OptionsParser = ExpectedParser.get();
    ClangTool Tool(OptionsParser.getCompilations(),
                   OptionsParser.getSourcePathList());

    CastMatchCallback dumper;
    MatchFinder Finder;
    Finder.addMatcher(CastMatcher, &dumper);
    Finder.addMatcher(AssignMatcher, &dumper);

    FOUT.open("census-dump.txt", std::ios::out);
    //return Tool.run(newFrontendActionFactory<clang::SyntaxOnlyAction>().get());
    return Tool.run(newFrontendActionFactory(&Finder).get());
}

// TODO
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
