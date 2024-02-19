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

using namespace clang::tooling;
using namespace llvm;

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::ento;

//StatementMatcher CastMatcher = castExpr(hasCastKind(CK_BitCast)).bind("cast");
StatementMatcher CastMatcher = castExpr().bind("cast");

// Apply a custom category to all cli options so that they are the only ones displayed
static llvm::cl::OptionCategory MyToolCategory("my-tool options");

// CommonOptionsParser declares HelpMessage with a description of the common cli options
// related to the compilation db and input files. (Nice to have help)
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);

// Help message for this specific tool.
static cl::extrahelp Morehelp("\nMore help text...\n");

class CastMatchCallback: public MatchFinder::MatchCallback {
    //BugReporter &BR_;
    //AnalysisDeclContext *ADC_;

public:
    //CastMatchCallback(BugReporter &Reporter, AnalysisDeclContext *Context)
    //    : BR_(Reporter), ADC_(Context) {}

    void run(MatchFinder::MatchResult const &result) override {
        auto *context = result.Context;
        if(CastExpr const * expr = result.Nodes.getNodeAs<clang::CastExpr>("cast"))
          //auto sr = expr->getSourceRange();
            std::cout << expr->getExprLoc().printToString(*result.SourceManager);
            std::cout << "\n";
            CastExpr const * expr2 = result.Nodes.getNodeAs<clang::CastExpr>("cast");
            expr2->dumpPretty(*context);
            //std::cout << expr->getExprLoc()->printToString(BR_.getSourceManager());
            std::cout << "\n";
            CastExpr const * expr3 = result.Nodes.getNodeAs<clang::CastExpr>("cast");
            expr3->dumpColor();
            std::cout << "\n";
            //e->getSubExprAsWritten();
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

