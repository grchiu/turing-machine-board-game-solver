import { configureStore } from "@reduxjs/toolkit";
import { fireEvent, render, screen, waitFor } from "@testing-library/react";
import { evaluateVerifier } from "deductions";
import { Provider, useSelector } from "react-redux";
import alert from "store/slices/alertSlice";
import comments, { commentsActions } from "store/slices/commentsSlice";
import rounds, { RoundsState } from "store/slices/roundsSlice";
import Round from "./Round";

jest.mock("deductions", () => ({
  evaluateVerifier: jest.fn(),
}));

const mockedEvaluateVerifier = evaluateVerifier as jest.MockedFunction<
  typeof evaluateVerifier
>;

function FirstRound() {
  const round = useSelector(
    (state: { rounds: RoundsState }) => state.rounds[0]
  );
  return <Round round={round} index={0} speculateMode={false} />;
}

describe("calculated verifier entry", () => {
  it("recovers after a verifier is clicked before its code is entered", async () => {
    mockedEvaluateVerifier.mockResolvedValue(true);
    const testStore = configureStore({
      reducer: { alert, comments, rounds },
    });
    testStore.dispatch(
      commentsActions.setCards({
        ind: [2],
        crypt: [330],
        color: 0,
        m: 0,
      })
    );

    render(
      <Provider store={testStore}>
        <FirstRound />
      </Provider>
    );

    fireEvent.click(screen.getByRole("button", { name: "A" }));
    expect(testStore.getState().alert.open).toBe(true);
    expect(mockedEvaluateVerifier).not.toHaveBeenCalled();

    for (const input of screen.getAllByRole("spinbutton")) {
      fireEvent.change(input, { target: { value: "1" } });
    }
    expect(testStore.getState().alert.open).toBe(false);

    fireEvent.click(screen.getByRole("button", { name: "A" }));
    await waitFor(() => expect(mockedEvaluateVerifier).toHaveBeenCalledTimes(1));
    await waitFor(() =>
      expect(testStore.getState().rounds[0].queries[0].state).toBe("solved")
    );
  });
});
