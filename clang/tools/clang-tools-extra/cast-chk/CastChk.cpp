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

#include "utils.h"

using namespace clang::tooling;
using namespace llvm;

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::ento;

// TODO: use TypeInfo from context to detect incompatible size casts.

// TODO TODO:
// &i -> trace to int i;

enum class CastExprType{
    Assignment,
    FunctionCall,
    BinaryOp
};

template <CastExprType t>
std::string getCastExprType() {
    switch(t) {
        case CastExprType::Assignment:
            return "assignment";
        case CastExprType::FunctionCall:
            return "call";
        case CastExprType::BinaryOp:
            return "binaryop";
    };
}

std::string getCastExprType(clang::DeclStmt const&) {
    return getCastExprType<CastExprType::Assignment>();
}
std::string getCastExprType(clang::CallExpr const&) {
    return getCastExprType<CastExprType::FunctionCall>();
}

struct DeclData {
    std::string ident;
    std::string type;
    std::string category;
    std::string linkedParameter;
};

bool operator==(DeclData const& lhs, DeclData const& rhs) {
    return lhs.ident == rhs.ident
        && lhs.type == rhs.type
        && lhs.linkedParameter == rhs.linkedParameter;
}
bool operator!=(DeclData const& lhs, DeclData const& rhs) {
    return !(lhs == rhs);
}

// todo check annonymous cast expressions.
using CensusDecl = clang::Decl;

struct CastData {
    CensusDecl const* from;
    DeclData fromData;
//    DeclData to;
//    std::vector<CensusDecl const*> lchain;
//    std::vector<CensusDecl const*> rchain;
    std::string expr;
    std::string exprType;
    std::string containerFunction;
    std::optional<std::string> linkedFunction;
    std::string location;
};

using Dominator = std::variant<CastData>;

template<>
struct std::hash<CensusDecl>
{
    unsigned operator()(CensusDecl const* decl) const noexcept {
        ODRHash hasher;
        hasher.AddDecl(decl);
        return hasher.CalculateHash();
    }
};

using DeclInfo = std::pair<DeclData, std::optional<Dominator>>;
// Census stores type & cast data for every decl.
using Census = std::unordered_map<CensusDecl const*, DeclInfo>;
Census census;
using CensusNode = decltype(census)::value_type;

std::ostream& dump(std::ostream &os, DeclData const& info) {
    os << "{ident: '" << info.ident << "', type: '" << info.type << "', "
       << "category: '" << info.category << "', linkedParameter: '" << info.linkedParameter << "'}\n";
    return os;
}

std::ostream& dump(std::ostream &os, CastData const& info) {
    std::stringstream ss;
    dump(ss, info.fromData);
    os << "{fromData: '" << ss.str() << "', expr: '" << info.expr << "', "
       << "exprType: '" << info.exprType << "', "
       << "containerFunction: '" << info.containerFunction << "', "
       << "linkedFunction: '" << info.linkedFunction.value_or("(none)") << "', "
       << "location: '" << info.location << "'}\n";

    return os;
}

bool isCastData(Dominator const& d) {
    return std::holds_alternative<CastData>(d);
}

bool operator==(clang::Decl const* lhs, Dominator const& rhs) {
    assert(lhs);
    if(isCastData(rhs)) {
        auto const& rhsDecl = std::get<CastData>(rhs).from;
        return lhs->getID() == rhsDecl->getID();
    }
    return false;
}
bool operator!=(clang::Decl const* lhs, Dominator const& rhs) {
    return !(lhs == rhs);
}

std::ostream& dump(std::ostream& os, Dominator const& d) {
    if(isCastData(d)) {
        dump(os, std::get<CastData>(d));
    }
    return os;
}

void dump(std::ostream& os, DeclInfo const& d) {
    auto const& [decl, dominator] = d;
    std::stringstream ss;
    dump(ss, decl);
    if(!dominator) {
        //dump(os, decl);
        os << ss.str();
        return;
    }
    dump(os, dominator.value());
    os << " ---> " << ss.str();
}

void declSummary(std::ostream &os, DeclData const& data) {
    os << "[" << data.category << "] " << data.ident << "(" << data.type << ")"
       << " " << data.linkedParameter;
}

void declCastSummary(std::ostream &os, DeclData const& data, CastData const& cast) {
    os << "[" << data.category << "] " << data.ident << "(" << data.type << ")"
       << " " << data.linkedParameter;
    os << "<" << cast.containerFunction << ">: " << cast.location << ">";
}

void censusSummary(std::ostream &os, CensusDecl const* decl, DeclData const& declData, std::optional<Dominator> const& dominator, int indent = 0) {
    assert(decl);
    std::string ws(indent, ' ');
    if(!decl) {
        os << "<end>}\n";
        return;
    }

    os << "{";
    if(!dominator) {
        declSummary(os, declData);
    } else if(isCastData(dominator.value())) {
        auto const& castData = std::get<CastData>(dominator.value());
        declCastSummary(os, declData, castData);
    }

    auto doesDeclDominate = [&decl](auto const& censusNode) {
        auto const& [_, data] = censusNode;
        auto const& [__, d] = data;
        if(!d)
            return false;
        if(isCastData(d.value()))
            return decl == d.value();
        return false;
    };

    std::vector<CensusNode> rchain;
    std::copy_if(std::begin(census), std::end(census), std::back_inserter(rchain), doesDeclDominate);
    if(rchain.empty()) {
        os << "\n" << ws << "-> <end>}\n";
        return;
    }

    for(auto const& [rdecl, rinfo]: rchain) {
        os << "\n" << ws << "-> ";
        auto const& [rdata, rdominator] = rinfo;
        censusSummary(os, rdecl, rdata, rdominator, indent+2);
    }
    os << ws << "}\n";
}

void censusSummary(std::ostream &os) {
    for(auto const& [decl, info]: census) {
        auto const& [data, dominator] = info;

        censusSummary(os, decl, data, dominator);
        os << "\n";
    }
}

/*
std::ostream& operator<<(std::ostream &os, CastData const & info) {
    os << "  Cast from: {" << info.from << "}\n"
       << "         to: {" << info.to << "}\n"
       << "       with: [" << info.exprType << "] " << info.expr << "\n"
       << "         in: function(" << info.containerFunction << ")" << "\n"
       << "  Linked fn: " << info.linkedFunction.value_or("(none)") << "\n"
       << "  <-LLinks-\n  " << info.lchain
       << "  -RLinks->\n  " << info.rchain;

    return os;
}
std::ostream& operator<<(std::ostream &os, std::vector<CensusDecl*> const & list) {
    //std::stringstream ss;
    for(auto const& i: list) {
        os << census[i];
    }
    os << "\n";

    return os;
}
std::ostream& operator<<(std::ostream &os, std::vector<CastData*> const & list) {
    for(auto const& i: list) {
        os << *i;
    }

    return os;
}
*/
/*
int f(void *pv){
    void* pv1 = pv;
    void* pv2 = pv;
    void* pv3 = pv2;

    int* pi = pv3;
    return *pi;
}
*/

// TODO TODO
//  * Replace pair of clang nodes with declaration strings.
//    There's no point in keeping pointers anyway. Since links may have different node type as parents.
//  ? It's more robust to use string to match decls and exprs.


// TODO TODO
//  - Add missing cast dumps. For example in other cast types.(?).
DeclarationMatcher DeclMatcher =
    varDecl(hasDescendant(declRefExpr().bind("assignee"))).bind("varDecl");

StatementMatcher CastMatcher =
    castExpr(
        allOf(
            hasCastKind(CK_BitCast),
            anyOf( // technically just any of expr or decl is needed.
                hasAncestor(declStmt().bind("var")),
                hasAncestor(binaryOperator().bind("binop")),
                hasAncestor(callExpr().bind("call")),
                hasAncestor(expr().bind("gexpr"))),
                hasDescendant(declRefExpr().bind("castee")))
        ).bind("cast");
    /*
StatementMatcher CastMatcher =
    compoundStmt(
        anyOf(
            hasDescendant(
                castExpr(
                    allOf(
                        hasCastKind(CK_BitCast),
                        anyOf( // technically just any of expr or decl is needed.
                            hasAncestor(declStmt().bind("var")),
                            hasAncestor(binaryOperator().bind("binop")),
                            hasAncestor(callExpr().bind("call")),
                            hasAncestor(expr().bind("gexpr"))),
                            hasDescendant(declRefExpr().bind("castee")))
                    ).bind("cast")),
            hasDescendant(
                varDecl(
                    hasDescendant(declRefExpr().bind("assignee"))
                    ).bind("varDecl"))));
        */


// Apply a custom category to all cli options so that they are the only ones displayed
static llvm::cl::OptionCategory MyToolCategory("my-tool options");

// CommonOptionsParser declares HelpMessage with a description of the common cli options
// related to the compilation db and input files. (Nice to have help)
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);

// Help message for this specific tool.
static cl::extrahelp Morehelp("\nMore help text...\n");

//std::ofstream FOUT;

std::string functionParameterMatch(
        clang::ASTContext & context,
        //clang::CastExpr const& castExpr,
        clang::Stmt const& castExpr,
        clang::DeclarationNameInfo const& name) {

    auto const *fn = getContainerFunctionDecl(context, castExpr);
    assert(fn);

    if(auto parmPos = getParameterMatch(fn, name)) {
        return toString(context, *fn, *parmPos);
    }

    // TODO: Reach decl. Assign chain?
    return "(Not a param)";
}

std::string functionParameterMatch(
        clang::ASTContext & context,
        clang::VarDecl const& var) {

    if(var.isLocalVarDecl()) {
        return "(Not a param)";
    }
    
    return "(No_Impl_Yet!)";
}

DeclData buildDeclData(
        clang::ASTContext & context,
        clang::VarDecl const& var) {
    return {
        toString(context, var),
        typeof(context, var),
        getTypeCategoryName(context, var),
        functionParameterMatch(context, var)
    };
}

DeclData buildDeclData(
        clang::ASTContext & context,
        clang::CastExpr const& castExpr,
        clang::DeclRefExpr const& e) {

    return {
        toString(context, e),                                       // ident
        typeof(context, e),                                         // type
        getTypeCategoryName(context, e),                            // category
        functionParameterMatch(context, castExpr, e.getNameInfo())  // linkedParameter
    };
}

DeclData buildDeclData(
        clang::ASTContext & context,
        clang::DeclRefExpr const& e) {

        auto const * stmt = e.getExprStmt();
        if(!stmt) {
            FOUT << "[ERROR](buildDeclData) Expr stmt from DeclRefExpr == nullptr";
            return {
                toString(context, e),
                typeof(context, e),
                getTypeCategoryName(context, e),
                {}
            };
        }

    return {
        toString(context, e),                                       // ident
        typeof(context, e),                                         // type
        getTypeCategoryName(context, e),                            // category
        functionParameterMatch(context, *stmt, e.getNameInfo())  // linkedParameter
    };
}

DeclData buildDeclData(
        clang::ASTContext const& context,
        clang::CastExpr const& castExpr,
        clang::DeclStmt const& e) {

    return {
        toString(context, e),
        typeof(context, castExpr),
        getTypeCategoryName(context, castExpr),
        "(Not a param)"
    };
}

DeclData buildDeclData(
        clang::ASTContext const& context,
        clang::CastExpr const& castExpr,
        clang::CallExpr const& e) {

    // Get cast expressions position in call argument list.
    unsigned argPos = 0;
    auto match = std::find_if(e.arg_begin(), e.arg_end(),
        [&] (auto const & arg) {
        argPos++;
        //FOUT << "[DEBUG](getTargetDecl) Argpos: " << argPos << "\n";
        return clang::Expr::isSameComparisonOperand(arg, &castExpr);
    });
    //FOUT << "[DEBUG](getTargetDecl) final argpos: " << argPos << "\n";

    std::string parmId;
    llvm::raw_string_ostream stream(parmId);
    if (argPos <= e.getNumArgs()) {
        auto const& parmd = getParamDecl(context, e, argPos - 1);
        auto const& parm = dyn_cast<clang::ParmVarDecl>(parmd);
        if(parm) {
            parm->printQualifiedName(stream);
        }
    }

    return {
        //toString(context, e),
        parmId,
        typeof(context, castExpr),
        getTypeCategoryName(context, castExpr),
        (argPos > e.getNumArgs())
            ? ("(Failed to match arg)")
            : (toString(context, e, argPos - 1))
    };
}

std::string getLinkedFunction(
        clang::ASTContext const& context,
        clang::CastExpr const& castExpr,
        clang::DeclStmt const&) {

    return "N/A";
}


void preprocess(
        clang::ASTContext & context,
        clang::DeclStmt const&) {
}

void preprocess(
        clang::ASTContext & context,
        clang::CallExpr const& call) {

    // Trigger cast check for the called function.
    auto const * calledFn = call.getDirectCallee();
    assert(calledFn);

    if(calledFn->hasBody()) {
        auto const *body = calledFn->getBody();
        assert(body);
        MatchFinder m;
        m.match(*body, context);
    }
}

std::string getLinkedFunction(
        clang::ASTContext & context,
        clang::CastExpr const& castExpr,
        clang::CallExpr const& call) {

    // Get function body
    // parm->getOriginalType : QualType
    // parm->getFunctionScopeIndex: unsigned parameter index
    auto const * calledFn = call.getDirectCallee();
    assert(calledFn);

    return calledFn->getNameAsString();
}

CensusDecl const* getTargetDecl(
        clang::ASTContext const& context,
        clang::CastExpr const& castExpr,
        clang::DeclStmt const& t) {

    return t.getSingleDecl();  // TODO examples where decls() or DeclGroup() may be needed.
}
CensusDecl const* getTargetDecl(
        clang::ASTContext const& context,
        clang::CastExpr const& castExpr,
        clang::CallExpr const& t) {

    // Get cast expressions position in call argument list.
    unsigned argPos = 0;
    auto match = std::find_if(t.arg_begin(), t.arg_end(),
        [&] (auto const & arg) {
            argPos++;
            return clang::Expr::isSameComparisonOperand(arg, &castExpr);
    });

    //if (match != t.arg_end()) {
    if (argPos <= t.getNumArgs()) {
        return getParamDecl(context, t, argPos - 1);
    }
    return nullptr;
}


/*
 * Cast data should be associated with rhs only. Why?
 *  - LHS participates in the cast, is not created or initialized by it.
 *  - RHS is at least initialized by the cast.
 *  => LHS can be involved in multiple casts which may not have same CastData.
 *  => RHS can be involved in one cast only.
*/

template<typename T>
void updateCensus(clang::ASTContext & context,
        clang::CastExpr const& castExpr,
        clang::DeclRefExpr const& castSource,
        T const* dest,
        std::string const& location) {

    assert(dest);
    preprocess(context, *dest);

    clang::Decl const* lhs = castSource.getDecl();
    if(!lhs) {
        FOUT << "[ERROR](updateCensus) LHS Decl == nullptr\n";
        auto lhs_ = castSource.getFoundDecl();
        if (!lhs_) {
            FOUT << "[ERROR](updateCensus) LHS' Decl == nullptr\n";
        }
        lhs = lhs_;
    }
    auto lhsData = buildDeclData(context, castExpr, castSource);
    auto rhs = getTargetDecl(context, castExpr, *dest);
    if(!rhs) {
        FOUT << "[ERROR](updateCensus) RHS Decl == nullptr\n";
    }
    auto rhsData = buildDeclData(context, castExpr, *dest);

    CastData castInfo{
        lhs,                                                // CastData.from
        lhsData,                                            // CastData.fromData
        toString(context, castExpr),                        // CastData.expr
        getCastExprType(*dest),                             // CastData.exprType
        getContainerFunction(context, castExpr),            // castData.containerFunction
        getLinkedFunction(context, castExpr, *dest),        // CastData.linkedFunction
        location                                            // CastData.location
    };

    if (census.find(lhs) == std::end(census)) {
        census.insert({lhs, {lhsData, {}}});
    } else {
        // LHS is already in census. 
        //  - Ensure that lhsdata is same.
        auto const& [data, _] = census[lhs];
        if(lhsData != data) {
            FOUT << "[WARN](updateCensus) LHS in Census with different DeclData\n";
        }
        //  - Cast data will be unchanged.
        // Nothing to do.
    }

    if (census.find(rhs) == std::end(census)) {
        census.insert({rhs, {rhsData, castInfo}});
    } else {
        // RHS is already in census.
        //  - Ensure that rhsdata is same.
        auto const& [data, cast] = census[rhs];
        if(rhsData != data) {
            FOUT << "[WARN](updateCensus) RHS in Census with different DeclData\n";
            FOUT << "[WARN](updateCensus) Old RHS Data: {";
            dump(FOUT, data);
            FOUT << "[WARN](updateCensus) }";
            FOUT << "[WARN](updateCensus) New RHS Data: {";
            dump(FOUT, rhsData);
            FOUT << "[WARN](updateCensus) }";
        }
        //  - If castdata is not present, update.
        if(!cast) {
            census[rhs] = {data, castInfo};
        } else {
            FOUT << "[WARN](updateCensus) RHS in Census with different CastData\n";
            FOUT << "[WARN](updateCensus) Old RHS CastData: {";
            dump(FOUT, cast.value());
            FOUT << "[WARN](updateCensus) }";
            FOUT << "[WARN](updateCensus) New RHS CastData: {";
            dump(FOUT, castInfo);
            FOUT << "[WARN](updateCensus) }";
        }

        //  - If castdata is present, ??.
        // Nothing to do. (YET)
    }

    FOUT << "Cast site: " << castInfo.location << "\n"
         << "    Casting: " << castInfo.fromData.ident << " -> " << rhsData.ident << "\n"
         << "       from: [" << castInfo.fromData.category << "] " << castInfo.fromData.type << "\n"
         << "         to: [" << rhsData.category << "] " << rhsData.type << "\n"
         << "       expr: " << "[" << castInfo.exprType << "] " << castInfo.expr << "\n"
         << "\n";
}

void processVar(MatchFinder::MatchResult const& result) {
    assert(result);
    auto *context = result.Context;
    assert(context);

    // [VarDecl]    [DeclRefExpr]
    // [int *pi2] = [pi]
    //  {pi -> p2}
    //  LHS    RHS

    auto const *rhs = result.Nodes.getNodeAs<clang::VarDecl>("varDecl");
    assert(rhs);
    auto const *lhsRef = result.Nodes.getNodeAs<clang::DeclRefExpr>("assignee");
    assert(lhsRef);
    auto const * lhs = lhsRef->getFoundDecl();
    if(!lhs) {
        FOUT << "[ERROR](processVar) LHS Decl == nullptr\n";
        auto lhs_ = lhsRef->getFoundDecl();
        if(!lhs_) {
            FOUT << "[ERROR](processVar) LHS' Decl == nullptr\n";
        }
        lhs = lhs_;
    }

    auto rhsData = buildDeclData(*context, *rhs);
    auto lhsData = buildDeclData(*context, *lhsRef);

    auto const& location = rhs->getLocation().printToString(*result.SourceManager);

    CastData castInfo{
        lhs,                                                // CastData.from
        lhsData,                                            // CastData.fromData
        {},                        // CastData.expr
        {},                             // CastData.exprType
        {},            // castData.containerFunction
        {},        // CastData.linkedFunction
        location                                            // CastData.location
    };

    if (census.find(lhs) == std::end(census)) {
        census.insert({lhs, {lhsData, {}}});
    } else {
        // LHS is already in census. 
        //  - Ensure that lhsdata is same.
        auto const& [data, _] = census[lhs];
        if(lhsData != data) {
            FOUT << "[WARN](processVar) LHS in Census with different DeclData\n";
        }
        //  - Cast data will be unchanged.
        // Nothing to do.
    }

    if (census.find(rhs) == std::end(census)) {
        census.insert({rhs, {rhsData, castInfo}});
    } else {
        // RHS is already in census.
        //  - Ensure that rhsdata is same.
        auto const& [data, cast] = census[rhs];
        if(rhsData != data) {
            FOUT << "[WARN](processVar) RHS in Census with different DeclData\n";
            FOUT << "[WARN](processVar) Old RHS Data: {";
            dump(FOUT, data);
            FOUT << "[WARN](processVar) }";
            FOUT << "[WARN](processVar) New RHS Data: {";
            dump(FOUT, rhsData);
            FOUT << "[WARN](processVar) }";
        }
        //  - If castdata is not present, update.
        if(!cast) {
            census[rhs] = {data, castInfo};
        } else {
            FOUT << "[WARN](processVar) RHS in Census with different CastData\n";
            FOUT << "[WARN](processVar) Old RHS CastData: {";
            dump(FOUT, cast.value());
            FOUT << "[WARN](processVar) }";
            FOUT << "[WARN](processVar) New RHS CastData: {";
            dump(FOUT, castInfo);
            FOUT << "[WARN](processVar) }";
        }
    }
}

void processCast(MatchFinder::MatchResult const& result) {
    assert(result);
    auto *context = result.Context;
    assert(context);
    // Source operand (i.e. the expression which is being cast)
    //auto const *castSource = castExpr->getSubExprAsWritten();
    auto const *castExpr = result.Nodes.getNodeAs<clang::CastExpr>("cast");
    auto const *castSource = result.Nodes.getNodeAs<clang::DeclRefExpr>("castee");
    assert(castSource);

    // Dest
    using namespace clang;
    auto const *var = result.Nodes.getNodeAs<clang::DeclStmt>("var");
    auto const *gexpr = result.Nodes.getNodeAs<clang::Expr>("gexpr");
    auto const *binop = result.Nodes.getNodeAs<clang::BinaryOperator>("binop");
    auto const *call = result.Nodes.getNodeAs<clang::CallExpr>("call");

    auto const& location = castExpr->getExprLoc().printToString(*result.SourceManager);

    if(!!var) {
        updateCensus(*context, *castExpr, *castSource, var, location);
    }
    else if (!!call) {
        updateCensus(*context, *castExpr, *castSource, call, location);
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

    /*
    // Dump ast
    FOUT << "------- AST DUMP----\n";
    std::string dmpstr;
    llvm::raw_string_ostream dmpstrm(dmpstr);
    castExpr->dump(dmpstrm, *context);
    FOUT << dmpstr << "\n";
    FOUT << "----end AST DUMP----\n";

    auto parents = context->getParents(*castExpr);
    FOUT << "---    Parent Dumps---";
    for(auto const& parent: parents) {
        std::string dumpstr;
        llvm::raw_string_ostream dumpstrm(dumpstr);
        parent.dump(dumpstrm, *context);
        FOUT << dumpstr << "\n";
        auto const * fn = parent.get<clang::FunctionDecl>();
    }
    FOUT << "---end Parent Dumps---";
    */
}

//---
class CastMatchCallback: public MatchFinder::MatchCallback {

public:
    void run(MatchFinder::MatchResult const &result) override {
        assert(result);
        auto *context = result.Context;
        assert(context);

        // Cast expression
        auto const *castExpr = result.Nodes.getNodeAs<clang::CastExpr>("cast");
        // Decl without cast
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

        /*
        FOUT << "# Census collection so far:\n";
        for(auto const& [_, data]: census) {
            dump(FOUT, data);
        }
        FOUT << "# Census end.\n";
        */

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

int main(int argc, const char **argv) {
    auto ExpectedParser = CommonOptionsParser::create(argc, argv, MyToolCategory);
    if(!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }

    CommonOptionsParser& OptionsParser = ExpectedParser.get();
    ClangTool Tool(OptionsParser.getCompilations(),
                   OptionsParser.getSourcePathList());

    CastMatchCallback dumper;
    MatchFinder Finder;
    Finder.addMatcher(CastMatcher, &dumper);
    Finder.addMatcher(DeclMatcher, &dumper);

    FOUT.open("census-dump.txt", std::ios::out);
    //return Tool.run(newFrontendActionFactory<clang::SyntaxOnlyAction>().get());
    return Tool.run(newFrontendActionFactory(&Finder).get());
}

