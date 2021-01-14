#ifndef BULLSAT_HPP_
#define BULLSAT_HPP_
#include <algorithm>
#include <cassert>
#include <deque>
#include <iostream>
#include <memory>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

namespace bullsat {

// definitions
enum class Status { Sat, Unsat, Unknown };
enum class LitBool { True, False, Undefine };
using Var = int;
struct Lit;
using Clause = std::vector<Lit>;
using CRef = std::shared_ptr<Clause>;

// x is
// even: positive x0 (0 -> x0, 2 -> x1)
// odd: negative !x0 (1 -> !x0, 3 -> !x1)
struct Lit {
  Lit() = default;
  Lit(const Lit &lit) = default;
  // 0-index
  // Lit(0, true) means x0
  // Lit(0, false) means !x0
  Lit(Var v, bool positive) {
    assert(v >= 0);
    x = positive ? 2 * v : 2 * v + 1;
  }
  bool operator==(Lit lit) const { return x == lit.x; }
  bool operator!=(Lit lit) const { return x != lit.x; }
  bool operator<(Lit lit) const { return x < lit.x; }
  bool pos() const { return !neg(); }
  bool neg() const { return x & 1; }
  Var var() const { return x >> 1; }
  size_t vidx() const { return static_cast<size_t>(var()); }
  size_t lidx() const { return static_cast<size_t>(x); }
  int x;
};

// ~x0 = !x0
inline Lit operator~(Lit p) {
  Lit q(p);
  q.x ^= 1;
  return q;
}

std::ostream &operator<<(std::ostream &os, const Lit &lit) {
  os << (lit.neg() ? "!x" : "x") << lit.var();
  return os;
}

std::ostream &operator<<(std::ostream &os, const Clause &clause) {
  std::for_each(clause.begin(), clause.end(),
                [&](Lit lit) { os << lit << " "; });
  return os;
}

class Solver {
public:
  explicit Solver(size_t variable_num) : que_head(0) {
    assings.resize(variable_num);
    watchers.resize(2 * variable_num);
    reasons.resize(variable_num);
    levels.resize(variable_num);
    que.clear();
  }
  [[nodiscard]] LitBool eval(Lit lit) const {
    if (!levels[lit.vidx()].has_value()) {
      return LitBool::Undefine;
    }
    if (lit.neg()) {
      return assings[lit.vidx()] ? LitBool::False : LitBool::True;
    }
    return assings[lit.vidx()] ? LitBool::True : LitBool::False;
  }
  [[nodiscard]] int decision_level() const {
    if (que.empty()) {
      return 0;
    }

    Lit l = que.back();
    return levels[l.vidx()].value_or(0);
  }
  void new_decision(Lit lit, std::optional<CRef> reason = std::nullopt) {
    enqueue(lit, reason);
    levels[lit.vidx()].value()++;
  }

  void enqueue(Lit lit, std::optional<CRef> reason = std::nullopt) {
    assert(!levels[lit.vidx()].has_value());
    levels[lit.vidx()] = decision_level();
    assings[lit.vidx()] = lit.pos() ? true : false;
    reasons[lit.vidx()] = reason;
    que.push_back(lit);
  }

  void pop_queue_until(int until_level) {
    assert(!que.empty());

    while (!que.empty()) {
      Lit lit = que.back();
      if (levels[lit.vidx()] > until_level) {
        reasons[lit.vidx()] = std::nullopt;
        levels[lit.vidx()] = std::nullopt;
        que.pop_back();
      } else {
        break;
      }
    }
    if (que.size() > 0) {
      que_head = que.size() - 1;
    } else {
      que_head = 0;
    }
  }

  void new_var() {
    // literal index
    watchers.push_back(std::vector<CRef>());
    watchers.push_back(std::vector<CRef>());
    // variable index
    assings.push_back(false);
    reasons.push_back(std::nullopt);
    levels.push_back(std::nullopt);
  }
  void attach_clause(const CRef &cr, bool learnt = false) {
    const Clause &clause = *cr;
    assert(clause.size() > 1);
    watchers[(~clause[0]).lidx()].push_back(cr);
    watchers[(~clause[1]).lidx()].push_back(cr);
    clauses.push_back(cr);
    if (learnt) {
      learnts.push_back(cr);
    }
  }
  void add_clause(const Clause &clause) {
    // grow the size
    std::for_each(clause.begin(), clause.end(), [&](Lit lit) {
      if (lit.vidx() >= assings.size()) {
        new_var();
      }
    });

    if (clause.size() == 1) {
      // Unit Clause
      enqueue(clause[0]);
    } else {
      CRef cr = std::make_shared<Clause>(clause);
      attach_clause(cr);
    }
  }
  [[nodiscard]] std::optional<CRef> propagate() {
    while (que_head < que.size()) {
      assert(que_head >= 0);
      const Lit lit = que[que_head++];
      const Lit nlit = ~lit;

      std::vector<CRef> &watcher = watchers[lit.lidx()];
      for (size_t i = 0; i < watcher.size();) {
        CRef cr = watcher[i];
        const size_t next_idx = i + 1;
        Clause &clause = *cr;

        assert(clause[0] == nlit || clause[1] == nlit);
        // make sure that the clause[1] it false.
        if (clause[0] == nlit) {
          std::swap(clause[0], clause[1]);
        }
        assert(clause[1] == nlit && eval(clause[1]) == LitBool::False);

        Lit first = clause[0];
        // Already satisfied
        if (eval(first) == LitBool::True) {
          i = next_idx;
          goto nextclause;
        }
        // clause[0] is False or Undefine
        // clause[1] is False
        // clause[2..] is False or True or Undefine.

        for (size_t k = 2; k < clause.size(); k++) {
          // Found a new lit to watch
          if (eval(clause[k]) != LitBool::False) {
            std::swap(clause[1], clause[k]);
            // Remove a value(swap the last one and pop back)
            watcher[i] = watcher.back();
            watcher.pop_back();
            // New watch
            watchers[(~clause[1]).lidx()].push_back(cr);
            goto nextclause;
          }
        }

        // clause[2..] is False
        if (eval(first) == LitBool::False) {
          // All literals are false
          // Conflict
          que_head = que.size();
          return std::move(cr);
        } else {
          // All literals excepting first are false
          // Unit Propagation
          assert(eval(first) == LitBool::Undefine);
          enqueue(first, cr);
          i = next_idx;
        }
      nextclause:;
      }
    }

    return std::nullopt;
  }

  [[nodiscard]] std::pair<Clause, int> analyze(CRef conflict) {
    Clause learnt_clause;

    const int conflicted_decision_level = decision_level();
    std::unordered_set<Var> checking_variables;
    int counter = 0;
    {
      const Clause clause = *conflict;

      // variables that are used to traverse by a conflicted clause
      for (const Lit lit : clause) {
        assert(eval(lit) == LitBool::False);
        checking_variables.insert(lit.var());
        if (levels[lit.vidx()] < conflicted_decision_level) {
          learnt_clause.emplace_back(lit);
        } else {
          counter += 1;
        }
      }
      assert(counter >= 1);
    }

    // traverse a implication graph to a 1-UIP(first-uinque-implication-point)
    std::optional<Lit> first_uip = std::nullopt;
    for (size_t i = que.size() - 1; true; i--) {
      Lit lit = que[i];
      // Skip a variable that isn't checked.
      if (checking_variables.count(lit.var()) == 0) {
        continue;
      }
      counter--;
      if (counter <= 0) {
        // 1-UIP
        first_uip = lit;
        break;
      }
      checking_variables.insert(lit.var());
      assert(reasons[lit.vidx()].has_value());
      CRef reason = reasons[lit.vidx()].value();
      const Clause clause = *reason;
      assert(clause[0] == lit);
      for (size_t j = 1; j < clause.size(); j++) {
        Lit clit = clause[j];
        // Already checked
        if (checking_variables.count(clit.var()) > 0) {
          continue;
        }
        checking_variables.insert(clit.var());
        if (levels[clit.vidx()] < conflicted_decision_level) {
          learnt_clause.push_back(clit);
        } else {
          counter += 1;
        }
      }
    }
    assert(first_uip.has_value());
    // learnt_clause[0] = !first_uip
    learnt_clause.push_back(~(first_uip.value()));
    std::swap(learnt_clause[0], learnt_clause.back());

    // Back Jump
    int back_jump_level = 0;
    for (size_t i = 1; i < learnt_clause.size(); i++) {
      assert(levels[learnt_clause[i].vidx()].has_value());
      back_jump_level =
          std::max(back_jump_level, levels[learnt_clause[i].vidx()].value());
    }

    return std::make_pair(learnt_clause, back_jump_level);
  }

  Status solve() {
    while (true) {
      if (std::optional<CRef> conflict = propagate()) {
        // Conflict
        if (decision_level() == 0) {
          return Status::Unsat;
        }
        auto [learnt_clause, back_jump_level] = analyze(conflict.value());
        pop_queue_until(back_jump_level);
        if (learnt_clause.size() == 1) {
          enqueue(learnt_clause[0]);
        } else {
          auto cr = std::make_shared<Clause>(learnt_clause);
          attach_clause(cr, true);
          enqueue(learnt_clause[0], cr);
        }

      } else {
        // No Conflict
        std::optional<Lit> next = std::nullopt;
        for (size_t v = 0; v < assings.size(); v++) {
          if (!levels[v].has_value()) {
            // undefine
            next = Lit(static_cast<Var>(v), assings[v]);
            break;
          }
        }
        if (next) {
          new_decision(next.value());
        } else {
          return Status::Sat;
        }
      }
    }
    return Status::Unknown;
  }
  // All variables
public:
  std::vector<bool> assings;
  std::optional<Status> status;

private:
  std::vector<CRef> clauses, learnts;
  std::vector<std::vector<CRef>> watchers;
  std::vector<std::optional<CRef>> reasons;
  std::vector<std::optional<int>> levels;
  std::deque<Lit> que;
  size_t que_head;
};
} // namespace bullsat

#endif // BULLSAT_HPP_
