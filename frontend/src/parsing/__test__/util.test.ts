import { getCriteriaIndexForCryptCard } from "parsing/util";

test("maps verification cards to criteria indices", () => {
  expect(getCriteriaIndexForCryptCard(16, 503)).toBe(1);
  expect(getCriteriaIndexForCryptCard(21, 339)).toBe(1);
  expect(getCriteriaIndexForCryptCard(25, 746)).toBe(1);
  expect(getCriteriaIndexForCryptCard(33, 654)).toBe(5);
  expect(getCriteriaIndexForCryptCard(34, 475)).toBe(2);
  expect(getCriteriaIndexForCryptCard(40, 434)).toBe(2);
});

test("returns null for unrelated cards", () => {
  expect(getCriteriaIndexForCryptCard(16, 339)).toBeNull();
  expect(getCriteriaIndexForCryptCard(99, 503)).toBeNull();
});
