import { PayloadAction, createSlice } from "@reduxjs/toolkit";

type Query = {
  verifier: Verifier;
  state: "solved" | "unsolved" | "unknown";
};

type Code = {
  shape: Shape;
  digit: Nullable<Digit>;
};

export type RoundsState = {
  code: Code[];
  queries: Query[];
  isPristine: boolean;
}[];

function createRound(isPristine: boolean): RoundsState[number] {
  return {
    code: (["triangle", "square", "circle"] as Shape[]).map((shape) => ({
      shape,
      digit: null,
    })),
    queries: (["A", "B", "C", "D", "E", "F"] as Verifier[]).map((verifier) => ({
      verifier,
      state: "unknown",
    })),
    isPristine,
  };
}

const initialState: RoundsState = [createRound(false)];

export const roundsSlice = createSlice({
  name: "rounds",
  initialState,
  reducers: {
    load: (_, action: PayloadAction<RoundsState>) => action.payload,
    reset: () => initialState,
    deleteRound: (state, action: PayloadAction<number>) => {
      state.splice(action.payload, 1);
    },
    addRound: (state) => {
      state.push(createRound(true));
    },
    updateCodeDigit: (
      state,
      action: PayloadAction<{
        index: number;
        shape: Shape;
        digit: Nullable<Digit>;
      }>
    ) => {
      const { index, shape, digit } = action.payload;
      const round = state[index];
      const code = round.code.find((code) => code.shape === shape)!;

      code.digit = digit;

      round.isPristine = false;

      state[index] = round;
    },
    updateQueryState: (
      state,
      action: PayloadAction<{ index: number; verifier: Verifier }>
    ) => {
      const { index, verifier } = action.payload;
      const round = state[index];
      const query = round.queries.find((query) => query.verifier === verifier)!;

      switch (query.state) {
        case "unknown":
          query.state = "unsolved";
          break;
        case "unsolved":
          query.state = "solved";
          break;
        case "solved":
          query.state = "unknown";
          break;
      }

      round.isPristine = false;

      state[index] = round;
    },
    recordSimulatedCheck: (
      state,
      action: PayloadAction<{
        code: Digit[];
        verifier: Verifier;
        startsNewRound: boolean;
        result: boolean;
      }>
    ) => {
      const { code, verifier, startsNewRound, result } = action.payload;
      let round = state[state.length - 1];
      const canUseLastRound =
        round &&
        round.code.every(({ digit }) => digit === null) &&
        round.queries.every((query) => query.state === "unknown");

      if (!round || (startsNewRound && !canUseLastRound)) {
        state.push(createRound(false));
        round = state[state.length - 1];
      }

      round.code.forEach((entry, index) => {
        entry.digit = code[index];
      });
      const query = round.queries.find((entry) => entry.verifier === verifier)!;
      query.state = result ? "solved" : "unsolved";
      round.isPristine = false;
    },
  },
});

export const roundsActions = roundsSlice.actions;

export default roundsSlice.reducer;
