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

struct CastOperand {
    std::string ident;
    std::string type;
    std::string category;
    std::string linkedParameter;
};

// todo check annonymous cast expressions.
using CastOperandDecl = clang::Decl;

struct CastData {
    CastOperand from;
    CastOperand to;
    std::vector<CastOperandDecl const*> lchain;
    std::vector<CastOperandDecl const*> rchain;
    std::string expr;
    std::string exprType;
    std::string containerFunction;
    std::optional<std::string> linkedFunction;
    std::string sourceLine;
};

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
    };
}

std::string getCastExprType(clang::DeclStmt const&) {
    return getCastExprType<CastExprType::Assignment>();
}
std::string getCastExprType(clang::CallExpr const&) {
    return getCastExprType<CastExprType::FunctionCall>();
}


template<>
struct std::hash<CastOperandDecl>
{
    unsigned operator()(CastOperandDecl const* decl) const noexcept {
        ODRHash hasher;
        hasher.AddDecl(decl);
        return hasher.CalculateHash();
    }
};

// Census adds cast history to a `decl`.
using Census = std::unordered_map<CastOperandDecl const*, CastData>;
Census census;

//std::ostream& operator<<(std::ostream &os, std::vector<CastData*> const & list);
std::ostream& operator<<(std::ostream &os, std::vector<CastOperandDecl const*> const & list);
std::ostream& operator<<(std::ostream &os, CastData const & info);
std::ostream& operator<<(std::ostream &os, CastOperand const & op) {
    os << "[" << op.category << "] " << op.ident << " [" << op.linkedParameter << "] <" << op.type << ">";
    return os;
}

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
std::ostream& operator<<(std::ostream &os, std::vector<CastOperandDecl*> const & list) {
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

std::ostream& dump(std::ostream &os, CastData const & info);
std::string dumpChain(std::ostream &os, std::vector<CastOperandDecl const*> const & list);

std::string dump(std::ostream &os, CastOperand const & info) {
    std::stringstream ss;
    ss << "{ident: '" << info.ident << "', type: '" << info.type << "', "
       << "category: '" << info.category << "', linkedParameter: '" << info.linkedParameter << "'},\n";

    return ss.str();
}
std::ostream& dump(std::ostream &os, CastData const & info) {
    os << "{from: '" << dump(os, info.from) << "', to: '" << dump(os, info.to) << "', "
       << "expr: '" << info.expr << "', exprType: '" << info.exprType << "', "
       << "containerFunction: '" << info.containerFunction << "', "
       << "linkedFunction: '" << info.linkedFunction.value_or("(none)") << "', "
       << "location: '" << info.sourceLine << "}, "
       << "lchainlength: '" << info.lchain.size() << "', lchain: {" << dumpChain(os, info.lchain) << "},\n"
       << "rchainlength: '" << info.rchain.size() << "', rchain: {" << dumpChain(os, info.rchain) << "},\n";

    return os;
}
std::string dumpChain(std::ostream &os, std::vector<CastOperandDecl const*> const & list) {
    std::stringstream ss;
    for(auto const& i: list) {
        if(i)
            dump(ss, census[i]);
    }
    return ss.str();
}

std::string censusSummary(std::ostream& os, std::vector<CastOperandDecl const*> chain);
std::string censusSummary(std::ostream& os, CastOperandDecl const* decl) {
    assert(decl);
    if (census.find(decl) == std::end(census)) {
        return "x";
    }

    std::stringstream ss;
    auto const& cast = census[decl];
    // check whether decl matches from or to;
    ss << census[decl].from.ident << " (" << census[decl].from.type << ")"
       << " -> " << censusSummary(os, census[decl].rchain);

    return ss.str();
    
}
std::string censusSummary(std::ostream& os, std::vector<CastOperandDecl const*> chain) {
    if (!chain.size())
        return "X << end of chain\n";

    std::stringstream ss;
    for(auto const& decl: chain) {
        censusSummary(ss, decl);
    }
    ss << "\n";

    return ss.str();
}

std::ostream& censusSummary(std::ostream& os) {
    os << "# Census start:\n";
    for(auto const& [decl, cast]: census) {
        //os << censusSummary(os, decl);
        // check whether decl matches from or to;
        os << cast.from.ident << " (" << cast.from.type << ")"
           << " -> " << censusSummary(os, cast.rchain);
    }
    os << "# Census end:\n";

    return os;
}

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

CastOperand buildCastOperand(
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

CastOperand buildCastOperand(
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

CastOperand buildCastOperand(
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

CastOperandDecl const* getTargetDecl(
        clang::ASTContext const& context,
        clang::CastExpr const& castExpr,
        clang::DeclStmt const& t) {

    return t.getSingleDecl();  // TODO examples where decls() or DeclGroup() may be needed.
}
CastOperandDecl const* getTargetDecl(
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

using CensusNode = std::pair<CastOperandDecl const*, CastData>;

template<typename T>
std::tuple<CensusNode, CensusNode> buildCensus(clang::ASTContext & context);

template<typename T>
void updateCensus(clang::ASTContext & context,
        clang::CastExpr const& castExpr,
        clang::DeclRefExpr const& castSource,
        T const* dest,
        std::string const& location) {

    assert(dest);
    CastData info{
        buildCastOperand(context, castExpr, castSource),    // CastData.from
        buildCastOperand(context, castExpr, *dest),         // CastData.to
        {},                                                 // lchain
        {},                                                 // rchain
        toString(context, castExpr),                        // CastData.expr
        getCastExprType(*dest),                             // CastData.exprType
        getContainerFunction(context, castExpr),            // castData.containerFunction
        getLinkedFunction(context, castExpr, *dest),        // CastData.linkedFunction
        location                                            // CastData.sourceLine
    };

    auto appendLChain = [](auto info, auto const& decl) -> auto {
        info.lchain.push_back(decl);
        return info;
    };
    auto appendRChain = [](auto info, auto const& decl) -> auto {
        info.rchain.push_back(decl);
        return info;
    };

    auto lhs = castSource.getDecl();
    auto rhs = getTargetDecl(context, castExpr, *dest);

    if (census.find(lhs) == std::end(census)) {
        census.insert({lhs, appendRChain(info, rhs)});
    } else {
        // LHS is already in census => add rhs to RChain.
        // Assumption: if rhs is in chain, rhs CastData does not need an update (other than lchain update).
        appendRChain(census[lhs], rhs);
    }

    if (census.find(rhs) == std::end(census)) {
        census.insert({rhs, appendLChain(info, lhs)});
    } else {
        // RHS is already in census => add lhs to LChain.
        // Assumption: if lhs is in chain, lhs CastData does not need an update (other than rchain update).
        appendLChain(census[rhs], lhs);
    }

    FOUT << "Cast site: " << info.sourceLine << "\n"
         << "    Casting: " << info.from.ident << " -> " << info.to.ident << "\n"
         << "       from: [" << info.from.category << "] " << info.from.type << "\n"
         << "         to: [" << info.to.category << "] " << info.to.type << "\n"
         << "       expr: " << "[" << info.exprType << "] " << info.expr << "\n"
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

