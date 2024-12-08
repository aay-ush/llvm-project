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
using LocalHistory = std::pair<std::reference_wrapper<History const>, std::optional<HistoryContext>>;

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
            CNS_DEBUG("");
            CNS_DEBUG("end.");
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

        CNS_DEBUG("");

        HistoryContext hc;
        std::vector<CensusKey> args;
        std::regex functionPrefix("^.*?\\$");
        std::regex fptrParmX("^.*?\\$.*\\$");

        unsigned i = 0;
        std::for_each(call.arg_begin(), call.arg_end(), [&](auto const *a) {
            CNS_DEBUG("for_each arg.");
            // get arg qn
            auto const key = qualifiedName(context, call, *a);
            if(census.find(key) == census.end()) {
                // Shouldn't really happen.
                FOUT << "[ERROR](makeHistoryContext) arg operand :(" << key << ") not in Census.\n";
                CNS_ERROR("continue.");
                CNS_DEBUG("end.");
                hc[params[i]] = params[i];
                i++;
                return;
            }

            // Avoid creating links between parameters e.g. hof.$1.$0 = hof.$0
            std::smatch matchParm, matchArg;
            if(std::regex_search(params[i], matchParm, functionPrefix)
                && std::regex_search(key, matchArg, functionPrefix)) {
                if(matchParm.str() == matchArg.str()) {
                    FOUT << "[INFO ](makeHistoryContext) arg operand (" << key << ") and param(" << params[i] << ") are both parameters of same function(" << matchArg.str() << "), skipping\n";
                    hc[params[i]] = params[i];
                    i++;
                    return;
                }
            }

            // If parameter is fptr param, use arg = param mapping to facilitate resolving fptr
            std::smatch fptrParm;
            if(std::regex_search(params[i], fptrParm, fptrParmX)) {
                FOUT << "[INFO ](makeHistoryContext) Found fptr parm: {key, value} = {" << params[i] << ", " << key << "}\n";
                FOUT << "[INFO ](makeHistoryContext) Reversed context pair: {key, value} = {" << key << ", " << params[i] << "}\n";
                hc[key] = params[i++];
            }
            else {
                FOUT << "[INFO ](makeHistoryContext) {key, value} = {" << params[i] << ", " << key << "}\n";
                hc[params[i++]] = key;
            }
            args.push_back(key);
            return;
        });

        FOUT << "[DEBUG](makeHistoryContext) " << String(context, call) << " produced " << args.size() << " args.\n";
        CNS_DEBUG("end.");

        return hc;
    }

    inline std::string regex_escape(const std::string &text) {
        const std::regex chars_to_escape("[.^$|()\\[\\]{}*+?\\\\]");
        return std::regex_replace(text, chars_to_escape, "\\$0");
    }


    std::string derefIdFromContext(std::string id, HistoryContext const& hc) {
        CNS_DEBUG("");
        //FOUT << "[INFO ](History::derefIdFromContext) Resolving {" << id << "}\n";
        for(unsigned i = 0; i != hc.size(); i++) {
            for(auto &[key, val]: hc) {
                //FOUT << "[INFO ](History::derefIdFromContext) hc[" << i << "]: [" << key << " ↦ " << val << "] ('" << id << "')\n";
                if(key == val) {
                    CNS_DEBUG("Key = value, skip");
                    continue;
                }
                //FOUT << "[INFO ](History::derefIdFromContext) ('" << id << "') -> "/*<< "\n"*/;
                std::regex pattern("\\b" + regex_escape(key));
                id = std::regex_replace(id, pattern, val, std::regex_constants::format_sed);
                //FOUT << "('" << id << "')\n";
            }
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
    CNS_DEBUG("");
    function_ = fn.getNameAsString();
    unsigned pos = 0;
    // ForEach parameter, lookup corresponding census operand
    std::for_each(fn.param_begin(), fn.param_end(), [&](auto const *p) {
            // get qualifiedName
            auto const& qn = function_ + ".$" + std::to_string(pos++);
            if(census.find(qn) == census.end()) {
                FOUT << "[ERROR](HistoryTemplate::HistoryTemplate) param operand for: " << qn << " not in Census.\n";
                FOUT << "[INFO ](HistoryTemplate::HistoryTemplate) Adding  " << qn << " operand in Census.\n";

                // If a param operand doesn't exist yet, which is possible if the function is processed for the first time, just initiate it because otherwise the template will not be created.
                census.insert(makeCensusSourceNode(buildOpData(*p)));
            }

            // TODO: what if a param is not in census but remaining are? Unlikely since call is processed as a unit.
            params_.push_back(qn);
        });
    CNS_DEBUG("end.");
}

HistoryTemplate::HistoryTemplate(clang::ASTContext &context, clang::CallExpr const& call, clang::DeclRefExpr const &fptr) {
    CNS_DEBUG("<fptr>");
    function_ = qualifiedName(context, fptr);
    unsigned pos = 0;
    // ForEach parameter, lookup corresponding census operand
    std::for_each(call.arg_begin(), call.arg_end(), [&](auto const *p) {
            // get qualifiedName
            auto const& qn = function_ + ".$" + std::to_string(pos++);
            if(census.find(qn) == census.end()) {
                FOUT << "[ERROR](HistoryTemplate::HistoryTemplate<fptr>) param operand for: " << qn << " not in Census.\n";
                FOUT << "[INFO ](HistoryTemplate::HistoryTemplate<fptr>) Adding  " << qn << " operand in Census.\n";
                // If a param operand doesn't exist yet, which is possible if the function is processed for the first time, just initiate it because otherwise the template will not be created.
                census.insert(makeCensusSourceNode(buildOpData(context, *p)));
            }
            // TODO: what if a param is not in census but remaining are?
            FOUT << "[INFO ](HistoryTemplate::HistoryTemplate<fptr>) Found param operand for: " << qn << " in Census.\n";
            params_.push_back(qn);
        });
    CNS_DEBUG("<fptr> end.");
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
        void extend(History const &h);

        //void extend(History const &h) {
        // Maybe a redo needed to extend context to branch histories
        // Or maybe in getcontextresolvedopstr, pass context for branches
        // nay, add current context to h.second!
        // Is an overload needed that takes only History instead of LocalHistory? ✓
        void extend(LocalHistory const &h);

        /*
        // REDO
        void extend_c(History h) {
            CNS_DEBUG("");
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
            CNS_DEBUG("end.");
        }
        */

        // Probably redo? How is local context passed to id()?
        std::string id(std::optional<HistoryContext const> hc) const {
            CNS_DEBUG("");
            // if operand or context are not same we don't get same string.
            // works for operands without context too and allows for extension of operand history.
            std::stringstream ss;
            ss << op_;
            auto const& op = getContextResolvedOpStr(hc);
            ss << "(" << op/*->qn_*/ << ")";
            CNS_DEBUG("end.");
            return ss.str();
        }

        // TODO: add a condense function to use in instantiation
        // -> If the history is instantiated, condense() can be used to get a new contextualized
        //    history with 0 depth/single branch.

        CensusKey opId() const {
            CNS_DEBUG("");
            CNS_DEBUG("end.");
            return op_;
        }

        bool hasContext() const {
            CNS_DEBUG("");
            CNS_DEBUG("end.");
            return !hc_.empty();
        }

        void addContext(HistoryContext hc) {
            CNS_DEBUG("");
            FOUT << "[INFO ](History::addContext) Adding context to history of " << idversion() << "\n";
            auto nhc = hc_;
            FOUT << "[INFO ](History::addContext) Current hc_.size() = " << hc_.size() << "\n";
            nhc.insert(hc.begin(), hc.end());
            FOUT << "[INFO ](History::addContext) After extending with input hc, hc.size() = " << nhc.size() << "\n";
            setContext(nhc);
            FOUT << "[INFO ](History::addContext) Updated hc_.size() = " << hc_.size() << "\n";
            CNS_DEBUG("end.");
        }
        /*
        History addContext(HistoryContext hc) const {
            CNS_DEBUG("");
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
            CNS_DEBUG("end.");
            return h;
        }
        */

        explicit History(CensusKey const& op): op_(op) {
            CNS_DEBUG("explicit()");
            //FOUT << "[INFO ](History::explicit) Init: " << op_ << "\n";
            updateVersion();
            FOUT << "[INFO ](History::explicit) Created : " << idversion() << "\n";
            CNS_DEBUG("explicit() end.");
        }

        History(HistoryContext const& context, CensusKey const& op):
            op_(op),
            hc_(context) {

            CNS_INFO("");
            //FOUT << "[INFO ](History::History(c,k)) Init: " << op_ << "(" << getContextResolvedOp().qn_ << ")\n";
            updateVersion();
            FOUT << "[INFO ](History::History(c,key)) Created : " << idversion() << "\n";
            CNS_INFO("end.");
        }

        History() = delete;
        ~History(){// = default;
            CNS_INFO("");
            version_ = "XXXXXX_" + version_;
            //FOUT << "[INFO ](History::~History()) end: " << op_ << "(" << getContextResolvedOp().qn_<< ")\n";
            FOUT << "[INFO ](History::~History()) end: " << idversion() << "\n";
            CNS_INFO("end.");
        }

        // History is extended by copying this history to another history. Supports branching.
        // Copied history should only be used in extending and should not be mixed with global history.
        History(History const& h)  = delete;// = default;
        /*
            op_(h.op_),
            hc_(h.hc_),
            branch_(h.branch_),
            version_(h.version_),
            copy_(h.copy_ + 1) {
            CNS_INFO("");

            updateVersion();
            //FOUT << "[INFO ](History::History(h)) Init: " << op_ << "(" << getContextResolvedOp().qn_ << "): [" << branch_.size() << "]\n";
            FOUT << "[INFO ](History::History(h)) Created: " << idversion() << "\n";
            CNS_INFO("end.");
        }
        */
        //History& operator=(History h) = default;
        History(History && h)  = default;// = default;

        std::string getContextResolvedOpStr(std::optional<HistoryContext const> lc) const;
        std::string getContextResolvedOpStr0(std::optional<HistoryContext const> lc) const;

        std::optional<OpData const> getContextResolvedOp(std::optional<HistoryContext const> hc) const {
            CNS_DEBUG("");

            auto rop = getContextResolvedOpStr(hc);
            if(census.find(rop) == census.end()) {
                CNS_DEBUG("end.");
                return {};
            }

            CNS_DEBUG("end.");
            return ops(rop);
        }

        auto bbegin() const {
            CNS_DEBUG("");
            CNS_DEBUG("end.");
            return branch_.begin();
        }
        auto bend() const {
            CNS_DEBUG("");
            CNS_DEBUG("end.");
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
            std::stringstream ss;
            ss << "H{" << op_ << "}<" << version_ << ">";
            return ss.str();
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
            std::stringstream ss;
            // version = opId + contextual id + hc.size : branch.size
            ss << /*op_ << "_" <<*/ id({hc_}) << "." << std::to_string(hc_.size()) << ":" << branch_.size() <<  "c" << std::to_string(copy_);
            FOUT << "[INFO ](History::updateVersion) Updated H{" << op_ << "}<" << version_ << "> to <" << ss.str() << ">\n";
            version_ = ss.str();
        }

};

void History::extend(History const &h) {
    CNS_INFO("<g>");
    FOUT << "[INFO ](History::extend<g>) Current version: " << idversion() << "\n";
    FOUT << "[INFO ](History::extend<g>) Input history version: " << h.idversion() << "\n";
    FOUT << "[INFO ](History::extend<g>) Current context size for H(" << h.op_ << "): " << h.hc_.size() << "\n";
    FOUT << "[INFO ](History::extend<g>) Extending input context using current version: " << idversion() << "\n";
    // Check if needed, maybe not since local context is stored with histories now.
    // Extend context of input history before appending to branch
    //h.hc_.insert(hc_.begin(), hc_.end());
    //FOUT << "[INFO ](History::extend<g>) Updated context size for input " << h.idversion() << ": " << h.hc_.size() << "\n";
    /*
    for(auto &bh: h.branch_) {
        FOUT << "[INFO ](History::extend<g>) Extending input branch " << bh.idversion() << " context using " << idversion() << "\n";
        bh.hc_.insert(hc_.begin(), hc_.end());
        bh.updateVersion();
    }
    */
    //h.updateVersion();
    // Irrelevant since it's a copy: FOUT << "[INFO ](History::extend<g>) New context size for H(" << h.version_ << "): " << h.hc_.size() << "\n";
    // end check

    // Check if this is needed?
    //FOUT << "[INFO ](History::extend<g>) Extending current context with updated input context " << h.idversion() << ": " << h.hc_.size() << "\n";
    //hc_ = h.hc_;
    // end check

    CNS_INFO("Extending current branch with new input");
    branch_.push_back({h, {hc_}});
    updateVersion();
    FOUT << "[INFO ](History::extend<g>) New version: " << idversion() << "\n";
    CNS_INFO("<g> end.");
}

void History::extend(LocalHistory const &h) {
    CNS_INFO("");
    FOUT << "[INFO ](History::extend) Current version: " << idversion() << "\n";
    FOUT << "[INFO ](History::extend) Input history version: " << h.first.get().idversion() << "\n";
    if(h.second) {
        FOUT << "[INFO ](History::extend) Input local context size: " << h.second.value().size() << "\n";
    }
    //FOUT << "[INFO ](History::extend) Current context size for H(" << h.op_ << "): " << h.hc_.size() << "\n";
    //FOUT << "[INFO ](History::extend) Extending context using H(" << op_ << "): branch(" << branch_.size() << "); context(" << hc_.size() << ")\n";
    //
    FOUT << "[INFO ](History::extend) Extending input's local context using current version: " << idversion() << "\n";
    // Extend context of input history before appending to branch
    HistoryContext nlhc;
    if(h.second) {
        nlhc = h.second.value();
        FOUT << "[INFO ](History::extend) Input local context size: " << nlhc.size() << "\n";
    }
    nlhc.insert(hc_.begin(), hc_.end());
    FOUT << "[INFO ](History::extend) Updated input local context size: " << nlhc.size() << "\n";

    //FOUT << "[INFO ](History::extend) New context size for H(" << h.op_ << "): " << h.hc_.size() << "\n";
    //FOUT << "[INFO ](History::extend) Updated context size for input " << h.idversion() << ": " << h.hc_.size() << "\n";
    /*
    for(auto &bh: h.first.get().branch_) {
        FOUT << "[INFO ](History::extend) Extending input branch " << bh.first.get().idversion() << " context using " << idversion() << "\n";
        HistoryContext bhlc;
        if(bh.second) {
            bhlc = bh.second.value();
        }
        bhlc.insert(hc_.begin(), hc_.end());
        bh.second.swap(bhlc);
    }
    */
    //h.updateVersion();
    //FOUT << "[INFO ](History::extend) Extending current context with updated input context " << h.idversion() << ": " << h.hc_.size() << "\n";
    //hc_ = h.hc_;
    CNS_INFO("Extending current branch with new input");
    branch_.emplace_back(h.first, nlhc);
    // Irrelevant since it's a copy: FOUT << "[INFO ](History::extend) New context size for H(" << h.version_ << "): " << h.hc_.size() << "\n";
    updateVersion();
    FOUT << "[INFO ](History::extend) New version: " << idversion() << "\n";
    CNS_INFO("end.");
}

std::unordered_map<CensusKey, History> TypeTransforms;

std::string History::getContextResolvedOpStr0(std::optional<HistoryContext const> lc) const {
    return "";
}

std::string History::getContextResolvedOpStr(std::optional<HistoryContext const> lc) const {
    CNS_DEBUG("");
    //FOUT << "[INFO ](History::getContextResolvedOpStr) Resolving op_{" << op_ << "} for H{" << op_ << "}<" << version_ << ">\n";
    auto rop = derefIdFromContext(op_, hc_);
    if(lc) {
        //FOUT << "[INFO ](History::getContextResolvedOpStr) Now resolving rop{" << rop << "} for H{" << op_ << "}<" << version_ << "> using input context\n";
        rop = derefIdFromContext(rop, lc.value());
    }

    if(op_ != rop) {
        // Check if rop exists
        if(census.find(rop) == census.end()) {
            CNS_DEBUG(" New rop does not match any census node.");
            auto ropp = rop.substr(0, rop.find("$") - 1);
            if(TypeTransforms.find(ropp) != TypeTransforms.end()) {
                // Found H(ropp)
                //FOUT << "[INFO ](History::getContextResolvedOpStr) Found H(container){" << ropp << "}\n";
                // Check if ropp has any resolution
                auto const &hropp = TypeTransforms.at(ropp);
                auto roppn = hropp.getContextResolvedOpStr(lc);
                if(ropp != roppn) {
                    // Try now
                    FOUT << "[INFO ](History::getContextResolvedOpStr) Resolved container, ropp = {" << roppn << "}\n";
                    rop = roppn + "." + rop.substr(rop.find("$"));
                    if(census.find(rop) == census.end()) {
                        FOUT << "[INFO ](History::getContextResolvedOpStr) However, rop = {" << rop << "} not in census\n";
                        // Create census node for rop and add op as dominator?
                    }
                    else {
                        FOUT << "[INFO ](History::getContextResolvedOpStr) rop = {" << rop << "} found in census\n";
                        // Who is the dominator for this?
                    }
                }
            }

            else {
                // Try removing the container:
                auto ropn = rop.substr(rop.find(".") + 1);
                //FOUT << "[INFO ](History::getContextResolvedOpStr) removing container, ropn = {" << ropn << "}\n";
                if(census.find(ropn) == census.end()) {
                    CNS_DEBUG("Removing container did not lead to any census match.");
                }
                else {
                    CNS_DEBUG("Removing container led to census match.");
                    rop = ropn;
                }
            }

        }
        else {
            CNS_DEBUG(" New rop found in census.");
        }
    }

    return rop;
    CNS_DEBUG("end.");
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
*/

// History context applies to extension
// => On extending history, context gets extended too.
// => Context is applied to op.use_
//
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
            /*
            auto blc = hc;
            if(bh.second) {
                blc.insert(bh.second.value().begin(), bh.second.value().end());
            }
            */
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
    /*
    std::vector<std::pair<std::string, std::string>> branch;
            //dumpH(os, hb_.first.get().opId(), hb_.first.get().updateCache(hcb), indent + 2);
            //branch.push_back({hb_.first.get().opId(), hb_.first.get().updateCache(hcb)});
    std::for_each(begin(branch), end(branch),
        [&](auto const& b) {
            dumpH(ss, b.first, b.second);
        });
    */

    return os;
}

// Two kinds of history:
//  - global history -> stored in TypeTransforms, the normal history
//      that is built by using the operand and then extended with use.
//  - contextualized history -> may or may not be part of TypeTransforms. Use in template instantiation and history extending operations. If global history with same opId exists, contextualized history will not be part of TypeTransforms.
//
// TODO redo: what is meant by equality for history object? When history is extended, it may be
//   for same operand but can have a different context
//   This equality is meant for global history only.
bool operator==(History const &a, History const &b) {
    CNS_DEBUG("");
    //FOUT << "[INFO ](History::operator==<h>) a: (" << a.id() << "/ " << a.opId() << ")\n";
    //FOUT << "[INFO ](History::operbtor==<h>) b: (" << b.id() << "/ " << b.opId() << ")\n";
    CNS_DEBUG("end.");
    // Support both context resolved history and otherwise.
    //return (a.id() == b.id()) || (a.opId() == b.opId());
    return a.opId() == b.opId(); //eturn a.id() == b.id(); // && (a.branch() == b.branch());
}
bool operator!=(History const &a, History const &b) {
    CNS_DEBUG("");
    CNS_DEBUG("end.");
    return !(a == b);
}
bool operator==(History const &a, CensusKey const &b) {
    CNS_DEBUG("");
    //FOUT << "[INFO ](History::operator==<h>) a: (" << a.id() << "/ " << a.opId() << ")\n";
    //FOUT << "[INFO ](History::operbtor==<ck>) b: (" << b << ")\n";
    CNS_DEBUG("end.");
    return a.opId() == b;
    //return (a.id() == b) || (a.opId() == b);
    //return (a.id() == b) && !(a.hasContext());
}
bool operator!=(History const &a, CensusKey const &b) {
    CNS_DEBUG("");
    CNS_DEBUG("end.");
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
    CNS_DEBUG("");
    // for each arg, add parm-arg pair to context.
    // return context

    if(params_.size() != call.getNumArgs()) {
        CNS_ERROR("Nb(params != Nb(args), possibly unsupported variadic function or a template mismatch.");
        FOUT << "[ERROR](HistoryTemplate::instantiate) FnTemplate(" << name() << ") instantiating for function call (" << String(context, call) << ") failed.\n";
        CNS_DEBUG("end.");
        return {};
    }

    HistoryContext lc = makeHistoryContext(params_, context, call);

    std::vector<LocalHistory> h;
    std::for_each(begin(params_), end(params_),
        [&](auto const &p) {
            CNS_DEBUG("for_each param step");

            if(TypeTransforms.find(p) == std::end(TypeTransforms)) {
                // Parameter history is not built yet.
                FOUT << "[INFO ](HistoryTemplate::instantiate) New history created for " << function_ << " var {" << p << "}\n";
                TypeTransforms.emplace(p, p);
                //TypeTransforms.insert({p, History(p)});
            }

            // Add hc to existing parameter history
            auto const &hp = TypeTransforms.at(p);
            h.emplace_back(hp, lc);
            FOUT << "[INFO ](HistoryTemplate::instantiate) Current context size for H(" << p << "): " << hp.getContext().size() << "\n";

            CNS_DEBUG("end for_each param step");
        });

    CNS_DEBUG("end.");
    return h;
}

// TypeSummary evaluates to key.type_ or key.qn_ or key.type_ -> (key.type_ | key.qn_) and so on.
class TypeSummary {
    public:

    TypeSummary(History const &h);

    void addNextBranch(TypeSummary const& branch) {
        //FOUT << "[INFO ](addNextBranch) {" << key_ << ";[" << nexts_.size() << "]_" << numi_ << "} Adding Nexts_ branch: {" << branch.key_ << "}\n";
        nexts_.push_back(branch);
        //FOUT << "[INFO ](addNextBranch) {" << key_ << ";[" << nexts_.size() << "]_" << numi_ << "} end\n";
    }

    std::string summarize(std::optional<unsigned> level, int indent = 0) const;

    ~TypeSummary() = default; /*{
        CNS_DEBUG("");
        FOUT << "[INFO ](~TypeSummary) Clearing {" << key_ << ";[" << nexts_.size() << "]_" << numi_ << "}\n";
        key_.clear();
        nexts_.clear();
        CNS_DEBUG("end.");
    }
    */

    TypeSummary(const TypeSummary& t) = default; /*: key_(t.key_), nexts_(t.nexts_) {
        numi_ = num_++;
        FOUT << "[INFO ](TypeSummary::copy) Copy created from _" << t.numi_ << "_: {" << key_ << ";[" << nexts_.size() << "]_" << numi_ << "}\n";
    }
    */
    TypeSummary(TypeSummary&& t) = default; /*: key_(std::move(t.key_)), nexts_(std::move(t.nexts_)) {
        numi_ = num_++;
        FOUT << "[INFO ](TypeSummary::move) Moved from _" << t.numi_ << "_: {" << key_ << ";[" << nexts_.size() << "]_" << numi_ << "}\n";
    }
    */

    TypeSummary& operator=(const TypeSummary& t) = default;
    TypeSummary& operator=(TypeSummary&& t) = default;

    std::string id() const {
        std::stringstream ss;
        ss << key_ << ";[" << size() << "]"; //_" << numi_;
        return ss.str();
    }

    unsigned size() const {
        return nexts_.size();
    }

    private:
    CensusKey key_;
    std::vector<TypeSummary> nexts_;
    //static unsigned num_;
    //unsigned numi_;
};

//unsigned TypeSummary::num_ = 0;

/*
bool operator==(TypeSummary lhs, CensusKey rhs) {
    return lhs.id() == rhs;
}
bool operator!=(TypeSummary lhs, CensusKey rhs) {
    return !(lhs == rhs);
}
*/

TypeSummary makeTypeSummaryLH(LocalHistory const& lh);

TypeSummary::TypeSummary(History const&h) {
    CNS_DEBUG("");
    key_ = h.opId();
    //numi_ = num_++;
    //FOUT << "[INFO ](TypeSummary::TypeSummary) Created summary {" << key_ << ";[" << nexts_.size() << "]_ " << num_ << "} for H<" << h.opId() << "} [h.branch.size=" << h.branch().size() << "]>\n";
    FOUT << "[INFO ](TypeSummary::TypeSummary) Created summary {" << key_ << ";[" << nexts_.size() << "]} for H<" << h.opId() << "} [h.branch.size=" << h.branch().size() << "]>\n";
    CNS_DEBUG("end.");
}

TypeSummary makeResolvedSummary(std::string const& keyOp, std::string const& keyRops) {
    CNS_INFO("");
    FOUT << "[INFO ](makeResolvedSummary) Building summary for {" << keyOp << ", " << keyRops << "}\n";
    auto const& op = ops(keyOp);
    FOUT << "[INFO ](makeResolvedSummary) {" << keyOp << "}: A\n";
    TypeSummary ts (TypeTransforms.at(keyOp));

    if(keyRops == keyOp) {
        FOUT << "[INFO ](makeResolvedSummary) {" << keyOp << "} = {" << keyRops << "}\n";
        FOUT << "[INFO ](makeResolvedSummary) {" << ts.id() << "}: end.\n";
        return ts;
    }

    // If op is a parameter and rop is arg, return ts
    std::regex pattern("^.*?\\$");
    if(std::regex_search(keyOp, pattern)) {
        // keyRops maybe arg (unless something like hof.$1.$0)
        // => summary of parameter should be assigned to arg --> ??
        //    not needed since that is taken care of by history
        if(!std::regex_search(keyRops, pattern)) {
            // => arg
            FOUT << "[INFO ](makeResolvedSummary) {" << ts.id() << "}: {" << keyRops << "} is probably an arg for param {" << keyOp << "}, stopping\n";
            FOUT << "[INFO ](makeResolvedSummary) {" << ts.id() << "}: end.\n";
            return ts;
        }

    }

    // If op.type is not empty
    if(!op.type_.empty()) {
        FOUT << "[INFO ](makeResolvedSummary) {" << ts.id() << "} has defined type. Not using {" << keyRops << "}\n";
        FOUT << "[INFO ](makeResolvedSummary) {" << ts.id() << "}: end.\n";
        return ts;
    }

    if(census.find(keyRops) == std::end(census)) {
        FOUT << "[INFO ](makeResolvedSummary) {" << ts.id() << "}: Cannot find keyRops{" << keyRops << "} in census\n";
        FOUT << "[INFO ](makeResolvedSummary) {" << ts.id() << "}: end.\n";
        return ts;
    }

    /*
    if((keyOp != keyRops)
            && std::regex_search(keyOp, pattern)
            && std::regex_search(keyRops, pattern)) {

        // Possibly, Strongly connected components (one param to next, maybe cyclic)
        CNS_INFO("SCC possibility detected, stopping recursion.");
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

        CNS_INFO("end.");
        return ts;
    }
    */


    FOUT << "[INFO ](makeResolvedSummary) {" << ts.id() << "}: Found keyRops{" << keyRops << "} in census\n";
    //auto th = TypeSummary(TypeTransforms.at(keyRops)); //, {});
    FOUT << "[INFO ](makeResolvedSummary) {" << ts.id() << "}: Adding branch from resolved keyRops{" << keyRops << "}\n";
    FOUT << "[INFO ](makeResolvedSummary) {" << ts.id() << "}: B\n";
    ts.addNextBranch(TypeTransforms.at(keyRops));

    FOUT << "[INFO ](makeResolvedSummary) {" << ts.id() << "}: end.\n";
    return ts;
}

TypeSummary makeTypeSummaryLH(LocalHistory const& lh) {
    CNS_INFO("");

    auto const &h = lh.first.get();
    auto hid = h.opId();
    FOUT << "[INFO ](makeTypeSummaryLH) Building summary for {" << hid << "}\n";
    auto rops = h.getContextResolvedOpStr(lh.second);
    FOUT << "[INFO ](makeTypeSummaryLH) {" << hid << "}: 1.\n";
    auto ts = makeResolvedSummary(h.opId(), rops);
    if(!lh.second) {
        FOUT << "[INFO ](makeTypeSummaryLH) {" << ts.id() << "}: No local context; end.\n";
        return ts;
    }

    FOUT << "[INFO ](makeTypeSummaryLH) {" << ts.id() << "}: Using local history context to summarize branches for {" << ts.id() << "} branch{size=" << h.branch().size() << "}\n";
    auto pc = h.getContext();
    if(lh.second) {
        CNS_DEBUG("Extending parent context with local");
        pc.insert(std::begin(lh.second.value()), std::end(lh.second.value()));
    }
    std::for_each(h.bbegin(), h.bend(),
        [&](auto const &bh) {
            if(bh.first.get().opId() == hid || bh.first.get().opId() == rops) {
                return;
            }
            auto pcn = pc;
            if(bh.second) {
                CNS_DEBUG("Extending parent context with branch local");
                pcn.insert(std::begin(bh.second.value()), std::end(bh.second.value()));
            }
            FOUT << "[INFO ](makeTypeSummaryLH) {" << ts.id() << "}: Creating summary for branch: {" << bh.first.get().opId() << "}\n";
            FOUT << "[INFO ](makeTypeSummaryLH) {" << ts.id() << "}: 2.\n";
            ts.addNextBranch(makeTypeSummaryLH({bh.first.get(), pcn}));
        });

    FOUT << "[INFO ](makeTypeSummaryLH) {" << ts.id() << "}: end.\n";
    return ts;
}

std::unordered_map<std::string, TypeSummary> TypeSummaries;

// S(a) = Typeof(a) -> S(next)
std::string TypeSummary::summarize(std::optional<unsigned> level, int indent) const {
    std::stringstream ss;

    //FOUT<< "{" << key_ << ";[" << nexts_.size() << "]_" << numi_ << "} LEVEL = " << level.value_or(599) << "\n";
    auto const& op = ops(key_);
    if(op.type_.empty()) {
        ss << "T {" << op.qn_ << "}";
    }
    else {
        ss << op.type_ << "{" << op.qn_ << "}";
    }

    if(level <= 0 && nexts_.size() > 0) {
        ss << "-> <...>\n";
        return ss.str();
    }

    if(level > 0 && nexts_.empty()) {
        if(TypeSummaries.find(key_) != std::end(TypeSummaries)) {
            ss << "\n";
            space(ss, indent);
            //ss << "Resolving further using TypeSummaries built so far:\n";
            //space(ss, indent);
            ss << ">+" << TypeSummaries.at(key_).summarize({level.value() - 1}, indent);
        }
    }

    std::for_each(begin(nexts_), end(nexts_),
        [&](auto const& ts) {
            ss << "\n";
            space(ss, indent);
            ss << "|-> " << ts.summarize({level.value() - 1}, indent + 2);
        });

    return ss.str();
}

std::ostream& operator<<(std::ostream &os, TypeSummary const &ts) {
    os << ts.summarize({1}) << "\n";
    return os;
}

void summarize(std::ostream &os, TypeSummary const &ts, unsigned depth = 0) {
    os << ts.summarize({depth}) << "\n";
}

// Create copies of typetransforms. Then resolve and update history branches as needed.
// Eliminate local history with history by using context.
void elaborateHistory(History const &h) {//, std::optional<int> level) {
    CNS_INFO("");
    TypeSummaries.clear();
    //summarize(h, level);
    std::for_each(begin(TypeTransforms), end(TypeTransforms),
        [&](auto const& h_) {
            auto const &h = h_.second;
            FOUT << "[INFO ](elaborateHistory) Making summary for {" << h_.first << "}; <branch.size() = " << h.branch().size() << ">\n";
            auto ts = TypeSummary(h);
            FOUT << "[INFO ](elaborateHistory) {" << ts.id() << "} <" << h.branch().size() << "> Adding branches.\n";
            std::for_each(h.bbegin(), h.bend(),
                [&](auto const&bh) {
                    // make type summary from local history and append to nexts_
                    auto pc = h.getContext();
                    if(bh.second) {
                        CNS_DEBUG("Extending parent context with branch");
                        pc.insert(std::begin(bh.second.value()), std::end(bh.second.value()));
                    }
                    /*
                    if(level > 0) {
                        CNS_INFO("Decrement level by 1");
                        auto nl = std::make_optional(level.value() - 1);
                        level.swap(nl);
                    }
                    */

                    FOUT << "[INFO ](elaborateHistory) {" << ts.id() << "} <" << h.branch().size() << "> Creating new branch: {" << bh.first.get().opId() << "}\n";
                    auto th = makeTypeSummaryLH({bh.first.get(), pc});
                    ts.addNextBranch(th);
                });

            //FOUT << "[INFO ](elaborateHistory) Adding Summary for {" << h_.first << "}: {" << TypeSummaries.at(h_.first).summary({4}) << "\n}\n";
            TypeSummaries.insert({h_.first, ts});
        });

    CNS_INFO("end.");
}

#endif  // HISTORY_H
