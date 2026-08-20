# Reconcile number_to_int64's two-channel result with the error convention

`int64_t number_to_int64(number x, bool *ok)` reports failure through an
out-param `ok` and a 0 return, with the documented subtlety that "0 with
*ok true really is the value zero." Everywhere else the library signals "no
defined result" with the single sticky error value (`NUMBER_ERROR`,
`number_is_error`) — so `number_to_int64` is the one API that uses a
different failure convention, which is a small inconsistency a caller has to
remember.

This is a minor ergonomics/consistency note, not a bug — the current
signature is perfectly usable. The question is only whether the two-channel
form earns its divergence.

## Plan

Consider (and possibly reject) alternatives:

- Leave as-is. `int64_t` has no spare sentinel (every bit pattern is a valid
  value), so the out-param is a legitimate, common C idiom — this may simply
  be the right design and the todo closes as "won't change."
- Or add a sibling that fits the rest of the API's shape, e.g. one that
  returns the value boxed such that failure is `number_is_error`-detectable,
  for callers who'd rather branch on the library's usual error channel.

Whichever way it goes, make sure the *reason* (int64 has no sentinel) is
stated at the declaration, so the divergence reads as deliberate.

## Verify

- If a sibling is added: test the value-zero-vs-failure distinction it draws,
  and that it agrees with `number_to_int64` on every in-range integer.

## Depends on

Nothing. Lowest priority of the outstanding items.
