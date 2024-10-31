#ifndef CENSUS_H
#define CENSUS_H

#include "OpData.h"

//--
// Dominator info.
struct DominatorData {
    OpData from_;
    std::string expr_;
    std::string exprType_;
    //std::optional<std::string> callee_;
};

bool operator==(DominatorData const &lhs, DominatorData const &rhs) {
    return lhs.from_ == rhs.from_;
}
bool operator!=(DominatorData const &lhs, DominatorData const &rhs) {
    return !(lhs == rhs);
}
//--

using Dominators = std::vector<DominatorData>;
using UseDefInfo = std::pair<OpData, std::optional<Dominators>>;

// Census stores OpData & DominatorData for every operand/node. => Use + Def data
// OpData is not necessarily the key for census.
// OpData is computed based on information available from context at the time.
//  => It can change with expression. For example, `int i` has same hash for `i` and `&i` but different data types, hence technically different decl data. If OpData is used as key, this difference will be missed.
using CensusDecl = clang::Decl;
using CensusKey = std::string;
using Census = std::unordered_map<CensusKey, UseDefInfo>;

Census census;
using CensusNode = decltype(census)::value_type;

std::pair<CensusKey, UseDefInfo> makeCensusNode(
        OpData const &node,
        DominatorData const &dom) {
    return {node.qn_, {node, {{dom}}}};
}

std::pair<CensusKey, UseDefInfo> makeCensusSourceNode(OpData const &node) {
    return {node.qn_, {node, {}}};
}

UseDefInfo makeUseDefInfo(OpData const &node, DominatorData const &dom) {
    return {node, {{dom}}};
}

/*
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

std::string getCastExprType(clang::DeclStmt const&) {
    return getCastExprType<CastExprType::Assignment>();
}
std::string getCastExprType(clang::CallExpr const&) {
    return getCastExprType<CastExprType::FunctionCall>();
}
std::string getCastExprType(clang::UnaryOperator const&) {
    return getCastExprType<CastExprType::UnaryOp>();
}
std::string getCastExprType(clang::DeclRefExpr const&) {
    return getCastExprType<CastExprType::BinaryOp>();
}
*/
//--

void OpDebugSummary(std::ostream &os, OpData const &data) {
    os << data.type_
       << " (key_qn: " << data.qn_ << " )"
       << " (cat: " << data.category_ << ")"
       << " (expr: " << data.expr_ << ")"
       << " (lp: " << data.linkedParm_ << ") in " << data.container_ << "()"
       << " at " << data.location_.substr(data.location_.find_last_of('/') + 1);
}

void OpSummary(std::ostream &os, OpData const &data) {
    os << "[" << data.category_ << "]" << data.expr_ << ": '" << data.type_ << "'"
       << " " << data.linkedParm_ << " at " << data.location_.substr(
               data.location_.find_last_of('/') + 1);
}

/*
void OpHistorySummary(std::ostream &os, OpData const &data) {
    os << data.type_ << "    [" << data.category_ << "]" << data.expr_
       << " :" << data.linkedParm_ << ": at " << data.location_.substr(
               data.location_.find_last_of('/') + 1);
}
*/

void OpTypeSummary(std::ostream &os, OpData const &data) {
    os << "'" << data.type_ << "'" << " " << data.linkedParm_;
}
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

std::ostream& dump(std::ostream &os, UseDefInfo const &d);
std::ostream& dump(std::ostream &os, OpData const &info);
std::ostream& dump(std::ostream &os, Dominators const &doms);
std::ostream& dump(std::ostream &os, DominatorData const &info);
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

OpData const& ops(CensusNode const &n) {
    CNS_DEBUG("<CensusNode>");
    auto const &[_, info] = n;
    auto const &[op, __] = info;
    //FOUT << "[DEBUG](ops<CensusNode>) Returning op (" << op.hash_ << ")\n";
    CNS_DEBUG("<CensusNode> end.");
    return op;
}

OpData& ops(CensusNode &n) {
    CNS_DEBUG("<CensusNode:no const>");
    auto &[_, info] = n;
    auto &[op, __] = info;
    //FOUT << "[DEBUG](ops<CensusNode: no const>) Returning op (" << op.hash_ << ")\n";
    CNS_DEBUG("<CensusNode:no const> end.");
    return op;
}

/*
OpData const& ops(unsigned hash) {
    CNS_DEBUG("<hash>");
    auto const &[op, __] = census[hash];
    //FOUT << "[DEBUG](ops<hash>) Returning op (" << op.hash_ << ")\n";
    CNS_DEBUG("<hash> end.");
    return op;
}
*/

OpData const& ops(CensusKey k) {
    CNS_DEBUG("<cesuskey>");
    auto const &[op, _] = census[k];
    CNS_DEBUG("<cesuskey> end");
    return op;
}

CensusKey const& opKey(CensusNode const& n) {
    CNS_DEBUG("");
    auto const &op = ops(n);
    //FOUT << "[DEBUG]("opKey) Returning key (" << op.qn_ << ")\n";
    CNS_DEBUG(" end.");
    return op.qn_;
}

std::vector<CensusKey> UseChain(OpData const &op) {
    CNS_DEBUG("");
    //CNS_INFO("Usechain");
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
        //if(match != std::end(doms)) {
            //FOUT << "[DEBUG](UseChain) Op dominates (" << in.second.first.hash_ << ")\n";
        //}
        CNS_DEBUG("end.");
        return match != std::end(doms);
    };

    std::vector<CensusNode> t;
    std::copy_if(std::begin(census), std::end(census), std::back_inserter(t), doesOpDominate);
    std::vector<CensusKey> usechain;
    std::transform(std::begin(t), std::end(t), std::back_inserter(usechain), opKey);
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

//--
std::ostream& dump(std::ostream &os, UseDefInfo const &d) {
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
    os << "{key: '" << info.qn_
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
       //<< "', CalledFunction: '" << domInfo.callee_.value_or("(N/A)")
       << "}\n";

    return os;
}
//--

std::ostream& space(std::ostream &os, int indent);
void OpSummary(std::ostream &os, OpData const &data);
void OpHistorySummary(std::ostream &os, OpData const &data);

void censusSummary() {
    CNS_DEBUG("<void>");

    // build usechain
    for(auto const &[_, info]: census) {
       auto const &[op, __] = info;

       elaborateUse(op, {3});
    }
    for(auto const& n: census) {
        auto const& op = ops(n);
        //FOUT << "[INFO](censusSummary<void>) op: " << op.qn_ << "\n";
        op.use_ = UseChain(op);
        //FOUT << "[INFO](censusSummary<void>) op.use_.size(): " << op.use_.size() << "\n";
    }

    /*
    // build history
    for(auto const &[_, info]: census) {
        auto const &[op, __] = info;
        buildHistory(op);
    }

    for(auto const &[_, info]: census) {
        auto const &[op, __] = info;
        elaborateHistory(op, {2});
    }
    */

    // display summary
    for(auto const &[_, info]: census) {
        auto &[op, __] = info;

        //OpSummary(FOUT, op);
        OpDebugSummary(FOUT, op);
        FOUT << "\n";
        /*
        int indent = 0;
        for(auto const& u: op.history_) {
            space(FOUT, indent + 4);
            //FOUT << "-> {" << u << "}";
            FOUT << "|-";
            //OpHistorySummary(FOUT, census[u].first);
            OpDebugSummary(FOUT, census[u].first);
            FOUT << "\n";
        }
        FOUT << "\n\n";
        */
    }
    CNS_DEBUG("<void> end.");
}

std::ostream& space(std::ostream &os, int indent) {
    for(int i = 0; i != indent; i++) {
        os << " ";
    }
    return os;
}

/*
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
*/
#endif  // CENSUS_H
