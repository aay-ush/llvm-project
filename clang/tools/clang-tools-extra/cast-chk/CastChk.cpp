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

#include <iostream>

#include "llvm/Support/raw_ostream.h"
#include <string>

using namespace clang::tooling;
using namespace llvm;

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::ento;

// TODO: use TypeInfo from context to detect incompatible size casts.

// TODO: include expressions that contain castexprs, to get the source type of cast. Current match only gives destination type.
StatementMatcher CastMatcher = castExpr(hasCastKind(CK_BitCast)).bind("cast");
//StatementMatcher CastMatcher = castExpr().bind("cast");

// Apply a custom category to all cli options so that they are the only ones displayed
static llvm::cl::OptionCategory MyToolCategory("my-tool options");

// CommonOptionsParser declares HelpMessage with a description of the common cli options
// related to the compilation db and input files. (Nice to have help)
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);

// Help message for this specific tool.
static cl::extrahelp Morehelp("\nMore help text...\n");

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

std::string prettyType(ASTContext *context, Expr const *expr) {
    assert(context);
    assert(expr);
    std::string type;
    llvm::raw_string_ostream stream(type);
    auto qtype = expr->getType();
    auto policy = context->getLangOpts();
    qtype.print(stream, policy);
    return type;
}

class CastMatchCallback: public MatchFinder::MatchCallback {
    //BugReporter &BR_;
    //AnalysisDeclContext *ADC_;

public:
    //CastMatchCallback(BugReporter &Reporter, AnalysisDeclContext *Context)
    //    : BR_(Reporter), ADC_(Context) {}

    void run(MatchFinder::MatchResult const &result) override {
        auto *context = result.Context;
        assert(context);
        auto policy = context->getLangOpts();

        auto const *expr = result.Nodes.getNodeAs<clang::CastExpr>("cast");
        assert(expr);

        /* Dumps the whole AST!
        std::cout << "TUD:\n";
        auto *tud = context->getTranslationUnitDecl();
        tud->dumpAsDecl();
        */

        std::cout << "Cast site: "
                  << expr->getExprLoc().printToString(*result.SourceManager)
                  << "\n";
        std::cout << "Cast expression: "
                  << toString(context, expr)
                  << "\n";
        //expr->dumpPretty(*context);
        //std::cout << "\n";

        if(auto const *subExpr = expr->getSubExprAsWritten())
        {
            std::cout << "    Subexpr:\n"
                      << toString(context, expr)
                      << "\n";
            //->dumpPretty(*context);
            //std::cout << "]\n";
        }

        std::string typeStr;
        llvm::raw_string_ostream stream(typeStr);

        auto qtype = expr->getType();
        qtype.print(stream, policy);
        //stream.flush();

        auto const * type = qtype.getTypePtr();
        assert(type);

        std::cout << "Cast Expression type: ["
                  << type->getTypeClassName()
                  << "] "
                  <<  prettyType(context, expr) << "\n";
        //type->dump();
        //std::cout << "\n";

        //auto qtt = ty->getPointeeType();
        auto qtt = type->getPointeeType();
        auto const *pointeeType = qtt.getTypePtr();
        //auto const *tyy = ty->getPointeeOrArrayElementType();
        if(pointeeType) {
            std::string pointeeTypeStr;
            llvm::raw_string_ostream pointeeStream(typeStr);
            /*
            std::cout << "    Pointee type: ["
                      << pointeeType->getTypeClassName()
                      << "] "
                      << prettyType(context, pointeeType) << "\n";// Dump:";
            */
            //pointeeType->dump();
            //std::cout << "\n";
        }


        /*
        std::cout << "AST node:\n";
        expr->dumpColor();
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

    //return Tool.run(newFrontendActionFactory<clang::SyntaxOnlyAction>().get());
    return Tool.run(newFrontendActionFactory(&Finder).get());
}

