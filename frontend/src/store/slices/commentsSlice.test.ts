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

  it("applies possible Nightmare letter assignments", () => {
    const setup = commentsReducer(
      undefined,
      commentsActions.setCards({
        ind: [2, 16, 23, 48],
        crypt: [330, 509, 754, 221],
        color: 3,
        m: 2,
      })
    );

    const narrowed = commentsReducer(
      setup,
      commentsActions.applyPossibleLetters([
        ["D"],
        ["B"],
        ["A"],
        ["C"],
      ])
    );

    expect(
      narrowed.map((comment) =>
        comment.letters
          .filter((letter) => !letter.isIrrelevant)
          .map((letter) => letter.letter)
      )
    ).toEqual([["D"], ["B"], ["A"], ["C"]]);
  });
});
