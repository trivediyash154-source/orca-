import { describe, expect, it } from 'vitest';

import {
  CODE_LENGTH,
  PASSWORD_MIN_LENGTH,
  firstErrorField,
  hasErrors,
  normalisePhone,
  validateCode,
  validatePhone,
  validateReset,
  validateSignIn,
  validateSignUp,
} from '../src/auth/validation';

/** The entry surface's validators (`src/auth/validation.ts`). They decide what a reader is told
 * about their own input, so what is pinned here is the DECISION, not the wording: which fields
 * are rejected, which are not, and — for sign-in specifically — that a password that is merely
 * short is never rejected locally, because doing so would lock an existing account out of a form
 * that cannot see it. Shape checking is all this build honestly has; none of these functions may
 * ever be presented as verification. */

const VALID_ADDRESSES: readonly string[] = [
  'jdoe@institution.edu',
  'a.b+tag@sub.domain.co.uk',
  'orca_operator@lab.example.org',
];

const INVALID_ADDRESSES: readonly string[] = [
  '',
  'jdoe',
  'jdoe@',
  '@institution.edu',
  'jdoe@institution',
  'jdoe@institution.e',
  'j doe@institution.edu',
  'jdoe@@institution.edu',
];

describe('the rules themselves', () => {
  it('holds the sign-up minimum at twelve characters, not the conventional eight', () => {
    // A deliberate departure: length is the only property this form can enforce that correlates
    // with resistance to offline cracking. Lowering it is a policy change, not a refactor.
    expect(PASSWORD_MIN_LENGTH).toBe(12);
  });

  it('holds the verification code at six digits', () => {
    expect(CODE_LENGTH).toBe(6);
  });
});

describe('email shape (sign-in, sign-up and reset share one rule)', () => {
  it('accepts addresses that genuinely deliver', () => {
    for (const email of VALID_ADDRESSES) {
      expect(validateSignIn({ email, password: 'x' }).email).toBeUndefined();
      expect(validateReset({ email }).email).toBeUndefined();
    }
  });

  it('rejects an address with no @, no domain or no usable TLD', () => {
    for (const email of INVALID_ADDRESSES) {
      expect(validateReset({ email }).email).toBeDefined();
    }
  });

  it('trims before judging, so a pasted address with padding is not a failure', () => {
    expect(validateReset({ email: '  jdoe@institution.edu  ' }).email).toBeUndefined();
  });

  it('names the repair rather than saying "invalid"', () => {
    expect(validateReset({ email: '' }).email).toBe('Enter an email address.');
    expect(validateReset({ email: 'jdoe' }).email).toMatch(/@ and a domain/);
  });
});

describe('validateSignIn', () => {
  it('passes a complete pair', () => {
    expect(validateSignIn({ email: 'jdoe@institution.edu', password: 'short' })).toEqual({});
  });

  it('reports a missing password', () => {
    expect(validateSignIn({ email: 'jdoe@institution.edu', password: '' }).password).toBe(
      'Enter your password.',
    );
  });

  it('never applies the length rule on sign-in -- the account already exists', () => {
    const errors = validateSignIn({ email: 'jdoe@institution.edu', password: 'a' });
    expect(errors.password).toBeUndefined();
    expect(hasErrors(errors)).toBe(false);
  });
});

describe('validateSignUp', () => {
  const base = {
    email: 'jdoe@institution.edu',
    password: 'correct horse battery',
    confirm: 'correct horse battery',
    organisation: 'Orbital Dynamics Lab',
  } as const;

  it('passes a complete, consistent form', () => {
    expect(validateSignUp(base)).toEqual({});
  });

  it(`rejects a password one character below the ${PASSWORD_MIN_LENGTH}-character rule`, () => {
    const short = 'a'.repeat(PASSWORD_MIN_LENGTH - 1);
    const errors = validateSignUp({ ...base, password: short, confirm: short });
    expect(errors.password).toBeDefined();
    // The count is quoted back, so the reader is not left counting characters themselves.
    expect(errors.password).toContain(String(PASSWORD_MIN_LENGTH - 1));
  });

  it(`accepts a password of exactly ${PASSWORD_MIN_LENGTH} characters (boundary is inclusive)`, () => {
    const exact = 'a'.repeat(PASSWORD_MIN_LENGTH);
    expect(validateSignUp({ ...base, password: exact, confirm: exact }).password).toBeUndefined();
  });

  it('reports an empty password as a choice to make, not as a length violation', () => {
    const errors = validateSignUp({ ...base, password: '', confirm: '' });
    expect(errors.password).toBe(
      `Choose a password of at least ${PASSWORD_MIN_LENGTH} characters.`,
    );
  });

  it('rejects a confirmation that does not match, on the confirm field', () => {
    const errors = validateSignUp({ ...base, confirm: 'correct horse batteries' });
    expect(errors.confirm).toBe('The two passwords do not match.');
    expect(errors.password).toBeUndefined();
  });

  it('distinguishes an empty confirmation from a mismatched one', () => {
    expect(validateSignUp({ ...base, confirm: '' }).confirm).toBe(
      'Re-enter the password to confirm it.',
    );
  });

  it('requires an organisation of at least two characters, whitespace not counted', () => {
    expect(validateSignUp({ ...base, organisation: '   ' }).organisation).toBeDefined();
    expect(validateSignUp({ ...base, organisation: 'X' }).organisation).toBe(
      'Enter at least two characters.',
    );
    expect(validateSignUp({ ...base, organisation: ' MIT ' }).organisation).toBeUndefined();
  });

  it('reports every wrong field at once rather than one at a time', () => {
    const errors = validateSignUp({ email: 'jdoe', password: 'a', confirm: 'b', organisation: '' });
    expect(Object.keys(errors).sort()).toEqual(['confirm', 'email', 'organisation', 'password']);
  });
});

describe('normalisePhone / validatePhone', () => {
  it('treats the separators people actually type as absent', () => {
    expect(normalisePhone('+1 (415) 555-0123')).toBe('+14155550123');
    expect(normalisePhone('+44 20 7946 0958')).toBe('+442079460958');
    expect(normalisePhone('+1.415.555.0123')).toBe('+14155550123');
  });

  it('accepts the same number written five different ways', () => {
    for (const phone of [
      '+14155550123',
      '+1 415 555 0123',
      '+1 (415) 555-0123',
      '+1-415-555-0123',
      ' +1.415.555.0123 ',
    ]) {
      expect(validatePhone({ phone }).phone).toBeUndefined();
    }
  });

  it('requires the country code, and says so', () => {
    expect(validatePhone({ phone: '4155550123' }).phone).toMatch(/Start with \+/);
  });

  it('reports an empty number as empty rather than as malformed', () => {
    expect(validatePhone({ phone: '' }).phone).toBe(
      'Enter a phone number, starting with the country code.',
    );
    // Separators alone are not a number.
    expect(validatePhone({ phone: ' - () ' }).phone).toBe(
      'Enter a phone number, starting with the country code.',
    );
  });

  it('holds the E.164 bounds: 8 to 15 digits, no letters', () => {
    expect(validatePhone({ phone: `+${'1'.repeat(7)}` }).phone).toBeDefined();
    expect(validatePhone({ phone: `+${'1'.repeat(8)}` }).phone).toBeUndefined();
    expect(validatePhone({ phone: `+${'1'.repeat(15)}` }).phone).toBeUndefined();
    expect(validatePhone({ phone: `+${'1'.repeat(16)}` }).phone).toBeDefined();
    expect(validatePhone({ phone: '+1415555O123' }).phone).toMatch(/no letters/);
  });
});

describe('validateCode', () => {
  it(`accepts exactly ${CODE_LENGTH} digits`, () => {
    expect(validateCode({ code: '0'.repeat(CODE_LENGTH) })).toEqual({});
    expect(validateCode({ code: ` ${'7'.repeat(CODE_LENGTH)} ` })).toEqual({});
  });

  it('rejects a code of the wrong length and quotes the length it got', () => {
    const short = '1'.repeat(CODE_LENGTH - 1);
    expect(validateCode({ code: short }).code).toContain(String(CODE_LENGTH - 1));
    expect(validateCode({ code: '1'.repeat(CODE_LENGTH + 1) }).code).toBeDefined();
  });

  it('rejects non-digits before it complains about length', () => {
    expect(validateCode({ code: 'abcdef' }).code).toMatch(/no letters or spaces/);
    expect(validateCode({ code: '12 34 56' }).code).toMatch(/no letters or spaces/);
  });

  it('reports an empty code as empty', () => {
    expect(validateCode({ code: '   ' }).code).toBe(`Enter the ${CODE_LENGTH}-digit code.`);
  });
});

describe('hasErrors / firstErrorField', () => {
  it('reports an empty result as no errors', () => {
    expect(hasErrors({})).toBe(false);
    expect(hasErrors({ email: 'Enter an email address.' })).toBe(true);
  });

  it("returns the first error in the CALLER's visual order, not object order", () => {
    const errors = { password: 'p', email: 'e' } as const;
    expect(firstErrorField(errors, ['email', 'password'])).toBe('email');
    expect(firstErrorField(errors, ['password', 'email'])).toBe('password');
  });

  it('ignores fields that are not in the order it was given', () => {
    expect(firstErrorField({ organisation: 'o' }, ['email', 'password'])).toBeNull();
    expect(firstErrorField({}, ['email'])).toBeNull();
  });
});
