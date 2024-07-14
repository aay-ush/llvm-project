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

enum class CastExprType{
    Assignment,
    FunctionCall,
    UnaryOp
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
    };
}

std::string getCastExprType(clang::DeclStmt const&) {
    return getCastExprType<CastExprType::Assignment>();
}
std::string getCastExprType(clang::CallExpr const&) {
    return getCastExprType<CastExprType::FunctionCall>();
}
std::string getCastExprType(clang::UnaryOperator const&) {
    return getCastExprType<CastExprType::UnaryOp>();
}
//--

struct DeclData {
    std::string dd_expr;
    std::string type;
    std::string category;
    std::string linkedParameter;
    std::string linkedFunction;
    std::string location;
};

bool operator==(DeclData const &lhs, DeclData const &rhs) {
    return lhs.dd_expr == rhs.dd_expr
        && lhs.type == rhs.type
        && lhs.linkedParameter == rhs.linkedParameter;
}
bool operator!=(DeclData const &lhs, DeclData const &rhs) {
    return !(lhs == rhs);
}
//--

// todo check annonymous cast expressions.
using CensusDecl = clang::Decl;

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

/*
 * Dominator data should be associated with target only. Why?
 *  - Source participates in the cast, it is not created or initialized by it.
 *  - Target is at least initialized by the cast/dominator.
 *  => Source can be involved in multiple casts which may not have same DominatorData.
 *  => Target can be involved in one cast only, unless it is also a source.
*/
struct DominatorData {
    //CensusDecl const *from;
    unsigned const from;
    DeclData fromData;
//    DeclData to;
//    std::vector<CensusDecl const*> lchain;
//    std::vector<CensusDecl const*> rchain;
    std::string expr;
    std::string exprType;
    std::optional<std::string> linkedFunction;
};

bool operator==(DominatorData const &lhs, DominatorData const &rhs) {
    if(!lhs.from) {
        //FOUT << "[ERROR](DominatorData-operator==) lhs.from == nullptr\n";
        return false;
    }
    if(!rhs.from) {
        //FOUT << "[ERROR](DominatorData-operator==) rhs.from == nullptr\n";
        return false;
    }

    //return lhs.from->getID() == rhs.from->getID();
    return lhs.from == rhs.from;
        //&& lhs.fromData == rhs.fromData
        //&& lhs.fromData.location == rhs.fromData.location;
}
bool operator!=(DominatorData const &lhs, DominatorData const &rhs) {
    return !(lhs == rhs);
}

// Check if decl dominates, i.e. decl == dominatorData.from
bool operator==(DominatorData const &lhs, CensusDecl const *rhs) {
    if(!lhs.from) {
        //FOUT << "[ERROR](DominatorData-operator==) lhs.from == nullptr\n";
        return false;
    }
    if(!rhs) {
        //FOUT << "[ERROR](DominatorData-operator==) rhs == nullptr\n";
        return false;
    }
    assert(rhs);
    //return rhs->getID() == lhs.from->getID();
    return HASH(rhs) == lhs.from;
}
bool operator!=(DominatorData const &lhs, CensusDecl const *rhs) {
    return !(lhs == rhs);
}
bool operator==(CensusDecl const *lhs, DominatorData const &rhs) {
    return rhs == lhs;
}
bool operator!=(CensusDecl const *lhs, DominatorData const &rhs) {
    return !(rhs == lhs);
}
bool operator==(DominatorData const& lhs, unsigned const rhs) {
    return lhs.from == rhs;
}
bool operator==(unsigned lhs, DominatorData const& rhs) {
    return rhs == lhs;
}
bool operator!=(DominatorData const& lhs, unsigned const rhs) {
    return !(lhs == rhs);
}
bool operator!=(unsigned lhs, DominatorData const& rhs) {
    return !(rhs == lhs);
}
//--


using Dominators = std::vector<DominatorData>;
using DeclInfo = std::pair<DeclData, std::optional<Dominators>>;

// Census stores decl & dominator data for every decl.
//using Census = std::unordered_map<CensusDecl const*, DeclInfo>;
using Census = std::unordered_map<unsigned, DeclInfo>;
Census census;
using CensusNode = decltype(census)::value_type;

/*
using DeclChainedData = std::pair<CensusDecl const*, DeclData const&>;
using CastChain = std::vector<DeclChainedData>;
using CastHistory = std::unordered_map<CensusDecl const*, std::vector<CastChain>>;
CastHistory history;
*/

std::ostream& dump(std::ostream &os, Dominators const &doms);
std::ostream& dump(std::ostream &os, DeclData const &info);
std::ostream& dump(std::ostream &os, DominatorData const &info);
void dump(std::ostream &os, DeclInfo const &d);

void declSummary(std::ostream &os, DeclData const &data);
void declCastSummary(std::ostream &os, DeclData const &data, DominatorData const &cast);
void censusSummary(std::ostream &os, unsigned decl, DeclData const &declData, int indent = 0);
void censusSummary(std::ostream &os);

template<typename T>
std::string functionParameterMatch(
        clang::ASTContext &context,
        T const &node,
        clang::DeclarationName const &name) {

    FOUT << "[INFO](fnParametermatch) declname\n";
    auto const *fn = getContainerFunctionDecl(context, node);
    assert(fn);
    if(!fn) {
        FOUT << "[INFO](fnParametermatch) fn == nullptr\n";
    }
    if(auto parmPos = getParameterMatch(fn, name)) {
        FOUT << "[INFO](fnParametermatch) parmPos: " << parmPos.value() << "; " << toString(context, *fn, *parmPos) << "\n";
        return toString(context, *fn, *parmPos);
    }

    FOUT << "[Warn](fnParametermatch) parmPos nullopt.\n";
    return "(Not a param)";
}

template<typename T>
std::string functionParameterMatch(
        clang::ASTContext &context,
        T const &node,
        clang::DeclarationNameInfo const &nameInfo) {

    FOUT << "[INFO](fnParametermatch) declnameinfo\n";
    return functionParameterMatch(context, node, nameInfo.getName());
}

std::string functionParameterMatch(
        clang::ASTContext &context,
        clang::VarDecl const &var) {

    if(var.isLocalVarDecl()) {
        return "(Not a param)";
    }

    FOUT << "[INFO](fnParametermatch) vardecl\n";
    if(var.isLocalVarDeclOrParm()) {
        return functionParameterMatch(context, var, var.getDeclName());
    }

    return "(No_Impl_Yet!)";
}

DeclData buildDeclData(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::VarDecl const &var) {

    FOUT << "[INFO](buildDeclData<VarDecl>) var: "
         << toString(context, var) << "; type: "
         << typeof(context, var) << "\n";

    return {
        toString(context, var),
        typeof(context, var),
        getTypeCategoryName(context, var),
        functionParameterMatch(context, var),
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
         << toString(context, d) << "; type: "
         << typeof(context, d) << "\n";

    return {
        toString(context, d),
        typeof(context, d),
        getTypeCategoryName(context, d),
        functionParameterMatch(context, e, d.getDeclName()),
        getContainerFunction(context, e),
        d.getLocation().printToString(sm)
    };
}

DeclData buildDeclData(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CastExpr const &castExpr,
        clang::UnaryOperator const &op) {

    FOUT << "[INFO](buildDeclData<UnaryOp>) op: "
         << toString(context, op) << "; type: "
         << typeof(context, op) << "\n";

    return {
        toString(context, op),               // dd_expr
        typeof(context, op),                 // type
        getTypeCategoryName(context, op),    // category
        "(TODO param_check)",                // linkedParameter
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

    return d;
    */
}

/*
DeclData buildDeclData(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CastExpr const &castExpr,
        clang::DeclRefExpr const &e) {

    FOUT << "[INFO](buildDeclData<DeclRef>) ref: "
         << toString(context, e) << "; type: "
         << typeof(context, e) << "\n";

    return {
        toString(context, e),                                       // dd_expr
        typeof(context, e),                                         // type
        getTypeCategoryName(context, e),                            // category
        functionParameterMatch(context, castExpr, e.getNameInfo()), // linkedParameter
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

// TODO: probably remove/rename
DeclData buildDeclData(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CastExpr const &castExpr,
        clang::DeclRefExpr const &e) {

    FOUT << "[INFO](buildDeclData<DeclRef2>) ref: "
         << toString(context, e) << "; type: "
         << typeof(context, e) << "\n";

    auto const *stmt = e.getExprStmt();
    auto const *decl = e.getDecl();
    if(!!decl) {
        FOUT << "[INFO](buildDeclData) Building data from Decl from DeclRefExpr\n";
        return {
            toString(context, e),
            typeof(context, e),
            getTypeCategoryName(context, e),
            functionParameterMatch(context, *decl, e.getNameInfo()),
            getContainerFunction(context, castExpr),
            castExpr.getExprLoc().printToString(sm),
        };
    }

    if(!!stmt) {
        FOUT << "[INFO](buildDeclData) Building data from Expr stmt from DeclRefExpr\n";
        return {
            toString(context, e),                                       // dd_expr
            typeof(context, e),                                         // type
            getTypeCategoryName(context, e),                            // category
            functionParameterMatch(context, *stmt, e.getNameInfo()),    // linkedParameter
            getContainerFunction(context, castExpr),
            castExpr.getExprLoc().printToString(sm)
        };
    }

    FOUT << "[ERROR](buildDeclData) DeclRefExpr has no decl or stmt.\n";

    return {
        toString(context, e),
        typeof(context, e),
        getTypeCategoryName(context, e),
        "(No Param match due to declrefexpr error)",
        getContainerFunction(context, castExpr),
        castExpr.getExprLoc().printToString(sm),
    };
}

DeclData buildDeclData(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CastExpr const &castExpr,
        clang::DeclStmt const &e) {

    FOUT << "[INFO](buildDeclData<Declstmt>) ds: "
         << toString(context, e) << "; type: "
         << typeof(context, castExpr) << "\n";

    return {
        toString(context, e),
        typeof(context, castExpr),
        getTypeCategoryName(context, castExpr),
        "(Not a param)",
        getContainerFunction(context, castExpr),
        e.getEndLoc().printToString(sm)
    };
}

DeclData buildDeclData(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CastExpr const &castExpr,
        clang::CallExpr const &e) {

    FOUT << "[INFO](buildDeclData<callExpr>) call: "
         << toString(context, e) << "\n";

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
            "(no parm found)",
            typeof(context, castExpr),
            getTypeCategoryName(context, castExpr),
            (argPos > e.getNumArgs())
                ? ("(Failed to match arg)")
                : (toString(context, e, argPos - 1)),
            getContainerFunction(context, castExpr),
            e.getExprLoc().printToString(sm)
        };
    }

    return {
        parmId,
        typeof(context, *parm),
        getTypeCategoryName(context, castExpr),
        (argPos > e.getNumArgs())
            ? ("(Failed to match arg)")
            : (toString(context, e, argPos - 1)),
        getContainerFunction(context, castExpr),
        e.getExprLoc().printToString(sm)
    };
}

std::string getLinkedFunction(
        clang::ASTContext const &context,
        clang::CastExpr const &castExpr,
        clang::DeclStmt const&) {

    return "N/A";
}

std::string getLinkedFunction(
        clang::ASTContext const &context,
        clang::CastExpr const &castExpr,
        clang::UnaryOperator const&) {

    return "N/A";
}

std::string getLinkedFunction(
        clang::ASTContext &context,
        clang::CastExpr const &castExpr,
        clang::CallExpr const &call) {

    auto const *calledFn = call.getDirectCallee();
    assert(calledFn);
    if(!calledFn) {
        return "[ERROR](getLinkedFunction) callee == nullptr";
    }

    return calledFn->getNameAsString();
}

enum class CastSourceTypes {
    UnaryOp,        // &i -> i
    Function,       // void (*)() -> void f()
    FunctionArg     // f((void*)&i) -> f.$0
};

template<CastSourceTypes s_type>
CensusDecl const* getSourceDecl(
        clang::ASTContext const &context,
        clang::DeclRefExpr const &source);

template<>
CensusDecl const* getSourceDecl<CastSourceTypes::UnaryOp>(
        clang::ASTContext const &context,
        clang::DeclRefExpr const &source) {

    FOUT << "[INFO](getSourceDecl<UnaryOp>)\n";
    auto const *decl = source.getDecl();
    return dyn_cast<clang::VarDecl>(decl);
}

template<>
CensusDecl const* getSourceDecl<CastSourceTypes::Function>(
        clang::ASTContext const &context,
        clang::DeclRefExpr const &source) {

    FOUT << "[INFO](getSourceDecl<Function>)\n";
    auto const *decl = source.getDecl();
    return dyn_cast<clang::FunctionDecl>(decl);
}

template<>
CensusDecl const* getSourceDecl<CastSourceTypes::FunctionArg>(
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

CensusDecl const* getTargetDecl(
        clang::ASTContext const &context,
        clang::CastExpr const &castExpr,
        clang::DeclStmt const &t) {

    return t.getSingleDecl();  // TODO examples where decls() or DeclGroup() may be needed.
}

CensusDecl const* getTargetDecl(
        clang::ASTContext const &context,
        clang::CastExpr const &castExpr,
        clang::CallExpr const &t) {

    // TODO CHK: If castexpr is a bitCast, then we have arg match
    //         otherwise it could just be fptr decay or l->r value.

    FOUT << "[INFO](getTargetDecl<callExpr>) call: "
         << toString(context, t) << "\n";

    FOUT << "[INFO](getTargetDecl<callExpr>) castExpr: "
         << toString(context, castExpr) << "\n";

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
std::ostream& dump(std::ostream &os, Dominators const &doms) {
    for(auto const &d: doms) {
        dump(os, d);
    }

    return os;
}

void dump(std::ostream &os, DeclInfo const &d) {
    auto const &[decl, dominators] = d;
    std::stringstream ss;
    dump(ss, decl);
    if(!dominators) {
        os << ss.str();
        return;
    }
    dump(os, dominators.value());
    os << " ---> " << ss.str();
}

std::ostream& dump(std::ostream &os, DeclData const &info) {
    os << "{dd_expr: '" << info.dd_expr << "', type: '" << info.type << "', "
       << "category: '" << info.category << "', linkedParameter: '" << info.linkedParameter << "'}\n";
    return os;
}

// TODO: Redo. Location should be from decldata not dominator.decldata.
std::ostream& dump(std::ostream &os, DominatorData const &info) {
    std::stringstream ss;
    dump(ss, info.fromData);
    os << "{fromData: '" << ss.str() << "', expr: '" << info.expr << "', "
       << "exprType: '" << info.exprType << "', "
       << "containerFunction: '" << info.fromData.linkedFunction << "', "
       << "linkedFunction: '" << info.linkedFunction.value_or("(none)") << "', "
       << "location: '" << info.fromData.location << "'}\n";

    return os;
}

//void declSummary(std::ostream &os, DeclData const &data, std::optional<Dominators const >const &doms) {
void declSummary(std::ostream &os, DeclData const &data) {
    os << "[" << data.category << "] " << data.dd_expr << ": '" << data.type << "'"
       << " " << data.linkedParameter;
    /*
    if(doms) {
        for(auto const &d: doms) {
            os << "<" << dom->containerFunction << ">: " << dom->location << ">";
        }
    }
    */
}

void censusSummary(std::ostream &os, unsigned decl, DeclData const &declData, int indent) {
    assert(decl);
    std::string ws(indent, ' ');
    if(!decl) {
        os << "<end>}\n";
        return;
    }

    static std::vector<unsigned> seenDecls;
    auto it = std::find(std::begin(seenDecls), std::end(seenDecls), decl);
    if(it != std::end(seenDecls)) {
        /*
        os << "[INFO](censusSummary) reached seen decl: " << decl
           << "\n[INFO](censusSummary) popping all decls since\n";
        */
        os << "{ re [" << decl << "] ";
        declSummary(os, declData);
        os << "\n" << ws << "-> <rend>}\n";
        seenDecls.erase(it, std::end(seenDecls));
        return;
    }

    //os << "[INFO](censusSummary) new decl to dump: " << decl << "\n";
    seenDecls.push_back(decl);
    os << "{ [" << decl << "] ";
    declSummary(os, declData);

    auto doesDeclDominate = [&decl](auto const &censusNode) {
        auto const &[_, info] = censusNode;
        auto const &[__, nodeDoms] = info;
        if(!nodeDoms)
            return false;
        auto const &doms = nodeDoms.value();
        return std::find(std::begin(doms), std::end(doms),
                decl) != std::end(doms);
    };

    std::vector<CensusNode> rchain;
    std::copy_if(std::begin(census), std::end(census), std::back_inserter(rchain), doesDeclDominate);

    if(rchain.empty()) {
        os << "\n" << ws << "-> <end>}\n";
        return;
    }

    for(auto const &[rdecl, rinfo]: rchain) {
        os << "\n" << ws << "-> ";
        auto const &[rdata, _] = rinfo;
        censusSummary(os, rdecl, rdata, indent+2);
    }
    os << ws << "}\n";
}

/*
void buildCastChain(CensusDecl const *decl, DeclData const &data, CastChain chain) {
    auto doesDeclDominate = [&decl](auto const &censusNode) {
        auto const &[_, info] = censusNode;
        auto const &[__, d] = info;
        if(!d)
            return false;
        auto const &doms = d.value();
        return std::find(std::begin(doms), std::end(doms),
                decl) != std::end(doms);
    };

    std::vector<CensusNode> c;
    std::copy_if(std::begin(census), std::end(census), std::back_inserter(c), doesDeclDominate);
    if(c.empty()) {
        return;
    }

    for(auto const &[rdecl, rinfo]: c) {
        auto const &[rdata, _] = rinfo;
}

void buildCastHistory(CensusDecl const *decl, DeclData const &data) {

    auto doesDeclDominate = [&decl](auto const &censusNode) {
        auto const &[_, info] = censusNode;
        auto const &[__, d] = info;
        if(!d)
            return false;
        auto const &doms = d.value();
        return std::find(std::begin(doms), std::end(doms),
                decl) != std::end(doms);
    };

    std::vector<CensusNode> chain;
    std::copy_if(std::begin(census), std::end(census), std::back_inserter(chain), doesDeclDominate);
    if(chain.empty()) {
        return;
    }

    for(auto const &[rdecl, rinfo]: chain) {
        auto const &[rdata, _] = rinfo;
        CastChain rchain;
        rchain.push_back({rdecl, rdata});
        buildCastChain(rdecl, rdata, rchain);
    }
}

void buildCastHistory() {
    for(auto const &[decl, info]: census) {
        auto const &[data, _] = info;
        buildCastHistory(decl, data);
    }
}
*/

void censusSummary(std::ostream &os) {
    for(auto const &[decl, info]: census) {
        auto const &[data, _] = info;

        censusSummary(os, decl, data);
        os << "\n";
    }
}
//---
void preprocess(
        clang::ASTContext &context,
        clang::DeclStmt const &decl) {}

void preprocess(
        clang::ASTContext &context,
        clang::UnaryOperator const &decl) {}

void preprocess(
        clang::ASTContext &context,
        clang::CallExpr const &call) {

    // Trigger cast check for the called function.
    auto const *calledFn = call.getDirectCallee();
    if(!calledFn) {
        FOUT << "[ERROR](preprocess) call.DirectCallee == nullptr\n";
        return;
    }
    assert(calledFn);

    if(calledFn->hasBody()) {
        auto const *body = calledFn->getBody();
        assert(body);
        MatchFinder m;
        m.match(*body, context);
    }
}

using DeclInfoSingleDom = std::pair<DeclData, std::optional<DominatorData>>;
void updateCensus(
        clang::ASTContext &context,
        CensusDecl const *domDecl,
        DeclInfo domInfo,
        CensusDecl const *decl,
        DeclInfoSingleDom info) {
    assert(decl);

    if(!!domDecl) {
    if (census.find(HASH(domDecl)) == std::end(census)) {
        census.insert({HASH(domDecl), domInfo});
    } else {
        // Dominator is already in census. 
        //  - Check that DomData is same.
        auto const &[oldData, _] = census[HASH(domDecl)];
        auto const &[newData, __] = domInfo;
        if(oldData != newData) {
            FOUT << "[WARN](updateCensus) domDecl in Census with different DeclData\n";
        }
        //  - Dominator data (of DomDecl)  will be unchanged.
        // Nothing to do.
    }
    }

    if(!decl) return;

    auto const &[newData, newDomData] = info;
    if (census.find(HASH(decl)) == std::end(census)) {
        if(newDomData) {
            census.insert({HASH(decl), {newData, {{newDomData.value()}}}});
        }
        else {
            census.insert({HASH(decl), {newData, {{}}}});
        }

    } else {
        // Decl is already in census.
        //  - Check that DeclData is same.
        auto &[oldData, oldDoms] = census[HASH(decl)];

        // Check existing decl data
        if(oldData != newData) {
            FOUT << "[WARN](updateCensus) decl in Census with different DeclData\n";
            FOUT << "[WARN](updateCensus) Old decl Data: {\n";
            dump(FOUT, oldData);
            FOUT << "[WARN](updateCensus) }\n";
            FOUT << "[WARN](updateCensus) New decl Data: {\n";
            dump(FOUT, newData);
            FOUT << "[WARN](updateCensus) }\n";
            return;
        }

        // If DominatorData is not present, update.
        if(!oldDoms && newDomData) {
            census[HASH(decl)] = {newData, {{newDomData.value()}}};

        } else if(newDomData) {
            auto &doms = oldDoms.value();
            doms.push_back(newDomData.value());
            FOUT << "[INFO](updateCensus) New Dominator Appended: {\n";
            dump(FOUT, newDomData.value());
            FOUT << "[INFO](updateCensus) }\n";
        }
    }
}

/*
template<typename Target_t, CastSourceTypes s_type>
void updateCensus(clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CastExpr const &castExpr,
        clang::DeclRefExpr const &castSource,
        Target_t const *dest) {

    assert(dest);
    preprocess(context, *dest);

    //auto const *lhs = getSourceDecl<s_type>(context, castSource);
    clang::Decl const *lhs = castSource.getDecl();
    if(!lhs) {
        FOUT << "[ERROR](updateCensus) LHS Decl == nullptr\n";
        auto lhs_ = castSource.getFoundDecl();
        if (!lhs_) {
            FOUT << "[ERROR](updateCensus) LHS' Decl == nullptr\n";
        }
        lhs = lhs_;
    }
    auto lhsData = buildDeclData(context, sm, castExpr, castSource);
}
*/

template<CastSourceTypes CS_t, typename T>
void updateCensus(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CastExpr const &castExpr,
        clang::DeclRefExpr const &castSource,
        T const &dest) {

    preprocess(context, dest);

    //clang::Decl const *lhs = castSource.getDecl();
    clang::Decl const *lhs = getSourceDecl<CS_t>(context, castSource);
    /*
    if(!lhs) {
        FOUT << "[ERROR](updateCensus) LHS Decl == nullptr\n";
        auto lhs_ = castSource.getFoundDecl();
        if (!lhs_) {
            FOUT << "[ERROR](updateCensus) LHS getFound Decl also == nullptr\n";
        }
        else {
            FOUT << "[INFO](updateCensus) LHS getFound Decl == "
                << static_cast<void const*>(lhs_)
                << "; " << \n";
        }
        lhs = lhs_;
    }
    */
    if(!lhs) {
        FOUT << "[INFO](updateCensus) LHS Decl == nullptr\n";
        auto rhs = getTargetDecl(context, castExpr, dest);
        if(!rhs) {
            FOUT << "[ERROR](updateCensus) RHS Decl == nullptr\n";
            return;
        }
        auto rhsData = buildDeclData(context, sm, castExpr, dest);
        DominatorData dd{
            0,
            {},
            toString(context, castExpr),
            getCastExprType(dest),
            getLinkedFunction(context, castExpr, dest),        // DominatorData.linkedFunction
        };
        FOUT << "[INFO](updateCensus) Updating RHS Data\n";
        updateCensus(context, lhs, {{},{}}, rhs, {rhsData, dd});
        FOUT << "Cast site: " << rhsData.location << "\n"
             << "    Casting: (nullptr)  -> "
             << "(" << static_cast<void const*>(rhs) << ")"
             << rhsData.dd_expr << "\n"
             << "       from: []\n"
             << "         to: [" << rhsData.category << "] " << rhsData.type << "\n"
             << "       expr: " << "[" << dd.exprType << "] " << dd.expr << "\n"
             << "\n";
        return;
    }

    auto lhsData = buildDeclData(context, sm, castExpr, castSource);

    auto rhs = getTargetDecl(context, castExpr, dest);
    if(!rhs) {
        FOUT << "[ERROR](updateCensus) RHS Decl == nullptr\n";
    }
    auto rhsData = buildDeclData(context, sm, castExpr, dest);

    DominatorData rhsDom{
       HASH( lhs),                                                // DominatorData.from
        lhsData,                                            // DominatorData.fromData
        toString(context, castExpr),                        // DominatorData.expr
        getCastExprType(dest),                             // DominatorData.exprType
        getLinkedFunction(context, castExpr, dest),        // DominatorData.linkedFunction
    };

    if(!rhs) return;

    updateCensus(context, lhs, {lhsData,{}}, rhs, {rhsData, rhsDom});

    FOUT << "Cast site: " << rhsData.location << "\n"
//         << "    Casting: " << lhsData.dd_expr << " -> " << rhsData.dd_expr << "\n"
         << "    Casting: (" << static_cast<void const*>(lhs) << ")<"
         << HASH(lhs) << ">"
         << lhsData.dd_expr << " -> "
         << "(" << static_cast<void const*>(rhs) << ")<"
         << HASH(rhs) << ">"
         << rhsData.dd_expr << "\n"
         << "       from: [" << lhsData.category << "] " << lhsData.type << "\n"
         << "         to: [" << rhsData.category << "] " << rhsData.type << "\n"
         << "       expr: " << "[" << rhsDom.exprType << "] " << rhsDom.expr << "\n"
         << "\n";
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

    //auto const *var = result.Nodes.getNodeAs<DeclStmt>("var");
    //auto const *gexpr = result.Nodes.getNodeAs<Expr>("gexpr");
    //auto const *binop = result.Nodes.getNodeAs<BinaryOperator>("binop");

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
        updateCensus<CastSourceTypes::UnaryOp>(*context, *result.SourceManager, *castExpr, *s_unaryCastee, *unaryOp);
    }
    else if (!!call) {
        updateCensus<CastSourceTypes::FunctionArg>(*context, *result.SourceManager, *castExpr, *s_callArg, *call);
    }
    else if(!!fptr) {
        updateCensus<CastSourceTypes::Function>(*context, *result.SourceManager, *castExpr, *s_fptrRef, *fptr);
    }

    /*
    } else if(!!binop) {
        callee = containerFn;
        cast.second = binop;
        castExprType = "binop";
        dest = toString(context, binop);
    */
    /*
    } else if(!!gexpr) {
        cast.second = gexpr;
    }
    */

}

void processVar(MatchFinder::MatchResult const &result) {
    assert(result);
    auto *context = result.Context;
    assert(context);

    // [VarDecl]    [DeclRefExpr]
    // [int *pi2] = [pi]
    //  {pi -> p2}
    //  LHS    RHS

    auto const *rhs = result.Nodes.getNodeAs<clang::VarDecl>("varDecl");
    assert(rhs);
    auto rhsData = buildDeclData(*context, *result.SourceManager,  *rhs);

    auto const *lhsRef = result.Nodes.getNodeAs<clang::DeclRefExpr>("assignee");
    auto const *lhsLit = result.Nodes.getNodeAs<clang::Expr>("literal");
    if(!lhsRef || lhsLit) {
        updateCensus(*context, nullptr, {{}, {}}, rhs, {rhsData, {}});
        return;
    }

    assert(lhsRef);
    auto const *lhs = lhsRef->getDecl();
    /*
    if(!lhs) {
        FOUT << "[ERROR](processVar) LHS getDecl == nullptr\n";
        auto lhs_ = lhsRef->getFoundDecl();
        if(!lhs_) {
            FOUT << "[ERROR](processVar) LHS' getFoundDecl == nullptr\n";
        }
        //lhs = lhs_;
    }
    */
    if(!lhs) {
        FOUT << "[INFO](processVar) LHS Decl == nullptr\n";
        if(!rhs) {
            FOUT << "[ERROR](processVar) RHS Decl == nullptr\n";
            return;
        }
        DominatorData dd{
            0,
            {},
            "null lhs-assign",
            "no type-assign",
            {},        // DominatorData.linkedFunction
        };
        FOUT << "[INFO](processVar) Updating RHS Data\n";
        updateCensus(*context, lhs, {{},{}}, rhs, {rhsData, dd});
        FOUT << "Assignment site: " << rhsData.location << "\n"
    //         << "    Casting: " << lhsData.dd_expr << " -> " << rhsData.dd_expr << "\n"
             << "    Assigning: (nullptr)" << " -> "
             << "(" << static_cast<void const*>(rhs) << ")"
             << rhsData.dd_expr << "\n"
             << "       from: []\n"
             << "         to: [" << rhsData.category << "] " << rhsData.type << "\n"
             << "       expr: " << "[" << dd.exprType << "] " << dd.expr << "\n"
             << "\n";
        return;
    }


    // We can have FunctionDecl too
    /*
    auto lhsDecl = dyn_cast<VarDecl>(lhsRef->getDecl());
    if(!lhsDecl) {
        FOUT << "[INFO](processVar) dyncast LHS Decl == nullptr\n";
        return;
    }
    */

    //auto const &location = rhs->getLocation().printToString(*result.SourceManager);

    auto lhsData = buildDeclData(*context, *result.SourceManager, *lhsRef, *lhs);
    DominatorData domData{
        HASH(lhs),                 // DominatorData.from
        lhsData,                   // DominatorData.fromData
        {},                        // DominatorData.expr
        {},                        // DominatorData.exprType
        {},                        // DominatorData.containerFunction
    };

    updateCensus(*context, lhs, {lhsData, {}}, rhs, {rhsData, domData});
    std::hash<CensusDecl> hasher;
    FOUT << "Assignment site: " << rhsData.location << "\n"
         << "    Assigning: (" << static_cast<void const*>(lhs) << ")<"
         << hasher(lhs) << ">"
         << lhsData.dd_expr << " -> "
         << "(" << static_cast<void const*>(rhs) << ")<"
         << hasher(rhs) << ">"
         << rhsData.dd_expr << "\n"
         << "       from: [" << lhsData.category << "] " << lhsData.type << "\n"
         << "         to: [" << rhsData.category << "] " << rhsData.type << "\n"
         << "       expr: " << "[" << domData.exprType << "] " << domData.expr << "\n"
         << "\n";
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

                /*
                allOf(
                    hasCastKind(CK_FunctionToPointerDecay),//LValueToRValue),
                    hasAncestor(
                        callExpr().bind("fptr")),
                    hasDescendant(declRefExpr().bind("callee"))),
                */

                hasDescendant(
                    unaryOperator(
                        hasDescendant(declRefExpr().bind("unaryCastee"))
                        ).bind("unaryOp")))
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
        auto *context = result.Context;
        assert(context);

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

