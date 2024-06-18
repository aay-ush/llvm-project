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

using DeclInfo = std::pair<DeclData, std::optional<CastData>>;

template<>
struct std::hash<CensusDecl>
{
    unsigned operator()(CensusDecl const* decl) const noexcept {
        ODRHash hasher;
        hasher.AddDecl(decl);
        return hasher.CalculateHash();
    }
};

// Census stores type & cast data for every decl.
using Census = std::unordered_map<CensusDecl const*, DeclInfo>;
Census census;

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

void dump(std::ostream& os, DeclInfo const& d) {
    auto const& [decl, cast] = d;
    std::stringstream ss;
    dump(ss, decl);
    if(!cast) {
        //dump(os, decl);
        os << ss.str();
        return;
    }
    dump(os, cast.value()) << " ---> " << ss.str();
}


void censusSummary(std::ostream &os, DeclData const& data, std::optional<CastData const> const& castData) {
    os << "[" << data.category << "] " << data.ident << "(" << data.type << ")"
       << " {" << data.linkedParameter << "}";
    if(castData) {
        auto const& c = castData.value();
        os << "<" << c.containerFunction << ">"; //": " << c.location << ">";
    }
    os << "  ";
}

void censusSummary(std::ostream &os, CensusDecl const* decl, DeclData const& data, std::optional<CastData const> const& castData) {
    assert(decl);
    if(!decl) {
        os << "x\n";
        return;
    }

    os << " [";
    censusSummary(os, data, castData);
    os << "-> ";

    auto match = std::find_if(std::begin(census),
                              std::end(census),
                              [&decl](auto const& i) {
                                auto const& [_, data] = i;
                                auto const& [__, castData] = data;
                                if(!castData)
                                    return false;

                                return (decl->getID() == castData.value().from->getID());
                              });
    if(match == std::end(census)) {
        os << "x\n]\n";
        return;
    }

    auto const& [rdecl, info] = *match;
    auto const& [rdata, castdata] = info;
    return censusSummary(os, rdecl, rdata, castdata);
}

void censusSummary(std::ostream &os) {
    Census uncasts;
    std::remove_copy_if(std::begin(census),
                        std::end(census),
                        std::inserter(uncasts, std::end(uncasts)),
                        [](auto const& i) {
                            auto const& [_, data] = i;
                            auto const& [__, castData] = data;
                            return castData.has_value();
                        });
    
    for(auto const& [decl, info]: census) {
        auto const& [data, cast] = info;
        censusSummary(os, decl, data, cast);
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
Change DeclData dump from:
{from: '{ident: 'p', type: 'void *', category: 'Pointer', linkedParameter: '{f.$0: void *} p'},
to
{from: '{ident: 'p', type: 'void *', category: 'Pointer', linkedParameter: '{f.$0: void *}'},

making it explicit that linkedParameter identifies parameter function and position of ident.
*/
/*
    ss << "[INFO](censusSummaryDecl) ";
    // check whether decl matches from or to;
    ss << census[decl].from.ident << " (" << census[decl].from.type << ")"
       << " -> " << censusSummary(os, census[decl].rchain);
    if (!chain.size())
        return "<< end of chain\n";
    ss << "[INFO](censusSummaryChain) ";
    //os << "# Census start:\n";
    os << "[INFO](censusSummary) ";
        // check whether decl matches from or to;
        os << cast.to.ident << " (" << cast.to.type << ")"
           << " -> " << censusSummary(os, cast.rchain);
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
StatementMatcher CastMatcher = castExpr(
                                allOf(
                                    hasCastKind(CK_BitCast),
                                    anyOf( // technically just any of expr or decl is needed.
                                        hasAncestor(declStmt().bind("var")),
                                        hasAncestor(binaryOperator().bind("binop")),
                                        hasAncestor(callExpr().bind("call")),
                                        hasAncestor(expr().bind("gexpr"))),
                                    hasDescendant(declRefExpr().bind("castee")))
                                ).bind("cast");


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
        clang::CastExpr const& castExpr,
        clang::DeclarationNameInfo const& name) {

    auto const *fn = getContainerFunctionDecl(context, castExpr);
    assert(fn);

    if(auto parmPos = getParameterMatch(fn, name)) {
        return toString(context, *fn, *parmPos);
    }

    // TODO: Reach decl. Assign chain?
    return "(Not a param)";
}

DeclData buildDeclData(
        clang::ASTContext & context,
        clang::CastExpr const& castExpr,
        clang::DeclRefExpr const& e) {

    return {
        toString(context, e),                                       // ident
        typeof(context, e),                                         // type
        getTypeCategoryName(context, e),                            // category
        functionParameterMatch(context, castExpr, e.getNameInfo())   // linkedParameter
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
std::string getLinkedFunction(
        clang::ASTContext & context,
        clang::CastExpr const& castExpr,
        clang::CallExpr const& call) {

    // Get function body and trigger match on function casts.
    // parm->getOriginalType : QualType
    // parm->getFunctionScopeIndex: unsigned parameter index
    auto const * calledFn = call.getDirectCallee();
    assert(calledFn);

    // Trigger cast check for the called function.
    if(calledFn->hasBody()) {
        auto const *body = calledFn->getBody();
        assert(body);
        MatchFinder m;
        m.match(*body, context);
    }

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

    if (match != t.arg_end()) {
        return getParamDecl(context, t, argPos - 1);
    }
    return nullptr;
}

//using CensusNode = decltype(census)::value_type;

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

    auto lhs = castSource.getDecl();
    if(!lhs) {
        FOUT << "[ERROR](updateCensus) LHS Decl == nullptr\n";
    }
    auto lhsData = buildDeclData(context, castExpr, castSource);
    auto rhs = getTargetDecl(context, castExpr, *dest);
    if(!rhs) {
        FOUT << "[ERROR](updateCensus) LHS Decl == nullptr\n";
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

    census.insert({lhs, {lhsData, {}}});
    census.insert({rhs, {rhsData, castInfo}});

    /*
    auto appendLChain = [](auto castInfo, auto const& decl) -> auto {
        castInfo.lchain.push_back(decl);
        return castInfo;
    };
    auto appendRChain = [](auto castInfo, auto const& decl) -> auto {
        castInfo.rchain.push_back(decl);
        return castInfo;
    };
    */


    /*
    if (census.find(lhs) == std::end(census)) {
        census.insert({lhs, appendRChain(castInfo, rhs)});
    } else {
        // LHS is already in census => add rhs to RChain.
        // Assumption: if rhs is in chain, rhs CastData does not need an update (other than lchain update).
        appendRChain(census[lhs], rhs);
    }
    */
    /*
    census.insert({lhs, {buildDeclData(context, castExpr, castSource),{},{},{},{},{},{},{},{}}});

    if (census.find(rhs) == std::end(census)) {
        census.insert({rhs, appendLChain(castInfo, lhs)});
    } else {
        // RHS is already in census => add lhs to LChain.
        // Assumption: if lhs is in chain, lhs CastData does not need an update (other than rchain update).
        appendLChain(census[rhs], lhs);
    }
    */

    FOUT << "Cast site: " << castInfo.location << "\n"
         << "    Casting: " << castInfo.fromData.ident << " -> " << rhsData.ident << "\n"
         << "       from: [" << castInfo.fromData.category << "] " << castInfo.fromData.type << "\n"
         << "         to: [" << rhsData.category << "] " << rhsData.type << "\n"
         << "       expr: " << "[" << castInfo.exprType << "] " << castInfo.expr << "\n"
         << "\n";

    /*
    // TODO TODO Find existing cast where RHS decl is lhs
    //  For every node in census (unord_map),
    //   - if key.second = source  => ++LHS -> key.chain.add(cast)
    //   - if key.first = source   => ??
    //   - if key.second = target  => ??
    //   - if key.first = target   => RHS++ -> cast.chain.add(key)
    // Build the cast chain using census map.
    auto isLinkedToCast = [&](auto const& castop, auto const& match) -> auto {
        if (!castop || !match)
            return false;

        return (castop->getID() == match->getID());
    };

    auto rhsLink = std::find_if(std::begin(census), std::end(census),
                            [&](auto const& node) {
                                auto const& lhsDecl = node.first.first;
                                return isLinkedToCast(lhsDecl, cast.first.second);
                            });
    if (rhsLink != census.end() && rhsLink->first != cast.first) {
        auto& [_, rhs] = *rhsLink;
        cast.second.chain.push_back(&rhs);
    }
    
    census.insert(cast);

    auto lhsLink = std::find_if(std::begin(census), std::end(census),
                            [&](auto const& node) {
                                auto const& rhsDecl = node.first.second;
                                return isLinkedToCast(rhsDecl, cast.first.first);
                            });
    if (lhsLink != census.end() && lhsLink->first != cast.first) {
        auto& [_, lhs] = *lhsLink;
        lhs.chain.push_back(&census[cast.first]);
    }
    */

}


//---
class CastMatchCallback: public MatchFinder::MatchCallback {

public:
    void run(MatchFinder::MatchResult const &result) override {
        assert(result);
        auto *context = result.Context;
        assert(context);
        //auto policy = context->getLangOpts();

        // Cast expression
        auto const *castExpr = result.Nodes.getNodeAs<clang::CastExpr>("cast");
        assert(castExpr);

        // Source operand (i.e. the expression which is being cast)
        //auto const *castSource = castExpr->getSubExprAsWritten();
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

        /* Dumps the whole AST!
        std::cout << "TUD:\n";
        auto *tud = context->getTranslationUnitDecl();
        tud->dumpAsDecl();
        */

        if(!FOUT.is_open()) {
            std::cout << "File open error.\n";
            return;
        }

        FOUT << "# Census collection so far:\n";
        for(auto const& [_, data]: census) {
           
            /*
            FOUT //<< "Cast expression: <" << toString(context, cast.first)
                 //<< " | " << toString(context, cast.second) << ">\n"
                 << "Data:\n" << data << "\n";
            */
            dump(FOUT, data);
        }
        FOUT << "# Census end.\n";

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

    FOUT.open("census-dump.txt", std::ios::out);
    //return Tool.run(newFrontendActionFactory<clang::SyntaxOnlyAction>().get());
    return Tool.run(newFrontendActionFactory(&Finder).get());
}

