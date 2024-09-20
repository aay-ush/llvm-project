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

// If the LHS is fn ptr, consider skipping the census update.
//  -> Can't, since fptr can be passed as arg which are needed in census.
//
// TODO TODO: resolve fnptr call to (*fnptr) call. 
//  (*comp)(a, b) -> should link a -> (*comp).$0
//  Currently, since *comp is not resolved, parm info is not available.

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
    CNS_DEBUG("<T>");
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
    CNS_DEBUG("<T> end.");
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

struct OpData {
    unsigned hash_ {0};
    std::string expr_;
    std::string type_;
    std::string category_;
    std::string linkedParm_;
    std::string container_;
    std::string location_;
    mutable std::vector<unsigned> use_ {};
};
bool operator==(OpData const &lhs, OpData const &rhs) {
    return lhs.hash_ == rhs.hash_;
}

bool operator!=(OpData const &lhs, OpData const &rhs) {
    return !(lhs == rhs);
}

void OpDebugSummary(std::ostream &os, OpData const &data) {
    os << "[" << data.category_ << "](" << data.hash_ << ") " << data.expr_ << ": '" << data.type_ << "'"
       << " " << data.linkedParm_ << " in " << data.container_ << "()";
}

void OpSummary(std::ostream &os, OpData const &data) {
    os << "[" << data.category_ << "]" << data.expr_ << ": '" << data.type_ << "'"
       << " " << data.linkedParm_ << " at " << data.location_.substr(
               data.location_.find_last_of('/') + 1);
}

void OpTypeSummary(std::ostream &os, OpData const &data) {
    os << "'" << data.type_ << "'" << " " << data.linkedParm_;
}
/*
struct ParameterData {
    unsigned argPos_;
    std::string argType_;
    std::string argName_;
};
struct FunctionInfo {
    clang::DeclarationNameInfo name_;
    std::vector<ParameterData> params_;
};

std::unordered_map<unsigned, FunctionInfo> Functions;
*/
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

struct DominatorData {
    OpData from_;
    std::string expr_;
    std::string exprType_;
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
using NodeInfo = std::pair<OpData, std::optional<Dominators>>;

// Census stores OpData & DominatorData for every operand/node.
// OpData is not necessarily the key for census.
// OpData is computed based on information available from context at the time.
//  => It can change with expression. For example, `int i` has same hash for `i` and `&i` but different data types, hence technically different decl data. If OpData is used as key, this difference will be missed.
using Census = std::unordered_map<unsigned, NodeInfo>;
Census census;
using CensusNode = decltype(census)::value_type;

std::pair<unsigned, NodeInfo> makeCensusNode(
        OpData const &node,
        DominatorData const &dom) {
    return {node.hash_, {node, {{dom}}}};
}

std::pair<unsigned, NodeInfo> makeCensusSourceNode(OpData const &node) {
    return {node.hash_, {node, {}}};
}

NodeInfo makeNodeInfo(
        OpData const &node,
        DominatorData const &dom) {
    return {node, {{dom}}};
}

// ---- Building Histree ----
// Census holds a collection of operand with its dominator(s):
//      Census = {<op, [dom]>}
// Hence, every target node has at least one history item.
// Source node may have 0, 1 or more history item(s).
//
// OpHistory = [Dominators] ?
// Wrong PoV.
//
// We need to look at USE rather than history.
// i.e. we need to see from Dominator node where it is used.
//      void *pv;
//      int *pi = (int*) pv
//      => Use(pv) = [pi]
//      However, just node information is not enough:
//      void *pv3 = (void*) pi;
//        Use(pv) = [pi, Use(pi)] = [pi, pv3, Use(pv3)]
//        void* -> int* -> void*
//
//      int *pi2 = pi;
//      => Use(pv) = [pi, Use(pi)]
//
//      This representation covers divergent nodes too:
//      void *pv2 = pv;
//      => Use(pv) = [pi, Use(pi), pv2, Use(pv2)]
//                 ~ {pi, pi2, pv2}
//
// UseChain: Since a census node is {node, dominator}, given a node, we can't tell
//    what is Use(node). We can only map Use(dominator) = [Use(node)]
// In order to valuate Use(node), we need to build a use-chain for 'node', i.e. vector of all nodes that are directly dominated by input node:
//      UseChain(pv, census) = [pi, pv2]
//      Use(pv)              = [Use(pi), Use(pv2)]
//                           = [Use(x) | x `belongs to` UseChain(pv, census)]
//
//  usechain(pi) = [pv2]
//  use(pv2) = x
//  use(pi) = [pv2]
//  use(pv2) = [pi]
//
// UseChain(a: CensusNode, census: Census) -> [OpData] {
//      for(i in census) {
//          for(d in doms(i)) {
//              if(d == a)
//                  UseChain(a) cons i
//          }
//      }
// }
//
// Use(a, census) -> [] {
//      for(i in UseChain(a, census)) {
//          Use(a) cons Use(i)
//      }
//
// Problem: Use(i) may not be defined when use(a) is invoked.
// -> Add Use field in opData. 
//      UseChain(a) is always defined.
//      Use(a): visit each element in vector UseChain(a) recursively to check if optional Use field is defined for i.
//      Stop when not.
//
// TODO:
//  Add a depth control parameter to elaborateUse() to control recursion depth.
//
// ---- Building Histree ----

std::optional<Dominators> const& doms(CensusNode const &n) {
    CNS_DEBUG("");
    auto const &[_, info] = n;
    auto const &[__, doms_] = info;
    CNS_DEBUG("end.");
    return doms_;
}

OpData const& ops(CensusNode const& n) {
    CNS_DEBUG("<CensusNode>");
    auto const &[_, info] = n;
    auto const &[op, __] = info;
    //FOUT << "[DEBUG](ops<CensusNode>) Returning op (" << op.hash_ << ")\n";
    CNS_DEBUG("<CensusNode> end.");
    return op;
}

OpData const& ops(unsigned hash) {
    CNS_DEBUG("<hash>");
    auto const &[op, __] = census[hash];
    //FOUT << "[DEBUG](ops<hash>) Returning op (" << op.hash_ << ")\n";
    CNS_DEBUG("<hash> end.");
    return op;
}

unsigned const& opHash(CensusNode const& n) {
    CNS_DEBUG("");
    auto const &op = ops(n);
    //FOUT << "[DEBUG]("opHash) Returning hash (" << op.hash_ << ")\n";
    CNS_DEBUG(" end.");
    return op.hash_;
}

std::vector<unsigned> UseChain(OpData const &op) {
    CNS_DEBUG("");
    auto doesOpDominate = [&op](CensusNode const &in) {
        CNS_DEBUG("");
        //FOUT << "[DEBUG](UseChain) Op.hash_ = " << op.hash_ << "\n";
        auto const &doms_ = doms(in);
        if(!doms_)
            return false;
        auto const &doms = doms_.value();
        auto match = std::find_if(std::begin(doms), std::end(doms),
                [&op](auto const &d) {
                    return op == d.from_;
                });
        if(match != std::end(doms)) {
            //FOUT << "[DEBUG](UseChain) Op dominates (" << in.second.first.hash_ << ")\n";
        }
        CNS_DEBUG("end.");
        return match != std::end(doms);
    };

    std::vector<CensusNode> t;
    std::copy_if(std::begin(census), std::end(census), std::back_inserter(t), doesOpDominate);
    std::vector<unsigned> usechain;
    std::transform(std::begin(t), std::end(t), std::back_inserter(usechain), opHash);
    CNS_DEBUG("end.");
    return usechain;
}

void elaborateUse(OpData const &node, std::optional<int> level) {
    CNS_DEBUG("");
    node.use_.clear();
    // Use(node) = [i, Use(i) | i in UseChain(node)]
    auto const &usechain = UseChain(node);
    //FOUT << "[DEBUG](elaborateUse) Building use chain for (" << node.hash_ << ")\n";
    for(auto const &hash: usechain) {
        if(level && (level.value() > 0)) {
            //FOUT << "[DEBUG](elaborateUse) Level = " << level.value() << "\n";
            elaborateUse(ops(hash), level.value() - 1);
        }

        //FOUT << "[DEBUG](elaborateUse) Adding hash(" << hash << ")\n";
        // Add the node 'p' from usechain
        node.use_.push_back(hash);
        // Add use(p)
        if(level && (level.value() > 0)) {
            std::copy(begin(ops(hash).use_), end(ops(hash).use_), back_inserter(node.use_));
        }
    }
    CNS_DEBUG("end.");
}

std::ostream& space(std::ostream &os, int indent);
void OpSummary(std::ostream &os, OpData const &data);

void censusSummary() {
    CNS_DEBUG("<void>");
    for(auto const &[_, info]: census) {
        auto const &[op, __] = info;

        elaborateUse(op, {1});
        OpSummary(FOUT, op);
        FOUT << "\n";
        int indent = 0;
        for(auto const& u: op.use_) {
            space(FOUT, indent + 2);
            //FOUT << "-> {" << u << "}";
            FOUT << "->";
            OpSummary(FOUT, census[u].first);
            FOUT << "\n";
        }
    }
    CNS_DEBUG("<void> end.");
}

std::ostream& dump(std::ostream &os, NodeInfo const &d);
std::ostream& dump(std::ostream &os, OpData const &info);
std::ostream& dump(std::ostream &os, Dominators const &doms);
std::ostream& dump(std::ostream &os, DominatorData const &info);

void censusSummary(std::ostream &os);
void censusSummary(std::ostream &os, OpData const& data, int indent = 0);
//--
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
        var.getLocation().printToString(sm)
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
        {}
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
        {}
    };

    /*
    switch(op.getOpcode()) {
        case UO_Deref:
            FOUT << "[INFO](buildOpData<UnaryOp>) op: *\n";
            break;
        case UO_AddrOf:
            FOUT << "[INFO](buildOpData<UnaryOp>) op: &\n";
            break;
        default:
            FOUT << "[INFO](buildOpData<UnaryOp>) op: -\n";
    };
    */
}

/*
OpData buildOpData(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CastExpr const &castExpr,
        clang::DeclRefExpr const &e) {

    FOUT << "[INFO](buildOpData<DeclRef>) ref: \n"
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
OpData buildOpData(
        clang::ASTContext &context,
        clang::CastExpr const &castExpr,
        clang::UnaryOperator const &e) {
}
*/
OpData buildOpData(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CastExpr const &castExpr,
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
        {}
    };
}

OpData buildOpData(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CastExpr const &castExpr,
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
        {}
    };
}

// TODO: probably remove/rename
OpData buildOpData(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::CastExpr const &castExpr,
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
        {}
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
        e.getExprLoc().printToString(sm)
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
        ss << "{" << String(context, *c) << ".$" << argPos - 1;

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

    CNS_DEBUG("<UnaryOp>");
    auto const *decl = source.getDecl();
    CNS_DEBUG("<UnaryOp> end.");
    return dyn_cast<clang::VarDecl>(decl);
}

template<>
CensusDecl const* getSourceDecl<CastSourceType::BinaryOp>(
        clang::ASTContext const &context,
        clang::DeclRefExpr const &source) {

    CNS_DEBUG("<BinaryOp>");
    CNS_DEBUG("<BinaryOp> end.");
    return source.getDecl();
}

template<>
CensusDecl const* getSourceDecl<CastSourceType::Function>(
        clang::ASTContext const &context,
        clang::DeclRefExpr const &source) {

    CNS_DEBUG("<Function>");
    auto const *decl = source.getDecl();
    CNS_DEBUG("<Function> end.");
    return dyn_cast<clang::FunctionDecl>(decl);
}

template<>
CensusDecl const* getSourceDecl<CastSourceType::FunctionArg>(
        clang::ASTContext const &context,
        clang::DeclRefExpr const &source) {

    CNS_DEBUG("<FunctionArg>");
    auto const *decl = source.getDecl();
    CNS_DEBUG("<FunctionArg> end.");
    return dyn_cast<clang::ParmVarDecl>(decl);
}

CensusDecl const* getTargetDecl(
        clang::ASTContext const &context,
        clang::CastExpr const &castExpr,
        clang::UnaryOperator const &op) {

    CNS_DEBUG("<UnaryOp>");

    //auto const *t1 = op.getReferencedDeclOfCallee();
    //auto const *t2 = op.getAsBuiltinConstantDeclRef(context);
    auto const *t3 = castExpr.getReferencedDeclOfCallee();
    auto const *t4 = castExpr.getAsBuiltinConstantDeclRef(context);

    /*
    if(!!t1) {
        CNS_INFO("<Unary> t1");
        return t1;
    }
    if(!!t2) {
        CNS_INFO("<Unary> t2");
        return t2;
    }
    */
    if(!!t3) {
        CNS_INFO("<Unary> t3");
        return t3;
    }
    if(!!t4) {
        CNS_INFO("<Unary> t4");
        return t4;
    }

    // TODO
    // Not having a unary declaration seems fine. *pi or &i e.g.
    CNS_ERROR("<Unary> nullptr");
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

    FOUT << "[INFO](getTargetDecl<callExpr>) call: \n"
         << String(context, t) << "\n";

    FOUT << "[INFO](getTargetDecl<callExpr>) castExpr: \n"
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
    if (match != t.arg_end() && argPos <= t.getNumArgs()) {
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
        CNS_INFO("<callExpr> no parm, returning nullptr.");
        return nullptr;
    }

    CNS_INFO("<callExpr> end.");
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
std::ostream& dump(std::ostream &os, NodeInfo const &d) {
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

std::ostream& dump(std::ostream &os, OpData const &info) {
    os << "{hash: '" << info.hash_
       << "', expr: '" << info.expr_
       << "', type: '" << info.type_
       << "', category: '" << info.category_
       << "', linkedParameter: '" << info.linkedParm_
       << "', containerFunction: '" << info.container_
       << "', location: '" << info.location_
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
       << "', ExprType: '" << domInfo.exprType_
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

void censusSummary(std::ostream &os, OpData const &node, int indent) {
    bool pushed = false;
    if(!node.hash_) {
        os << "<end>}\n";
        return;
    }

    static std::vector<unsigned> seenNodes;
    auto it = std::find(std::begin(seenNodes), std::end(seenNodes), node.hash_);
    if(it != std::end(seenNodes)) {
        os << "{ (revisiting) ";
        OpSummary(os, node);
        os << "\n"; space(os, indent) << "-> <end>}\n";
        return;
    }

    pushed = true;
    //os << "<Pushing " << node.hash_ << ">\n";
    seenNodes.push_back(node.hash_);
    os << "{ (first visit) ";
    OpSummary(os, node);

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
//---
/*
void preprocess(
        clang::ASTContext &context,
        clang::DeclStmt const &decl) {}
*/

// TODO TODO
// Before preprocessing, check if function is meant to be filtered.
// Filter seen functions too.
//
void preprocess(
        clang::ASTContext &context,
        clang::UnaryOperator const &op) {
    CNS_DEBUG("<UnaryOp>");
    CNS_DEBUG("<UnaryOp> end.");
}

void preprocess(
        clang::ASTContext &context,
        clang::DeclRefExpr const &op) {
    CNS_DEBUG("<DeclRefExpr>");
    CNS_DEBUG("<DeclRefExpr> end.");
}

/*
void addFunction(
        clang::ASTContext &context,
        clang::FunctionDecl const &f) {

    CNS_DEBUG("");
    auto const nameinfo = f.getNameInfo();
    std::vector<ParameterData> params;
    unsigned pos = 0;
    std::for_each(f.param_begin(), f.param_end(), [&](auto const *p) {
            // append param data to params.
            params.push_back({pos++, Typename(context, *p), String(context, *p)});
        });
    Functions.insert({cnsHash(context, nameinfo), {nameinfo, params}});

    // TODO: maintain fp->f links such that fp can be resolved to f for f in Functions and valid fp.
    CNS_DEBUG("end");
}
*/

std::vector<unsigned> seenFunctions;
std::vector<std::string> ignoreFunctions;

void preprocess(
        clang::ASTContext &context,
        clang::CallExpr const &call) {

    CNS_DEBUG("<CallExpr>");
    // Trigger cast check for the called function.
    auto const *calledFn = getCalleeDecl(call);
    // assert still throws error for qsort. WHY?
    assert(calledFn);
    if(!calledFn) {
        CNS_INFO("<CallExpr> null callee decl");
        CNS_DEBUG("<CallExpr> end.");
        return;
    }

    auto h = cnsHash(context, *calledFn);
    if(std::find(begin(seenFunctions), end(seenFunctions), h) != end(seenFunctions)) {
        FOUT << "[INFO](preprocess<CallExpr>) Skipping processed function: " << String(context, *calledFn) << "\n";
        CNS_DEBUG("<CallExpr> end.");
        return; // already seen
    }
    if(std::find(begin(ignoreFunctions), end(ignoreFunctions), calledFn->getNameAsString()) != end(ignoreFunctions)) {
        FOUT << "[INFO](preprocess<CallExpr>) Skipping ignored function: " << String(context, *calledFn) << "\n";
        CNS_DEBUG("<CallExpr> Adding ignored function to seen functions.");
        seenFunctions.push_back(cnsHash(context, *calledFn));
        CNS_DEBUG("<CallExpr> end.");
        return; // ignore
    }
    /*
    if(calledFn) {
        FOUT << "[INFO](preprocess) Adding function : " << String(context, *calledFn) << "\n";
        addFunction(context, *calledFn);
    }
    */

    ////if(!calledFn) {
        //// Couldn't find function decl from callexpr
        //// But maybe the call args have a function pointer?
        //// If yes, now's the time to check it.
        //CNS_DEBUG("Checking for Fptr args in function.");
        //
        //std::for_each(call.arg_begin(), call.arg_end(),
            //[&] (auto const *arg) -> void{
            //CNS_DEBUG("start fparg finder.");
            //FOUT << "[DEBUG](preprocess<call>) Arg: " << String(context, *arg) << "\n";
            //auto const *argType = arg->getType().getTypePtrOrNull();
            //if(!argType) {
                //CNS_DEBUG("Arg Type could not be retrieved.");
                //CNS_DEBUG("end fparg finder.");
                //return;
            //}
            //
            //if(argType->isFunctionPointerType()) {
                //CNS_DEBUG("Found FPtr arg.");
            //
                ///*
                //auto const* dt = static_cast<clang::DecltypeType const*>(argType);
                //if(dt) {
                    //CNS_DEBUG("DecltypeType not null");
                    //auto const* d = dt->getAs<clang::NamedDecl>();
                    //if(d) {
                        //CNS_DEBUG("Really?!!!>>>>");
                    //}
                    //else {
                        //CNS_DEBUG("Unsurprising. <<<< ");
                    //}
                //}
                //else {
                    //CNS_DEBUG("DecltypeType null");
                //}
                //auto fh = cnsHash(context, *arg);
                //FOUT << "[DEBUG](preprocess<call> fpargfinder: cnshash(fparg) = " << fh << "\n";
                //auto it = Functions.find(fh);
                //auto it = std::find_if(Functions.begin(), Functions.end()
                //if (it != Functions.end()) {
                    //CNS_DEBUG(">>> Found fp arg match in fdb");
                //}
                //else {
                    //CNS_DEBUG("<<< Didn't find fp arg match in fdb");
                //}
                //*/
                //// FP Type arg.Get the decl out of it and preprocess it.
                //auto const *dre = dyn_cast<DeclRefExpr>(arg);
                //if(dre) {
                    //CNS_DEBUG(">>>DeclRefExpr Arg for Fptr. Find Dom to resolve.");
                //// at this point census probably has the dominator for this fptr.
                //// The dominator is the functionproto or the callee decl.
                //// Get the callee decl from functioncensus.
                ///*
                    //if(auto it = census.find(cnsHash(context, dre)); it != end(census)) {
                            //// 1. get the dominator function.
                            //// 2. find Functions(cnsHash(context, function.name))
                            //// 3. from Functions, find the link between function args and params.
                    //}
                    //else {
                        //CNS_INFO("Could not find function.");
                        //CNS_DEBUG("end fparg finder.");
                        //return;
                    //}
                    //*/
                //}
                //else {
                    //CNS_DEBUG("<<>DeclRefExpr Arg not found for Fptr");
                //}
            //}
            //else {
                //CNS_DEBUG("<<<Fptr arg not found.");
            //}
            //
            ////CNS_DEBUG("Arg is not a function pointer.");
            //CNS_DEBUG("end fparg finder.");
        //});
        //
        ////CNS_DEBUG("<CallExpr> end.");
        ////return;
    ////}

    if(calledFn->hasBody()) {
        auto const *body = calledFn->getBody();
        assert(body);
        MatchFinder m;
        m.match(*body, context);
    }
    seenFunctions.push_back(h);
    CNS_DEBUG("<CallExpr> end.");
}

bool isNodeDominatorNew(
        OpData const &node,
        DominatorData const &dom) {

    CNS_DEBUG("");
    auto &[_, doms_] = census[node.hash_];
    if(!doms_) {
        CNS_INFO("No doms present currently.");
        return true;
    }

    FOUT << "[INFO](isNodeDominatorNew) Checking current doms for this dom[" << dom.from_.hash_ << "]\n";
    auto doms = doms_.value();
    CNS_DEBUG(" end.");
    return std::find(begin(doms), end(doms), dom)
        == end(doms);
}

void appendNodeDominator(
        OpData const &node,
        DominatorData const &dom) {

    CNS_DEBUG("");
    auto &[_, doms_] = census[node.hash_];
    if(!doms_) {
        CNS_INFO("Dominator Initialized.");
        census[node.hash_] = makeNodeInfo(node, dom);
    } else {
        CNS_INFO("Appending to dominators.");
        auto &doms = doms_.value();
        doms.push_back(dom);
    }

    CNS_INFO("New Dominator Appended: {");
    dump(FOUT, dom);
    CNS_INFO("}");
    CNS_DEBUG("end.");
}

void chkNodeDataForChange(OpData const &node) {
    CNS_DEBUG("");
    auto const& [old, _] = census[node.hash_];
    if(old != node) {
        FOUT << "[WARN](chkNodeDataForChange) Old node with different OpData:\n"
             << "Old data:\n";
        dump(FOUT, old);
        FOUT << "\nNew data:\n";
        dump(FOUT, node);
        FOUT << "\n";
    }
    CNS_DEBUG(" end.");
}

void addDomNode(OpData const &dom) {
    CNS_DEBUG("");
    if(census.find(dom.hash_) == std::end(census)) {
        CNS_INFO("Inserting new node for 'dom (census 'from')'.");
        census.insert(makeCensusSourceNode(dom));
        return;
    }
    CNS_INFO("'dom (census 'from')' is already in census.");
    // If dominator is in census, do nothing
    // except warning of decl data change, if any.
    chkNodeDataForChange(dom);
    CNS_DEBUG(" end.");
}

bool isSpuriousFPDom(OpData const &from, OpData const &to) {
    // If from: FP and to: func.$0: (not void*/fp)
    // then return false
    // -> handle cases where fp is mistakenly matched to f.$0

    if(from.category_ != "FunctionPointer") {
        return false;
    }

    if(to.category_ == "FunctionPointer"){ // || to.type_ == "void *") {
        return false;
    }

    return true;
}

void updateCensus(
        OpData const &from,
        OpData const &to,
        DominatorData const &dom) {

    CNS_DEBUG("<from, to, dom>");

    if(isSpuriousFPDom(from, to)) {
        CNS_WARN("Skipping spurious fptr link to non-fptr.");
        CNS_DEBUG("<from, to, dom> end");
        return;
    }

    addDomNode(from);
    auto it = census.find(to.hash_);
    if(it == std::end(census)) {
        // `to` not in census
        CNS_INFO("<0> Inserting new node for 'to'.");
        census.insert(makeCensusNode(to, dom));
        CNS_DEBUG("<from, to, dom> end");
        return;
    }

    CNS_INFO("<0> 'to' already in census.");
    chkNodeDataForChange(to);
    if(isNodeDominatorNew(to, dom)) {
        CNS_INFO("<0> 'to' has new dominator.");
        appendNodeDominator(to, dom);
    }
    CNS_DEBUG("<from, to, dom> end.");
}

void logCensusUpdate(
        OpData const &lhs,
        OpData const &rhs,
        DominatorData const &dom) {

    FOUT << "Match site: " << rhs.location_ << "\n"
         << "   Linking: [" << lhs.hash_ << "]"
         << lhs.expr_ << " {" << lhs.linkedParm_ << "} -> "
         << "[" << rhs.hash_ << "]"
         << rhs.expr_ << " {" << rhs.linkedParm_ << "}\n"
         << "       from: [" << lhs.category_ << "] " << lhs.type_ << "\n"
         << "         to: [" << rhs.category_ << "] " << rhs.type_ << "\n"
         << "       expr: " << "[" << dom.exprType_ << "] " << dom.expr_ << "\n"
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

    CNS_DEBUG("<T>");
    preprocess(context, dest);

    CNS_INFO("<Cast> building lhs data.");
    auto lhs = buildOpData(context, sm, castExpr, castSource);
    CNS_INFO("<Cast> building rhs data.");
    auto rhs = buildOpData<CS_t>(context, sm, castExpr, dest);

    DominatorData dom{
        lhs,
        String(context, castExpr),
        getCastExprType(dest),
        getLinkedFunction(context, castExpr, dest)
    };

    updateCensus(lhs, rhs, dom);
    logCensusUpdate(lhs, rhs, dom);
    CNS_DEBUG("<T> end.");
}

void processCast(MatchFinder::MatchResult const &result) {
    CNS_DEBUG("");
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
        CNS_INFO("binop processing");
        auto const *bl = result.Nodes.getNodeAs<DeclRefExpr>("binLhs");
        auto const *br = result.Nodes.getNodeAs<DeclRefExpr>("binRhs");
        if(!bl) {
            CNS_ERROR("binop lHS == nullptr.");
            return;
        }
        if(!br) {
            CNS_ERROR("binop RHS == nullptr.");
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

    CNS_DEBUG(" end.");
}

void updateCensus(
        clang::ASTContext &context,
        clang::SourceManager const &sm,
        clang::DeclRefExpr const &src,
        clang::VarDecl const &dest) {

    CNS_DEBUG("<declrefexpr, varDecl>");
    auto const *lhsDecl = src.getDecl();
    assert(lhsDecl);
    CNS_INFO("<declrefexpr, varDecl> building lhs data.");
    auto lhs = buildOpData(context, sm, src, *lhsDecl);
    CNS_INFO("<declrefexpr, varDecl> building rhs data.");
    auto rhs = buildOpData(context, sm, dest);

    DominatorData dom{lhs, {}, {}, {}};
    updateCensus(lhs, rhs, dom);
    logCensusUpdate(lhs, rhs, dom);
    CNS_DEBUG("<declrefexpr, varDecl> end.");
}

void processVar(MatchFinder::MatchResult const &result) {
    CNS_DEBUG("");
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
        auto const &rhsData = buildOpData(*context, *result.SourceManager, *lhsLit);
        census.insert(makeCensusNode(rhsData));
        FOUT << "Match site: " << rhsData.location_ << "\n"
             << "   Linking: [literal] -> [" << rhsData.hash_ << "]"
             << rhsData.expr_ << "{" << rhsData.linkedParm_ << "}\n"
             << "       rhs: [" << rhsData.category_ << "] " << rhsData.type_ << "\n"
             << "          : inside " << rhsData.container_ << "()\n"
             << "\n";
        */
        return;
    }

    assert(lhsRef);
    //auto const *lhs = lhsRef->getDecl();

    // We can have FunctionDecl too?
    /*
    auto lhsDecl = dyn_cast<VarDecl>(lhsRef->getDecl());
    if(!lhsDecl) {
        CNS_INFO("dyncast LHS Decl == nullptr");
        return;
    }
    */
    updateCensus(*context, *result.SourceManager, *lhsRef, *rhs);
    CNS_DEBUG(" end.");
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

//  todo
// Function matcher = 
//    void f(int i);
//    f(x); 
//    matcher: Functions.push_back({f: void, i.1 -> CANCEL
//  NO todo 
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
                    hasCastKind(CK_LValueToRValue),
                    hasDescendant(
                        declRefExpr(hasType(pointerType(pointee(functionType())))).bind("callee"))),

                hasDescendant(
                    unaryOperator(
                        hasDescendant(declRefExpr().bind("unaryCastee"))
                        ).bind("unaryOp")),

                // lhs: declrefexpr or expr(hasDescendant(declrefexpr))
                // rhs: declrefexpr or expr(hasDescendant(declrefexpr)) or literal
                hasParent(
                    binaryOperator(
                        isAssignmentOperator(),
                        hasLHS(expr(declRefExpr().bind("lhsref")).bind("binLhs")),
                        hasRHS(expr(declRefExpr().bind("rhsref")).bind("binRhs"))).bind("binOp")))

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
        //censusSummary(FOUT);
        censusSummary();
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

void buildIgnoreList();

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

    buildIgnoreList();
    FOUT.open("census-dump.txt", std::ios::out);
    //return Tool.run(newFrontendActionFactory<clang::SyntaxOnlyAction>().get());
    return Tool.run(newFrontendActionFactory(&Finder).get());
}

void buildIgnoreList() {
    std::ifstream in;
    in.open("cstdlib.ignore", std::ios::in);
    for(std::string l; std::getline(in, l); ) {
        ignoreFunctions.push_back(l.substr(l.find_last_of(',') + 1));
    }
}

// TODO
// Add unary op handling when it is ancestor of cast.
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
