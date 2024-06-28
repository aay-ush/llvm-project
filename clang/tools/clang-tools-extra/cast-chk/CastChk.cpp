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
    /*
    std::string linkedFunction;
    std::string location;
    */
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

/*
 * Dominator data should be associated with target only. Why?
 *  - Source participates in the cast, it is not created or initialized by it.
 *  - Target is at least initialized by the cast/dominator.
 *  => Source can be involved in multiple casts which may not have same DominatorData.
 *  => Target can be involved in one cast only, unless it is also a source.
*/
struct DominatorData {
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

bool operator==(DominatorData const& lhs, DominatorData const& rhs) {
    return lhs.from->getID() == rhs.from->getID()
        && lhs.fromData == rhs.fromData
        && lhs.location == rhs.location;
}
bool operator!=(DominatorData const& lhs, DominatorData const& rhs) {
    return !(lhs == rhs);
}

bool operator==(DominatorData const& lhs, CensusDecl const* rhs) {
    assert(lhs);
    return rhs->getID() == lhs.from->getID();
}
bool operator!=(DominatorData const& lhs, CensusDecl const* rhs) {
    return !(lhs == rhs);
}

template<>
struct std::hash<CensusDecl>
{
    unsigned operator()(CensusDecl const* decl) const noexcept {
        ODRHash hasher;
        hasher.AddDecl(decl);
        return hasher.CalculateHash();
    }
};

using Dominators = std::vector<DominatorData>;
using DeclInfo = std::pair<DeclData, std::optional<Dominators>>;

// Census stores decl & dominator data for every decl.
using Census = std::unordered_map<CensusDecl const*, DeclInfo>;
Census census;
using CensusNode = decltype(census)::value_type;

std::ofstream FOUT;
std::ofstream FOUT2;

struct TypeData {
    std::string tname;
};
bool operator ==(TypeData const& lhs, TypeData const& rhs) {
    return lhs.tname == rhs.tname;
}
bool operator !=(TypeData const& lhs, TypeData const& rhs) {
    return !(lhs == rhs);
}

// With every decl, store the orderd_type.
// censusI2[decl].insert({censusI2[decl].size() + 1, new_type})
using OrderedType = std::pair<unsigned, TypeData>;
struct OrderedTypeCmp {
    bool operator()(OrderedType const& lhs, OrderedType const& rhs) const {
        if(lhs.second == rhs.second)
            return false;
        return lhs.first < rhs.first;
    }
};

using CensusI2 = std::unordered_map<CensusDecl const*, std::set<OrderedType, OrderedTypeCmp>>;
CensusI2 census2;

/*
using DeclChainedData = std::pair<CensusDecl const*, DeclData const&>;
using CastChain = std::vector<DeclChainedData>;
using CastHistory = std::unordered_map<CensusDecl const*, std::vector<CastChain>>;
CastHistory history;
*/

std::ostream& dump(std::ostream &os, Dominators const& doms);
std::ostream& dump(std::ostream &os, DeclData const& info);
std::ostream& dump(std::ostream &os, DominatorData const& info);
void dump(std::ostream& os, DeclInfo const& d);

void declSummary(std::ostream &os, DeclData const& data);
void declCastSummary(std::ostream &os, DeclData const& data, DominatorData const& cast);
void censusSummary(std::ostream &os, CensusDecl const* decl, DeclData const& declData, int indent = 0);
void censusSummary(std::ostream &os);
void census2Summary(std::ostream &os);

std::string functionParameterMatch(
        clang::ASTContext & context,
        clang::Stmt const& stmt,
        clang::DeclarationNameInfo const& name) {

    auto const *fn = getContainerFunctionDecl(context, stmt);
    assert(fn);

    if(auto parmPos = getParameterMatch(fn, name)) {
        return toString(context, *fn, *parmPos);
    }

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
        functionParameterMatch(context, *stmt, e.getNameInfo())     // linkedParameter
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

std::string getLinkedFunction(
        clang::ASTContext & context,
        clang::CastExpr const& castExpr,
        clang::CallExpr const& call) {

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

//---
std::ostream& dump(std::ostream &os, Dominators const& doms) {
    for(auto const& d: doms) {
        dump(os, d);
    }

    return os;
}

void dump(std::ostream& os, DeclInfo const& d) {
    auto const& [decl, dominators] = d;
    std::stringstream ss;
    dump(ss, decl);
    if(!dominators) {
        os << ss.str();
        return;
    }
    dump(os, dominators.value());
    os << " ---> " << ss.str();
}

std::ostream& dump(std::ostream &os, DeclData const& info) {
    os << "{ident: '" << info.ident << "', type: '" << info.type << "', "
       << "category: '" << info.category << "', linkedParameter: '" << info.linkedParameter << "'}\n";
    return os;
}

std::ostream& dump(std::ostream &os, DominatorData const& info) {
    std::stringstream ss;
    dump(ss, info.fromData);
    os << "{fromData: '" << ss.str() << "', expr: '" << info.expr << "', "
       << "exprType: '" << info.exprType << "', "
       << "containerFunction: '" << info.containerFunction << "', "
       << "linkedFunction: '" << info.linkedFunction.value_or("(none)") << "', "
       << "location: '" << info.location << "'}\n";

    return os;
}

//void declSummary(std::ostream &os, DeclData const& data, std::optional<Dominators const> const& doms) {
void declSummary(std::ostream &os, DeclData const& data) {
    os << "[" << data.category << "] " << data.ident << "(" << data.type << ")"
       << " " << data.linkedParameter;
    /*
    if(doms) {
        for(auto const& d: doms) {
            os << "<" << dom->containerFunction << ">: " << dom->location << ">";
        }
    }
    */
}

void censusSummary(std::ostream &os, CensusDecl const* decl, DeclData const& declData, int indent) {
    assert(decl);
    std::string ws(indent, ' ');
    if(!decl) {
        os << "<end>}\n";
        return;
    }

    os << "{";
    declSummary(os, declData);

    auto doesDeclDominate = [&decl](auto const& censusNode) {
        auto const& [_, info] = censusNode;
        auto const& [__, nodeDoms] = info;
        if(!nodeDoms)
            return false;
        auto const& doms = nodeDoms.value();
        return std::find(std::begin(doms), std::end(doms),
                decl) != std::end(doms);
    };

    std::vector<CensusNode> rchain;
    std::copy_if(std::begin(census), std::end(census), std::back_inserter(rchain), doesDeclDominate);

    if(rchain.empty()) {
        os << "\n" << ws << "-> <end>}\n";
        return;
    }

    for(auto const& [rdecl, rinfo]: rchain) {
        os << "\n" << ws << "-> ";
        auto const& [rdata, _] = rinfo;
        censusSummary(os, rdecl, rdata, indent+2);
    }
    os << ws << "}\n";
}

/*
void buildCastChain(CensusDecl const* decl, DeclData const& data, CastChain chain) {
    auto doesDeclDominate = [&decl](auto const& censusNode) {
        auto const& [_, info] = censusNode;
        auto const& [__, d] = info;
        if(!d)
            return false;
        auto const& doms = d.value();
        return std::find(std::begin(doms), std::end(doms),
                decl) != std::end(doms);
    };

    std::vector<CensusNode> c;
    std::copy_if(std::begin(census), std::end(census), std::back_inserter(c), doesDeclDominate);
    if(c.empty()) {
        return;
    }

    for(auto const& [rdecl, rinfo]: c) {
        auto const& [rdata, _] = rinfo;
}

void buildCastHistory(CensusDecl const* decl, DeclData const& data) {

    auto doesDeclDominate = [&decl](auto const& censusNode) {
        auto const& [_, info] = censusNode;
        auto const& [__, d] = info;
        if(!d)
            return false;
        auto const& doms = d.value();
        return std::find(std::begin(doms), std::end(doms),
                decl) != std::end(doms);
    };

    std::vector<CensusNode> chain;
    std::copy_if(std::begin(census), std::end(census), std::back_inserter(chain), doesDeclDominate);
    if(chain.empty()) {
        return;
    }

    for(auto const& [rdecl, rinfo]: chain) {
        auto const& [rdata, _] = rinfo;
        CastChain rchain;
        rchain.push_back({rdecl, rdata});
        buildCastChain(rdecl, rdata, rchain);
    }
}

void buildCastHistory() {
    for(auto const& [decl, info]: census) {
        auto const& [data, _] = info;
        buildCastHistory(decl, data);
    }
}
*/

void census2Summary(std::ostream &os) {
    for(auto const& [decl, typeset]: census2) {
        declSummary(os, census[decl].first);
        os << ": ";
        for(auto const& [_, type]: typeset) {
            os << type.tname << " -> ";
        }
        os << "(end)\n";
    }
}

void censusSummary(std::ostream &os) {
    for(auto const& [decl, info]: census) {
        auto const& [data, _] = info;

        censusSummary(os, decl, data);
        os << "\n";
    }
}
//---
void preprocess(
        clang::ASTContext & context,
        clang::DeclStmt const& decl) {}

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

void updateCensusI2(
        clang::ASTContext & context,
        CensusDecl const* domDecl,
        std::string domType,
        CensusDecl const* decl,
        std::string declType) {

    // If domDecl is not already in census
    if(census2.find(domDecl) == std::end(census2)) {
        //  - insert domDecl with domDecl's type
        FOUT2 << "Inserting domdecl with type {0, " << domType << "}\n";
        census2.insert({domDecl, {{0, {domType}}}});
        //  - add decl's type to domDecl's type set.
        FOUT2 << "Inserting domdecl with type {" << census2[domDecl].size() << ", " << declType << "}\n";
        census2[domDecl].insert({census2[domDecl].size(), {declType}});
    }
    else { // just add the new type (decl's)
        FOUT2 << "Inserting domdecl with type {" << census2[domDecl].size() << ", " << declType << "}\n";
        census2[domDecl].insert({census2[domDecl].size(), {declType}});
    }

    // If decl is not already in census
    if(census2.find(decl) == std::end(census2)) {
        //  - insert decl with decl's type
        FOUT2 << "Inserting decl with type {0, " << declType << "}\n";
        census2.insert({decl, {{0, {declType}}}});

    } else {// probably nothing to do
        //census2[decl].insert({census2[decl].size(), {declType}});
    }
}

using DeclInfoSingleDom = std::pair<DeclData, std::optional<DominatorData>>;
void updateCensus(
        clang::ASTContext & context,
        CensusDecl const* domDecl,
        DeclInfo domInfo,
        CensusDecl const* decl,
        DeclInfoSingleDom info) {
    assert(decl);

    updateCensusI2(context, domDecl, domInfo.first.type, decl, info.first.type);

    if (census.find(domDecl) == std::end(census)) {
        census.insert({domDecl, domInfo});
    } else {
        // Dominator is already in census. 
        //  - Check that DomData is same.
        auto const& [oldData, _] = census[domDecl];
        auto const& [newData, __] = domInfo;
        if(oldData != newData) {
            FOUT << "[WARN](updateCensus) domDecl in Census with different DeclData\n";
        }
        //  - Dominator data (of DomDecl)  will be unchanged.
        // Nothing to do.
    }

    auto const& [newData, newDomData] = info;
    if (census.find(decl) == std::end(census)) {
        census.insert({decl, {newData, {{newDomData.value()}}}});

    } else {
        // Decl is already in census.
        //  - Check that DeclData is same.
        auto & [oldData, oldDoms] = census[decl];

        // Check existing decl data
        if(oldData != newData) {
            FOUT << "[WARN](updateCensus) decl in Census with different DeclData\n";
            FOUT << "[WARN](updateCensus) Old decl Data: {";
            dump(FOUT, oldData);
            FOUT << "[WARN](updateCensus) }";
            FOUT << "[WARN](updateCensus) New decl Data: {";
            dump(FOUT, newData);
            FOUT << "[WARN](updateCensus) }";
        }

        // If DominatorData is not present, update.
        if(!oldDoms) {
            census[decl] = {newData, {{newDomData.value()}}};

        } else {
            /*
            // Nothing to do. (YET)
            FOUT << "[WARN](updateCensus) decl in Census with different DominatorData\n";
            FOUT << "[WARN](updateCensus) Old DominatorData: {";
            dump(FOUT, oldDomData.value());
            FOUT << "[WARN](updateCensus) }";
            FOUT << "[WARN](updateCensus) New DominatorData: {";
            dump(FOUT, newDomData.value());
            FOUT << "[WARN](updateCensus) }";
            */
            auto &doms = oldDoms.value();
            doms.push_back(newDomData.value());
            FOUT << "[INFO](updateCensus) New Dominator Appended: {\n";
            dump(FOUT, newDomData.value());
            FOUT << "[INFO](updateCensus) }\n";
        }
    }
}

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

    DominatorData rhsDom{
        lhs,                                                // DominatorData.from
        lhsData,                                            // DominatorData.fromData
        toString(context, castExpr),                        // DominatorData.expr
        getCastExprType(*dest),                             // DominatorData.exprType
        getContainerFunction(context, castExpr),            // DominatorData.containerFunction
        getLinkedFunction(context, castExpr, *dest),        // DominatorData.linkedFunction
        location                                            // DominatorData.location
    };

    updateCensus(context, lhs, {lhsData,{}}, rhs, {rhsData, rhsDom});

    FOUT << "Cast site: " << rhsDom.location << "\n"
         << "    Casting: " << rhsDom.fromData.ident << " -> " << rhsData.ident << "\n"
         << "       from: [" << rhsDom.fromData.category << "] " << rhsDom.fromData.type << "\n"
         << "         to: [" << rhsData.category << "] " << rhsData.type << "\n"
         << "       expr: " << "[" << rhsDom.exprType << "] " << rhsDom.expr << "\n"
         << "\n";
}


void processCast(MatchFinder::MatchResult const& result) {
    assert(result);
    auto *context = result.Context;
    assert(context);
    auto const *castExpr = result.Nodes.getNodeAs<CastExpr>("cast");
    auto const *castSource = result.Nodes.getNodeAs<DeclRefExpr>("castee");
    assert(castSource);

    auto const *var = result.Nodes.getNodeAs<DeclStmt>("var");
    auto const *gexpr = result.Nodes.getNodeAs<Expr>("gexpr");
    auto const *binop = result.Nodes.getNodeAs<BinaryOperator>("binop");
    auto const *call = result.Nodes.getNodeAs<CallExpr>("call");

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

    DominatorData domData{
        lhs,                       // DominatorData.from
        lhsData,                   // DominatorData.fromData
        {},                        // DominatorData.expr
        {},                        // DominatorData.exprType
        {},                        // DominatorData.containerFunction
        {},                        // DominatorData.linkedFunction
        location                   // DominatorData.location
    };

    updateCensus(*context, lhs, {lhsData, {}}, rhs, {rhsData, domData});
}

//----------------------------------------------------------------------------
// MATCHERS

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

        FOUT << "# Census summary so far:\n";
        censusSummary(FOUT);
        FOUT << "# Census summary end.\n";
        FOUT2 << "# Census summary so far:\n";
        census2Summary(FOUT2);
        FOUT2 << "# Census summary end.\n";

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

    CommonOptionsParser& OptionsParser = ExpectedParser.get();
    ClangTool Tool(OptionsParser.getCompilations(),
                   OptionsParser.getSourcePathList());

    CastMatchCallback dumper;
    MatchFinder Finder;
    Finder.addMatcher(CastMatcher, &dumper);
    Finder.addMatcher(DeclMatcher, &dumper);

    FOUT.open("census-dump.txt", std::ios::out);
    FOUT2.open("census2-dump.txt", std::ios::out);
    //return Tool.run(newFrontendActionFactory<clang::SyntaxOnlyAction>().get());
    return Tool.run(newFrontendActionFactory(&Finder).get());
}

