export function formatNumber(value, digits = 0) {
  if (value == null || isNaN(Number(value))) return "-";
  return Number(value).toLocaleString(undefined, {
    maximumFractionDigits: digits,
    minimumFractionDigits: digits
  });
}

export function formatCurrency(value, currency = "USD") {
  if (value == null || isNaN(Number(value))) return "-";
  return Number(value).toLocaleString(undefined, {
    style: "currency",
    currency,
    maximumFractionDigits: 2
  });
}

