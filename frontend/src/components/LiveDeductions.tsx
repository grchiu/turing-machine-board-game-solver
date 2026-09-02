import { solveCurrentState } from "deductions";
import { useAppDispatch } from "hooks/useAppDispatch";
import { useAppSelector } from "hooks/useAppSelector";
import { useEffect, useMemo } from "react";
import { store } from "store";
import { commentsActions } from "store/slices/commentsSlice";

type Props = {
  enabled: boolean;
};

export function LiveDeductions({ enabled }: Props) {
  const dispatch = useAppDispatch();
  const registrationStatus = useAppSelector(
    (state) => state.registration.status
  );
  const comments = useAppSelector((state) => state.comments);
  const rounds = useAppSelector((state) => state.rounds);

  const solverStateKey = useMemo(
    () =>
      JSON.stringify({
        cards: comments.map((comment) =>
          comment.criteriaCards.map((card) => card.id)
        ),
        nightmare: comments[0]?.nightmare || false,
        rounds,
      }),
    [comments, rounds]
  );

  useEffect(() => {
    if (
      !enabled ||
      registrationStatus !== "ready" ||
      comments.length === 0
    ) {
      return;
    }

    let cancelled = false;
    solveCurrentState(store.getState())
      .then((result) => {
        if (!cancelled) {
          dispatch(
            commentsActions.applyPossibleVerifiers(result.possibleVerifiers)
          );
        }
      })
      .catch((error) => {
        if (!cancelled) {
          console.error("Live deduction failed", error);
        }
      });

    return () => {
      cancelled = true;
    };
  }, [comments.length, dispatch, enabled, registrationStatus, solverStateKey]);

  return null;
}
