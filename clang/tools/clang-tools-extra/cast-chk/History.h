#ifndef HISTORY_H
#define HISTORY_H

#include "Census.h"
#include "utils.h"

#include <functional>
#include <regex>

// History context contains substitutions for HistoryTemplate that can be used to provide history.
// How are the pairings stored?
// Context is extensible.
// H(a) = H(f.0) => HT(f) is instatiated with f.0=a
// Context stores 0=a
// Instantiate returns c1 with f.0=a
// H(b) = H(f.1) => C1 is extended with f.1 = b
// Instantiate returns c2 with f.0=a and f.1=b
// => context is not composable.
// Context should be composable? What if ht is instantiated separately and then combined? -> implies partial instantiation -> avoid.
// Context lives with an HT object. Each HT object has one context only. wrong. Since many versions or complete instatitations are possible.
// Avoiding composability avoid problems where two context with different values of same parameter are combined.
using HistoryContext = std::unordered_map<CensusKey, CensusKey>;

class History;
//using LocalHistoryNO = std::pair<std::reference_wrapper<History const>, std::optional<HistoryContext>>;

class LocalHistory {
public:
    LocalHistory(History const& domH, DominatorData const& domInfo, std::optional<HistoryContext> lc = {}):
        linkInfo_(domInfo),
        h_(domH),
        context_(lc) {}

    std::string id() const;
    History const& history() const;

    std::optional<HistoryContext> context() const {
        return context_;
    }

    DominatorData linkInfo() const {
        return linkInfo_;
    }

private:
    DominatorData linkInfo_;    // cast type, etc
    std::reference_wrapper<History const> h_;
    std::optional<HistoryContext> context_;
};

// Template: for every parameter in a function, store it's Census operand and associate it to the parameter.
class HistoryTemplate {
    public:
        // History instantiation requires all args to be instantiated as a unit.
        // Callexpr has an arg iterator that can be used to instantiate all args for every arg op.
        //<> instantiate(<> varargs, unsigned argPos);
        std::vector<LocalHistory> instantiate(clang::ASTContext &context, clang::CallExpr const &call);

        HistoryTemplate(clang::FunctionDecl const &fn);
        HistoryTemplate(clang::ASTContext &context, clang::CallExpr const& call, clang::DeclRefExpr const &fptr);

        HistoryTemplate() = delete;
        ~HistoryTemplate() = default;
        HistoryTemplate(HistoryTemplate const&) = default;
        // TODO assignment

        std::string name() const {
            return function_;
        }

    private:
        std::string function_;
        std::vector<CensusKey> params_;
};

bool operator==(HistoryTemplate const &a, HistoryTemplate const&b) {
    // Since C does not have overloading, function names will not be same.
    return a.name() == b.name();
}
bool operator!=(HistoryTemplate const &a, HistoryTemplate const&b) {
    return !(a == b);
}
bool operator==(HistoryTemplate const &a, std::string const&b) {
    return a.name() == b;
}
bool operator!=(HistoryTemplate const &a, std::string const&b) {
    return !(a.name() == b);
}

namespace {
    HistoryContext makeHistoryContext(
            std::vector<CensusKey> const &params,
            clang::ASTContext &context,
            clang::CallExpr const &call) {

        LOG_FUNCTION_TIME;
        auto const logKey = String(context, call);
        CNS_DEBUG_MSG(logKey, "begin");

        HistoryContext hc;
        std::vector<CensusKey> args;
        std::regex functionPrefix("^.*?\\$");
        std::regex fptrParmX("^.*?\\$.*\\$");

        unsigned i = 0;
        std::for_each(call.arg_begin(), call.arg_end(), [&](auto const *a) {
            CNS_DEBUG_MSG(logKey, "for_each arg.");
            // get arg qn
            auto const key = qualifiedName(context, *a); //qualifiedName(context, call, *a);
            if(census.find(key) == census.end()) {
                // Shouldn't really happen.
                CNS_ERROR(logKey, "arg operand ({}) not in Census", key);
                CNS_ERROR_MSG(logKey, "continue.");
                CNS_DEBUG_MSG(logKey, "end");
                hc[params[i]] = params[i];
                i++;
                return;
            }

            // Avoid creating links between parameters e.g. hof.$1.$0 = hof.$0
            std::smatch matchParm, matchArg;
            if(std::regex_search(params[i], matchParm, functionPrefix)
                && std::regex_search(key, matchArg, functionPrefix)) {
                if(matchParm.str() == matchArg.str()) {
                    CNS_INFO(logKey, "Arg operand ({}) and param ({}) are both parameters of same function ({}), skipping", key, params[i], matchArg.str());
                    hc[params[i]] = params[i];
                    i++;
                    return;
                }
            }

            // If parameter is fptr param, use arg = param mapping to facilitate resolving fptr
            std::smatch fptrParm;
            if(std::regex_search(params[i], fptrParm, fptrParmX)) {
                CNS_DEBUG(logKey, "Found fptr parm: {{key, value}} = {{{}, {}}}", params[i], key);
                CNS_DEBUG(logKey, "Reversed context pair: {{key, value}} = {{{}, {}}}", key, params[i]);
                hc[key] = params[i++];
            }
            else {
                CNS_DEBUG(logKey, "{{key, value}} = {{{}, {}}}", params[i], key);
                hc[params[i++]] = key;
            }
            args.push_back(key);
            return;
        });

        CNS_DEBUG(logKey, "{} produced {} args", String(context, call), args.size());
        CNS_DEBUG_MSG(logKey, "end");

        return hc;
    }

    inline std::string regex_escape(const std::string &text) {
        const std::regex chars_to_escape("[.^$|()\\[\\]{}*+?\\\\]");
        return std::regex_replace(text, chars_to_escape, "\\$0");
    }

    bool hasPrefix(std::string const &key) {
        return key.find_last_of(".") != std::string::npos;
    }

    inline std::pair<std::string, std::string> unpack(std::string const &key) {
        auto pos = key.find_last_of(".");
        auto prefix = key.substr(0, pos);
        auto suffix = key.substr(pos);
        return {prefix, suffix};
    }

    std::string derefIdFromContext(std::string id, HistoryContext const& hc) {
        LOG_FUNCTION_TIME;
        auto const logKey = id;
        CNS_DEBUG_MSG(logKey, "begin");
        CNS_DEBUG(logKey, "Resolving <{}>", id);
        for(unsigned i = 0; i != hc.size(); i++) {
            //auto start0 = std::chrono::steady_clock::now();
            auto loopKey = std::to_string(i);
            for(auto &[key, val]: hc) {
                CNS_DEBUG(loopKey, "[{} ↦ {}] ({})", key, val, id);
                //auto start = std::chrono::steady_clock::now();
                if(key == val) {
                    CNS_DEBUG_MSG(loopKey, "Key == value, skip");
                    continue;
                }

                auto pid = id;
                if(id == key) {
                    CNS_DEBUG(loopKey, "id({}) == key({})", id, key);
                    id = val;
                }
                else if(hasPrefix(id)) {
                    //id = evalCensusKey(id, key, val);
                    auto [prefix, suffix] = unpack(id);
                    if(prefix == key) {
                        CNS_DEBUG(loopKey, "Prefix({}) == key({})", prefix, key);
                        CNS_DEBUG(loopKey, "Suffix = {}", suffix);
                        id = val + suffix;
                    }
                    else {
                        CNS_DEBUG(loopKey, "Prefix({}) != key({})", prefix, key);
                        if(!hasPrefix(prefix)) {
                            CNS_DEBUG(loopKey, "No further prefix in {}", prefix);
                            CNS_DEBUG(loopKey, "<{}> -> <{}>\n", pid, id);
                            continue;
                        }

                        auto [prefix2, suffix2] = unpack(prefix);
                        if(prefix2 == key) {
                            CNS_DEBUG(loopKey, "Prefix2({}) == key({})", prefix2, key);
                            CNS_DEBUG(loopKey, "Suffix2 = {}", suffix2);
                            id = val + suffix2 + suffix;
                        }
                        else {
                            CNS_DEBUG(loopKey, "key({}) not found in id({})", key, id);
                            CNS_DEBUG(loopKey, "<{}> -> <{}>\n", pid, id);
                        }
                    }
                }
                /*
                auto duration = std::chrono::steady_clock::now() - start;
                fmt::print(fOUT, "[ INFO] :TIME TRACE: {} took {}μs\n",
                        "evalCensusKey",
                       std::chrono::duration_cast<std::chrono::microseconds>(duration).count());
                */
                CNS_DEBUG(loopKey, "<{}> -> <{}>\n", pid, id);
            }
            /*
            auto duration0 = std::chrono::steady_clock::now() - start0;
            fmt::print(fOUT, "[ INFO] :TIME TRACE: {} took {}μs\n",
                    "deref inside loop",
                   std::chrono::duration_cast<std::chrono::microseconds>(duration0).count());
            */
        }

        return id;
    }
}

// History(a) => we know type1 -> type 2 for a or its qualified name.
// Dr.sheng:
// History template should only refer to parameters and constant pointers.
// History template should not have census involvment.
//
HistoryTemplate::HistoryTemplate(clang::FunctionDecl const &fn) {
    LOG_FUNCTION_TIME;
    auto const logKey = fn.getNameAsString();
    CNS_DEBUG_MSG(logKey, "begin");

    function_ = fn.getNameAsString();
    unsigned pos = 0;
    // ForEach parameter, lookup corresponding census operand
    std::for_each(fn.param_begin(), fn.param_end(), [&](auto const *p) {
            // get qualifiedName
            auto const& qn = function_ + ".$" + std::to_string(pos++);
            if(census.find(qn) == census.end()) {
                CNS_INFO(logKey, "param operand for <{}> not in Census", qn);
                CNS_INFO(logKey, "Adding <{}> operand in Census", qn);

                // If a param operand doesn't exist yet, which is possible if the function is processed for the first time, just initiate it because otherwise the template will not be created.
                census.insert(makeCensusSourceNode(buildOpData(*p)));
            }

            // TODO: what if a param is not in census but remaining are? Unlikely since call is processed as a unit.
            params_.push_back(qn);
        });
    CNS_DEBUG_MSG(logKey, "end");
}

HistoryTemplate::HistoryTemplate(clang::ASTContext &context, clang::CallExpr const& call, clang::DeclRefExpr const &fptr) {
    LOG_FUNCTION_TIME;
    auto const logKey = qualifiedName(context, fptr);
    CNS_DEBUG_MSG(logKey, "<fptr> begin");
    function_ = qualifiedName(context, fptr);
    unsigned pos = 0;
    // ForEach parameter, lookup corresponding census operand
    // Since we are building from fptr, there is no declaration for function available that can be used to retrieve parameter info.
    // (Because fptr value is not known at compile time)
    // Utilizing the naming scheme for census operands, build a parameter keys for fptr function, knowing that OpData will be incomplete.
    std::for_each(call.arg_begin(), call.arg_end(), [&](auto const *p) {
            // get qualifiedName
            auto const& qn = function_ + ".$" + std::to_string(pos++);
            if(census.find(qn) == census.end()) {
                CNS_INFO(logKey, "<fptr> Param operand for <{}> not in Census", qn);
                CNS_INFO(logKey, "<fptr> Adding <{}> operand in Census", qn);
                // If a param operand doesn't exist yet, which is possible if the function is processed for the first time, just initiate it because otherwise the template will not be created.
                // ARG
                auto aop = buildOpData(context, *p);
                /*
                if(auto pos = function_.find("$"); pos != std::string::npos) {
                    aop.qn_ = function_.substr(0, pos) + String(context, *p);
                }
                else {
                    aop.qn_ = function_ + "." + String(context, *p);
                }
                if(census.find(aop.qn_) == std::end(census)) {
                    CNS_INFO("<SEE>Built arg opdata '{}' for '{}'", aop.qn_, String(context, *p));
                    //census.insert(makeCensusSourceNode(buildOpData(context, *p)));
                    census.insert(makeCensusSourceNode(aop));
                }
                */

                auto pop = aop; // Assume parameter will have same info as arg
                pop.qn_ = qn;   // Other than qn
                CNS_INFO(logKey, "<SEE>Built param opdata '{}' for '{}'", pop.qn_, String(context, *p));
                //census.insert(makeCensusSourceNode(pop));
                DominatorData dom = {aop, String(context, *p), "TODOArg"};
                auto lhsce = dyn_cast<CastExpr>(p);
                if(lhsce) {
                    dom.exprType_ = lhsce->getCastKindName();
                }
                else {
                    dom.exprType_ = "NotACast";
                }

                census.insert(makeCensusNode(pop, dom));
            }
            // TODO: what if a param is not in census but remaining are?
            CNS_DEBUG(logKey, "<fptr> Found param operand for <{}> in Census", qn);
            params_.push_back(qn);
        });
    CNS_DEBUG_MSG(logKey, "<fptr> end");
}

// History instantiation should not change dominator info for
// parameters. Why? The parameters in history are impertinent and adding multiple dominators for params just pollutes the history.
// ---> Operand cannot be changed.
// Should Use_ be changed? Maybe. Depends on how history is elaborated.
// No. Because the param has to be substituted not used.

// History is created from operand.
// The operand may be an operand or placeholder.
// Only parameters can be placeholders.
// So either create a specialized operand. Or treat parameter operand as placeholders.
// Either way only templates can have placeholders.
// When templates get instantiated, operator= is used to substitute the real operand in place of placeholder. Since the assignment changes only history, it happens in History class not operand.
// Instantiation transfers history of placeholder to argument, via operator=
// So H(a) = H(p or f.0) => operands in either history are not changed. However, H(a) is augmented with H(p)'s history. This equality resides in history class.
// H(a) = H(f.0) = H(f.1.0)
// How to represent the equality that H(f.1.0) is instantiated by H(a)->H(f.0)?
// Now instantiation is also an abstraction or context.
// From use, we know H(f.0) = H(f.1.0)
// Keeping a HistoryContext, where H(a) = H(f.0) i.e. a->f.0
// we can extrapolate H(f.1.0) to H(a) i.e. a->f.0 (from context) -> f.1.0 (from instantiation)
// Now if H(b) = H(f.1) i.e. b->f.1
// Using context, we can establish a->b.0
//
// Context is not part of history, it is part of template/instantiation.
// -> Instantiate returns a context that can be used to get history.
// Equality is part of history.
// Equality allows us to say H(a) includes the operands in H(f.0)
//
//
// History takes a context to return a history.
// Using the context, any placeholders can be resolved.
// Using the ops_ vector, equality is traversed.
// Using ops_.use_, equality/history can be elaborated as needed.
// Context is part of history? It should be to avoid incorrect context and operand pairing.
// It can't be. Because f.0 has single history. It is replaced by many args. So it has different contexts. The template doesn't change. so history doesn't either.
// However, history equality changes. i.e. H(a) = H(f.0) should be done with context. So that if f.0 -> f.1.0, H(a) = H(b) can be established.
// History Context should be part of history so that it can change history for parameters depending on context. History template is provided by opdata.use_
//
// Now how does history operator= work?
// History must link to other history objects instead of opData.
// OpData can have different history based on context. So linking to history is the only way to ensure it.
// when histories are linked, navigating history is a simple task. As is assignment, it works like a linked list.
// Now how are the histories stored? Are they kept with the operand? Perhaps the head of the list can be stored with the operand.

std::string makeHistoryId(std::string key, std::string val) {
    return key + "(" + val + ")";
}

// History instantiation => add context to history aka history branch aka opIDs
// So create temporary history with call context then:
// -> copy resolved opIds from branch to arg history.
// Doesn't work for the simple reason that resolution is affected by the assignment history which is recorded after instantiation.


// Store references in branch => get global updates
// Store functors for context => get global updates + add local updates? How?
//    All branch histories should be resolved by involving the history context + the container context. However, container context should not modify the branch context.
//    => resolution must accept an input context
//    Since branches can be created at multiple levels, do we need to pass input at each level? TBD
//
//    Instead of just a branch of histories, store history-local context pair.
class History {
    public:
        // Usually for aliasing
        void extend(History const &h, DominatorData const& linkInfo);

        //void extend(History const &h) {
        // Maybe a redo needed to extend context to branch histories
        // Or maybe in getcontextresolvedopstr, pass context for branches
        // nay, add current context to h.second!
        // Is an overload needed that takes only History instead of LocalHistory? ✓
        void extend(LocalHistory const &h);

        /*
        // REDO
        void extend_c(History h) {
            CNS_DEBUG_MSG("");
            // Extend context of input history before appending to branch
            h.hc_.insert(hc_.begin(), hc_.end());
            // Before appending to branch:
            //  - check if h is already in branch
            //      - check opId, id() and branch size or maybe just id should suffice
            //  - see if condense operation is possible.

            bool found = false;
            for(auto const &bh: branch_) {
                if(bh.id() == h.id()) {
                    FOUT << "[INFO ](History::extend_c) History {" << h.id() << "} already part of {" << id() << "}\n";
                    found = true;
                    break;
                }
            }
            if(!found) {
                FOUT << "[INFO ](History::extend_c) Adding History {" << h.id() << "} to {" << id() << "}\n";
                branch_.push_back(h);
            }
            CNS_DEBUG_MSG("end");
        }
        */

        // Probably redo? How is local context passed to id()?
        std::string id(std::optional<HistoryContext const> hc) const {
            // if operand or context are not same we don't get same string.
            // works for operands without context too and allows for extension of operand history.
            std::string sid;
            sid.reserve(64);
            sid = op_;
            auto const& op = getContextResolvedOpStr(hc);
            sid += "(" + op + ")";
            return sid;
        }

        // TODO: add a condense function to use in instantiation
        // -> If the history is instantiated, condense() can be used to get a new contextualized
        //    history with 0 depth/single branch.

        CensusKey opId() const {
            return op_;
        }

        bool hasContext() const {
            return !hc_.empty();
        }

        void addContext(HistoryContext hc) {
            auto const logKey = op_;
            CNS_DEBUG_MSG(logKey, "begin");
            CNS_DEBUG(logKey, "Adding context to history of {}", idversion());
            auto nhc = hc_;
            CNS_DEBUG(logKey, "Current context.size() = {}", hc_.size());
            nhc.insert(hc.begin(), hc.end());
            CNS_DEBUG(logKey, "After extending with input context, context.size() = {}", nhc.size());
            setContext(nhc);
            CNS_DEBUG(logKey, "Updated context.size() = {}", hc_.size());
            CNS_DEBUG_MSG(logKey, "end");
        }
        /*
        History addContext(HistoryContext hc) const {
            CNS_DEBUG_MSG("");
            FOUT << "[INFO ](History::addContext) Adding context to history of " << idversion() << "\n";
            auto nhc = hc_;
            FOUT << "[INFO ](History::addContext) Current hc_.size() = " << nhc.size() << "\n";
            nhc.insert(hc.begin(), hc.end());
            FOUT << "[INFO ](History::addContext) After extending with input hc, hc.size() = " << nhc.size() << "\n";
            auto h = History(*this);
            h.setContext(nhc);
            h.updateVersion();
            FOUT << "[INFO ](History::addContext) New history created with hc.size() = " << h.hc_.size() << "\n";
            FOUT << "[INFO ](History::addContext) Returning history " << h.idversion() << "\n";
            CNS_DEBUG_MSG("end");
            return h;
        }
        */

        explicit History(CensusKey const& op): op_(op) {
            auto const logKey = op_;
            CNS_DEBUG_MSG(logKey, "<explicit> begin");
            CNS_DEBUG(logKey, "<explicit> init: {}", op_);
            updateVersion();
            CNS_DEBUG(logKey, "<explicit> Created: {}", idversion());
            CNS_DEBUG_MSG(logKey, "<explicit> end");
        }

        History(HistoryContext const& context, CensusKey const& op):
            op_(op),
            hc_(context) {

            auto const logKey = op_;
            CNS_DEBUG_MSG(logKey, "<context, key> begin");
            //CNS_DEBUG("<context, key> init: {} ({})", op_, getContextResolvedOp({}).qn_);
            updateVersion();
            CNS_DEBUG(logKey, "<context, key> Created: {}", idversion());
            CNS_DEBUG_MSG(logKey, "<context, key> end");
        }

        History() = delete;
        ~History() = default;
        /*
        {
            CNS_DEBUG_MSG("");
            version_ = "XXXXXX_" + version_;
            CNS_DEBUG("Destructing history of {}({})", op_, getContextResolvedOp().qn_);
            CNS_DEBUG("Destructing: {}", idversion());
            CNS_DEBUG_MSG("end");
        }
        */

        // History is extended by copying this history to another history. Supports branching.
        // Copied history should only be used in extending and should not be mixed with global history.
        History(History const& h)  = delete;// = default;
        /*
            op_(h.op_),
            hc_(h.hc_),
            branch_(h.branch_),
            version_(h.version_),
            copy_(h.copy_ + 1) {
            CNS_INFO_MSG("");

            updateVersion();
            CNS_DEBUG("<copy> init: {}({}): [{}]", op_, getContextResolvedOp().qn_, branch_.size());
            CNS_DEBUG("<copy> Created: {}", idversion());
            CNS_DEBUG_MSG("end");
        }
        */
        //History& operator=(History h) = default;
        History(History && h)  = default;// = default;

        std::string getContextResolvedOpStr(std::optional<HistoryContext const> lc) const;
        std::string getContextResolvedOpStr0(std::optional<HistoryContext const> lc) const;

        std::optional<OpData const> getContextResolvedOp(std::optional<HistoryContext const> hc) const {
            auto rop = getContextResolvedOpStr(hc);
            if(census.find(rop) == census.end()) {
                return {};
            }

            return ops(rop);
        }

        auto bbegin() const {
            return branch_.begin();
        }
        auto bend() const {
            return branch_.end();
        }
        auto branch() const {
            return branch_;
        }
        auto getContext() const {
            return hc_;
        }
        auto& branch() {
            return branch_;
        }
        auto& getContext() {
            return hc_;
        }

        auto version() const {
            return version_;
        }

        std::string idversion() const {
            std::string sidv;
            sidv.reserve(64);
            sidv = "H{" + op_ + "}<" + version_ + ">";
            return sidv;
        }

        std::string updateCache(HistoryContext const& lc) const {
            return getContextResolvedOpStr(lc);
        }

    private:
        CensusKey op_;
        HistoryContext hc_ {};
        // Vector since history is a tree. just like use_, history can have multiple branches.
        std::vector<LocalHistory> branch_;
        std::string version_;
        unsigned copy_{0};

        void setContext(HistoryContext hc) {
            hc_ = hc;
            updateVersion();
        }

        void updateVersion() {
            std::string sv;
            sv.reserve(64);
            // version = opId + contextual id + hc.size : branch.size
            sv = /*op_ + "_" +*/ id({hc_}) + "."
                + std::to_string(hc_.size()) + ":"
                + std::to_string(branch_.size()) + "c" + std::to_string(copy_);
            CNS_DEBUG(op_, "Updated H{{{}}}<{}> to <{}>", op_, version_, sv);
            version_ = sv;
        }

};

void History::extend(History const &h, DominatorData const& linkInfo) {
    LOG_FUNCTION_TIME;
    auto const logKey = h.opId() + "(<- " + linkInfo.from_.qn_ + ")";
    CNS_DEBUG_MSG(logKey, "<g> begin");
    CNS_DEBUG(logKey, "<g> Current version: {}", idversion());
    CNS_DEBUG(logKey, "<g> Input history version: {}; Dominator{{{}, {}}}", h.idversion(), linkInfo.from_.qn_, linkInfo.exprType_);
    CNS_DEBUG(logKey, "<g> Current context size for H({}): {}", h.op_, h.hc_.size());
    CNS_DEBUG(logKey, "<g> Extending input context using current version: {}", idversion());
    // Check if needed, maybe not since local context is stored with histories now.
    // Extend context of input history before appending to branch
    //h.hc_.insert(hc_.begin(), hc_.end());
    //CNS_DEBUG("<g> Updated context size for input {}: {}", h.idversion(), h.hc_.size());
    /*
    for(auto &bh: h.branch_) {
        CNS_DEBUG(<g> Extending input branch {} context using {}", bh.idversion(), idversion());
        bh.hc_.insert(hc_.begin(), hc_.end());
        bh.updateVersion();
    }
    */
    //h.updateVersion();
    // Irrelevant since it's a copy: CNS_DEBUG("<g> New context size for H({}): {}", h.version_, h.hc_.size());
    // end check

    // Check if this is needed?
    // CNS_DEBUG("<g> Extending current context with updated input context {}: {}", h.idversion(), h.hc_.size());
    //hc_ = h.hc_;
    // end check

    CNS_DEBUG_MSG(logKey, "Extending current branch with new input");
    branch_.push_back({h, linkInfo, {hc_}});
    updateVersion();
    CNS_DEBUG(logKey, "New version: {}", idversion());
    CNS_DEBUG_MSG(logKey, "<g> end");
}

void History::extend(LocalHistory const &h) {
    LOG_FUNCTION_TIME;
    auto const logKey = h.id();
    CNS_DEBUG_MSG(logKey, "<lh> begin");
    CNS_DEBUG(logKey, "<lh> Current version: {}", idversion());
    CNS_DEBUG(logKey, "<lh> Input history version: {}; Dominator{{{}, {}}}", h.history().idversion(), h.linkInfo().from_.qn_, h.linkInfo().exprType_);
    if(h.context()) {
        CNS_DEBUG(logKey, "<lh> Input local context size: {}", h.context().value().size());
    }
    //CNS_DEBUG("<lh> Current context size for H({}): {}", h.op_, h.hc_.size());
    //CNS_DEBUG("<lh> Extending context using H({}): branch({}); context({})", op_, branch_.size(), hc_.size());
    CNS_DEBUG(logKey, "<lh> Extending input's local context using current version: {}", idversion());
    // Extend context of input history before appending to branch
    HistoryContext nlhc;
    if(h.context()) {
        nlhc = h.context().value();
        CNS_DEBUG(logKey, "<lh> Input local context size: {}", nlhc.size());
    }
    nlhc.insert(hc_.begin(), hc_.end());
    CNS_DEBUG(logKey, "<lh> Updated input local context sie: {}", nlhc.size());

    //CNS_DEBUG("New context size for H({}): {}", h.op_, h.hc_.size());
    //CNS_DEBUG("Updated context size for input {}: {}", h.idversion(), h.hc_.size());
    /*
    for(auto &bh: h.first.get().branch_) {
        CNS_DEBUG("<lh> Extending input branch {} context using {}", bh.first.get().idversion(), idversion());
        HistoryContext bhlc;
        if(bh.second) {
            bhlc = bh.second.value();
        }
        bhlc.insert(hc_.begin(), hc_.end());
        bh.second.swap(bhlc);
    }
    */
    //h.updateVersion();
    //CNS_DEBUG("<lh> Extending current context with updated input context {}: {}", h.idversion(), h.hc_.size());
    //hc_ = h.hc_;
    CNS_DEBUG_MSG(logKey, "<lh> Extending current branch with new input");
    //branch_.emplace_back(h.first, nlhc);
    branch_.push_back(h);
    // Irrelevant since it's a copy: CNS_DEBUG("<lh> New context size for H({}): {}", h.version_, h.hc_.size());
    updateVersion();
    CNS_DEBUG(logKey, "<lh> New version: {}", idversion());
    CNS_DEBUG_MSG(logKey, "<lh> end");
}

std::string LocalHistory::id() const {
    return h_.get().opId();
}
History const& LocalHistory::history() const {
    return h_.get();
}


std::unordered_map<CensusKey, History> TypeTransforms;

std::string History::getContextResolvedOpStr0(std::optional<HistoryContext const> lc) const {
    return "";
}

std::string History::getContextResolvedOpStr(std::optional<HistoryContext const> lc) const {
    LOG_FUNCTION_TIME;
    auto const logKey = op_;
    CNS_DEBUG_MSG(logKey, "begin");
    CNS_DEBUG(logKey, "Resolving op_ {{{}}} for H{{{}}}<{}>", op_, op_, version_);
    auto rop = derefIdFromContext(op_, hc_);
    if(lc) {
        CNS_DEBUG(logKey, "Now resolving rop{{{}}} for H{{{}}}<{}> using input context", rop, op_, version_);
        rop = derefIdFromContext(rop, lc.value());
    }

    if(op_ != rop) {
        // Check if rop exists
        if(census.find(rop) == census.end()) {
            CNS_DEBUG_MSG(logKey, "New rop does not match any census node.");
            auto ropp = rop.substr(0, rop.find("$") - 1);
            if(TypeTransforms.find(ropp) != TypeTransforms.end()) {
                // Found H(ropp)
                CNS_DEBUG(logKey, "Found H(container){{{}}}", ropp);
                // Check if ropp has any resolution
                auto const &hropp = TypeTransforms.at(ropp);
                auto roppn = hropp.getContextResolvedOpStr(lc);
                if(ropp != roppn) {
                    // Try now
                    CNS_DEBUG(logKey, "Resolved container, ropp = {{{}}}", roppn);
                    rop = roppn + "." + rop.substr(rop.find("$"));
                    if(census.find(rop) == census.end()) {
                        CNS_DEBUG(logKey, "However, rop = {{{}}} not in Census", rop);
                        // Create census node for rop and add op as dominator?
                    }
                    else {
                        CNS_DEBUG(logKey, "rop = {{{}}} found in Census", rop);
                        // Who is the dominator for this?
                    }
                }
            }

            else {
                // Try removing the container:
                auto ropn = rop.substr(rop.find(".") + 1);
                CNS_DEBUG(logKey, "Removing container, ropn = {{{}}}", ropn);
                if(census.find(ropn) == census.end()) {
                    CNS_DEBUG_MSG(logKey, "Removing container did not lead to any census match.");
                }
                else {
                    CNS_DEBUG_MSG(logKey, "Removing container led to census match.");
                    rop = ropn;
                }
            }

        }
        else {
            CNS_DEBUG_MSG(logKey, "New rop found in census.");
            if(ops(rop).type_.empty()) {
                CNS_DEBUG(logKey, "rOp({}) has no type, trying to remove container for correct rop", rop);
                // Try removing the container:
                auto ropn = rop.substr(rop.find(".") + 1);
                CNS_DEBUG(logKey, "Removing container, ropn = {{{}}}", ropn);
                if(census.find(ropn) == census.end()) {
                    CNS_DEBUG_MSG(logKey, "Removing container did not lead to any census match.");
                }
                else {
                    CNS_DEBUG_MSG(logKey, "Removing container led to census match.");
                    rop = ropn;
                    if(ops(rop).type_.empty()) {
                        CNS_DEBUG(logKey, "rOp({}) has no type defined either", rop);
                    }
                    else {
                        CNS_DEBUG(logKey, "rOp({}) has type: {}", rop, ops(rop).type_);
                    }
                }
            }
            else {
                CNS_DEBUG(logKey, "rOp('{}') has type: {}", rop, ops(rop).type_);
            }
        }
    }

    return rop;
    CNS_DEBUG_MSG(logKey, "end");
}

/*
std::ostream& dumpHistory(std::ostream &os, History const&h);

std::ostream& operator<<(std::ostream &os, std::vector<LocalHistory> const &b) {
    os << "  {";
    std::for_each(begin(b), end(b), [&](auto const &h) {
        dumpHistory(os, h.first.get());
    });
    os << "  }";
    return os;
}
std::ostream& operator<<(std::ostream &os, HistoryContext const &hc) {
    os << "{";
    std::for_each(begin(hc), end(hc), [&](auto const &p) {
        os << "  {" << p.first << " = " << p.second << "}, ";
    });
    os << "}";
    return os;
}
std::ostream& dumpHistory(std::ostream &os, History const&h) {
    os << "{op_ :'" << h.opId() << "',\n"
       << " hc_ :'" << h.getContext() << "',\n"
       << " branch: '" << h.branch() << "'\n"
       << "}\n";
    return os;
}

std::ostream& dumpH(std::ostream &os, std::string const& ops_, std::string const& rops, int indent = 0) {
    //space(os, indent);
    auto const& op = ops(ops_);
    if(op.type_.empty()) {
        if(census.find(rops) == std::end(census)) {
            os << "T" << "{" << rops << " = " << ops_ << "}";
        }
        else {
            auto rop = ops(rops);
            if(rop.type_.empty() && (!rop.qn_.empty())) {
                os << "T" << "{" << rop.qn_ << "}";
            }
            else if(rop.qn_.empty()) {
                os << rop.type_ << "{ ~" << op.qn_ << "}";
            }
            else {
                os << rop.type_ << "{" << op.qn_ << " = " << rop.qn_ << "}";
            }
        }
    }
    else {
        os << op.type_ << "{" << op.qn_ << "}";
    }
    return os;
}

std::ostream& dumpLocalH(std::ostream &os, LocalHistory const& lh, int indent = 0) {
    auto const& h = lh.first.get();
    dumpH(os, h.opId(), h.getContextResolvedOpStr(lh.second), indent);
    os << "\n";

    indent += 2;
    auto hc = h.getContext();
    // Augment history context with local context from input
    if(lh.second) {
        hc.insert(lh.second.value().begin(), lh.second.value().end());
    }

    std::for_each(h.bbegin(), h.bend(),
        [&](auto const& bh){
            // Augment local context with container context + input local context
            //auto blc = hc;
            //if(bh.second) {
            //    blc.insert(bh.second.value().begin(), bh.second.value().end());
            //}
            space(os, indent);
            os << " |--> ";
            //dumpH(os, bh.first.get().opId(), bh.first.get().updateCache(blc), indent + 2);
            //dumpLocalH(os, LocalHistory(bh.first, hc), indent + 2);
            dumpH(os, bh.first.get().opId(), bh.first.get().getContextResolvedOpStr({hc}), indent + 2);
            //dumpLocalH(os, LocalHistory(bh.first, bh.second), indent + 2);
            os << "{ " << bh.first.get().version() << "}\n";
        });

    return os;
}

std::ostream& operator<<(std::ostream &os, History const& h) {
    int indent = 0;
    dumpH(os, h.opId(), h.getContextResolvedOpStr({}), indent);
    os << "{ " << h.version() << "}\n";
    os << "\n";

    std::for_each(h.bbegin(), h.bend(),
        [&](auto const& blh) {
            auto hc = h.getContext();
            // Augment local context with branch parent context
            if(blh.second) {
                hc.insert(blh.second.value().begin(), blh.second.value().end());
            }
            //ss << " |--> " << LocalHistory(hb_.first, hcb) << "\n";

            space(os, indent);
            os << " |--> ";
            dumpLocalH(os, LocalHistory(blh.first, hc), indent + 2);
            os << "\n";
        });

    os << "\n";
    //std::vector<std::pair<std::string, std::string>> branch;
    //        //dumpH(os, hb_.first.get().opId(), hb_.first.get().updateCache(hcb), indent + 2);
    //        //branch.push_back({hb_.first.get().opId(), hb_.first.get().updateCache(hcb)});
    //std::for_each(begin(branch), end(branch),
    //    [&](auto const& b) {
    //        dumpH(ss, b.first, b.second);
    //    });

    return os;
}
*/

// History context applies to extension
// => On extending history, context gets extended too.
// => Context is applied to op.use_
//
std::string dumpH(std::string const &ops_, std::string const &rops, int indent = 0) {
    auto const &op = ops(ops_);
    std::string sdh;
    sdh.reserve(64);
    if(op.type_.empty()) {
        if(census.find(rops) == std::end(census)) {
            sdh.append("T{" + rops + " = " + ops_ + "}");
        }
        else {
            auto rop = ops(rops);
            if(rop.type_.empty() && (!rop.qn_.empty())) {
                sdh.append("T{" + rop.qn_ + "}");
            }
            else if(rop.qn_.empty()) {
                sdh.append(rop.type_ + "{ ~" + op.qn_ + "}");
            }
            else {
                sdh.append(rop.type_ + "{" + op.qn_ + " = " + rop.qn_ + "}");
            }
        }
    }
    else {
        sdh.append(op.type_ + "{" + op.qn_ + "}");
    }
    return sdh;
}

/*
std::string dumpLocalH(LocalHistory const& lh, int indent = 0) {
    auto const& h = lh.first.get();
    std::string slh;
    slh.reserve(256);
    slh.append(dumpH(h.opId(), h.getContextResolvedOpStr(lh.second), indent));
    slh.append("\n");

    indent += 2;
    auto hc = h.getContext();
    // Augment history context with local context from input
    if(lh.second) {
        hc.insert(lh.second.value().begin(), lh.second.value().end());
    }

    std::for_each(h.bbegin(), h.bend(),
        [&](auto const& bh){
            // Augment local context with container context + input local context
            //auto blc = hc;
            //if(bh.second) {
            //    blc.insert(bh.second.value().begin(), bh.second.value().end());
            //}
            slh.append(space(indent));
            slh.append(" |--> ");
            slh.append(dumpH(bh.first.get().opId(), bh.first.get().getContextResolvedOpStr({hc}), indent + 2));
            //dumpH(os, bh.first.get().opId(), bh.first.get().updateCache(blc), indent + 2);
            //dumpLocalH(os, LocalHistory(bh.first, hc), indent + 2);
            //dumpLocalH(os, LocalHistory(bh.first, bh.second), indent + 2);
            slh.append("{" + bh.first.get().version() + "}\n");
        });

    return slh;
}

std::string dump(History const& h) {
    int indent = 0;
    std::string sh;
    sh.reserve(1024);
    sh.append(dumpH(h.opId(), h.getContextResolvedOpStr({}), indent));
    sh.append("{ " + h.version() + "}\n\n");

    std::for_each(h.bbegin(), h.bend(),
        [&](auto const& blh) {
            auto hc = h.getContext();
            // Augment local context with branch parent context
            if(blh.second) {
                hc.insert(blh.second.value().begin(), blh.second.value().end());
            }
            //ss << " |--> " << LocalHistory(hb_.first, hcb) << "\n";

            sh.append(space(indent));
            sh.append(" |--> ");
            sh.append(dumpLocalH(LocalHistory(blh.first, hc), indent + 2));
            sh.append("\n");
        });

    sh.append("\n");
    //std::vector<std::pair<std::string, std::string>> branch;
    //        //dumpH(os, hb_.first.get().opId(), hb_.first.get().updateCache(hcb), indent + 2);
    //        //branch.push_back({hb_.first.get().opId(), hb_.first.get().updateCache(hcb)});
    //std::for_each(begin(branch), end(branch),
    //    [&](auto const& b) {
    //        dumpH(ss, b.first, b.second);
    //    });

    return sh;
}
*/


// Two kinds of history:
//  - global history -> stored in TypeTransforms, the normal history
//      that is built by using the operand and then extended with use.
//  - contextualized history -> may or may not be part of TypeTransforms. Use in template instantiation and history extending operations. If global history with same opId exists, contextualized history will not be part of TypeTransforms.
//
// TODO redo: what is meant by equality for history object? When history is extended, it may be
//   for same operand but can have a different context
//   This equality is meant for global history only.
bool operator==(History const &a, History const &b) {
    auto const logKey = a.opId() + "==" + b.opId();
    CNS_DEBUG_MSG(logKey, "<h> begin");
    CNS_DEBUG(logKey, "<h> a: ({}/{})", a.id({}), a.opId());
    CNS_DEBUG(logKey, "<h> b: ({}/{})", b.id({}), b.opId());
    CNS_DEBUG_MSG(logKey, "end");
    // Support both context resolved history and otherwise.
    //return (a.id() == b.id()) || (a.opId() == b.opId());
    return a.opId() == b.opId(); //eturn a.id() == b.id(); // && (a.branch() == b.branch());
}
bool operator!=(History const &a, History const &b) {
    auto const logKey = a.opId() + "!=" + b.opId();
    CNS_DEBUG_MSG(logKey, "<h> begin");
    CNS_DEBUG_MSG(logKey, "<h> end");
    return !(a == b);
}
bool operator==(History const &a, CensusKey const &b) {
    auto const logKey = a.opId() + "==" + b;
    CNS_DEBUG_MSG(logKey, "<h, k>");
    CNS_DEBUG(logKey, "<h, k> a: ({}/{})", a.id({}), a.opId());
    CNS_DEBUG(logKey, "<h, k> b: ({})", b);
    CNS_DEBUG_MSG(logKey, "<h, k> end");
    return a.opId() == b;
    //return (a.id() == b) || (a.opId() == b);
    //return (a.id() == b) && !(a.hasContext());
}
bool operator!=(History const &a, CensusKey const &b) {
    auto const logKey = a.opId() + "!=" + b;
    CNS_DEBUG_MSG(logKey, "<h, k>");
    CNS_DEBUG_MSG(logKey, "<h, k> end");
    return !(a == b);
}

/*
History History::append(History h) {
    History t(this);
    std::copy(begin(h.ops_), end(h.ops_), back_inserter(t));
    return t;
}
*/

// concat history
// How do we concatenate history?
// Concat is simple for operands.
// For functions, an operand is instantiated first.
// h(t<hof>.instantiate(pi, 0)) => H(pi) = H(pi) + H(hof.0)
// if H(hof.0) = hof.1.0
//History operator+(History const& h1, History const& h2) {
//    return h1.append(h2);
//}

std::vector<HistoryTemplate> TransformTemplates;

std::vector<LocalHistory> HistoryTemplate::instantiate(clang::ASTContext &context, clang::CallExpr const &call) {
    LOG_FUNCTION_TIME;

    auto const logKey = String(context, call);
    CNS_DEBUG_MSG(logKey, "begin");
    // for each arg, add parm-arg pair to context.
    // return context

    if(params_.size() != call.getNumArgs()) {
        CNS_ERROR_MSG(logKey, "Nb(params) != Nb(args), possibly unsupported variadic function or a template mismatch.");
        CNS_ERROR(logKey, "FnTemplate({}) istantiating for function call ({}) failed.", name(), String(context, call));
        CNS_DEBUG_MSG(logKey, "end");
        return {};
    }

    HistoryContext lc = makeHistoryContext(params_, context, call);

    std::vector<LocalHistory> h;
    unsigned pos = 0;
    std::for_each(begin(params_), end(params_),
        [&](auto const &p) {
            CNS_DEBUG_MSG(logKey, "for_each param step");

            if(TypeTransforms.find(p) == std::end(TypeTransforms)) {
                // Parameter history is not built yet.
                CNS_INFO(logKey, "New history created for {} var {{{}}}", function_, p);
                TypeTransforms.emplace(p, p);
                //TypeTransforms.insert({p, History(p)});
            }

            // Add hc to existing parameter history
            auto const &hp = TypeTransforms.at(p);
            //h.emplace_back(hp, lc);
            // get Dominator data for arg(from) -> parm(to)
            DominatorData linkInfo;
            auto itp = census.find(p);
            if(itp == census.end()) {
                // CastChk ensures that Census is built before history instantiation
                CNS_ERROR(logKey, "Instantiating history for param not in Census: '{}'", p);
            }
            else {
                auto const *arge = call.getArg(pos);
                if(!!arge) {
                    auto const argqn = qualifiedName(context, *arge); //qualifiedName(context, call, *arge);
                    auto pdoms = doms(*itp);
                    if(!pdoms) {
                        CNS_ERROR(logKey, "Param '{}' has no doms", p);
                    }
                    else {
                        auto itpd = std::find(begin(pdoms.value()), end(pdoms.value()), argqn);
                        if(itpd == std::end(pdoms.value())) {
                            CNS_ERROR(logKey, "Param '{}' is not dominated by arg '{}'", p, argqn);
                        }
                        else {
                            linkInfo = *itpd;
                        }
                    }
                }
                else {
                    CNS_ERROR(logKey, "Cannot retrieve arge expr for arg[{}]", pos);
                }
            }
            pos++;
            h.emplace_back(hp, linkInfo, lc);
            CNS_DEBUG(logKey, "Current context size for H({}): {}", p, hp.getContext().size());

            CNS_DEBUG_MSG(logKey, "end for_each param step");
        });

    CNS_DEBUG_MSG(logKey, "end");
    return h;
}

class CastStat;
// TypeSummary evaluates to key.type_ or key.qn_ or key.type_ -> (key.type_ | key.qn_) and so on.
class TypeSummary {
    public:

    explicit TypeSummary(History const &h);

    void addNextBranch(TypeSummary const& branch) {
        auto const logKey = id();
        CNS_DEBUG_MSG(logKey, "begin");
        CNS_DEBUG(logKey, "Adding nexts_ branch: {{{}[{}]}}", branch.key_, branch.label());
        nexts_.push_back(branch);
        CNS_DEBUG(logKey, "Updated branch size: {}", nexts_.size());
        CNS_DEBUG_MSG(logKey, "end");
    }

    std::string summarizeNoStat(std::optional<unsigned> level, int indent = 0) const;
    std::string summarize(CastStat&, std::optional<unsigned> level, int indent = 0) const;

    ~TypeSummary()= default;
    /*
        CNS_DEBUG_MSG("");
        CNS_DEBUG("Clearing {{{}}};[{}]_{}", key_, nexts_.size(), numi_);
        key_.clear();
        nexts_.clear();
        CNS_DEBUG_MSG("end");
    }
    */

    TypeSummary(const TypeSummary& t): //= default;
        key_(t.key_),
        linkLabel_(t.linkLabel_),
        nexts_(t.nexts_) {

        numi_ = num_++;
        CNS_DEBUG(key_, "Copy created from _{}_: {{{}}};[{}]_{}", t.numi_, key_, nexts_.size(), numi_);
    }

    TypeSummary(TypeSummary&& t): // = default;
        key_(std::move(t.key_)),
        linkLabel_(std::move(t.linkLabel_)),
        nexts_(std::move(t.nexts_)) {

        numi_ = num_++;
        CNS_DEBUG(key_, "Moved from _{}_: {{{}}};[{}]_{}", t.numi_, key_, nexts_.size(), numi_);
    }

    TypeSummary& operator=(const TypeSummary& t) = default;
    TypeSummary& operator=(TypeSummary&& t) = default;

    std::string id() const {
        std::string sid;
        sid.reserve(64);
        sid = key_ + ";[" + std::to_string(size()) + "];(" + linkLabel_ + ")_" + std::to_string(numi_);
        return sid;
    }

    void setlabel(std::string const& label) {
        CNS_DEBUG(key_, "Setting label '{}' to TypeSummary", label);
        linkLabel_ = label;
    }

    std::string label() const {
        return linkLabel_;
    }

    unsigned size() const {
        return nexts_.size();
    }

    private:
    CensusKey key_;
    std::string linkLabel_; //{"root"};
    std::vector<TypeSummary> nexts_;
    static unsigned num_;
    unsigned numi_;
};

unsigned TypeSummary::num_ = 0;

/*
bool operator==(TypeSummary lhs, CensusKey rhs) {
    return lhs.id() == rhs;
}
bool operator!=(TypeSummary lhs, CensusKey rhs) {
    return !(lhs == rhs);
}
*/

class OverflowGuard {
public:
    static OverflowGuard& get() {
        static OverflowGuard g(1000);
        if(g.value() > 0) {
            g.decrement();
        }
        return g;
    }
    static void reset() {
        auto &og = OverflowGuard::get();
        og.stack_depth_ = 1000;
    }

    unsigned value() const {
        return stack_depth_;
    }
    bool ok() const {
        return value() > 0;
    }

private:
    OverflowGuard(unsigned sd): stack_depth_{sd} {}
    void decrement() {
        stack_depth_--;
    }

    unsigned stack_depth_;
};


TypeSummary makeTypeSummaryLH(LocalHistory const& lh);

TypeSummary::TypeSummary(History const&h) {
    auto const logKey = "{" + h.opId() + "}";
    CNS_DEBUG_MSG(logKey, "begin");
    key_ = h.opId();
    numi_ = num_++;
    CNS_DEBUG(logKey, "Created summary {{{}}};[{}]_{} for H<{}>[h.branch.size() = {}]", key_, nexts_.size(), numi_, h.opId(), h.branch().size());
    CNS_DEBUG(logKey, "Created summary {{{}}};[{}] for H<{}>[h.branch.size() = {}]", key_, nexts_.size(), h.opId(), h.branch().size());
    CNS_DEBUG_MSG(logKey, "end");
}

TypeSummary makeResolvedSummary(std::string const& keyOp, std::string const& keyRops, std::string const& linkType = {}) {
    LOG_FUNCTION_TIME;
    auto const logKey = keyOp + "(" + keyRops + ")";
    CNS_DEBUG_MSG(logKey, "begin");
    CNS_INFO(logKey, "Building summary for {{{}, {}}}", keyOp, keyRops);
    auto const& op = ops(keyOp);
    CNS_DEBUG(logKey, "{{{}}}: A", keyOp);
    TypeSummary ts (TypeTransforms.at(keyOp));
    ts.setlabel(linkType);

    if(keyRops == keyOp) {
        CNS_INFO(logKey, "{{{} = {}}}", keyOp, keyRops);
        CNS_DEBUG(logKey, "{{{}}}: end", ts.id());
        return ts;
    }

    // If op is a parameter and rop is arg, return ts
    if(keyOp.find("$") != std::string::npos) {
        // keyRops maybe arg (unless something like hof.$1.$0)
        // => summary of parameter should be assigned to arg --> ??
        //    not needed since that is taken care of by history
        if(keyRops.find("$") == std::string::npos) {
            // => arg
            CNS_INFO(logKey, "{{{}}}: {{{}}} is probably an arg for param {{{}}}, stopping", ts.id(), keyRops, keyOp);
            CNS_DEBUG(logKey, "{{{}}}: end", ts.id());
            return ts;
        }
    }

    // If op.type is not empty
    if(!op.type_.empty()) {
        CNS_INFO(logKey, "{{{}}} has defined type. Not using {{{}}}", ts.id(), keyRops);
        CNS_DEBUG(logKey, "{{{}}}: end", ts.id());
        return ts;
    }

    if(census.find(keyRops) == std::end(census)) {
        CNS_INFO(logKey, "{{{}}}: Cannot find keyRops{{{}}} in Census", ts.id(), keyRops);
        CNS_DEBUG(logKey, "{{{}}}: end", ts.id());
        return ts;
    }

    /*
    if((keyOp != keyRops)
            && std::regex_search(keyOp, pattern)
            && std::regex_search(keyRops, pattern)) {

        // Possibly, Strongly connected components (one param to next, maybe cyclic)
        CNS_INFO_MSG("SCC possibility detected, stopping recursion.");
        auto const &hr = TypeTransforms.at(keyRops);
        std::for_each(hr.bbegin(), hr.bend(),
            [&](auto const &bh_) {
                auto const& bh = bh_.first.get();
                auto bkeyRops = bh.getContextResolvedOpStr(bh_.second);
                if(bh.opId() != keyOp || bkeyRops != keyOp) {
                    auto th = makeResolvedSummary(bh.opId(), bkeyRops);
                    ts.addNextBranch(th);
                }
            });

        CNS_DEBUG_MSG("end");
        return ts;
    }
    */


    CNS_DEBUG(logKey, "{{{}}}: Found keyRops{{{}}} in Census", ts.id(), keyRops);
    //auto th = TypeSummary(TypeTransforms.at(keyRops)); //, {});
    CNS_INFO(logKey, "{{{}}}: Adding branch from resolved keyRops{{{}}}", ts.id(), keyRops);
    CNS_DEBUG(logKey, "{{{}}}: B", ts.id());
    auto tts = TypeSummary(TypeTransforms.at(keyRops));
    tts.setlabel(linkType);
    ts.addNextBranch(tts);

    CNS_DEBUG(logKey, "{{{}}}: end", ts.id());
    return ts;
}

std::vector<std::string> ignoreFunctions;
bool isIgnoredFunction(std::string const& key) {
    CNS_DEBUG(key, "Checking if {{{}}} is ignored", key);
    auto pos = key.find(".$");
    if(pos != std::string::npos) {
        auto fn = key.substr(0, pos - 1);
        if(std::find(begin(ignoreFunctions), end(ignoreFunctions), fn) != end(ignoreFunctions)) {
            CNS_DEBUG(key, "'{}'() is ignored", fn);
            return true;
        }
    }
    return false;
}

TypeSummary makeTypeSummaryLH(LocalHistory const& lh) {
    auto const logKey = lh.id() + "_" + lh.linkInfo().exprType_;
    CNS_DEBUG_MSG(logKey, "begin");

    auto const &h = lh.history();
    auto hid = lh.id();

    if(isIgnoredFunction(h.opId())) {
        CNS_INFO(logKey, "skipping, ignored function: {}", hid);
        auto ts = makeResolvedSummary(hid, hid, lh.linkInfo().exprType_);
        //ts.setlabel(lh.linkInfo().exprType_);
        CNS_DEBUG_MSG(logKey, "end");
        return ts;
    }

    auto rops = h.getContextResolvedOpStr(lh.context());
    CNS_DEBUG_MSG(logKey, "1");
    auto ts = makeResolvedSummary(hid, rops, lh.linkInfo().exprType_);
    //ts.setlabel(lh.linkInfo().exprType_);
    if(!lh.context()) {
        CNS_DEBUG(logKey, "{{{}}}: No local context; end", ts.id());
        return ts;
    }

    CNS_DEBUG(logKey, "{{{}}}: Using local history context to summarize branches for {{{}}} branch{{size={}}}", ts.id(), ts.id(), h.branch().size());
    auto pc = h.getContext();
    if(lh.context()) {
        CNS_DEBUG_MSG(logKey, "Extending parent context with local");
        pc.insert(std::begin(lh.context().value()), std::end(lh.context().value()));
    }
    std::for_each(h.bbegin(), h.bend(),
        [&](auto const &bh) {
            if(bh.id() == hid || bh.id() == rops) {
                return;
            }
            auto pcn = pc;
            if(bh.context()) {
                CNS_DEBUG_MSG(ts.id(), "Extending parent context with branch local");
                pcn.insert(std::begin(bh.context().value()), std::end(bh.context().value()));
            }

            CNS_DEBUG(ts.id(), "Creating summary for branch: {{{}}}", bh.id());
            CNS_DEBUG_MSG(ts.id(), "{{{}}}: 2");
            auto &og = OverflowGuard::get();
            if(og.ok()) {
                ts.addNextBranch(makeTypeSummaryLH({bh.history(), bh.linkInfo(), pcn}));
            }
            else {
                CNS_WARN(ts.id(), "Stopping makeTypeSummaryLH recursion, skipping {{{}}} because overflow guard limit exceeded.", bh.id());
            }
        });

    CNS_DEBUG(logKey, "{{{}}}: end", ts.id());
    return ts;
}

std::unordered_map<CensusKey, TypeSummary> TypeSummaries;

class CastStat {
public:
    CastStat(std::string label): label_(label) {}

    void print(std::FILE *fp) const {
        fmt::print(fp, "[Cast Statistics] {} :\n", label_);

        fmt::print(fp, "Total BitCasts: {}\n", castCount_);
        fmt::print(fp, "Total void * casts : {}\n", voidCount_);

        auto printMap_ = [&](auto const& stat) {
            for(auto const &[key, value]: stat) {
                fmt::print(fp, "{{'{}': {}}}, ", key, value);
            }
            fmt::print(fp, "\n");
        };

        auto printMap = [&](auto const& msg, auto const& stat) {
            fmt::print(fp, "\n{}: ", msg);
            printMap_(stat);
        };

        printMap("Types involved {Type: Count}", typeCounts_);
        printMap("Type Categories involved {Category: Count}", categoryCounts_);
        printMap("Functions involved {Func: Count}", funcCounts_);
        printMap("Locations involved {Location: Count}", locationCounts_);

        fmt::print(fp, "\n[end Cast Statistics] {}\n", label_);
        /*
        fmt::print(fp, "\nTypes involved {{Type: Count}}: ");
        printMap(typeCounts_);
        fmt::print(fp, "\nType Categories involved {{Category: Count}}: ");
        printMap(categoryCounts_);
        fmt::print(fp, "\nFunctions involved {{Func: Count}}:");
        printMap(funcCounts_);
        fmt::print(fp, "\nLocations involved {{Location: Count}}:");
        printMap(locationCounts_);
        fmt::print(fp, "\n[end Cast Statistics] {}\n", label_);
        */
    }

    void extend(CastStat const& cst) {
        castCount_ += cst.castCount_;
        voidCount_ += cst.voidCount_;

        auto extendStat = [&](auto const& start, auto const& stop, auto & dest) {
            std::for_each(start, stop, [&](auto const& kv) {
                     dest[kv.first] += kv.second;
                });
        };

        extendStat(std::begin(cst.typeCounts_), std::end(cst.typeCounts_), typeCounts_);
        extendStat(std::begin(cst.funcCounts_), std::end(cst.funcCounts_), funcCounts_);
        extendStat(std::begin(cst.locationCounts_), std::end(cst.locationCounts_), locationCounts_);
        extendStat(std::begin(cst.categoryCounts_), std::end(cst.categoryCounts_), categoryCounts_);
    }

    void record(OpData const& op, std::string const& origin) {
        if(op.type_.empty()) {
            typeCounts_["T"] += 1;
        }
        else {
            typeCounts_[op.type_] += 1;
        }

        if(origin == "BitCast") {
            castCount_++;
            voidCount_ += (op.type_ == "void *");

            locationCounts_[op.location_] += 1;

            if(op.container_.empty()) {
                funcCounts_["UnknownFn"] += 1;
            }
            else {
                funcCounts_[op.container_] += 1;
            }
        }

        if(origin == "BitCast" || label_.find(op.qn_) != std::string::npos) {
            categoryCounts_[op.linkedRecordCategory_] += 1;
            categoryCounts_[op.category_] += 1;
        }
    }

    // functions to view funcCounts/typeCount

private:
    unsigned castCount_ = 0;
    unsigned voidCount_ = 0;
    std::string label_;
    //CensusKey key_; // Required to lookup metadata?
    std::unordered_map<std::string, unsigned> funcCounts_;
    std::unordered_map<std::string, unsigned> typeCounts_;
    std::unordered_map<std::string, unsigned> locationCounts_;
    std::unordered_map<std::string, unsigned> categoryCounts_;
};

std::string TypeSummary::summarize(CastStat &cst, std::optional<unsigned> level, int indent) const {
    LOG_FUNCTION_TIME;
    auto const logKey = key_ + ";[" + std::to_string(nexts_.size()) + "]_{" + std::to_string(numi_) + "}";
    std::string ssr;
    ssr.reserve(1024);
    CNS_DEBUG(logKey, "LEVEL = {}", level.value_or(599)); // TODO Level upper limit
    auto const& op = ops(key_);
    cst.record(op, linkLabel_);
    if(op.type_.empty()) {
        ssr = "T {" + key_ + "}(" + linkLabel_ + ")";
    }
    else {
        ssr = op.type_ + "{" + key_ + "}(" + linkLabel_ + ")";
    }
    if(!op.linkedRecord_.empty()) {
        ssr += "{" + op.linkedRecord_ + ": " + op.linkedRecordCategory_ + "}";
    }

    if(level <= 0 && nexts_.size() > 0) {
        ssr.append("-> <...>\n");
        return ssr;
    }

    /*
    if(level > 0 && nexts_.empty()) {
        if(TypeSummaries.find(key_) != std::end(TypeSummaries)) {
            ssr.append("\n");
            ssr.append(space(indent));
            //ss << "Resolving further using TypeSummaries built so far:\n";
            //space(ss, indent);
            ssr += ">+" + TypeSummaries.at(key_).summarize(cst, {level.value() - 1}, indent);
        }
    }
    */

    std::for_each(begin(nexts_), end(nexts_),
        [&](auto const& ts) {
            ssr.append("\n");
            ssr.append(space(indent));
            ssr += "|->" + ts.summarize(cst, {level.value() - 1}, indent + 2);
        });

    return ssr;
}

// S(a) = Typeof(a) -> S(next)
std::string TypeSummary::summarizeNoStat(std::optional<unsigned> level, int indent) const {
    LOG_FUNCTION_TIME;
    auto const logKey = key_ + ";[" + std::to_string(nexts_.size()) + "]_{" + std::to_string(numi_) + "}";
    std::string ssr;
    ssr.reserve(1024);
    CNS_DEBUG(logKey, "LEVEL = {}", level.value_or(599)); // TODO Level upper limit
    auto const& op = ops(key_);
    if(op.type_.empty()) {
        ssr = "T {" + op.qn_ + "}";
    }
    else {
        ssr = op.type_ + "{" + op.qn_ + "}";
    }

    if(level <= 0 && nexts_.size() > 0) {
        ssr.append("-> <...>\n");
        return ssr;
    }

    if(level > 0 && nexts_.empty()) {
        if(TypeSummaries.find(key_) != std::end(TypeSummaries)) {
            ssr.append("\n");
            ssr.append(space(indent));
            //ss << "Resolving further using TypeSummaries built so far:\n";
            //space(ss, indent);
            ssr += ">+" + TypeSummaries.at(key_).summarizeNoStat({level.value() - 1}, indent);
        }
    }

    std::for_each(begin(nexts_), end(nexts_),
        [&](auto const& ts) {
            ssr.append("\n");
            ssr.append(space(indent));
            ssr += "|->" + ts.summarizeNoStat({level.value() - 1}, indent + 2);
        });

    return ssr;
}

/*
std::ostream& operator<<(std::ostream &os, TypeSummary const &ts) {
    os << ts.summarize({1}) << "\n";
    return os;
}

void summarize(std::ostream &os, TypeSummary const &ts, unsigned depth = 0) {
    os << ts.summarize({depth}) << "\n";
}
*/


// Create copies of typetransforms. Then resolve and update history branches as needed.
// Eliminate local history with history by using context.
//void elaborateHistory(History const &histree) {//, std::optional<int> level) {
void elaborateHistories() {
    LOG_FUNCTION_TIME;
    constexpr auto logKey = "<elab>";
    CNS_DEBUG_MSG(logKey, "begin");
    TypeSummaries.clear();
    //summarize(h, level);
    std::for_each(begin(TypeTransforms), end(TypeTransforms),
        [&](auto const& h_) {
            auto const sumkey = h_.first + "[" + std::to_string(h_.second.branch().size()) + "]";
            auto const &h = h_.second;
            CNS_DEBUG_MSG(sumkey, "Making summary");
            auto ts = TypeSummary(h);
            CNS_DEBUG(sumkey, "{{{}}} <{}>", ts.id(), h.branch().size());
            std::for_each(h.bbegin(), h.bend(),
                [&](auto const&bh) {
                    // make type summary from local history and append to nexts_
                    auto pc = h.getContext();
                    if(bh.context()) {
                        CNS_DEBUG_MSG(sumkey, "Extending parent context with branch");
                        pc.insert(std::begin(bh.context().value()), std::end(bh.context().value()));
                    }
                    /*
                    if(level > 0) {
                        CNS_INFO_MSG("Decrement level by 1");
                        auto nl = std::make_optional(level.value() - 1);
                        level.swap(nl);
                    }
                    */

                    CNS_DEBUG(sumkey, "{{{}}} <{}> Creating new branch: {{{}}}", ts.id(), h.branch().size(), bh.id());
                    auto th = makeTypeSummaryLH({bh.history(), bh.linkInfo(), pc});
                    //ts.addNextBranch(th);
                    auto &og = OverflowGuard::get();
                    if(og.ok()) {
                        auto th = makeTypeSummaryLH({bh.history(), bh.linkInfo(), pc});
                        ts.addNextBranch(th);
                    }
                    else {
                        CNS_WARN(sumkey, "Stopping makeTypeSummaryLH recursion for {{{}}}, skipping {{{}}} because overflow guard limit exceeded.", ts.id(), bh.id());
                    }
                });

            //CNS_DEBUG("Adding Summary for {{{}}}: {{{}}}", h_.first, TypeSummaries.at(h_.first).summary({4}));
            TypeSummaries.insert({h_.first, ts});
            OverflowGuard::reset();
        });

    CNS_DEBUG_MSG(logKey, "end");
}

/*
template <> struct fmt::formatter<History>: formatter<string_view> {
    template <typename FormatContext>
    auto format(History const& h, FormatContext& ctx) {
        string_view vh;
        return formatter<string_view>::format(vh, ctx);
    }
};

template <> struct fmt::formatter<TypeSummary>: formatter<string_view> {
    template <typename FormatContext>
    auto format(TypeSummary const& ts, FormatContext& ctx) {
        string_view vts = ts.summarize({depth});
        return formatter<string_view>::format(vts, ctx);
    }
};
*/

#endif  // HISTORY_H
