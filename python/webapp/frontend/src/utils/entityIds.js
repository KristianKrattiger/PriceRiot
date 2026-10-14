/** Match node/edge/register ids when JSON or UI mixes number vs string. */
export function sameEntityId(a, b) {
  return a === b || String(a) === String(b);
}
