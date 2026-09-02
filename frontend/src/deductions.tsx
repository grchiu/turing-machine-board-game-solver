import { RootState, store } from "store";
import { alertActions } from "store/slices/alertSlice";
import { CommentsState } from "store/slices/commentsSlice";
import { getCriteriaIndexForCryptCard } from "parsing/util";
import type { CriteriaCard } from "hooks/useCriteriaCard";

export type Query = {
  code: number[];
  verifierIdx: number;
  result: boolean;
};

export type SearchProgress = {
  rounds: number;
  checks: number;
  nodes: number;
  memoStates: number;
  optimizingExpectedValue: boolean;
};

export type SearchResult = {
  status: 0 | 1 | 2;
  code: number[];
  solutionKnown: boolean;
  solution: number[];
  verifierIdx: number;
  startsNewRound: boolean;
  worstCaseRounds: number;
  worstCaseChecks: number;
  liveWorlds: number;
  liveCodes: number;
  greenWorlds: number;
  redWorlds: number;
  greenCodes: number;
  redCodes: number;
  answerMatchesLiveWorld: boolean;
  answerResultKnown: boolean;
  answerResult: boolean;
  expectedValueOptimized: boolean;
  expectedValueLimitReached: boolean;
  checkOptimizationLimitReached: boolean;
  expectedRounds: number;
  expectedChecks: number;
  nodes: number;
  memoStates: number;
};

export type SolverResult = {
  codes: string[];
  possibleVerifiers: number[][];
  possibleLetters: string[][];
};

const workerUrl = "/turing-machine-board-game-solver/wasm/worker.mjs";
const sharedWorker = new Worker(workerUrl);

function checkDigits(state: RootState, possibleCodes: string[]) {
  const digits = { triangle: new Set(), square: new Set(), circle: new Set() };
  for (const code of possibleCodes) {
    digits.triangle.add(Number(code[0]));
    digits.square.add(Number(code[1]));
    digits.circle.add(Number(code[2]));
  }
  for (const { shape, digit } of state.digitCode) {
    if (digits[shape].has(digit)) {
      return false;
    }
  }

  return true;
}

function checkVerifiers(state: RootState, possibleVerifiers: number[][]) {
  for (let i = 0; i < state.comments.length; i += 1) {
    const firstCard = state.comments[i].criteriaCards[0];
    for (const criteria of firstCard.irrelevantCriteria) {
      // the verifiers are 1-indexed in the frontend
      if (possibleVerifiers[i].includes(criteria - 1)) {
        return false;
      }
    }
    // extreme mode
    const secondCard = state.comments[i].criteriaCards[1] || {
      irrelevantCriteria: [],
    };
    for (const criteria of secondCard.irrelevantCriteria) {
      if (
        possibleVerifiers[i].includes(criteria - 1 + firstCard.criteriaSlots)
      ) {
        return false;
      }
    }
  }
  return true;
}

function checkLetters(state: RootState, possibleLetters: string[][]) {
  if (!state.comments[0].nightmare) {
    return true;
  }
  for (let i = 0; i < state.comments.length; i += 1) {
    const letters = state.comments[i].letters;
    for (const letter of letters) {
      if (letter.isIrrelevant && possibleLetters[i].includes(letter.letter)) {
        return false;
      }
    }
  }
  return true;
}

function getQueries(state: RootState) {
  const queries: Query[] = [];
  for (const round of state.rounds) {
    const code: number[] = [];
    for (const { digit } of round.code) {
      if (!(digit !== null && digit >= 1 && digit <= 5)) {
        continue;
      }
      code.push(digit);
    }
    if (code.length !== 3) {
      continue;
    }
    for (const query of round.queries) {
      if (query.state === "unknown") {
        continue;
      }
      queries.push({
        code,
        verifierIdx: query.verifier.charCodeAt(0),
        result: query.state === "solved",
      });
    }
  }
  return queries;
}

function getGameMode(state: RootState) {
  if (state.comments[0].nightmare) {
    return 2;
  }
  if (state.comments[0].criteriaCards.length > 1) {
    return 1;
  }
  return 0;
}

function getSolverInput(state: RootState) {
  const numVerifiers = state.comments.length;
  const mode = getGameMode(state);
  const cards = [
    ...state.comments.map(({ criteriaCards }) => {
      return criteriaCards[0].id;
    }),
    ...(mode === 1
      ? state.comments.map(({ criteriaCards }) => {
          return criteriaCards[1].id;
        })
      : []),
  ];

  return {
    type: "solve_wasm",
    verifierCards: cards,
    queries: getQueries(state),
    mode,
    numVerifiers,
  };
}

export async function solveCurrentState(state: RootState): Promise<SolverResult> {
  return waitForWorker(getSolverInput(state));
}

export async function evaluateVerifier(
  criteriaCards: CriteriaCard[],
  cryptCardId: number,
  code: number[]
): Promise<boolean | null> {
  for (const criteriaCard of criteriaCards) {
    const criteriaIdx = getCriteriaIndexForCryptCard(
      criteriaCard.id,
      cryptCardId
    );
    if (criteriaIdx === null) {
      continue;
    }
    const response = await waitForWorker({
      type: "evaluate_verifier_wasm",
      cardId: criteriaCard.id,
      criteriaIdx,
      code,
    });
    return response.known ? response.result : null;
  }
  return null;
}

export async function checkDeductions(state: RootState) {
  const result = await solveCurrentState(state);

  console.log(result);
  console.log(state);
  if (result.codes.length === 0) {
    store.dispatch(
      alertActions.openAlert({
        message: `There are no more possible codes.
          Please double-check that you have the correct verifiers and that your query results are correct.
          If this problem still occurs, please file a bug report.`,
        level: "error",
      })
    );
  } else if (
    !(
      checkVerifiers(state, result.possibleVerifiers) &&
      checkDigits(state, result.codes) &&
      checkLetters(state, result.possibleLetters)
    )
  ) {
    store.dispatch(
      alertActions.openAlert({
        message: `You have made an invalid deduction!`,
        level: "warning",
      })
    );
  } else {
    store.dispatch(
      alertActions.openAlert({
        message: `All deductions are valid so far!`,
        level: "success",
      })
    );
  }
}

function getStandardSearchInput(
  state: RootState,
  answer?: number[],
  optimizeExpectedValue = false,
  expectedValueNodeBudget = 0,
  expectedValueTimeBudgetMs = 0,
  checkOptimizationNodeBudget = 0,
  checkOptimizationTimeBudgetMs = 0
) {
  const mode = getGameMode(state);
  const cards = state.comments.map(({ criteriaCards }) =>
    criteriaCards.map(({ id }) => id)
  );
  const lastRound = state.rounds[state.rounds.length - 1];
  const code = lastRound?.code.map(({ digit }) => digit);
  const checkedVerifierIndices = lastRound?.queries
    .filter(
      (query) =>
        query.state !== "unknown" &&
        query.verifier.charCodeAt(0) - 65 < state.comments.length
    )
    .map((query) => query.verifier.charCodeAt(0) - 65);
  const currentRound =
    code?.every((digit) => digit !== null && digit >= 1 && digit <= 5) &&
    checkedVerifierIndices &&
    checkedVerifierIndices.length > 0 &&
    checkedVerifierIndices.length < 3
      ? { code: code as number[], checkedVerifierIndices }
      : null;
  let simulationCardIndicesByVerifier: number[] | null = null;
  let criteriaIndices: Array<number | null>;
  if (mode === 2) {
    criteriaIndices = Array(state.comments.length).fill(null);
    const cardIndicesByVerifier = Array(state.comments.length).fill(-1);
    const usedCards = Array(state.comments.length).fill(false);

    const assignVerifier = (verifierIdx: number): boolean => {
      if (verifierIdx === state.comments.length) {
        return true;
      }
      const cryptCardId =
        state.comments[verifierIdx].criteriaCards[0]?.cryptCard?.id;
      if (cryptCardId === undefined) {
        return false;
      }
      for (let cardIdx = 0; cardIdx < state.comments.length; cardIdx += 1) {
        if (usedCards[cardIdx]) {
          continue;
        }
        const criteriaCard = state.comments[cardIdx].criteriaCards[0];
        const criteriaIdx = criteriaCard
          ? getCriteriaIndexForCryptCard(criteriaCard.id, cryptCardId)
          : null;
        if (criteriaIdx === null) {
          continue;
        }
        usedCards[cardIdx] = true;
        criteriaIndices[cardIdx] = criteriaIdx;
        cardIndicesByVerifier[verifierIdx] = cardIdx;
        if (assignVerifier(verifierIdx + 1)) {
          return true;
        }
        usedCards[cardIdx] = false;
        criteriaIndices[cardIdx] = null;
        cardIndicesByVerifier[verifierIdx] = -1;
      }
      return false;
    };

    if (assignVerifier(0)) {
      simulationCardIndicesByVerifier = cardIndicesByVerifier;
    }
  } else {
    criteriaIndices = state.comments.map(({ criteriaCards }) => {
      let criteriaOffset = 0;
      for (const criteriaCard of criteriaCards) {
        const criteriaIndex = criteriaCard.cryptCard
          ? getCriteriaIndexForCryptCard(
              criteriaCard.id,
              criteriaCard.cryptCard.id
            )
          : null;
        if (criteriaIndex !== null) {
          return criteriaOffset + criteriaIndex;
        }
        criteriaOffset += criteriaCard.criteriaSlots;
      }
      return null;
    });
  }
  const simulationCriteriaIndices = criteriaIndices.every(
    (index): index is number => index !== null
  )
    ? criteriaIndices
    : null;
  return {
    cards,
    queries: getQueries(state),
    currentRound,
    mode,
    answer: answer ?? null,
    simulationCriteriaIndices,
    simulationCardIndicesByVerifier,
    optimizeExpectedValue,
    expectedValueNodeBudget,
    expectedValueTimeBudgetMs,
    checkOptimizationNodeBudget,
    checkOptimizationTimeBudgetMs,
  };
}

export async function searchStandard(
  state: RootState,
  onProgress: (progress: SearchProgress) => void,
  answer?: number[],
  optimizeExpectedValue = false,
  expectedValueNodeBudget = 0,
  expectedValueTimeBudgetMs = 0,
  checkOptimizationNodeBudget = 0,
  checkOptimizationTimeBudgetMs = 0
): Promise<SearchResult> {
  return waitForWorker(
    {
      type: "search_standard_wasm",
      ...getStandardSearchInput(
        state,
        answer,
        optimizeExpectedValue,
        expectedValueNodeBudget,
        expectedValueTimeBudgetMs,
        checkOptimizationNodeBudget,
        checkOptimizationTimeBudgetMs
      ),
    },
    onProgress
  );
}

export async function getPossibleCodes(comments: CommentsState) {
  const cards = comments.map(({ criteriaCards }) => {
    return criteriaCards.map((card) => card.id);
  });
  const possibleVerifiers: number[][] = [];
  for (const comment of comments) {
    const current: number[] = [];
    let criteriaIdx = 0;
    for (const criteriaCard of comment.criteriaCards) {
      for (let i = 0; i < criteriaCard.criteriaSlots; i += 1) {
        if (!criteriaCard.irrelevantCriteria.includes(i + 1)) {
          current.push(criteriaIdx);
        }
        criteriaIdx += 1;
      }
    }
    possibleVerifiers.push(current);
  }
  // console.log(cards, possibleVerifiers);

  return waitForWorker({
    type: "get_possible_codes",
    cards,
    possibleVerifiers,
  });
}

let workId = 0;
const promiseResolves: { [id: number]: any } = {};
const promiseRejects: { [id: number]: (error: Error) => void } = {};
const progressCallbacks: { [id: number]: (data: SearchProgress) => void } = {};
async function waitForWorker(
  data: { [key: string]: any },
  onProgress?: (progress: SearchProgress) => void
): Promise<any> {
  const currentWorkId = workId++;
  return new Promise((res, reject) => {
    promiseResolves[currentWorkId] = res;
    promiseRejects[currentWorkId] = reject;
    if (onProgress) {
      progressCallbacks[currentWorkId] = onProgress;
    }
    if (data.type === "search_standard_wasm") {
      const searchWorker = new Worker(workerUrl);
      searchWorker.onmessage = (event) => {
        handleWorkerMessage(event);
        if (event.data.type === "result") {
          searchWorker.terminate();
        }
      };
      searchWorker.onerror = (event) => {
        rejectWorkerRequests(event, [currentWorkId]);
        searchWorker.terminate();
      };
      searchWorker.postMessage({ ...data, id: currentWorkId });
      return;
    }
    sharedWorker.postMessage({ ...data, id: currentWorkId });
  });
}

function handleWorkerMessage(e: MessageEvent) {
  const data = e.data;
  if (data.type === "search_progress") {
    progressCallbacks[data.id]?.(data);
    return;
  }
  const resolve = promiseResolves[data.id];
  resolve?.(data);
  delete promiseResolves[data.id];
  delete promiseRejects[data.id];
  delete progressCallbacks[data.id];
}

function rejectWorkerRequests(event: ErrorEvent, ids: number[]) {
  const error = new Error(event.message || "The solver worker failed.");
  for (const id of ids) {
    promiseRejects[id]?.(error);
    delete promiseResolves[id];
    delete promiseRejects[id];
    delete progressCallbacks[id];
  }
}

sharedWorker.onmessage = handleWorkerMessage;

sharedWorker.onerror = function onerror(event) {
  rejectWorkerRequests(event, Object.keys(promiseRejects).map(Number));
};
