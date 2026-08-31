import type { DecimalString } from "../api/types";

export function formatNanoseconds(value: DecimalString): string {
  const nanoseconds = BigInt(value);
  const negative = nanoseconds < 0n;
  const magnitude = negative ? -nanoseconds : nanoseconds;
  const wholeSeconds = magnitude / 1_000_000_000n;
  const remainder = magnitude % 1_000_000_000n;
  const fraction = remainder === 0n
    ? ""
    : `.${remainder.toString().padStart(9, "0").replace(/0+$/, "")}`;
  return `${negative ? "-" : ""}${wholeSeconds}${fraction} s (${value} ns)`;
}

export function formatFrequency(hertz: number): string {
  if (Math.abs(hertz) >= 1_000) return `${hertz} Hz (${hertz / 1_000} kHz)`;
  return `${hertz} Hz`;
}

export function formatBer(value: number | null): string {
  return value === null ? "NotEvaluated" : `${value}（无量纲）`;
}
