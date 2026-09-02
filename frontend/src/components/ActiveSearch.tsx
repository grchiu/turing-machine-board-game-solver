import PlayIcon from "@mui/icons-material/PlayArrowRounded";
import SearchIcon from "@mui/icons-material/TravelExploreRounded";
import Box from "@mui/material/Box";
import Button from "@mui/material/Button";
import CircularProgress from "@mui/material/CircularProgress";
import Paper from "@mui/material/Paper";
import Typography from "@mui/material/Typography";
import { searchClassic, SearchProgress, SearchResult } from "deductions";
import { useAppDispatch } from "hooks/useAppDispatch";
import { useAppSelector } from "hooks/useAppSelector";
import { useEffect, useMemo, useRef, useState } from "react";
import { RootState, store } from "store";
import { roundsActions } from "store/slices/roundsSlice";

const initialProgress: SearchProgress = {
  rounds: 0,
  checks: 0,
  nodes: 0,
  memoStates: 0,
};

const OPTIMIZE_EXPECTED_VALUE = true;

function formatNumber(value: number) {
  return new Intl.NumberFormat().format(value);
}

function plural(value: number, singular: string) {
  return `${value} ${singular}${value === 1 ? "" : "s"}`;
}

function average(value: number, singular: string) {
  const formatted = new Intl.NumberFormat(undefined, {
    minimumFractionDigits: 1,
    maximumFractionDigits: 2,
  }).format(value);
  return `${formatted} ${singular}${value === 1 ? "" : "s"}`;
}

function getSearchEvidenceKey(state: RootState) {
  return JSON.stringify({
    cards: state.comments.map((comment) => ({
      nightmare: comment.nightmare,
      cardIds: comment.criteriaCards.map((card) => card.id),
    })),
    rounds: state.rounds.flatMap((round) => {
      const queries = round.queries.filter(
        (query) => query.state !== "unknown"
      );
      return queries.length === 0 ? [] : [{ code: round.code, queries }];
    }),
  });
}

function getRecordedScore(state: RootState) {
  const rounds = state.rounds.filter((round) => {
    const hasCompleteCode = round.code.every(
      ({ digit }) => digit !== null && digit >= 1 && digit <= 5
    );
    const hasVerifierCheck = round.queries
      .slice(0, state.comments.length)
      .some((query) => query.state !== "unknown");
    return hasCompleteCode && hasVerifierCheck;
  }).length;
  const checks = state.rounds.reduce(
    (total, round) =>
      total +
      round.queries
        .slice(0, state.comments.length)
        .filter((query) => query.state !== "unknown").length,
    0
  );
  return { rounds, checks };
}

export function ActiveSearch() {
  const dispatch = useAppDispatch();
  const state = useAppSelector((currentState) => currentState);
  const isClassic =
    state.comments.length > 0 &&
    !state.comments[0].nightmare &&
    state.comments[0].criteriaCards.length === 1;
  const [isSearching, setIsSearching] = useState(false);
  const [progress, setProgress] = useState(initialProgress);
  const [result, setResult] = useState<SearchResult | null>(null);
  const [error, setError] = useState("");
  const [isSimulating, setIsSimulating] = useState(false);
  const [simulationStep, setSimulationStep] = useState("");
  const [simulationBound, setSimulationBound] = useState<{
    rounds: number;
    checks: number;
  } | null>(null);
  const isSearchingRef = useRef(false);
  const simulatedEvidenceKey = useRef("");

  const searchStateKey = useMemo(
    () => JSON.stringify({ comments: state.comments, rounds: state.rounds }),
    [state.comments, state.rounds]
  );
  const searchEvidenceKey = getSearchEvidenceKey(state);
  const currentStateKey = useRef(searchStateKey);
  currentStateKey.current = searchStateKey;

  useEffect(() => {
    if (isSearchingRef.current) {
      return;
    }
    if (simulatedEvidenceKey.current === searchEvidenceKey) {
      simulatedEvidenceKey.current = "";
      return;
    }
    setResult(null);
    setProgress(initialProgress);
    setError("");
    setSimulationBound(null);
  }, [searchEvidenceKey]);

  async function runActiveSearch() {
    const stateKeyAtStart = searchStateKey;
    isSearchingRef.current = true;
    setIsSearching(true);
    setResult(null);
    setError("");
    setSimulationStep("");
    setSimulationBound(null);
    setProgress(initialProgress);
    try {
      const searchResult = await searchClassic(
        state,
        setProgress,
        undefined,
        OPTIMIZE_EXPECTED_VALUE
      );
      if (currentStateKey.current === stateKeyAtStart) {
        setResult(searchResult);
      }
    } catch (searchError) {
      console.error(searchError);
      setError("Search failed. Check the browser console for details.");
    } finally {
      isSearchingRef.current = false;
      setIsSearching(false);
    }
  }

  async function runSimulation() {
    if (!result || result.status !== 2 || !result.solutionKnown) {
      return;
    }

    const answerCode = result.solution;
    const answer = answerCode.join("");
    const firstResult = result;
    isSearchingRef.current = true;
    setIsSearching(true);
    setIsSimulating(true);
    setResult(null);
    setError("");
    setSimulationStep("");
    setProgress(initialProgress);
    try {
      const initialScore = getRecordedScore(store.getState());
      setSimulationBound({
        rounds: initialScore.rounds + firstResult.worstCaseRounds,
        checks: initialScore.checks + firstResult.worstCaseChecks,
      });
      for (let step = 0; step < 64; step += 1) {
        setProgress(initialProgress);
        const searchResult =
          step === 0
            ? firstResult
            : await searchClassic(
                store.getState(),
                setProgress,
                answerCode,
                OPTIMIZE_EXPECTED_VALUE
              );
        if (!searchResult.answerMatchesLiveWorld) {
          setError(`${answer} is not a live answer for these checks.`);
          return;
        }
        if (searchResult.status === 1) {
          setResult(searchResult);
          return;
        }
        if (searchResult.status !== 2) {
          setError("Simulation could not find a next verifier check.");
          return;
        }
        if (!searchResult.answerResultKnown) {
          setError(
            `${answer} does not uniquely determine this verifier result.`
          );
          return;
        }

        const code = searchResult.code.join("");
        const verifier = String.fromCharCode(
          65 + searchResult.verifierIdx
        ) as Verifier;
        setSimulationStep(
          `${searchResult.startsNewRound ? `New round ${code}` : `Continue ${code}`}: ${verifier} ${
            searchResult.answerResult ? "green" : "red"
          }`
        );
        dispatch(
          roundsActions.recordSimulatedCheck({
            code: searchResult.code as Digit[],
            verifier,
            startsNewRound: searchResult.startsNewRound,
            result: searchResult.answerResult,
          })
        );
        simulatedEvidenceKey.current = getSearchEvidenceKey(store.getState());
      }
      setError("Simulation stopped after too many verifier checks.");
    } catch (searchError) {
      console.error(searchError);
      setError("Simulation failed. Check the browser console for details.");
    } finally {
      isSearchingRef.current = false;
      setIsSearching(false);
      setIsSimulating(false);
      setSimulationStep("");
    }
  }

  const recommendation = result?.code.join("");
  const verifier = result ? String.fromCharCode(65 + result.verifierIdx) : "";
  const recordedScore = getRecordedScore(state);

  return (
    <Paper
      component="section"
      id="active-search-section"
      sx={{
        width: 320,
        margin: (theme) => theme.spacing(2, "auto"),
        borderRadius: 2,
      }}
    >
      <Box p={2}>
        <Button
          id="active-search__button"
          variant="contained"
          size="large"
          fullWidth
          disabled={!isClassic || isSearching}
          startIcon={
            isSearching ? (
              <CircularProgress size={20} color="inherit" />
            ) : (
              <SearchIcon />
            )
          }
          onClick={runActiveSearch}
          sx={{ minHeight: 56, fontSize: 20 }}
        >
          {isSearching ? (isSimulating ? "SIMULATING" : "SEARCHING") : "SEARCH"}
        </Button>

        <Box
          mt={2}
          px={1.5}
          pt={1.5}
          pb={0.75}
          minHeight={132}
          textAlign="left"
          sx={{
            border: 1,
            borderColor: "divider",
            borderRadius: 1,
            backgroundColor: "background.default",
          }}
          aria-live="polite"
        >
          {!isClassic && (
            <Typography variant="body2">Classic puzzles only.</Typography>
          )}
          {isClassic && !isSearching && !result && !error && (
            <Typography variant="body2">
              Ready to find the next verifier check.
            </Typography>
          )}
          {isSearching && (
            <>
              <Typography variant="body1">
                {simulationStep || (progress.rounds === 0 && progress.checks === 0
                  ? "Building live worlds..."
                  : progress.checks === 0
                    ? `Proving ${plural(progress.rounds, "round")}...`
                    : `Proving ${plural(progress.rounds, "round")} / ${plural(progress.checks, "check")}...`)}
              </Typography>
              <Typography variant="body2">
                {formatNumber(progress.nodes)} search nodes
              </Typography>
              <Typography variant="body2">
                {formatNumber(progress.memoStates)} memoized states
              </Typography>
            </>
          )}
          {error && <Typography color="error">{error}</Typography>}
          {result?.status === 0 && (
            <Typography color="error">
              No live solution matches the recorded checks.
            </Typography>
          )}
          {result?.status === 1 && (
            <>
              <Typography variant="body1">
                Puzzle complete: {recommendation}
              </Typography>
              <Typography variant="body2">
                Solved: {plural(recordedScore.rounds, "round")},{" "}
                {plural(recordedScore.checks, "verifier check")}
              </Typography>
              {simulationBound && (
                <Typography variant="body2">
                  Planned worst case: {plural(simulationBound.rounds, "round")},{" "}
                  {plural(simulationBound.checks, "verifier check")}.
                </Typography>
              )}
            </>
          )}
          {result?.status === 2 && (
            <>
              <Typography variant="body1" sx={{ fontSize: 20 }}>
                {result.startsNewRound
                  ? `New round: test ${recommendation} on ${verifier}`
                  : `Continue ${recommendation}: check ${verifier}`}
              </Typography>
              <Typography variant="body2">
                From here: {plural(result.worstCaseRounds, "more round")},{" "}
                {plural(result.worstCaseChecks, "check")}
              </Typography>
              {result.expectedValueOptimized && (
                <Typography variant="body2">
                  Average: {average(result.expectedRounds, "more round")},{" "}
                  {average(result.expectedChecks, "check")}
                </Typography>
              )}
              <Typography variant="caption" color="text.secondary">
                {formatNumber(result.nodes)} nodes, {formatNumber(result.memoStates)} memo states
              </Typography>
              {result.solutionKnown && (
                <Button
                  variant="contained"
                  fullWidth
                  startIcon={<PlayIcon />}
                  onClick={runSimulation}
                  sx={{ mt: 2 }}
                >
                  SIMULATE
                </Button>
              )}
            </>
          )}
        </Box>
      </Box>
    </Paper>
  );
}
