#include "cards.hpp"
#include "code.hpp"
#include "json.hpp"
#include "search.hpp"
#include "solver.hpp"
#include <cstdint>
#include <emscripten/emscripten.h>

using json = nlohmann::json;

EM_JS(void, emit_search_progress,
      (uint32_t searchId, uint8_t rounds, uint16_t checks, double nodes,
       uint32_t memoStates), {
        self.postMessage({
          type: "search_progress",
          id: searchId,
          rounds: rounds,
          checks: checks,
          nodes: nodes,
          memoStates: memoStates,
        });
      });

int wasm_json(char *input, char *output, const std::function<json(json)> &foo) {
  json data = json::parse(input);

  const auto outputJson = foo(data);

  const auto result = outputJson.dump();
  strcpy(output, result.c_str());
  return result.size();
}

extern "C" EMSCRIPTEN_KEEPALIVE void solve_wasm(uint8_t *input,
                                                uint8_t *output) {
  // input parsing
  size_t offset = 0;
  auto mode = game_mode_t{input[offset]};
  offset += 1;
  auto numVerifiers = input[offset];
  offset += 1;
  auto cards = std::vector<card_t>{numVerifiers};
  for (uint8_t i = 0; i < numVerifiers; i += 1) {
    cards[i] = all_cards[input[offset]];
    offset += 1;
  }
  if (mode == game_mode_t::extreme) {
    // copy all verifiers from the other card to the verifier slot
    for (uint8_t i = 0; i < numVerifiers; i += 1) {
      const auto otherCard = all_cards[input[offset]];
      for (const auto verifier : otherCard) {
        cards[i].push_back(verifier);
      }
      offset += 1;
    }
  }

  auto numQueries = input[offset];
  offset += 1;

  auto queries = std::vector<query_t>{numQueries};
  for (size_t i = 0; i < numQueries; i += 1) {
    queries[i] = query_t{{input[offset], input[offset + 1], input[offset + 2]},
                         (char)input[offset + 3],
                         (bool)input[offset + 4]};
    offset += 5;
  }

  auto result = solve(cards, queries, mode);
  offset = 0;
  // transform result

  // 1. N_CODES,[codes]
  output[offset] = (uint8_t)result.possibleCodes.size();
  offset += 1;
  for (const auto &code : result.possibleCodes) {
    for (const auto &c : code) {
      output[offset] = c - '0';
      offset += 1;
    }
  }
  // 2. [(N_CARDS, [cards])]
  for (const auto &card : result.possibleVerifiers) {
    output[offset] = (uint8_t)card.size();
    offset += 1;
    for (const auto &verifierIdx : card) {
      output[offset] = verifierIdx;
      offset += 1;
    }
  }
  // 3. [(N_LETTERS, [letters])]
  for (const auto &possibleMatch : result.possibleMatches) {
    output[offset] = (uint8_t)possibleMatch.size();
    offset += 1;
    for (auto letter : possibleMatch) {
      output[offset] = letter;
      offset += 1;
    }
  }
}

extern "C" EMSCRIPTEN_KEEPALIVE int get_possible_codes(char *input,
                                                       char *output) {
  return wasm_json(input, output, [](const json &data) {
    std::vector<std::vector<uint8_t>> slots = data["cards"];
    std::vector<std::vector<uint8_t>> possibleVerifiers =
        data["possibleVerifiers"];

    const auto codes =
        possible_codes_from_possible_verifiers(slots, possibleVerifiers);

    auto outputJson = json();
    outputJson["codes"] = codes;

    return outputJson;
  });
}

extern "C" EMSCRIPTEN_KEEPALIVE int search_classic_wasm(char *input,
                                                         char *output) {
  return wasm_json(input, output, [](const json &data) {
    const auto cardIds = data["cards"].get<std::vector<uint8_t>>();
    auto game = std::vector<card_t>{};
    for (const auto cardId : cardIds) {
      game.push_back(all_cards[cardId]);
    }

    auto queries = std::vector<query_t>{};
    for (const auto &query : data["queries"]) {
      queries.push_back(query_t{
          query["code"].get<code_t>(),
          static_cast<char>(query["verifierIdx"].get<uint8_t>()),
          query["result"].get<bool>(),
      });
    }

    const auto searchId = data["id"].get<uint32_t>();
    auto answer = std::optional<code_t>{};
    if (data.contains("answer") && !data["answer"].is_null()) {
      answer = data["answer"].get<code_t>();
    }
    const auto optimizeExpectedValue =
        data.value("optimizeExpectedValue", false);
    auto simulationCriteriaIndices =
        std::optional<std::vector<uint8_t>>{};
    if (data.contains("simulationCriteriaIndices") &&
        !data["simulationCriteriaIndices"].is_null()) {
      simulationCriteriaIndices =
          data["simulationCriteriaIndices"].get<std::vector<uint8_t>>();
    }
    auto initialRound = round_context_t{};
    if (!data["currentRound"].is_null()) {
      const auto code = data["currentRound"]["code"].get<code_t>();
      initialRound.active = true;
      initialRound.codeIdx = static_cast<uint8_t>(
          (code[0] - MIN_NUMBER) * 25 + (code[1] - MIN_NUMBER) * 5 +
          (code[2] - MIN_NUMBER));
      for (const auto verifierIdx :
           data["currentRound"]["checkedVerifierIndices"]
               .get<std::vector<uint8_t>>()) {
        initialRound.checkedVerifierMask |= uint8_t{1} << verifierIdx;
      }
    }
    const auto result = search_classic(
        game, queries, initialRound,
        [searchId](const search_progress_t &progress) {
          emit_search_progress(searchId, progress.rounds, progress.checks,
                               static_cast<double>(progress.nodes),
                               static_cast<uint32_t>(progress.memoStates));
        },
        answer, optimizeExpectedValue, simulationCriteriaIndices);

    auto outputJson = json();
    outputJson["status"] = static_cast<uint8_t>(result.status);
    outputJson["code"] = result.code;
    outputJson["solutionKnown"] = result.solutionKnown;
    outputJson["solution"] = result.solution;
    outputJson["verifierIdx"] = result.verifierIdx;
    outputJson["startsNewRound"] = result.startsNewRound;
    outputJson["worstCaseRounds"] = result.worstCaseRounds;
    outputJson["worstCaseChecks"] = result.worstCaseChecks;
    outputJson["liveWorlds"] = result.liveWorlds;
    outputJson["liveCodes"] = result.liveCodes;
    outputJson["greenWorlds"] = result.greenWorlds;
    outputJson["redWorlds"] = result.redWorlds;
    outputJson["greenCodes"] = result.greenCodes;
    outputJson["redCodes"] = result.redCodes;
    outputJson["answerMatchesLiveWorld"] = result.answerMatchesLiveWorld;
    outputJson["answerResultKnown"] = result.answerResultKnown;
    outputJson["answerResult"] = result.answerResult;
    outputJson["expectedValueOptimized"] = result.expectedValueOptimized;
    outputJson["expectedRounds"] = result.expectedRounds;
    outputJson["expectedChecks"] = result.expectedChecks;
    outputJson["nodes"] = result.nodes;
    outputJson["memoStates"] = result.memoStates;
    return outputJson;
  });
}
