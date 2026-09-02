import SolvedIcon from "@mui/icons-material/CheckBoxRounded";
import UnsolvedIcon from "@mui/icons-material/DisabledByDefaultRounded";
import BlankBoxIcon from "@mui/icons-material/CheckBoxOutlineBlankRounded";
import DeleteIcon from "@mui/icons-material/UndoRounded";
import Box from "@mui/material/Box";
import Button from "@mui/material/Button";
import Divider from "@mui/material/Divider";
import Grid from "@mui/material/Grid";
import { alpha, useTheme } from "@mui/material/styles";
import { useAppDispatch } from "hooks/useAppDispatch";
import { useAppSelector } from "hooks/useAppSelector";
import { FC, useState } from "react";
import { RoundsState, roundsActions } from "store/slices/roundsSlice";
import ShapeIcon from "./ShapeIcon";
import SingleCharLabel from "./SingleCharLabel";
import TextField from "./TextField";
import { evaluateVerifier } from "deductions";
import { alertActions } from "store/slices/alertSlice";

type Props = {
  round: RoundsState[number];
  index: number;
  speculateMode: boolean;
};

const Round: FC<Props> = ({ round, index, speculateMode }) => {
  const dispatch = useAppDispatch();
  const theme = useTheme();
  const comments = useAppSelector((state) => state.comments);
  const [pendingVerifier, setPendingVerifier] = useState<Verifier | null>(null);

  const updateVerifier = async (
    verifier: Verifier,
    queryState: RoundsState[number]["queries"][number]["state"]
  ) => {
    if (speculateMode) {
      dispatch(roundsActions.updateQueryState({ index, verifier }));
      return;
    }
    if (queryState !== "unknown") {
      dispatch(
        roundsActions.setQueryState({
          index,
          verifier,
          queryState: "unknown",
        })
      );
      return;
    }

    const code = round.code.map(({ digit }) => digit);
    if (!code.every((digit) => digit !== null && digit >= 1 && digit <= 5)) {
      dispatch(
        alertActions.openAlert({
          message: "Enter a complete code before checking a verifier.",
          level: "warning",
        })
      );
      return;
    }
    const cryptCardId = comments
      .find((comment) => comment.verifier === verifier)
      ?.criteriaCards[0]?.cryptCard?.id;
    if (cryptCardId === undefined) {
      dispatch(
        alertActions.openAlert({
          message: "The verification card could not be identified.",
          level: "warning",
        })
      );
      return;
    }

    setPendingVerifier(verifier);
    try {
      const result = await evaluateVerifier(
        comments.flatMap(({ criteriaCards }) => criteriaCards),
        cryptCardId,
        code as number[]
      );
      if (result === null) {
        dispatch(
          alertActions.openAlert({
            message: "The verification card could not be identified.",
            level: "warning",
          })
        );
        return;
      }
      dispatch(
        roundsActions.setQueryState({
          index,
          verifier,
          queryState: result ? "solved" : "unsolved",
        })
      );
    } catch (error) {
      console.error("Verifier evaluation failed", error);
      dispatch(
        alertActions.openAlert({
          message: "The verifier result could not be calculated.",
          level: "error",
        })
      );
    } finally {
      setPendingVerifier(null);
    }
  };

  return (
    <Box>
      <Grid container spacing={0.5}>
        {round.code.map((code) => (
          <Grid key={code.shape} item xs={4}>
            <TextField
              prefixId={`rounds__round-${index + 1}-${code.shape}`}
              customRadius={
                code.shape !== "square"
                  ? code.shape === "triangle"
                    ? theme.spacing(2, 0, 0, 0)
                    : theme.spacing(0, 2, 0, 0)
                  : undefined
              }
              value={code.digit}
              onChange={(value) => {
                dispatch(alertActions.closeAlert());
                dispatch(
                  roundsActions.updateCodeDigit({
                    index,
                    shape: code.shape,
                    digit: value ? (Number(value) as Digit) : null,
                  })
                );
              }}
              icon={
                <ShapeIcon
                  shape={code.shape as "triangle" | "square" | "circle"}
                  sizeMultiplier={0.5}
                />
              }
              type="number"
            />
          </Grid>
        ))}
      </Grid>
      <Box mt={0.5}>
        <Grid container spacing={0.5}>
          {round.queries.map((query) => (
            <Grid item xs={2} key={query.verifier}>
              <Button
                id={`rounds__round-${
                  index + 1
                }-verifier-${query.verifier.toLowerCase()}-button`}
                aria-label={query.verifier}
                disabled={pendingVerifier === query.verifier}
                sx={{
                  minWidth: "100%",
                  p: 0,
                  background:
                    query.verifier === "E" || query.verifier === "F"
                      ? alpha(theme.palette.primary.main, 0.1)
                      : null,
                  borderRadius: theme.spacing(
                    0,
                    0,
                    query.verifier === "F" ? 2 : 0,
                    query.verifier === "A" ? 2 : 0
                  ),
                }}
                onClick={() => updateVerifier(query.verifier, query.state)}
              >
                <Box width={1}>
                  <Box
                    sx={{
                      textAlign: "center",
                      borderRadius:
                        query.verifier === "F"
                          ? theme.spacing(0, 0, 2, 0)
                          : null,
                    }}
                  >
                    <SingleCharLabel>{query.verifier}</SingleCharLabel>
                    <Box position="relative">
                      <Box>
                        {query.state === "unknown" && (
                          <BlankBoxIcon
                            sx={{ color: theme.palette.primary.main }}
                          />
                        )}
                        {query.state === "solved" && (
                          <SolvedIcon
                            sx={{ color: theme.palette.primary.dark }}
                          />
                        )}
                        {query.state === "unsolved" && (
                          <UnsolvedIcon
                            sx={{ color: theme.palette.secondary.dark }}
                          />
                        )}
                      </Box>
                    </Box>
                  </Box>
                </Box>
              </Button>
            </Grid>
          ))}
        </Grid>
      </Box>
      {round.isPristine && (
        <Box>
          <Box mt={2}>
            <Button
              id={`rounds__round-${index + 1}-undo-button`}
              aria-label="undo"
              color="secondary"
              fullWidth
              size="small"
              onClick={() => {
                dispatch(roundsActions.deleteRound(index));
              }}
            >
              <DeleteIcon />
            </Button>
          </Box>
        </Box>
      )}
      <Box my={2}>
        <Divider />
      </Box>
    </Box>
  );
};

export default Round;
