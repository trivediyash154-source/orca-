/**
 * A line-buffering NDJSON parser — Phase 9 brief §24: "a stream may produce a partial JSON
 * object in one chunk and the remainder in the next. Never call JSON.parse() on arbitrary
 * raw chunks." This class only ever calls `JSON.parse()` on a complete line (everything up
 * to and including a `\n`), buffering everything before that across as many `push()` calls
 * as it takes to arrive.
 */

export interface ParsedLine {
  readonly raw: string;
  readonly value: unknown;
}

export interface ParseFailure {
  readonly raw: string;
  readonly error: Error;
}

export class NdjsonLineParser {
  private buffer = '';

  /**
   * Appends `chunk` to the internal buffer and extracts every complete line now available.
   * Each complete line is parsed as JSON individually — one malformed line produces a
   * `ParseFailure` for that line only, never discards or corrupts any other line in the
   * same or a later chunk (brief §50: "multiple messages in one chunk," "malformed JSON"
   * must each be handled, not conflated).
   */
  push(chunk: string): Array<ParsedLine | ParseFailure> {
    this.buffer += chunk;
    const results: Array<ParsedLine | ParseFailure> = [];

    let newlineIndex: number;
    while ((newlineIndex = this.buffer.indexOf('\n')) >= 0) {
      const line = this.buffer.slice(0, newlineIndex);
      this.buffer = this.buffer.slice(newlineIndex + 1);
      const trimmed = line.trim();
      if (trimmed.length === 0) {
        continue; // a stray blank line is not a message, not an error
      }
      try {
        results.push({ raw: line, value: JSON.parse(trimmed) as unknown });
      } catch (err) {
        results.push({ raw: line, error: err instanceof Error ? err : new Error(String(err)) });
      }
    }
    return results;
  }

  /** Whatever has arrived since the last complete line, still awaiting its `\n`. Exposed
   * only for tests -- production code never needs to inspect a partial line. */
  pendingBuffer(): string {
    return this.buffer;
  }
}

export function isParseFailure(item: ParsedLine | ParseFailure): item is ParseFailure {
  return 'error' in item;
}
