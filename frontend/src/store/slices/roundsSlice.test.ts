import roundsReducer, { roundsActions } from "./roundsSlice";

describe("verifier result entry", () => {
  it("cycles green, red, and off in speculate mode", () => {
    let state = roundsReducer(
      undefined,
      roundsActions.updateQueryState({ index: 0, verifier: "A" })
    );
    expect(state[0].queries[0].state).toBe("solved");

    state = roundsReducer(
      state,
      roundsActions.updateQueryState({ index: 0, verifier: "A" })
    );
    expect(state[0].queries[0].state).toBe("unsolved");

    state = roundsReducer(
      state,
      roundsActions.updateQueryState({ index: 0, verifier: "A" })
    );
    expect(state[0].queries[0].state).toBe("unknown");
  });

  it("sets a calculated result directly", () => {
    const state = roundsReducer(
      undefined,
      roundsActions.setQueryState({
        index: 0,
        verifier: "B",
        queryState: "unsolved",
      })
    );
    expect(state[0].queries[1].state).toBe("unsolved");
  });
});

describe("simulated rounds", () => {
  it("records continued checks and starts new rounds", () => {
    let state = roundsReducer(
      undefined,
      roundsActions.recordSimulatedCheck({
        code: [1, 5, 4],
        verifier: "A",
        startsNewRound: true,
        result: false,
      })
    );
    expect(state).toHaveLength(1);
    expect(state[0].code.map(({ digit }) => digit)).toEqual([1, 5, 4]);
    expect(state[0].queries[0].state).toBe("unsolved");

    state = roundsReducer(
      state,
      roundsActions.recordSimulatedCheck({
        code: [1, 5, 4],
        verifier: "C",
        startsNewRound: false,
        result: true,
      })
    );
    expect(state).toHaveLength(1);
    expect(state[0].queries[2].state).toBe("solved");

    state = roundsReducer(
      state,
      roundsActions.recordSimulatedCheck({
        code: [2, 1, 4],
        verifier: "B",
        startsNewRound: true,
        result: true,
      })
    );
    expect(state).toHaveLength(2);
    expect(state[1].code.map(({ digit }) => digit)).toEqual([2, 1, 4]);
    expect(state[1].queries[1].state).toBe("solved");
  });
});
