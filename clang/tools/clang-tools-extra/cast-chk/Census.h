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
    CNS_DEBUGM("<T>");
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
    CNS_DEBUGM("<T> end");
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

std::string OpDebugSummary(OpData const &data) {
    std::stringstream ss;
    ss << data.type_
       << " (key_qn: " << data.qn_ << " )"
       << " (cat: " << data.category_ << ")"
       << " (expr: " << data.expr_ << ")"
       << " (lp: " << data.linkedParm_ << ") in " << data.container_ << "()"
       << " at " << data.location_.substr(data.location_.find_last_of('/') + 1);

    return ss.str();
}

std::string OpSummary(OpData const &data) {
    std::stringstream ss;
    ss << "[" << data.category_ << "]" << data.expr_ << ": '" << data.type_ << "'"
       << " " << data.linkedParm_ << " at " << data.location_.substr(
               data.location_.find_last_of('/') + 1);

    return ss.str();
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

std::string dump(UseDefInfo const &d);
std::string dump(OpData const &info);
std::string dump(Dominators const &doms);
std::string dump(DominatorData const &info);
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
    CNS_DEBUGM("");
    auto const &[_, info] = n;
    auto const &[__, doms_] = info;
    CNS_DEBUGM("end");
    return doms_;
}

OpData const& ops(CensusNode const &n) {
    CNS_DEBUGM("<CensusNode const>");
    auto const &[_, info] = n;
    auto const &[op, __] = info;
    CNS_DEBUG("<CensusNode const> Returning op: {}", op.qn_);
    CNS_DEBUGM("<CensusNode const> end");
    return op;
}

OpData& ops(CensusNode &n) {
    CNS_DEBUGM("<CensusNode>");
    auto &[_, info] = n;
    auto &[op, __] = info;
    CNS_DEBUG("ops<CensusNode> Returning op: {}", op.qn_);
    CNS_DEBUGM("<CensusNode> end");
    return op;
}

/*
OpData const& ops(unsigned hash) {
    CNS_DEBUGM("<hash>");
    auto const &[op, __] = census[hash];
    //FOUT << "[DEBUG](ops<hash>) Returning op (" << op.hash_ << ")\n";
    CNS_DEBUGM("<hash> end");
    return op;
}
*/

OpData const& ops(CensusKey k) {
    CNS_DEBUGM("<cesuskey>");
    auto const &[op, _] = census[k];
    CNS_DEBUGM("<cesuskey> end");
    return op;
}

CensusKey const& opKey(CensusNode const& n) {
    CNS_DEBUGM("");
    auto const &op = ops(n);
    CNS_DEBUG("Returning key: {}", op.qn_);
    CNS_DEBUGM(" end");
    return op.qn_;
}

std::vector<CensusKey> UseChain(OpData const &op) {
    CNS_DEBUGM("");
    auto doesOpDominate = [&op](CensusNode const &in) {
        CNS_DEBUG("(doesOpDominate) op.qn_ = {}", op.qn_);
        auto const &doms_ = doms(in);
        if(!doms_)
            return false;
        auto const &doms = doms_.value();
        auto match = std::find_if(std::begin(doms), std::end(doms),
                [&op](auto const &d) {
                    return op == d.from_;
                });
        //if(match != std::end(doms)) {
            //CNS_DEBUG("Op dominates {}", in.second.first.hash_);
        //}
        CNS_DEBUGM("end");
        return match != std::end(doms);
    };

    std::vector<CensusNode> t;
    std::copy_if(std::begin(census), std::end(census), std::back_inserter(t), doesOpDominate);
    std::vector<CensusKey> usechain;
    std::transform(std::begin(t), std::end(t), std::back_inserter(usechain), opKey);
    CNS_DEBUGM("end");
    return usechain;
}

void elaborateUse(OpData const &node, std::optional<int> level) {
    CNS_DEBUGM("");
    node.use_.clear();
    // Use(node) = [i, Use(i) | i in UseChain(node)]
    auto const &usechain = UseChain(node);
    CNS_DEBUG("Building use chain for {}", node.hash_);
    for(auto const &hash: usechain) {
        if(level && (level.value() > 0)) {
            CNS_DEBUG("Level = {}", level.value());
            elaborateUse(ops(hash), level.value() - 1);
        }

        CNS_DEBUG("Adding hash: {}", hash);
        // Add the node 'p' from usechain
        node.use_.push_back(hash);
        // Add use(p)
        if(level && (level.value() > 0)) {
            std::copy(begin(ops(hash).use_), end(ops(hash).use_), back_inserter(node.use_));
        }
    }
    CNS_DEBUGM("end");
}

//--
/*
std::ostream& dump(std::ostream &os, UseDefInfo const &d);
std::ostream& dump(std::ostream &os, OpData const &info);
std::ostream& dump(std::ostream &os, Dominators const &doms);
std::ostream& dump(std::ostream &os, DominatorData const &info);

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
*/

std::string dump(UseDefInfo const &d) {
    auto const &[data, dominators] = d;
    std::stringstream ss;
    ss << dump(data);
    if(!dominators) {
        return ss.str();
    }
    ss << dump(dominators.value()) << " -> " << dump(data);
    return ss.str();
}

std::string dump(OpData const &info) {
    //fmt::print(fOUT, "{{key: '{}', expr: '{}', type: '{}', category: '{}',
    //        linkedParameter: '{}', containerFunction: '{}', location: '{}}}'\n",
    //        info.qn_, info.expr_, info.type_, info.category_,
    //        info.linkedParm_, info.container_, info.location_);
    std::stringstream ss;
    ss << "{key: '" << info.qn_
       << "', expr: '" << info.expr_
       << "', type: '" << info.type_
       << "', category: '" << info.category_
       << "', linkedParameter: '" << info.linkedParm_
       << "', containerFunction: '" << info.container_
       << "', location: '" << info.location_
       << "'}\n";
    return ss.str();
}

std::string dump(Dominators const& doms) {
    std::stringstream ss;
    for(auto const &d: doms) {
        ss << dump(d);
    }
    return ss.str();
}

std::string dump(DominatorData const& domInfo) {
    std::stringstream ss;
    ss << "{from: " << dump(domInfo.from_)
       << "', LinkingExpr: '" << domInfo.expr_
       << "', ExprType: '" << domInfo.exprType_
       //<< "', CalledFunction: '" << domInfo.callee_.value_or("(N/A)")
       << "}\n";
    return ss.str();
}
//--

std::ostream& space(std::ostream &os, int indent);
void OpSummary(std::ostream &os, OpData const &data);
void OpHistorySummary(std::ostream &os, OpData const &data);

void censusSummary() {
    CNS_DEBUGM("<void>");

    // build usechain
    /*
    for(auto const &[_, info]: census) {
       auto const &[op, __] = info;

       elaborateUse(op, {3});
    }
    */
    for(auto const& n: census) {
        auto const& op = ops(n);
        CNS_DEBUG("<void> op: {}", op.qn_);
        //op.use_ = UseChain(op);
        elaborateUse(op, {3});
        CNS_DEBUG("<void> op.use_.size() = {}", op.use_.size());
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
        fmt::print(fOUT, "{}\n\n", OpDebugSummary(op));
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
    CNS_DEBUGM("<void> end");
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

    CNS_DEBUGM("<unaryop>");
    CNS_DEBUGM("<unaryop> end");
    return "N/A";
}

std::string getLinkedFunction(
        clang::ASTContext const &context,
        clang::CastExpr const &castExpr,
        clang::DeclRefExpr const&) {

    CNS_DEBUGM("<declrefexpr>");
    CNS_DEBUGM("<declrefe> end");
    return "N/A";
}


std::string getLinkedFunction(
        clang::ASTContext &context,
        clang::CastExpr const &castExpr,
        clang::CallExpr const &call) {

    CNS_DEBUGM("<callexpr>");
    auto const *calledFn = getCalleeDecl(call);
    assert(calledFn);
    if(!calledFn) {
        return "";
    }

    CNS_DEBUGM("<callexpr> end");
    return calledFn->getNameAsString();
}
*/
#endif  // CENSUS_H
