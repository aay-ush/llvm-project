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

#include <iostream>
#include <fstream>
#include <sstream>

#include "llvm/Support/raw_os_ostream.h"
//#include "llvm/Support/raw_ostream.h"
#include <string>

using namespace clang::tooling;
using namespace llvm;

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::ento;

// TODO: use TypeInfo from context to detect incompatible size casts.

// TODO: include expressions that contain castexprs, to get the source type of cast. Current match only gives destination type.
//   - It maybe unnecessary since parent can be retrieved instead.
//StatementMatcher CastMatcher = castExpr(hasCastKind(CK_BitCast)).bind("cast");

// TODO TODO
//  - Add function name in cast site.
//  - Add match for parameters so their casts can be tagged.
//  - Add info on cast operation (assignment, parameter passing, etc.)
//  - Create history tag for cast and function.
//  - Add missing cast dumps. For example in other cast types.(?).
StatementMatcher CastMatcher = castExpr(
                                allOf(
                                    hasCastKind(CK_BitCast),
                                    anyOf( // technically just any of expr or decl is needed.
                                        hasAncestor(varDecl().bind("var")),
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

std::ofstream FOUT;

// Stringer to get source statement from Stmt.
std::string toString(ASTContext *context, Stmt const *stmt) {
    assert(context);
    assert(stmt);
    clang::LangOptions defaultOps;
    std::string oStr;
    llvm::raw_string_ostream stream(oStr);
    //stmt->printPretty(stream, NULL, PrintingPolicy(defaultOps));
    auto policy = context->getLangOpts();
    stmt->printPretty(stream, NULL, policy);
    return oStr;
}

std::string prettyType(ASTContext *context, QualType qtype);
std::string prettyType(ASTContext *context, Expr const *expr);

// Stringer to get source statement from Expr.
std::string prettyType(ASTContext *context, Expr const *expr) {
    assert(context);
    assert(expr);
    auto qtype = expr->getType();
    return prettyType(context, qtype);
}

// Stringer to get source statement from QualType
std::string prettyType(ASTContext *context, QualType qtype) {
    assert(context);

    auto policy = context->getLangOpts();
    return qtype.getAsString(policy);
}

std::string prettyType(ASTContext *context, clang::Type const *type) {
    assert(context);
    assert(type);

    std::string oStr;
    llvm::raw_string_ostream stream(oStr);

    type->dump(stream, *context);
    return oStr; 
}

std::string getTypeClassName(ASTContext *context, Expr const *expr) {
    assert(context);
    assert(expr);
    auto qtype = expr->getType();
    auto const * type = qtype.getTypePtr();
    assert(type);
    return type->getTypeClassName();
}

class CastMatchCallback: public MatchFinder::MatchCallback {

public:
    void run(MatchFinder::MatchResult const &result) override {
        assert(result);
        auto *context = result.Context;
        assert(context);
        auto policy = context->getLangOpts();

        // Cast expression
        auto const *castExpr = result.Nodes.getNodeAs<clang::CastExpr>("cast");
        assert(castExpr);
        // Source operand (i.e. the expression which is being cast)
        //auto const *castSource = castExpr->getSubExprAsWritten();
        auto const *castSource = result.Nodes.getNodeAs<clang::DeclRefExpr>("castee");
        assert(castSource);

        std::string dest;
        auto const *var = result.Nodes.getNodeAs<clang::VarDecl>("var");
        auto const *gexpr = result.Nodes.getNodeAs<clang::Expr>("gexpr");
        auto const *binop = result.Nodes.getNodeAs<clang::BinaryOperator>("binop");
        auto const *call = result.Nodes.getNodeAs<clang::CallExpr>("call");

        if(!!var) {
            dest = var->getNameAsString();
        } else if(!!binop) {
            dest = toString(context, binop);
        } else if(!!call) {
            dest = toString(context, call);
        } else if(!!gexpr) {
            dest = toString(context, gexpr);
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

        FOUT << "Cast site: "
                  << castExpr->getExprLoc().printToString(*result.SourceManager)
                  << "\n";
        FOUT << "    Casting: "
                  << toString(context, castSource)
                  << "\n       from: ["
                  << getTypeClassName(context, castSource)
                  << "] "
                  << prettyType(context, castSource)
                  << "\n";

        FOUT << "         to: ["
                  << getTypeClassName(context, castExpr)
                  << "] "
                  << prettyType(context, castExpr)
                  << "\n";
        FOUT << "         in: "
                  << toString(context, castExpr)
                  << "\n";
        FOUT << " Destination: " << dest << "\n";
        FOUT << "\n";

        /*
        std::cout << "Casting to ["
                  << getTypeClassName(context, castExpr)
                  << "] "
                  <<  prettyType(context, castExpr) << "\n";

        std::cout << "    Subexpression type: ["
                  << getTypeClassName(context, castSource)
                  << "] "
                  <<  prettyType(context, castSource) << "\n";
        */

        /*
        // Pointee info
        auto qtype = castExpr->getType();
        auto const * type = qtype.getTypePtr();
        assert(type);
        auto qtt = type->getPointeeType();

        auto const * pointeeType = qtt.getTypePtr();
        assert(pointeeType);
        std::cout << "Pointee type: ["
                  << pointeeType->getTypeClassName()
                  << "] "
                  << prettyType(context, qtt) << "\n";

        //
        std::cout << "AST node:\n";
        castExpr->dumpColor();
        std::cout << "\n";
        */
    }
};

/*
namespace {
class CastCheckerMatcher: public Checker<check::ASTDecl<VarDecl>> {
public:
    void check(AnalysisManager &AM, BugReporter &BR) const;
}
}

void CastCheckerMatcher::check() const {
    MatchFinder f;
    CastMatchCallback cb(BR, AM.getAnalaysisDeclContext(TU));
    F.addMatcher(CastMatcher, &cb);
    F.matchAST(AM.getASTContext());
}
*/

///////////////////////////////////////////////////////////////////////////////////////////
// Steps to execute:
//  - TODO
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

