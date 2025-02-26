#ifndef CENSUS_H
#define CENSUS_H

#include "OpData.h"

//--
struct CNSCondition {
    std::string condition_;
    std::string location_;
};

std::string String(CNSCondition const &c) {
    return c.condition_;
}

template<typename T>
CNSCondition getOriginCondition(ASTContext &context, T const &node) {
    auto const logKey = String(context, node) + " <T>";
    CNS_DEBUG_MSG(logKey, "begin");
    auto parents = context.getParents(node);
    if (parents.size() == 0) {
        CNS_INFO_MSG(logKey, "No parents found on node => no Origin Condition");
        CNS_DEBUG_MSG(logKey, "end");
        return {"NoCond", "N/A"};
    }

    CNS_DEBUG_MSG(logKey, "Found parents");
    while(parents.size() != 0) {
        if(auto const *casestmt = parents[0].template get<clang::CaseStmt>(); casestmt) {
            CNS_DEBUG_MSG(logKey, "Found parent case statement");

            // For switch-case, switch has condition (lhs), and case has match (rhs) value
            std::string rhs = String(context, *(casestmt->getLHS()));
            CNS_DEBUG(logKey, "Case match: {}", rhs);

            std::string lhs;
            auto cstmt = context.getParents(*casestmt); // Compound block
            auto swstmt = context.getParents(cstmt[0]);
            if(auto const* swt = swstmt[0].template get<clang::SwitchStmt>(); swt) {
                CNS_DEBUG_MSG(logKey, "Found parent switch statement");
                lhs = String(context, *(swt->getCond()));
                CNS_DEBUG(logKey, "Switch condition: {}", lhs);
            }

            CNS_DEBUG_MSG(logKey, "end");
            return {
                lhs + " == " + rhs,
                casestmt->getCaseLoc().printToString(context.getSourceManager())
            };
        }

        else if(auto const *ifstmt = parents[0].template get <clang::IfStmt>(); ifstmt) {
            CNS_DEBUG_MSG(logKey, "Found parent if statement");

            auto condition = String(context, *(ifstmt->getCond()));
            CNS_DEBUG(logKey, "Condition: {}", condition);

            CNS_DEBUG_MSG(logKey, "end");
            return {
                condition,
                ifstmt->getIfLoc().printToString(context.getSourceManager())
            };
        }

        CNS_DEBUG_MSG(logKey, "Checking grandparent");
        parents = context.getParents(parents[0]);
    }

    CNS_INFO_MSG(logKey, "Could not find origin condition");
    CNS_DEBUG_MSG(logKey, "end");
    return {"NoCond", "N/A"};
}
//
//

std::string getDomExprType(clang::ASTContext &context, clang::Expr const &e) {
    if(auto const * ce = castExpr_(&e); ce) {
        return "Cast";
    }

    if(auto const *memex = getMemberExpr(context, &e); memex) {
        // TODO CHK: Maybe get record type::member name/type
        return  "Member: `" + String(context, *memex) + "`";
    }

    return "N/A";
    // TODO check for decl
}

// Dominator info.
class DominatorData {
public:

    OpData op() const {
        return from_;
    }

    std::string linkType() const {
        return exprType_ + "(" + castKind_ + ")";
    }

    std::string exprType() const {
        return exprType_;
    }

    std::string castKind() const {
        return castKind_;
    }

    std::string linkExpr() const {
        return expr_;
    }

    CNSCondition parentCondition() const {
        return originCondition_;
    }

    DominatorData(OpData from, std::string expr, std::string exprType, std::string castKind, CNSCondition originCondition):
        from_(from),
        expr_(expr),
        exprType_(exprType),
        castKind_(castKind),
        originCondition_(originCondition) {}

    DominatorData() = default;
    ~DominatorData() = default;
    DominatorData(DominatorData const&) = default;
    DominatorData(DominatorData &&) = default;
    DominatorData& operator=(DominatorData const&) = default;
    DominatorData& operator=(DominatorData &&) = default;

private:
    OpData from_;
    std::string expr_;
    std::string exprType_ {};
    std::string castKind_ {};
    CNSCondition originCondition_ {"NoCond", "N/A"};
    //std::optional<std::string> callee_;
};

DominatorData makeDominatorData(clang::ASTContext &context, OpData from, clang::Expr const &expr) {
    return {
        from,
        String(context, expr),
        getDomExprType(context, expr),
        getCastKind(context, expr),
        getOriginCondition(context, expr)
        //getLinkedFunction(context, castExpr, dest)
    };
}

DominatorData makeDominatorData(clang::ASTContext &context, OpData from, clang::VarDecl const& var) {
    auto logKey = String(context, var);
    CNS_DEBUG_MSG(logKey, "begin");
    if(auto const *initEx = var.getInit(); initEx) {
        CNS_DEBUG(logKey, "Found init expr: '{}'", String(context, *initEx));
        CNS_DEBUG_MSG(logKey, "end");
        return {
            from,
            String(context, *initEx),
            "InitVarDecl " + getDomExprType(context, *initEx),
            getCastKind(context, *initEx),
            getOriginCondition(context, *initEx)
        };
    }

    CNS_DEBUG_MSG(logKey, "No init expr found for vardecl");
    CNS_DEBUG_MSG(logKey, "end");
    return {
        from,
        String(context, var),
        "VarDecl (No init)",
        "N/A",
        {"N/A", "N/A"}
    };
}


bool operator==(DominatorData const &lhs, DominatorData const &rhs) {
    return lhs.op() == rhs.op();
    //return (lhs.linkExpr() == rhs.linkExpr())
    //    && (lhs.linkType() == rhs.linkType());
}
bool operator!=(DominatorData const &lhs, DominatorData const &rhs) {
    return !(lhs == rhs);
}

std::string String(DominatorData const &d) {
    return d.op().qn_ + "{" + d.linkType() + "(" + String(d.parentCondition()) + ")}";
}
//

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

bool operator==(DominatorData const &lhs, CensusKey const &rhs) {
    return lhs.op().qn_ == rhs;
}
bool operator!=(DominatorData const &lhs, CensusKey const &rhs) {
    return !(lhs == rhs);
}

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
    CNS_DEBUG_MSG("<T>");
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
    CNS_DEBUG_MSG("<T> end");
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
    std::string sopds;
    sopds.reserve(1024);
    sopds = data.type_;
    sopds += " (key_qn: " + data.qn_ + " )"
        + " (cat: " + data.category_ + ")"
        + " (expr: " + data.expr_ + ")"
        + " (lp: " + data.linkedParm_ + ") in " + data.container_ + "()"
        + " at " + data.location_.substr(data.location_.find_last_of('/') + 1);

    return sopds;
}

std::string OpSummary(OpData const &data) {
    std::string sops;
    sops.reserve(1024);
    sops = "[" + data.category_ + "]" + data.expr_ + ": '" + data.type_ + "'"
        + " " + data.linkedParm_ + " at "
        + data.location_.substr(data.location_.find_last_of('/') + 1);

    return sops;
}

/*
void OpHistorySummary(std::ostream &os, OpData const &data) {
    os << data.type_ << "    [" << data.category_ << "]" << data.expr_
       << " :" << data.linkedParm_ << ": at " << data.location_.substr(
               data.location_.find_last_of('/') + 1);
}

void OpTypeSummary(std::ostream &os, OpData const &data) {
    os << "'" << data.type_ << "'" << " " << data.linkedParm_;
}
*/
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
    CNS_DEBUG_MSG(n.first, "begin");
    auto const &[_, info] = n;
    auto const &[__, doms_] = info;
    CNS_DEBUG_MSG(n.first, "end");
    return doms_;
}

OpData const& ops(CensusNode const &n) {
    CNS_DEBUG_MSG(n.first, "<CensusNode const> begin");
    auto const &[_, info] = n;
    auto const &[op, __] = info;
    CNS_DEBUG(n.first, "<CensusNode const> Returning op: {}", op.qn_);
    CNS_DEBUG_MSG(n.first, "<CensusNode const> end");
    return op;
}

OpData& ops(CensusNode &n) {
    CNS_DEBUG_MSG(n.first, "<CensusNode> begin");
    auto &[_, info] = n;
    auto &[op, __] = info;
    CNS_DEBUG(n.first, "ops<CensusNode> Returning op: {}", op.qn_);
    CNS_DEBUG_MSG(n.first, "<CensusNode> end");
    return op;
}

/*
OpData const& ops(unsigned hash) {
    CNS_DEBUG_MSG("<hash>");
    auto const &[op, __] = census[hash];
    //FOUT << "[DEBUG](ops<hash>) Returning op (" << op.hash_ << ")\n";
    CNS_DEBUG_MSG("<hash> end");
    return op;
}
*/

OpData const& ops(CensusKey const& k) {
    CNS_DEBUG_MSG(k, "<cesuskey> begin");
    auto const &[op, _] = census[k];
    CNS_DEBUG_MSG(k, "<cesuskey> end");
    return op;
}

CensusKey const& opKey(CensusNode const& n) {
    CNS_DEBUG_MSG(n.first, "begin");
    auto const &op = ops(n);
    CNS_DEBUG(n.first, "Returning key: {}", op.qn_);
    CNS_DEBUG_MSG(n.first, "end");
    return op.qn_;
}

std::vector<CensusKey> UseChain(OpData const &op) {
    CNS_DEBUG_MSG(op.qn_, "begin");
    auto doesOpDominate = [&op](CensusNode const &in) {
        CNS_DEBUG(op.qn_, "(doesOpDominate) op.qn_ = {}", op.qn_);
        auto const &doms_ = doms(in);
        if(!doms_)
            return false;
        auto const &doms = doms_.value();
        auto match = std::find_if(std::begin(doms), std::end(doms),
                [&op](auto const &d) {
                    return op == d.op();
                });
        //if(match != std::end(doms)) {
            //CNS_DEBUG("Op dominates {}", in.second.first.hash_);
        //}
        CNS_DEBUG_MSG(op.qn_, "end");
        return match != std::end(doms);
    };

    std::vector<CensusNode> t;
    std::copy_if(std::begin(census), std::end(census), std::back_inserter(t), doesOpDominate);
    std::vector<CensusKey> usechain;
    std::transform(std::begin(t), std::end(t), std::back_inserter(usechain), opKey);
    CNS_DEBUG_MSG(op.qn_, "end");
    return usechain;
}

/*
void elaborateUse(OpData const &node, std::optional<int> level) {
    CNS_DEBUG_MSG(node.qn_, "begin");
    node.use_.clear();
    // Use(node) = [i, Use(i) | i in UseChain(node)]
    auto const &usechain = UseChain(node);
    CNS_DEBUG(node.qn_, "Building use chain for {}", node.hash_);
    for(auto const &hash: usechain) {
        if(level && (level.value() > 0)) {
            CNS_DEBUG(node.qn_, "Level = {}", level.value());
            elaborateUse(ops(hash), level.value() - 1);
        }

        CNS_DEBUG(node.qn_, "Adding hash: {}", hash);
        // Add the node 'p' from usechain
        node.use_.push_back(hash);
        // Add use(p)
        if(level && (level.value() > 0)) {
            std::copy(begin(ops(hash).use_), end(ops(hash).use_), back_inserter(node.use_));
        }
    }
    CNS_DEBUG_MSG(node.qn_, "end");
}
*/

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
       << "', ExprType: '" << domInfo.castType_
       //<< "', CalledFunction: '" << domInfo.callee_.value_or("(N/A)")
       << "}\n";

    return os;
}
*/

std::string dump(UseDefInfo const &d) {
    auto const &[data, dominators] = d;
    std::string sud;
    sud.reserve(1024);
    sud.append(dump(data));
    if(!dominators) {
        return sud;
    }
    sud.append(dump(dominators.value()));
    sud += " -> " + dump(data);
    return sud;
}

std::string dump(OpData const &info) {
    //fmt::print(fOUT, "{{key: '{}', expr: '{}', type: '{}', category: '{}',
    //        linkedParameter: '{}', containerFunction: '{}', location: '{}}}'\n",
    //        info.qn_, info.expr_, info.type_, info.category_,
    //        info.linkedParm_, info.container_, info.location_);
    std::string sod;
    sod.reserve(128);
    sod = "{key: '" + info.qn_
        + "', expr: '" + info.expr_
        + "', type: '" + info.type_
        + "', category: '" + info.category_
        + "', linkedParameter: '" + info.linkedParm_
        + "', containerFunction: '" + info.container_
        + "', recordType: '" + info.linkedRecord_
        + "', recordCategory: '" + info.linkedRecordCategory_
        + "', location: '" + info.location_
        //+ "', castkind: '" + info.castKind_
        + "'}\n";
    return sod;
}

std::string dump(Dominators const& doms) {
    std::string sds;
    sds.reserve(128);
    for(auto const &d: doms) {
        sds.append(dump(d));
    }
    return sds;
}

std::string dump(DominatorData const& domInfo) {
    std::string sd;
    sd = "{from: " + dump(domInfo.op())
        + "', LinkingExpr: '" + domInfo.linkExpr()
        + "', LinkType: '" + domInfo.linkType()
        + "', OriginExpr: '" + String(domInfo.parentCondition())
        //+ "', CalledFunction: '" + domInfo.callee_.value_or("(N/A)")
        + "}\n";
    return sd;
}
//--

std::string space(int indent);

void censusSummary() {
    constexpr auto logKey = "<cenSus>";
    CNS_DEBUG_MSG(logKey, "<void> begin");

    if(SEVERITY_FILTER & cns::logging::severity::Info) {
    // build usechain
    /*
    for(auto const &[_, info]: census) {
       auto const &[op, __] = info;

       elaborateUse(op, {3});
    }
    for(auto const& n: census) {
        auto const& op = ops(n);
        CNS_DEBUG("<void> op: {}", op.qn_);
        //op.use_ = UseChain(op);
        //elaborateUse(op, {3});
        CNS_DEBUG("<void> op.use_.size() = {}", op.use_.size());
    }
    */

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
    }
    CNS_DEBUG_MSG(logKey, "<void> end");
}

std::string space(int indent) {
    return std::string(indent, ' ');
}

/*
std::ostream& space(std::ostream &os, int indent);
void OpSummary(std::ostream &os, OpData const &data);
void OpHistorySummary(std::ostream &os, OpData const &data);

std::ostream& space(std::ostream &os, int indent) {
    for(int i = 0; i != indent; i++) {
        os << " ";
    }
    return os;
}
*/
/*
std::string getLinkedFunction(
        clang::ASTContext const &context,
        clang::CastExpr const &castExpr,
        clang::UnaryOperator const&) {

    CNS_DEBUG_MSG("<unaryop>");
    CNS_DEBUG_MSG("<unaryop> end");
    return "N/A";
}

std::string getLinkedFunction(
        clang::ASTContext const &context,
        clang::CastExpr const &castExpr,
        clang::DeclRefExpr const&) {

    CNS_DEBUG_MSG("<declrefexpr>");
    CNS_DEBUG_MSG("<declrefe> end");
    return "N/A";
}


std::string getLinkedFunction(
        clang::ASTContext &context,
        clang::CastExpr const &castExpr,
        clang::CallExpr const &call) {

    CNS_DEBUG_MSG("<callexpr>");
    auto const *calledFn = getCalleeDecl(call);
    assert(calledFn);
    if(!calledFn) {
        return "";
    }

    CNS_DEBUG_MSG("<callexpr> end");
    return calledFn->getNameAsString();
}
*/
#endif  // CENSUS_H
