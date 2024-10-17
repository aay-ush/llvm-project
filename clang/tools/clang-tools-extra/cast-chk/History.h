#ifndef HISTORY_H
#define HISTORY_H

#include "Census.h"

// History context contains substitutions for HistoryTemplate that can be used to provide history.
// How are the parinings stored?
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

// Template: for every parameter in a function, store it's Census operand and associate it to the parameter.
class HistoryTemplate {
    public:
        //History instantiate(OpData arg, unsigned argPos);
        // History instantiation requires all args to be instantiated as a unit.
        // Callexpr has an arg iterator that can be used to instantiate all args for every arg op.
        //<> instantiate(<> varargs, unsigned argPos);
        std::vector<History> instantiate(clang::ASTContext const &context, clang::CallExpr const &call);

        HistoryTemplate(clang::FunctionDecl const &fn);

        HistoryTemplate(){ // = delete;
            CNS_DEBUG("");
            CNS_DEBUG("end.");
        }
        ~HistoryTemplate() {// = default;
            CNS_DEBUG("");
            CNS_DEBUG("end.");
        }
        HistoryTemplate(HistoryTemplate const&) = default;

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

std::vector<History> HistoryTemplate::instantiate(clang::ASTContext const &context, clang::CallExpr const &call) {
    CNS_DEBUG("");
    // for each arg, add parm-arg pair to context.
    // return context
    HistoryContext hc;

    unsigned i = 0;
    std::vector<CensusKey> args;
    std::for_each(call.arg_begin(), call.arg_end(), [&](auto const &a) {
            // get arg qn
            CensusKey qn;
            if(census.find(qn) == census.end()) {
                // Shouldn't really happen.
                FOUT << "[ERROR](HistoryTemplate::instantiate) arg operand for: " << qn << " not in Census.\n";
                CNS_DEBUG("end.");
                return;
            }

            //auto &[op, _] = census[qn];
            hc[params_[i++]] = qn;
            args.push_back(qn);
    });

    std::vector<History> h;
    std::for_each(begin(params_), end(params_),
        [&](auto const &p) {
            h.push_back({hc, p});
        });
    /*
    std::transform(begin(params_), end(params_), back_inserter(h),
        [&](auto const &p) {
            return History(hc, p);
        });
    */
    CNS_DEBUG("end.");
    return h;
}

// History(a) => we know type1 -> type 2 for a or its qualified name.
// Dr.sheng:
// History template should only refer to parameters and constant pointers.
// History template should not have census involvment.
//
HistoryTemplate::HistoryTemplate(clang::FunctionDecl const &fn) {
    CNS_DEBUG("");
    function_ = fn.getNameAsString();
    // ForEach parameter, lookup corresponding census operand
    std::for_each(fn.param_begin(), fn.param_end(), [&](auto const&p) {
            // get qn
            CensusKey qn;
            if(census.find(qn) == census.end()) {
                FOUT << "[ERROR](HistoryTemplate::HistoryTemplate) param operand for: " << qn << " not in Census.\n";
                CNS_DEBUG("end.");
                return;
            }
            // TODO: what if a param is not in census but remaining are?
            params_.push_back(qn);
        });
    CNS_DEBUG("end.");
}

// History instantiation should not change dominator info for
// parameters. Why? The parameters in history are impertinent and adding multiple dominators for params just pollutes the history.
// ---> Operand cannot be changed.
// Should Use_ be changed? Maybe. Depends on how history is elaborated.
// No. Because the param has to be substituted not used.
/*
std::vector<History> HistoryTemplate::instantiate(clang::ASTContext const &context, clang::CallExpr const &call) {
    unsigned i = 0;
    std::vector<OpData> args;
    std::for_each(call.arg_begin(), call.arg_end(), [&](auto const &a) {
            // get arg qn
            CensusKey qn;
            if(census.find(qn) == census.end()) {
                // Shouldn't really happen.
                FOUT << "[ERROR](HistoryTemplate::instantiate) arg operand for: " << qn << " not in Census.\n";
                CNS_DEBUG("end.");
                return;
            }

            auto &[op, _] = census[qn];
            std::shared_ptr<History> hp = op.history_.lock();
            *hp = History(params[i++]);
            //op_a.use_.append(params[i++]);
            args.append(op_a);
    });

    auto targs = substitute(args);
    std::vector<History> h;
    std::transform(begin(targs), end(targs), back_inserter(h), History::History);
    return h;
}

std::vector<OpData> HistoryTemplate::substitue(std::vector<OpData> &args) {
    // for n times
    std::vector<OpData> targs;
    std::transform(begin(params), end(params), back_inserter(targs), ops);

    // for each param operand p in params
    for(unsigned i = 0; i != targs.size(); i++) {
        targs[i] = args[i];

        //   for each use qn u in p.use_
        std::for_each(begin(targs[i].use_), end(targs[i].use_), [&](auto const &u) {
            auto match = targs.find(u);
            if( match != targs.end()) {
                CNS_DEBUG("Param match found.");
                if(targs.size() > PTRDIFF_MAX) {
                    CNS_ERROR("Cannot take position of parm, array too large.");
                    return;
                }

                unsigned pos = std::distance(targs.begin(), match);
                if(pos >= targs.size()) {
                    CNS_INFO("Parm pos outside of targs bounds.");
                    return;
                }

                //      if u in param_qns at pos i
                //          replace u with args[i]
                targs[pos] = args[pos];
            }
        });
    }

    return targs;
}
*/

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
class History {
    public:
        // Could be type or container.$param or container.local
        //std::string history() const;
        //History history(int ilevel); // iLevel > 0
        // operator string

        void extend(History const& h) {
            CNS_DEBUG("");
            branch_.push_back(h);
            CNS_DEBUG("end.");
        }

        std::string id() const {
            CNS_DEBUG("");
            // if operand or context are not same we don't get same string.
            // works for operands without context too and allows for extension of operand history.
            std::stringstream ss;
            ss << op_;
            auto const& op = getContextResolvedOp();
            ss << "(" << op.qn_ << ")";
            CNS_DEBUG("end.");
            return ss.str();
        }

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

        explicit History(CensusKey const& op): op_(op) {}

        History(HistoryContext const& context, CensusKey const& op):
            op_(op),
            hc_(context) {

            CNS_DEBUG("");
            CNS_DEBUG("end.");
        }

        History() = delete;
        ~History(){// = default;
            CNS_DEBUG("");
            CNS_DEBUG("end.");
        }

        // History can be extended but coying is not useful.
        History(History const&) = default;
        History& operator=(History const&) = default;

        OpData const& getContextResolvedOp() const {
            CNS_DEBUG("");
            // Substitute from context if applicable
            if(hc_.find(op_) != hc_.end()) {
                CNS_DEBUG("end.");
                return ops(hc_.at(op_));
            }

            CNS_DEBUG("end.");
            return ops(op_);
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

    private:
        //std::string history_;
        CensusKey op_;
        HistoryContext hc_ {};
        // Vector since history is a tree. just like use_, history can have multiple branches.
        std::vector<History> branch_;   // Requires copy
};

std::ostream& operator<<(std::ostream &os, History const& h) {
    std::stringstream ss;
    auto const& op = h.getContextResolvedOp();
    if(op.type_.empty()) {
        ss << op.qn_;
    }
    else {
        ss << op.type_;
    }
    ss << " -> ";
    std::for_each(h.bbegin(), h.bend(),
        [&](auto const& h_) {
            ss << h_;
        });
    ss << "\n";

    os << ss.str();
    return os;
}

bool operator==(History const &a, History const &b) {
    CNS_DEBUG("");
    CNS_DEBUG("end.");
    return a.id() == b.id();
}
bool operator!=(History const &a, History const &b) {
    CNS_DEBUG("");
    CNS_DEBUG("end.");
    return !(a == b);
}
bool operator==(History const &a, CensusKey const &b) {
    CNS_DEBUG("");
    CNS_DEBUG("end.");
    return (a.id() == b) && !(a.hasContext());
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

#endif  // HISTORY_H
