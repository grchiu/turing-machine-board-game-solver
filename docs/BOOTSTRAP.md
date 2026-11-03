# Local Development And Agent Bootstrap

## Checkout And Context

Repository: `grchiu/turing-machine-board-game-solver`. Standard workspace:
`${HOME}/workspaces/turing-machine-board-game-solver`. Clone remote `main` and
open a new context in that checkout. Read this guide and the [README](../README.md)
for current search semantics. Do not restore old source branches, patches,
worktrees or cached dependency installations.
All new GitHub checkouts use HTTPS with approved GitHub CLI authentication, not
an inherited SSH key. Do not change existing remotes to follow this guide.

```sh
mkdir -p "$HOME/workspaces"
git clone --branch main --single-branch https://github.com/grchiu/turing-machine-board-game-solver.git "$HOME/workspaces/turing-machine-board-game-solver"
```

## Components And Toolchains

- C++ solver and native tests: a compiler supporting the C++23 flags used by
  the [Makefile](../Makefile), plus Make.
- Browser solver: Emscripten builds `wasm.cpp` into checked-in WASM/JS artifacts
  under `frontend/public/wasm/`. Regenerate them consistently with source changes.
- React/TypeScript frontend: Node/npm, [package.json](../frontend/package.json)
  and [package-lock.json](../frontend/package-lock.json). The
  [Pages workflow](../.github/workflows/pages.yml) declares the current CI runtime
  and build flags; review those before upgrading toolchains.

Provision toolchains through the approved host setup path. Emscripten is needed
for rebuilding WASM, not merely serving the checked-in artifacts.

## Configuration

No private `.env`, external API credential or database is needed for local
solver use. `frontend/.env` is tracked public build configuration, not a secret
file to retrieve from another machine. Check its effective public path and the
Pages workflow when serving under a subdirectory; an upstream homepage field
in package metadata is not authority to deploy to the upstream project.

No browser profile or application-data transfer is required. GitHub Pages
publishing is a separate authenticated operation; a push to main triggers the
checked-in deployment workflow. Do not push as a bootstrap connectivity test.

## Build And Test

From the repo root:

```sh
make
make doctest
./test.out
```

For an authorized WASM rebuild with Emscripten available:

```sh
make wasm
```

Frontend setup follows the existing CI compatibility flags:

```sh
cd frontend
npm install --legacy-peer-deps
CI=true npm test -- --runInBand
npm run build
```

For local UI work, run `npm start` inside `frontend`. The README documents port
3000 and the project subpath; resolve port conflicts before starting. Verify
WASM worker loading, a representative puzzle and public-path routing. Unit
success is not proof that the browser can load the generated artifacts.

## Solver Invariants

The README defines classic/extreme/nightmare active minimax behavior. Minimum
round guarantees and bounded check-count/expected-value tie-breaks are distinct;
do not describe capped optimization as proof of every tie-break. Puzzle
verification data may be used to simulate answers, but must not leak hidden
criteria into the planner. Preserve upstream credits and licensing.
