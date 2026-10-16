import { describe, expect, it } from 'vitest';

import { isParseFailure, NdjsonLineParser } from '../src/ipc/ndjsonParser.js';

describe('NdjsonLineParser', () => {
  it('parses a single complete line delivered in one chunk', () => {
    const parser = new NdjsonLineParser();
    const results = parser.push('{"type":"ready"}\n');
    expect(results).toHaveLength(1);
    expect(isParseFailure(results[0]!)).toBe(false);
    expect(results[0]).toEqual({ raw: '{"type":"ready"}', value: { type: 'ready' } });
  });

  it('buffers a partial line across two chunks and only emits once complete', () => {
    const parser = new NdjsonLineParser();
    const first = parser.push('{"type":"snap');
    expect(first).toHaveLength(0);
    expect(parser.pendingBuffer()).toBe('{"type":"snap');

    const second = parser.push('shot","simulationTimeSec":30}\n');
    expect(second).toHaveLength(1);
    expect(second[0]).toEqual({
      raw: '{"type":"snapshot","simulationTimeSec":30}',
      value: { type: 'snapshot', simulationTimeSec: 30 },
    });
    expect(parser.pendingBuffer()).toBe('');
  });

  it('handles a partial line split across three chunks, byte by byte at the boundary', () => {
    const parser = new NdjsonLineParser();
    const whole = '{"a":1}\n';
    let all: ReturnType<NdjsonLineParser['push']> = [];
    for (const ch of whole) {
      all = all.concat(parser.push(ch));
    }
    expect(all).toHaveLength(1);
    expect(all[0]).toEqual({ raw: '{"a":1}', value: { a: 1 } });
  });

  it('extracts multiple complete messages delivered in a single chunk', () => {
    const parser = new NdjsonLineParser();
    const results = parser.push('{"a":1}\n{"a":2}\n{"a":3}\n');
    expect(results).toHaveLength(3);
    expect(results.map((r) => (r as { value: { a: number } }).value.a)).toEqual([1, 2, 3]);
  });

  it('does not lose a trailing partial message after emitting complete ones in the same chunk', () => {
    const parser = new NdjsonLineParser();
    const results = parser.push('{"a":1}\n{"a":2');
    expect(results).toHaveLength(1);
    expect(parser.pendingBuffer()).toBe('{"a":2');
    const rest = parser.push('}\n');
    expect(rest).toHaveLength(1);
    expect((rest[0] as { value: { a: number } }).value.a).toBe(2);
  });

  it('ignores blank lines without producing a result', () => {
    const parser = new NdjsonLineParser();
    const results = parser.push('\n\n{"a":1}\n\n');
    expect(results).toHaveLength(1);
  });

  it('reports one malformed line as a ParseFailure without discarding neighboring valid lines', () => {
    const parser = new NdjsonLineParser();
    const results = parser.push('{"a":1}\nnot json at all\n{"a":2}\n');
    expect(results).toHaveLength(3);
    expect(isParseFailure(results[0]!)).toBe(false);
    expect(isParseFailure(results[1]!)).toBe(true);
    expect(isParseFailure(results[2]!)).toBe(false);
    if (isParseFailure(results[1]!)) {
      expect(results[1].error).toBeInstanceOf(Error);
      expect(results[1].raw).toBe('not json at all');
    }
  });

  it('a malformed line does not corrupt buffering for subsequent chunks', () => {
    const parser = new NdjsonLineParser();
    parser.push('garbage{{{\n');
    const results = parser.push('{"ok":true}\n');
    expect(results).toHaveLength(1);
    expect(isParseFailure(results[0]!)).toBe(false);
  });

  it('never calls JSON.parse on an incomplete line', () => {
    const parser = new NdjsonLineParser();
    // A string that would throw if parsed as-is, but never gets a newline -- must never
    // surface as a ParseFailure, since it was never a complete message.
    const results = parser.push('{"incomplete":');
    expect(results).toHaveLength(0);
  });
});
