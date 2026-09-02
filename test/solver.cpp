#include "../solver.hpp"
#include "../search.hpp"
#include "doctest.h"
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

TEST_CASE("test generated problems") {
  std::ifstream file("test/problems.txt");
  std::string line;

  while (std::getline(file, line)) {
    std::stringstream lineStream(line);
    std::string cell;

    std::getline(lineStream, cell, ',');
    const auto hashCode = cell;

    std::getline(lineStream, cell, ',');
    const auto mode = game_mode_t{(uint8_t)std::stoul(cell)};

    std::getline(lineStream, cell, ',');
    const auto numVerifier = (uint8_t)std::stoul(cell);

    auto game = std::vector<card_t>{numVerifier};
    for (uint8_t i = 0; i < numVerifier; i += 1) {
      std::getline(lineStream, cell, ',');
      game[i] = all_cards[std::stoul(cell)];
    }
    if (mode == game_mode_t::extreme) {
      for (uint8_t i = 0; i < numVerifier; i += 1) {
        std::getline(lineStream, cell, ',');
        for (const auto &verifier : all_cards[std::stoul(cell)]) {
          game[i].push_back(verifier);
        }
      }
    }
    std::getline(lineStream, cell, ',');
    const auto result = solve(game, {}, mode);
    const auto solutionFound =
        result.possibleCodes.find(cell) != result.possibleCodes.end();
    if (!solutionFound) {
      std::cout << line << std::endl;
    }
    CHECK(solutionFound == true);
  }

  file.close();
}

TEST_CASE("test B65 FWE R") {
  auto game = std::vector<card_t>{};
  for (const auto card : {7, 10, 14, 18, 19, 22}) {
    game.push_back(all_cards[card]);
  }
  const auto result = solve(game, {
    query_t{{2, 4, 3}, 'C', false},
    query_t{{2, 4, 3}, 'D', false},
    query_t{{2, 4, 3}, 'E', false},
    query_t{{1, 4, 5}, 'B', false},
    query_t{{1, 4, 5}, 'E', false},
    query_t{{1, 4, 5}, 'F', false},
    query_t{{5, 3, 1}, 'A', true},
    query_t{{5, 3, 1}, 'C', true},
    query_t{{5, 3, 1}, 'F', false},
    }, game_mode_t::classic);
  CHECK(result.possibleCodes.size() == 1);
  CHECK(result.possibleCodes.find("251") != result.possibleCodes.end());
}

TEST_CASE("classic search optimizes rounds before checks") {
  auto game = std::vector<card_t>{};
  for (const auto card : {2, 5, 8, 12}) {
    game.push_back(all_cards[card]);
  }

  const auto result = search_standard(game, {});
  CHECK(result.status == search_status_t::recommendation);
  CHECK(result.liveWorlds > 1);
  CHECK(result.liveCodes > 1);
  CHECK(result.verifierIdx < game.size());
  CHECK(result.startsNewRound == true);
  CHECK(result.worstCaseRounds >= 1);
  CHECK(result.worstCaseChecks >= 1);
  CHECK(result.worstCaseChecks >= result.worstCaseRounds);
  CHECK(result.worstCaseChecks <= result.worstCaseRounds * 3);
  CHECK(result.expectedValueOptimized == false);

  CHECK(result.greenWorlds + result.redWorlds == result.liveWorlds);
  CHECK(result.greenCodes <= result.liveCodes);
  CHECK(result.redCodes <= result.liveCodes);
}

TEST_CASE("classic search recognizes a completed puzzle") {
  auto game = std::vector<card_t>{};
  for (const auto card : {7, 10, 14, 18, 19, 22}) {
    game.push_back(all_cards[card]);
  }
  const auto queries = std::vector<query_t>{
      query_t{{2, 4, 3}, 'C', false},
      query_t{{2, 4, 3}, 'D', false},
      query_t{{2, 4, 3}, 'E', false},
      query_t{{1, 4, 5}, 'B', false},
      query_t{{1, 4, 5}, 'E', false},
      query_t{{1, 4, 5}, 'F', false},
      query_t{{5, 3, 1}, 'A', true},
      query_t{{5, 3, 1}, 'C', true},
      query_t{{5, 3, 1}, 'F', false},
  };

  const auto result = search_standard(game, queries);
  CHECK(result.status == search_status_t::solved);
  CHECK(result.liveCodes == 1);
  CHECK(result.worstCaseRounds == 0);
  CHECK(result.worstCaseChecks == 0);
  CHECK(result.code == code_t{2, 5, 1});
}

TEST_CASE("classic search can continue an in-progress round") {
  auto game = std::vector<card_t>{};
  for (const auto card : {17, 36, 38, 42}) {
    game.push_back(all_cards[card]);
  }

  const auto first = search_standard(game, {});
  REQUIRE(first.status == search_status_t::recommendation);
  REQUIRE(first.startsNewRound == true);
  const auto codeIdx = static_cast<uint8_t>(
      (first.code[0] - 1) * 25 + (first.code[1] - 1) * 5 +
      first.code[2] - 1);

  for (const auto answer : {false, true}) {
    const auto query = query_t{
        first.code, static_cast<char>('A' + first.verifierIdx), answer};
    const auto next = search_standard(
        game, {query},
        round_context_t{
            true, codeIdx,
            static_cast<uint8_t>(uint8_t{1} << first.verifierIdx)});

    CHECK(next.status == search_status_t::recommendation);
    CHECK(next.startsNewRound == false);
    CHECK(next.code == first.code);
  }

  const auto cappedRound = search_standard(
      game, {}, round_context_t{true, codeIdx, 0b00000111});
  CHECK(cappedRound.status == search_status_t::recommendation);
  CHECK(cappedRound.startsNewRound == true);
}

TEST_CASE("classic search can simulate a known answer") {
  auto game = std::vector<card_t>{};
  for (const auto card : {17, 36, 38, 42}) {
    game.push_back(all_cards[card]);
  }

  const auto answer = code_t{5, 4, 1};
  auto queries = std::vector<query_t>{};
  auto round = round_context_t{};
  auto solved = false;

  for (size_t step = 0; step < 12; step += 1) {
    const auto result = search_standard(game, queries, round, {}, answer, true);
    REQUIRE(result.answerMatchesLiveWorld == true);
    if (step == 0) {
      CHECK(result.worstCaseRounds == 2);
      CHECK(result.worstCaseChecks == 6);
      CHECK(result.expectedValueOptimized == true);
      CHECK(result.expectedRounds > 0);
      CHECK(result.expectedRounds <= result.worstCaseRounds);
      CHECK(result.expectedChecks > 0);
      CHECK(result.expectedChecks <= result.worstCaseChecks);
    }
    if (result.status == search_status_t::solved) {
      CHECK(result.code == answer);
      solved = true;
      break;
    }

    REQUIRE(result.status == search_status_t::recommendation);
    REQUIRE(result.answerResultKnown == true);
    queries.push_back(query_t{
        result.code, static_cast<char>('A' + result.verifierIdx),
        result.answerResult});
    const auto codeIdx = static_cast<uint8_t>(
        (result.code[0] - 1) * 25 + (result.code[1] - 1) * 5 +
        result.code[2] - 1);
    if (result.startsNewRound) {
      round = round_context_t{
          true, codeIdx,
          static_cast<uint8_t>(uint8_t{1} << result.verifierIdx)};
    } else {
      round.checkedVerifierMask |= uint8_t{1} << result.verifierIdx;
    }
  }

  CHECK(solved == true);
}

TEST_CASE("classic simulation uses the verification cards to resolve a world") {
  auto game = std::vector<card_t>{};
  for (const auto card : {16, 21, 25, 33, 34, 40}) {
    game.push_back(all_cards[card]);
  }

  const auto answer = code_t{4, 3, 3};
  const auto criteriaIndices = std::vector<uint8_t>{1, 1, 1, 5, 2, 2};
  auto queries = std::vector<query_t>{};
  auto round = round_context_t{};
  auto solved = false;

  for (size_t step = 0; step < 12; step += 1) {
    const auto result = search_standard(game, queries, round, {}, answer, false,
                                       criteriaIndices);
    REQUIRE(result.answerMatchesLiveWorld == true);
    REQUIRE(result.solutionKnown == true);
    CHECK(result.solution == answer);
    if (result.status == search_status_t::solved) {
      CHECK(result.code == answer);
      solved = true;
      break;
    }

    REQUIRE(result.status == search_status_t::recommendation);
    REQUIRE(result.answerResultKnown == true);
    queries.push_back(query_t{
        result.code, static_cast<char>('A' + result.verifierIdx),
        result.answerResult});
    const auto codeIdx = static_cast<uint8_t>(
        (result.code[0] - 1) * 25 + (result.code[1] - 1) * 5 +
        result.code[2] - 1);
    if (result.startsNewRound) {
      round = round_context_t{
          true, codeIdx,
          static_cast<uint8_t>(uint8_t{1} << result.verifierIdx)};
    } else {
      round.checkedVerifierMask |= uint8_t{1} << result.verifierIdx;
    }
  }

  CHECK(solved == true);
}

TEST_CASE("classic search derives the hidden solution from verification cards") {
  auto game = std::vector<card_t>{};
  for (const auto card : {16, 21, 25, 33, 34, 40}) {
    game.push_back(all_cards[card]);
  }

  const auto result = search_standard(
      game, {}, {}, {}, std::nullopt, false,
      std::vector<uint8_t>{1, 1, 1, 5, 2, 2});

  REQUIRE(result.status == search_status_t::recommendation);
  REQUIRE(result.solutionKnown == true);
  CHECK(result.solution == code_t{4, 3, 3});
  CHECK(result.answerMatchesLiveWorld == true);
  CHECK(result.answerResultKnown == true);
}

TEST_CASE("standard search supports extreme verifier pairs") {
  const auto cardPairs = std::vector<std::pair<size_t, size_t>>{
      {25, 23}, {24, 16}, {36, 4}, {42, 14}};
  auto game = std::vector<card_t>{};
  for (const auto &[firstCardId, secondCardId] : cardPairs) {
    auto verifierOptions = all_cards[firstCardId];
    const auto &secondCard = all_cards[secondCardId];
    verifierOptions.insert(verifierOptions.end(), secondCard.begin(),
                           secondCard.end());
    game.push_back(std::move(verifierOptions));
  }

  // #F4D EXI: the real criteria are spread across both cards in each pair.
  const auto criteriaIndices = std::vector<uint8_t>{1, 3, 0, 8};
  const auto result = search_standard(game, {}, {}, {}, std::nullopt, false,
                                      criteriaIndices);

  REQUIRE(result.status == search_status_t::recommendation);
  REQUIRE(result.liveWorlds > 0);
  REQUIRE(result.solutionKnown == true);
  CHECK(result.solution == code_t{3, 4, 2});
  CHECK(result.answerMatchesLiveWorld == true);
  CHECK(result.answerResultKnown == true);
}

TEST_CASE("standard search supports six-verifier extreme puzzles") {
  const auto cardPairs = std::vector<std::pair<size_t, size_t>>{
      {10, 2}, {14, 36}, {11, 43}, {46, 21}, {27, 25}, {3, 20}};
  auto game = std::vector<card_t>{};
  for (const auto &[firstCardId, secondCardId] : cardPairs) {
    auto verifierOptions = all_cards[firstCardId];
    const auto &secondCard = all_cards[secondCardId];
    verifierOptions.insert(verifierOptions.end(), secondCard.begin(),
                           secondCard.end());
    game.push_back(std::move(verifierOptions));
  }

  const auto result = search_standard(game, {});

  CHECK(result.status == search_status_t::recommendation);
  CHECK(result.liveWorlds > 0);
  CHECK(result.liveCodes > 1);
  CHECK(result.verifierIdx < game.size());
}

TEST_CASE("expected-value limit preserves the optimal extreme plan") {
  const auto cardPairs = std::vector<std::pair<size_t, size_t>>{
      {27, 5}, {14, 17}, {20, 45}, {25, 11}, {48, 19}, {7, 22}};
  auto game = std::vector<card_t>{};
  for (const auto &[firstCardId, secondCardId] : cardPairs) {
    auto verifierOptions = all_cards[firstCardId];
    const auto &secondCard = all_cards[secondCardId];
    verifierOptions.insert(verifierOptions.end(), secondCard.begin(),
                           secondCard.end());
    game.push_back(std::move(verifierOptions));
  }

  const auto result = search_standard(game, {}, {}, {}, std::nullopt, true,
                                      std::nullopt, 1);

  CHECK(result.status == search_status_t::recommendation);
  CHECK(result.worstCaseRounds == 3);
  CHECK(result.worstCaseChecks == 6);
  CHECK(result.expectedValueOptimized == false);
  CHECK(result.expectedValueLimitReached == true);
}

TEST_CASE("expected-value limit returns the harder five-verifier plan") {
  const auto cardPairs = std::vector<std::pair<size_t, size_t>>{
      {39, 4}, {5, 16}, {9, 17}, {48, 25}, {19, 23}};
  auto game = std::vector<card_t>{};
  for (const auto &[firstCardId, secondCardId] : cardPairs) {
    auto verifierOptions = all_cards[firstCardId];
    const auto &secondCard = all_cards[secondCardId];
    verifierOptions.insert(verifierOptions.end(), secondCard.begin(),
                           secondCard.end());
    game.push_back(std::move(verifierOptions));
  }

  const auto result = search_standard(game, {}, {}, {}, std::nullopt, true,
                                      std::nullopt, 1);

  CHECK(result.status == search_status_t::recommendation);
  CHECK(result.worstCaseRounds == 3);
  CHECK(result.worstCaseChecks == 7);
  CHECK(result.expectedValueOptimized == false);
  CHECK(result.expectedValueLimitReached == true);
}

TEST_CASE("active search supports Nightmare verifier assignments") {
  auto game = std::vector<card_t>{};
  for (const auto cardId : {2, 16, 23, 48}) {
    game.push_back(all_cards[cardId]);
  }

  const auto worlds =
      get_standard_worlds(game, {}, game_mode_t::nightmare);
  CHECK(worlds.size() == 192);

  // #I48 M53: A->23, B->16, C->48, D->2, with solution 151.
  const auto result = search_standard(
      game, {}, {}, {}, std::nullopt, false,
      std::vector<uint8_t>{0, 1, 2, 4}, 0, 0, false,
      game_mode_t::nightmare, std::vector<uint8_t>{2, 1, 3, 0}, 1);

  REQUIRE(result.status == search_status_t::recommendation);
  CHECK(result.liveWorlds == 192);
  CHECK(result.solutionKnown == true);
  CHECK(result.solution == code_t{1, 5, 1});
  CHECK(result.answerMatchesLiveWorld == true);
  CHECK(result.answerResultKnown == true);
  CHECK(result.worstCaseRounds == 3);
  CHECK(result.worstCaseChecks == 9);
  CHECK(result.checkOptimizationLimitReached == true);
}
