#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <queue>
#include <unordered_map>
#include <utility>
#include <vector>

#include "solver.hpp"

struct search_progress_t {
  uint8_t rounds;
  uint16_t checks;
  uint64_t nodes;
  size_t memoStates;
};

using search_progress_callback_t =
    std::function<void(const search_progress_t &)>;

enum struct search_status_t : uint8_t {
  no_worlds = 0,
  solved = 1,
  recommendation = 2,
};

struct round_context_t {
  bool active = false;
  uint8_t codeIdx = 0;
  uint8_t checkedVerifierMask = 0;
};

struct search_result_t {
  search_status_t status = search_status_t::no_worlds;
  code_t code{0, 0, 0};
  bool solutionKnown = false;
  code_t solution{0, 0, 0};
  uint8_t verifierIdx = 0;
  bool startsNewRound = false;
  uint8_t worstCaseRounds = 0;
  uint16_t worstCaseChecks = 0;
  size_t liveWorlds = 0;
  size_t liveCodes = 0;
  size_t greenWorlds = 0;
  size_t redWorlds = 0;
  size_t greenCodes = 0;
  size_t redCodes = 0;
  bool answerMatchesLiveWorld = false;
  bool answerResultKnown = false;
  bool answerResult = false;
  bool expectedValueOptimized = false;
  double expectedRounds = 0;
  double expectedChecks = 0;
  uint64_t nodes = 0;
  size_t memoStates = 0;
};

struct classic_world_t {
  std::vector<verifier_t> verifiers;
  uint8_t solutionIdx;
};

const auto get_classic_worlds = [](const std::vector<card_t> &game,
                                   const std::vector<query_t> &queries) {
  auto worlds = std::vector<classic_world_t>{};
  cartesianProduct(game, [&worlds, &queries](
                             const std::vector<verifier_t> &combination) {
    if (!has_single_solution(combination) ||
        has_redundant_information(combination)) {
      return;
    }

    for (const auto &query : queries) {
      const auto verifierIdx = static_cast<size_t>(query.verifierIdx - 'A');
      if (verifierIdx >= combination.size() ||
          combination[verifierIdx].isValid(query.code) != query.result) {
        return;
      }
    }

    worlds.push_back(classic_world_t{
        combination,
        static_cast<uint8_t>(find_solution_idx(get_solution(combination))),
    });
  });
  return worlds;
};

namespace search_detail {

using state_t = std::vector<uint64_t>;

struct node_key_t {
  state_t worlds;
  bool active;
  uint8_t codeIdx;
  uint8_t checkedVerifierMask;

  bool operator==(const node_key_t &other) const {
    return worlds == other.worlds && active == other.active &&
           codeIdx == other.codeIdx &&
           checkedVerifierMask == other.checkedVerifierMask;
  }
};

struct node_hash_t {
  size_t operator()(const node_key_t &node) const {
    size_t seed = node.worlds.size();
    for (const auto word : node.worlds) {
      seed ^= std::hash<uint64_t>{}(word) + 0x9e3779b9 + (seed << 6) +
              (seed >> 2);
    }
    seed ^= static_cast<size_t>(node.active) << 1;
    seed ^= static_cast<size_t>(node.codeIdx) << 8;
    seed ^= static_cast<size_t>(node.checkedVerifierMask) << 16;
    return seed;
  }
};

struct state_hash_t {
  size_t operator()(const state_t &state) const {
    size_t seed = state.size();
    for (const auto word : state) {
      seed ^= std::hash<uint64_t>{}(word) + 0x9e3779b9 + (seed << 6) +
              (seed >> 2);
    }
    return seed;
  }
};

struct action_t {
  uint8_t codeIdx;
  uint8_t verifierIdx;
  bool startsNewRound;
};

struct candidate_t {
  action_t action;
  state_t green;
  state_t red;
  size_t greenWorlds;
  size_t redWorlds;
  size_t greenCodes;
  size_t redCodes;
};

struct memo_result_t {
  bool solvable = false;
  action_t action{0, 0, false};
};

struct expected_result_t {
  bool solvable = false;
  action_t action{0, 0, false};
  uint64_t totalRounds = 0;
  uint64_t totalChecks = 0;
};

class classic_search_t {
public:
  classic_search_t(const std::vector<classic_world_t> &worlds,
                   size_t numVerifiers, round_context_t initialRound,
                   search_progress_callback_t progressCallback,
                   std::optional<uint8_t> answerIdx,
                   bool optimizeExpectedValue,
                   std::optional<std::vector<verifier_t>> simulationVerifiers)
      : worlds_(worlds), numVerifiers_(numVerifiers),
        initialRound_(initialRound),
        progressCallback_(std::move(progressCallback)),
        answerIdx_(answerIdx),
        optimizeExpectedValue_(optimizeExpectedValue),
        simulationVerifiers_(std::move(simulationVerifiers)),
        wordCount_((worlds.size() + 63) / 64) {
    atomicGreenMasks_.resize(NUM_CODES * numVerifiers_,
                             state_t(wordCount_, 0));
    for (uint8_t codeIdx = 0; codeIdx < NUM_CODES; codeIdx += 1) {
      for (uint8_t verifierIdx = 0; verifierIdx < numVerifiers_;
           verifierIdx += 1) {
        auto &greenMask =
            atomicGreenMasks_[codeIdx * numVerifiers_ + verifierIdx];
        for (size_t worldIdx = 0; worldIdx < worlds_.size();
             worldIdx += 1) {
          if (worlds_[worldIdx].verifiers[verifierIdx].isValid(
                  code_map[codeIdx])) {
            greenMask[worldIdx / 64] |= uint64_t{1} << (worldIdx % 64);
          }
        }
      }
    }
  }

  search_result_t run() {
    auto result = search_result_t{};
    result.liveWorlds = worlds_.size();
    if (worlds_.empty()) {
      return result;
    }

    auto root = state_t(wordCount_, std::numeric_limits<uint64_t>::max());
    if (worlds_.size() % 64 != 0) {
      root.back() = (uint64_t{1} << (worlds_.size() % 64)) - 1;
    }
    result.liveCodes = countCodes(root);
    if (answerIdx_) {
      result.solutionKnown = true;
      result.solution = code_map[*answerIdx_];
      result.answerMatchesLiveWorld = std::any_of(
          worlds_.begin(), worlds_.end(), [this](const auto &world) {
            if (world.solutionIdx != *answerIdx_) {
              return false;
            }
            if (!simulationVerifiers_) {
              return true;
            }
            for (size_t i = 0; i < world.verifiers.size(); i += 1) {
              if (world.verifiers[i].name != (*simulationVerifiers_)[i].name) {
                return false;
              }
            }
            return true;
          });
      if (!result.answerMatchesLiveWorld) {
        return result;
      }
    }
    if (result.liveCodes <= 1) {
      result.status = search_status_t::solved;
      result.code = code_map[worlds_.front().solutionIdx];
      result.expectedValueOptimized = optimizeExpectedValue_;
      return result;
    }

    targetRounds_ = minimumRounds(result.liveCodes, initialRound_);
    targetChecks_ = 0;
    reportProgress();
    const auto optimalRoundCount = findOptimalRounds(root, initialRound_);
    if (optimalRoundCount == noRoundSolution) {
      result.nodes = nodes_;
      result.memoStates = memoStateCount();
      return result;
    }

    targetRounds_ = optimalRoundCount;
    const auto maxChecks =
        remainingChecks(initialRound_) + optimalRoundCount * 3;
    auto plan = memo_result_t{};
    const auto minimumScoreChecks =
        static_cast<uint16_t>(minimumChecks(result.liveCodes));
    for (uint16_t checks = minimumScoreChecks; checks <= maxChecks;
         checks += 1) {
      targetChecks_ = checks;
      reportProgress();
      plan = canSolveWithinBudgets(root, initialRound_, optimalRoundCount,
                                   checks);
      if (plan.solvable) {
        break;
      }
    }
    if (!plan.solvable) {
      result.nodes = nodes_;
      result.memoStates = memoStateCount();
      return result;
    }
    if (optimizeExpectedValue_) {
      const auto expectedPlan = optimizeExpectedValueWithinBudgets(
          root, initialRound_, optimalRoundCount, targetChecks_);
      if (expectedPlan.solvable) {
        plan.action = expectedPlan.action;
        result.expectedValueOptimized = true;
        result.expectedRounds = static_cast<double>(expectedPlan.totalRounds) /
                                static_cast<double>(result.liveWorlds);
        result.expectedChecks = static_cast<double>(expectedPlan.totalChecks) /
                                static_cast<double>(result.liveWorlds);
      }
    }
    const auto split = makeCandidate(root, plan.action);
    result.status = search_status_t::recommendation;
    result.code = code_map[plan.action.codeIdx];
    result.verifierIdx = plan.action.verifierIdx;
    result.startsNewRound = plan.action.startsNewRound;
    result.worstCaseRounds = optimalRoundCount;
    result.worstCaseChecks = targetChecks_;
    result.greenWorlds = split.greenWorlds;
    result.redWorlds = split.redWorlds;
    result.greenCodes = split.greenCodes;
    result.redCodes = split.redCodes;
    setAnswerResult(result, plan.action);
    result.nodes = nodes_;
    result.memoStates = memoStateCount();
    reportProgress();
    return result;
  }

private:
  static constexpr uint8_t noRoundSolution =
      std::numeric_limits<uint8_t>::max();
  static uint8_t minimumChecks(size_t possibilities) {
    uint8_t checks = 0;
    size_t outcomes = 1;
    while (outcomes < possibilities) {
      outcomes *= 2;
      checks += 1;
    }
    return checks;
  }

  static uint8_t usedChecks(const round_context_t &round) {
    return round.active
               ? static_cast<uint8_t>(
                     __builtin_popcount(round.checkedVerifierMask))
               : 0;
  }

  static uint8_t remainingChecks(const round_context_t &round) {
    const auto used = usedChecks(round);
    return round.active && used < 3 ? static_cast<uint8_t>(3 - used) : 0;
  }

  static uint8_t minimumRounds(size_t possibilities,
                               const round_context_t &round) {
    const auto checksNeeded = minimumChecks(possibilities);
    const auto freeChecks = remainingChecks(round);
    if (checksNeeded <= freeChecks) {
      return 0;
    }
    return static_cast<uint8_t>((checksNeeded - freeChecks + 2) / 3);
  }

  size_t countWorlds(const state_t &state) const {
    size_t count = 0;
    for (const auto word : state) {
      count += static_cast<size_t>(__builtin_popcountll(word));
    }
    return count;
  }

  size_t countCodes(const state_t &state) const {
    auto seen = std::array<bool, NUM_CODES>{};
    size_t count = 0;
    for (size_t worldIdx = 0; worldIdx < worlds_.size(); worldIdx += 1) {
      if ((state[worldIdx / 64] &
           (uint64_t{1} << (worldIdx % 64))) == 0) {
        continue;
      }
      const auto solutionIdx = worlds_[worldIdx].solutionIdx;
      if (!seen[solutionIdx]) {
        seen[solutionIdx] = true;
        count += 1;
      }
    }
    return count;
  }

  uint64_t minimumExpectedChecks(const state_t &state) {
    const auto memoized = expectedCheckLowerBoundMemo_.find(state);
    if (memoized != expectedCheckLowerBoundMemo_.end()) {
      return memoized->second;
    }

    auto weights = std::array<size_t, NUM_CODES>{};
    for (size_t worldIdx = 0; worldIdx < worlds_.size(); worldIdx += 1) {
      if ((state[worldIdx / 64] &
           (uint64_t{1} << (worldIdx % 64))) != 0) {
        weights[worlds_[worldIdx].solutionIdx] += 1;
      }
    }
    auto queue = std::priority_queue<size_t, std::vector<size_t>,
                                     std::greater<size_t>>{};
    for (const auto weight : weights) {
      if (weight > 0) {
        queue.push(weight);
      }
    }

    uint64_t total = 0;
    while (queue.size() > 1) {
      const auto first = queue.top();
      queue.pop();
      const auto second = queue.top();
      queue.pop();
      const auto combined = first + second;
      total += combined;
      queue.push(combined);
    }
    expectedCheckLowerBoundMemo_[state] = total;
    return total;
  }

  candidate_t makeCandidate(const state_t &state,
                            const action_t &action) const {
    const auto &greenMask =
        atomicGreenMasks_[action.codeIdx * numVerifiers_ +
                          action.verifierIdx];
    auto green = state_t(wordCount_, 0);
    auto red = state_t(wordCount_, 0);
    for (size_t wordIdx = 0; wordIdx < wordCount_; wordIdx += 1) {
      green[wordIdx] = state[wordIdx] & greenMask[wordIdx];
      red[wordIdx] = state[wordIdx] & ~greenMask[wordIdx];
    }
    return candidate_t{action,
                       green,
                       red,
                       countWorlds(green),
                       countWorlds(red),
                       countCodes(green),
                       countCodes(red)};
  }

  void setAnswerResult(search_result_t &result, const action_t &action) const {
    if (!answerIdx_) {
      return;
    }

    if (simulationVerifiers_) {
      result.answerResultKnown = true;
      result.answerResult =
          (*simulationVerifiers_)[action.verifierIdx].isValid(
              code_map[action.codeIdx]);
      return;
    }

    auto seenResult = std::optional<bool>{};
    for (const auto &world : worlds_) {
      if (world.solutionIdx != *answerIdx_) {
        continue;
      }
      const auto currentResult = world.verifiers[action.verifierIdx].isValid(
          code_map[action.codeIdx]);
      if (seenResult && *seenResult != currentResult) {
        return;
      }
      seenResult = currentResult;
    }
    if (seenResult) {
      result.answerResultKnown = true;
      result.answerResult = *seenResult;
    }
  }

  std::vector<candidate_t> getCandidates(
      const state_t &state, const round_context_t &round,
      uint8_t rounds) const {
    auto candidates = std::vector<candidate_t>{};
    candidates.reserve(NUM_CODES * numVerifiers_ + numVerifiers_);

    if (remainingChecks(round) > 0) {
      for (uint8_t verifierIdx = 0; verifierIdx < numVerifiers_;
           verifierIdx += 1) {
        if ((round.checkedVerifierMask &
             (uint8_t{1} << verifierIdx)) == 0) {
          auto candidate = makeCandidate(
              state, action_t{round.codeIdx, verifierIdx, false});
          if (candidate.greenWorlds > 0 && candidate.redWorlds > 0) {
            candidates.push_back(std::move(candidate));
          }
        }
      }
    }

    if (rounds > 0) {
      for (uint8_t codeIdx = 0; codeIdx < NUM_CODES; codeIdx += 1) {
        for (uint8_t verifierIdx = 0; verifierIdx < numVerifiers_;
             verifierIdx += 1) {
          auto candidate = makeCandidate(
              state, action_t{codeIdx, verifierIdx, true});
          if (candidate.greenWorlds > 0 && candidate.redWorlds > 0) {
            candidates.push_back(std::move(candidate));
          }
        }
      }
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const candidate_t &left, const candidate_t &right) {
      if (left.action.startsNewRound != right.action.startsNewRound) {
        return !left.action.startsNewRound;
      }
      const auto leftCodeScore =
          std::max(left.greenCodes, left.redCodes);
      const auto rightCodeScore =
          std::max(right.greenCodes, right.redCodes);
      if (leftCodeScore != rightCodeScore) {
        return leftCodeScore < rightCodeScore;
      }
      const auto leftWorldScore =
          std::max(left.greenWorlds, left.redWorlds);
      const auto rightWorldScore =
          std::max(right.greenWorlds, right.redWorlds);
      return leftWorldScore < rightWorldScore;
    });
    return candidates;
  }

  round_context_t nextRound(const round_context_t &round,
                            const action_t &action) const {
    if (action.startsNewRound) {
      return round_context_t{
          true, action.codeIdx,
          static_cast<uint8_t>(uint8_t{1} << action.verifierIdx)};
    }
    return round_context_t{
        true, round.codeIdx,
        static_cast<uint8_t>(round.checkedVerifierMask |
                             (uint8_t{1} << action.verifierIdx))};
  }

  node_key_t makeNode(const state_t &state,
                      const round_context_t &round) const {
    return node_key_t{
        state, round.active, round.codeIdx, round.checkedVerifierMask};
  }

  bool canSolveInRounds(const state_t &state,
                        const round_context_t &round, uint8_t rounds) {
    nodes_ += 1;
    maybeReportProgress();

    const auto codeCount = countCodes(state);
    if (codeCount <= 1) {
      return true;
    }
    if (minimumRounds(codeCount, round) > rounds) {
      return false;
    }

    auto &memo = roundFeasibilityMemo_[rounds];
    const auto node = makeNode(state, round);
    const auto memoized = memo.find(node);
    if (memoized != memo.end()) {
      return memoized->second;
    }

    for (const auto &candidate : getCandidates(state, round, rounds)) {
      const auto roundCost = candidate.action.startsNewRound ? 1 : 0;
      if (roundCost > rounds) {
        continue;
      }
      const auto childRounds = static_cast<uint8_t>(rounds - roundCost);
      const auto childRound = nextRound(round, candidate.action);

      const state_t *first = &candidate.green;
      const state_t *second = &candidate.red;
      if (std::make_pair(candidate.redCodes, candidate.redWorlds) >
          std::make_pair(candidate.greenCodes, candidate.greenWorlds)) {
        std::swap(first, second);
      }

      if (canSolveInRounds(*first, childRound, childRounds) &&
          canSolveInRounds(*second, childRound, childRounds)) {
        memo[node] = true;
        return true;
      }
    }

    memo[node] = false;
    return false;
  }

  uint8_t findOptimalRounds(const state_t &state,
                            const round_context_t &round) {
    if (countCodes(state) <= 1) {
      return 0;
    }
    const auto node = makeNode(state, round);
    const auto memoized = optimalRoundMemo_.find(node);
    if (memoized != optimalRoundMemo_.end()) {
      return memoized->second;
    }

    const auto lowerBound = minimumRounds(countCodes(state), round);
    for (size_t rounds = lowerBound; rounds < worlds_.size(); rounds += 1) {
      targetRounds_ = static_cast<uint8_t>(rounds);
      if (canSolveInRounds(state, round, static_cast<uint8_t>(rounds))) {
        optimalRoundMemo_[node] = static_cast<uint8_t>(rounds);
        return static_cast<uint8_t>(rounds);
      }
    }
    optimalRoundMemo_[node] = noRoundSolution;
    return noRoundSolution;
  }

  memo_result_t canSolveWithinBudgets(const state_t &state,
                                      const round_context_t &round,
                                      uint8_t rounds, uint16_t checks) {
    nodes_ += 1;
    maybeReportProgress();

    if (countCodes(state) <= 1) {
      return memo_result_t{true, action_t{0, 0, false}};
    }
    const auto codeCount = countCodes(state);
    if (minimumRounds(codeCount, round) > rounds ||
        minimumChecks(codeCount) > checks) {
      return memo_result_t{};
    }

    const auto node = makeNode(state, round);
    const auto limitKey =
        (static_cast<uint32_t>(rounds) << 16) |
        static_cast<uint32_t>(checks);
    auto &memo = scoreFeasibilityMemo_[limitKey];
    const auto memoized = memo.find(node);
    if (memoized != memo.end()) {
      return memoized->second;
    }

    for (const auto &candidate : getCandidates(state, round, rounds)) {
      const auto roundCost = candidate.action.startsNewRound ? 1 : 0;
      if (roundCost > rounds) {
        continue;
      }
      const auto childRound = nextRound(round, candidate.action);
      const auto childRounds = static_cast<uint8_t>(rounds - roundCost);
      const auto childChecks = static_cast<uint16_t>(checks - 1);

      const state_t *first = &candidate.green;
      const state_t *second = &candidate.red;
      if (std::make_pair(candidate.redCodes, candidate.redWorlds) >
          std::make_pair(candidate.greenCodes, candidate.greenWorlds)) {
        std::swap(first, second);
      }

      if (canSolveWithinBudgets(*first, childRound, childRounds,
                                childChecks)
              .solvable &&
          canSolveWithinBudgets(*second, childRound, childRounds,
                                childChecks)
              .solvable) {
        const auto result = memo_result_t{true, candidate.action};
        memo[node] = result;
        return result;
      }
    }

    const auto result = memo_result_t{};
    memo[node] = result;
    return result;
  }

  expected_result_t optimizeExpectedValueWithinBudgets(
      const state_t &state, const round_context_t &round, uint8_t rounds,
      uint16_t checks) {
    nodes_ += 1;
    maybeReportProgress();

    if (countCodes(state) <= 1) {
      return expected_result_t{true};
    }
    const auto codeCount = countCodes(state);
    if (minimumRounds(codeCount, round) > rounds ||
        minimumChecks(codeCount) > checks) {
      return expected_result_t{};
    }

    const auto node = makeNode(state, round);
    const auto limitKey =
        (static_cast<uint32_t>(rounds) << 16) |
        static_cast<uint32_t>(checks);
    auto &memo = expectedValueMemo_[limitKey];
    const auto memoized = memo.find(node);
    if (memoized != memo.end()) {
      return memoized->second;
    }

    auto best = expected_result_t{};
    const auto worldCount = countWorlds(state);
    for (const auto &candidate : getCandidates(state, round, rounds)) {
      const auto roundCost = candidate.action.startsNewRound ? 1 : 0;
      if (roundCost > rounds) {
        continue;
      }
      const auto childRound = nextRound(round, candidate.action);
      const auto childRounds = static_cast<uint8_t>(rounds - roundCost);
      const auto childChecks = static_cast<uint16_t>(checks - 1);

      if (!canSolveWithinBudgets(candidate.green, childRound, childRounds,
                                 childChecks)
               .solvable ||
          !canSolveWithinBudgets(candidate.red, childRound, childRounds,
                                 childChecks)
               .solvable) {
        continue;
      }

      const auto checkLowerBound =
          worldCount + minimumExpectedChecks(candidate.green) +
          minimumExpectedChecks(candidate.red);
      if (best.solvable && checkLowerBound > best.totalChecks) {
        continue;
      }

      const auto green = optimizeExpectedValueWithinBudgets(
          candidate.green, childRound, childRounds, childChecks);
      const auto red = optimizeExpectedValueWithinBudgets(
          candidate.red, childRound, childRounds, childChecks);
      if (!green.solvable || !red.solvable) {
        continue;
      }

      const auto current = expected_result_t{
          true,
          candidate.action,
          green.totalRounds + red.totalRounds + roundCost * worldCount,
          green.totalChecks + red.totalChecks + worldCount,
      };
      if (!best.solvable ||
          std::make_pair(current.totalChecks, current.totalRounds) <
              std::make_pair(best.totalChecks, best.totalRounds)) {
        best = current;
      }
    }

    memo[node] = best;
    return best;
  }

  size_t memoStateCount() const {
    size_t count = 0;
    for (const auto &entry : roundFeasibilityMemo_) {
      count += entry.second.size();
    }
    for (const auto &entry : scoreFeasibilityMemo_) {
      count += entry.second.size();
    }
    for (const auto &entry : expectedValueMemo_) {
      count += entry.second.size();
    }
    return count + optimalRoundMemo_.size();
  }

  void maybeReportProgress() {
    if (!progressCallback_ || nodes_ % 50000 != 0) {
      return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - lastProgress_ >= std::chrono::milliseconds(150)) {
      reportProgress();
      lastProgress_ = now;
    }
  }

  void reportProgress() const {
    if (progressCallback_) {
      progressCallback_(search_progress_t{
          targetRounds_, targetChecks_, nodes_, memoStateCount()});
    }
  }

  const std::vector<classic_world_t> &worlds_;
  size_t numVerifiers_;
  round_context_t initialRound_;
  search_progress_callback_t progressCallback_;
  std::optional<uint8_t> answerIdx_;
  bool optimizeExpectedValue_;
  std::optional<std::vector<verifier_t>> simulationVerifiers_;
  size_t wordCount_;
  std::vector<state_t> atomicGreenMasks_;
  std::unordered_map<
      uint8_t, std::unordered_map<node_key_t, bool, node_hash_t>>
      roundFeasibilityMemo_;
  std::unordered_map<node_key_t, uint8_t, node_hash_t> optimalRoundMemo_;
  std::unordered_map<
      uint32_t,
      std::unordered_map<node_key_t, memo_result_t, node_hash_t>>
      scoreFeasibilityMemo_;
  std::unordered_map<
      uint32_t,
      std::unordered_map<node_key_t, expected_result_t, node_hash_t>>
      expectedValueMemo_;
  std::unordered_map<state_t, uint64_t, state_hash_t>
      expectedCheckLowerBoundMemo_;
  uint64_t nodes_ = 0;
  uint8_t targetRounds_ = 0;
  uint16_t targetChecks_ = 0;
  std::chrono::steady_clock::time_point lastProgress_ =
      std::chrono::steady_clock::now();
};

} // namespace search_detail

const auto search_classic = [](const std::vector<card_t> &game,
                               const std::vector<query_t> &queries,
                               round_context_t initialRound = {},
                               search_progress_callback_t progressCallback =
                                   search_progress_callback_t{},
                               std::optional<code_t> answer = std::nullopt,
                               bool optimizeExpectedValue = false,
                               std::optional<std::vector<uint8_t>>
                                   simulationCriteriaIndices = std::nullopt) {
  const auto worlds = get_classic_worlds(game, queries);
  auto answerIdx = std::optional<uint8_t>{};
  if (answer) {
    answerIdx = static_cast<uint8_t>(
        ((*answer)[0] - MIN_NUMBER) * 25 +
        ((*answer)[1] - MIN_NUMBER) * 5 + ((*answer)[2] - MIN_NUMBER));
  }
  auto simulationVerifiers = std::optional<std::vector<verifier_t>>{};
  if (simulationCriteriaIndices &&
      simulationCriteriaIndices->size() == game.size()) {
    auto verifiers = std::vector<verifier_t>{};
    verifiers.reserve(game.size());
    for (size_t i = 0; i < game.size(); i += 1) {
      const auto criteriaIdx = (*simulationCriteriaIndices)[i];
      if (criteriaIdx >= game[i].size()) {
        verifiers.clear();
        break;
      }
      verifiers.push_back(game[i][criteriaIdx]);
    }
    if (verifiers.size() == game.size()) {
      simulationVerifiers = std::move(verifiers);
    }
  }
  if (!answerIdx && simulationVerifiers) {
    const auto solution = get_solution(*simulationVerifiers);
    if (solution.count() == 1) {
      answerIdx = static_cast<uint8_t>(find_solution_idx(solution));
    }
  }
  auto search = search_detail::classic_search_t{
      worlds, game.size(), initialRound, std::move(progressCallback),
      answerIdx, optimizeExpectedValue, std::move(simulationVerifiers)};
  return search.run();
};
