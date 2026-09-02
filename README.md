# Turing Machine Board Game Solver

An interactive deduction sheet and solver for the board game
[Turing Machine](https://boardgamegeek.com/boardgame/356123/turing-machine).

Try this fork on [GitHub Pages](https://grchiu.github.io/turing-machine-board-game-solver/).

## Upstream and Credits

This repository is a fork of
[alexander-zibert/turing-machine-board-game-solver](https://github.com/alexander-zibert/turing-machine-board-game-solver).
The upstream project implemented the criteria cards, verifier logic, classic and
extreme solving, WASM integration, and the interactive deduction sheet on which
this work is built.

The frontend was originally derived from
[zyle87/turing-machine-interactive-sheet](https://github.com/zyle87/turing-machine-interactive-sheet).
The verification-card lookup data was adapted upstream from
[manurFR/turingmachine](https://github.com/manurFR/turingmachine).

Other useful links:

- [Official Turing Machine site](https://turingmachine.info/)
- [Play on Board Game Arena](https://en.boardgamearena.com/gamepanel?game=turingmachine)

## Changes in This Fork

This fork adds an active strategy search for classic puzzles. Instead of only
checking deductions entered by the player, the solver can recommend the next
code and verifier that produce the best guaranteed path to a solution.

The main additions are:

- An adaptive minimax search that minimizes rounds before verifier checks.
- Re-entrant recommendations after every red or green result. The solver may
  recommend another verifier with the current code or start a new round with a
  different code.
- Expected-value tie-breaking among plans with the same worst-case guarantee.
- Toggleable live verifier-category deductions as results are entered.
- An optional Speculate Mode for manually cycling checks through green, red,
  and off; normal mode calculates the real verifier result automatically.
- Search progress showing explored nodes and memoized states.
- A simulation mode that follows the recommended policy through a complete
  puzzle without displaying the hidden solution in advance.
- Updated WASM bindings, worker messages, tests, and GitHub Pages deployment.

Active search and simulation currently support classic puzzles only. The
original passive deduction tools continue to support the modes implemented by
the upstream solver.

## Minimax Search

### Worlds

A **world** is one possible assignment of a hidden criterion to every verifier
card. Before searching, the solver enumerates the Cartesian product of the
criteria cards and keeps only worlds that:

1. Produce exactly one solution code.
2. Do not contain a verifier made redundant by all the others.
3. Agree with every red or green result recorded so far.

Several worlds can have the same solution code. The puzzle is complete when all
remaining worlds agree on one code; the hidden criteria do not themselves need
to be uniquely identified.

### State and Actions

A search state contains:

- A bitset of the live worlds.
- The code currently being tested, if a round is in progress.
- A bit mask of the verifiers already checked in that round.

From that state, an action is either:

- **Continue the round:** check an unused verifier with the current code, up to
  the three-check limit.
- **Start a new round:** choose any of the 125 codes and check one verifier with
  it.

Every action partitions the live worlds into a green branch and a red branch.
For a plan to satisfy a budget, both branches must be solvable within the
remaining budget. Search runs again after each observed result, which is why it
can decide after one or two checks that starting a new round is better than
using all three checks.

### Optimization Order

Plans are compared lexicographically:

1. Minimize the maximum number of additional rounds.
2. Among those plans, minimize the maximum number of additional verifier
   checks.
3. Among plans with the same worst-case bounds, minimize expected verifier
   checks.
4. Finally, minimize expected rounds.

The first two stages are strict minimax guarantees. The expected-value stage is
only a tie-breaker and cannot weaken either worst-case bound. Expected values
currently assume that each live world is equally likely.

Expected-value optimization can be disabled with
`OPTIMIZE_EXPECTED_VALUE` in
`frontend/src/components/ActiveSearch.tsx`. Live verifier-category updates can
be switched on or off with the **Auto Deductions** toolbar button.

### Search Strategy

The implementation uses iterative deepening and memoized feasibility searches:

1. Enumerate all live worlds and precompute the green-world bitset for every
   `(code, verifier)` pair.
2. Start from an information-theoretic lower bound based on
   `ceil(log2(live codes))`, the checks remaining in the current round, and the
   limit of three checks per new round.
3. Find the smallest round budget for which every possible result path can
   finish.
4. Hold that round budget fixed and increase the check budget until a complete
   policy exists.
5. Optionally search within those fixed worst-case budgets for the policy with
   the best expected score.

Candidate actions are ordered to try continuing the current round first, then
actions with the smallest worst branch by code count and world count. This does
not change optimality, but it tends to find a proof sooner.

Memoization keys include the live-world bitset and the complete round context.
Separate memo tables are maintained for round feasibility, combined
round/check feasibility, and expected-value optimization.

### Simulation and Hidden Criteria

Pasted puzzle data includes verification-card numbers. The frontend can map
those numbers to the actual hidden criteria and therefore derive the puzzle's
solution. That information is deliberately excluded from minimax planning: the
search still considers every live world consistent with the player's observed
results.

The hidden criteria are used only by simulation to determine whether a
recommended check should be recorded as red or green. The solution is kept
internal until the simulated deductions establish it normally.

## Building

### Native CLI

```sh
make
./a.out
```

### WASM

Install a working
[Emscripten toolchain](https://emscripten.org/docs/getting_started/downloads.html),
then run:

```sh
make wasm
```

### Frontend

```sh
cd frontend
npm install --legacy-peer-deps
npm start
```

The development server opens the app at
`http://localhost:3000/turing-machine-board-game-solver/`.

For a production build:

```sh
cd frontend
npm run build
```

## Tests

Native solver tests:

```sh
make doctest
./test.out
```

Frontend tests:

```sh
cd frontend
CI=true npm test -- --runInBand
```

## Project Status

- [x] Initial deductions before the game
- [x] Deductions based on machine answers
- [x] Classic and extreme passive solving
- [x] Interactive frontend with a WASM solver and web worker
- [x] Active classic-puzzle strategy search
- [x] Adaptive round and verifier-check minimax
- [x] Expected-value tie-breaking
- [x] Live verifier-category deductions
- [x] Answer-free simulation from verification-card data
- [x] Tests based on known puzzles and solutions
- [x] GitHub Pages deployment
- [ ] Puzzle generation
- [ ] Offline service-worker support
