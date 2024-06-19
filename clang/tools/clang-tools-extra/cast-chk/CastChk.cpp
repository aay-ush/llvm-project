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

bool operator==(CensusDecl const* lhs, DominatorData const& rhs) {
    assert(lhs);
    return lhs->getID() == rhs.from->getID();
}
bool operator!=(CensusDecl const* lhs, DominatorData const& rhs) {
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

using DeclInfo = std::pair<DeclData, std::optional<DominatorData>>;

// Census stores decl & dominator data for every decl.
using Census = std::unordered_map<CensusDecl const*, DeclInfo>;
Census census;
using CensusNode = decltype(census)::value_type;

std::ofstream FOUT;


std::ostream& dump(std::ostream& os, DominatorData const& d);
std::ostream& dump(std::ostream &os, DeclData const& info);
std::ostream& dump(std::ostream &os, DominatorData const& info);
void dump(std::ostream& os, DeclInfo const& d);

void declSummary(std::ostream &os, DeclData const& data);
void declCastSummary(std::ostream &os, DeclData const& data, DominatorData const& cast);
void censusSummary(std::ostream &os, CensusDecl const* decl, DeclData const& declData, std::optional<DominatorData const> const& dominator, int indent = 0);
void censusSummary(std::ostream &os);

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

//---
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

void declSummary(std::ostream &os, DeclData const& data, std::optional<DominatorData const> const& dom) {
    os << "[" << data.category << "] " << data.ident << "(" << data.type << ")"
       << " " << data.linkedParameter;
    if(dom) {
        os << "<" << dom->containerFunction << ">: " << dom->location << ">";
    }
}

void censusSummary(std::ostream &os, CensusDecl const* decl, DeclData const& declData, std::optional<DominatorData const> const& dominator, int indent) {
    assert(decl);
    std::string ws(indent, ' ');
    if(!decl) {
        os << "<end>}\n";
        return;
    }

    os << "{";
    declSummary(os, declData, dominator);

    auto doesDeclDominate = [&decl](auto const& censusNode) {
        auto const& [_, info] = censusNode;
        auto const& [__, d] = info;
        if(!d)
            return false;
        return decl == d.value();
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

void updateCensus(
        CensusDecl const* domDecl,
        DeclInfo domInfo,
        CensusDecl const* decl,
        DeclInfo info) {
    assert(decl);

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

    if (census.find(decl) == std::end(census)) {
        census.insert({decl, info});
    } else {
        // Decl is already in census.
        //  - Check that DeclData is same.
        auto const& [oldData, oldDomData] = census[decl];
        auto const& [newData, newDomData] = info;
        if(oldData != newData) {
            FOUT << "[WARN](updateCensus) decl in Census with different DeclData\n";
            FOUT << "[WARN](updateCensus) Old decl Data: {";
            dump(FOUT, oldData);
            FOUT << "[WARN](updateCensus) }";
            FOUT << "[WARN](updateCensus) New decl Data: {";
            dump(FOUT, newData);
            FOUT << "[WARN](updateCensus) }";
        }
        //  - If DominatorData is not present, update.
        if(!oldDomData) {
            census[decl] = {info};

        } else if(oldDomData.value() != newDomData.value()) {
            // Nothing to do. (YET)
            FOUT << "[WARN](updateCensus) decl in Census with different DominatorData\n";
            FOUT << "[WARN](updateCensus) Old DominatorData: {";
            dump(FOUT, oldDomData.value());
            FOUT << "[WARN](updateCensus) }";
            FOUT << "[WARN](updateCensus) New DominatorData: {";
            dump(FOUT, newDomData.value());
            FOUT << "[WARN](updateCensus) }";
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

    DominatorData castInfo{
        lhs,                                                // DominatorData.from
        lhsData,                                            // DominatorData.fromData
        toString(context, castExpr),                        // DominatorData.expr
        getCastExprType(*dest),                             // DominatorData.exprType
        getContainerFunction(context, castExpr),            // DominatorData.containerFunction
        getLinkedFunction(context, castExpr, *dest),        // DominatorData.linkedFunction
        location                                            // DominatorData.location
    };

    updateCensus(lhs, {lhsData,{}}, rhs, {rhsData, castInfo});

    FOUT << "Cast site: " << castInfo.location << "\n"
         << "    Casting: " << castInfo.fromData.ident << " -> " << rhsData.ident << "\n"
         << "       from: [" << castInfo.fromData.category << "] " << castInfo.fromData.type << "\n"
         << "         to: [" << rhsData.category << "] " << rhsData.type << "\n"
         << "       expr: " << "[" << castInfo.exprType << "] " << castInfo.expr << "\n"
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

    updateCensus(lhs, {lhsData, {}}, rhs, {rhsData, domData});

    /*
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
        //  - If DominatorData is not present, update.
        if(!cast) {
            census[rhs] = {data, castInfo};
        } else {
            FOUT << "[WARN](processVar) RHS in Census with different DominatorData\n";
            FOUT << "[WARN](processVar) Old RHS DominatorData: {";
            dump(FOUT, cast.value());
            FOUT << "[WARN](processVar) }";
            FOUT << "[WARN](processVar) New RHS DominatorData: {";
            dump(FOUT, castInfo);
            FOUT << "[WARN](processVar) }";
        }
    }
    */
}

//----------------------------------------------------------------------------

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
    //return Tool.run(newFrontendActionFactory<clang::SyntaxOnlyAction>().get());
    return Tool.run(newFrontendActionFactory(&Finder).get());
}

