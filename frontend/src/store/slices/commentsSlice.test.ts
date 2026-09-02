import commentsReducer, { commentsActions } from "./commentsSlice";

describe("comments live deductions", () => {
  it("applies and restores possible verifier criteria", () => {
    const setup = commentsReducer(
      undefined,
      commentsActions.setCards({
        ind: [2, 5],
        crypt: [101, 102],
        color: 0,
        m: 0,
      })
    );

    const narrowed = commentsReducer(
      setup,
      commentsActions.applyPossibleVerifiers([[0, 2], [1]])
    );
    expect(narrowed[0].criteriaCards[0].irrelevantCriteria).toEqual([2]);
    expect(narrowed[1].criteriaCards[0].irrelevantCriteria).toEqual([1]);

    const restored = commentsReducer(
      narrowed,
      commentsActions.applyPossibleVerifiers([[0, 1, 2], [0, 1]])
    );
    expect(restored[0].criteriaCards[0].irrelevantCriteria).toEqual([]);
    expect(restored[1].criteriaCards[0].irrelevantCriteria).toEqual([]);
  });
});
