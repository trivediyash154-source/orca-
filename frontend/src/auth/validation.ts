/**
 * Form validation for the entry surface. Pure functions, no React and no DOM: the code that
 * decides whether a field is wrong can be called from a test with a string, and the component
 * layer is left with nothing to decide except where to put the message.
 *
 * Every message names the specific defect and the specific repair. "Invalid input" tells the
 * reader that something is wrong and nothing about what to do next, which on an account form is
 * the difference between a corrected typo and an abandoned session.
 *
 * Nothing here consults the network. Whether an address exists, whether a password is the right
 * one and whether a number can receive a message are all questions only a provider can answer,
 * and this repository has no provider (see `services/authService.ts`). These functions therefore
 * check shape and nothing more, and must never be presented as verification.
 */

export interface FieldErrors {
  readonly email?: string;
  readonly password?: string;
  readonly confirm?: string;
  readonly organisation?: string;
  readonly phone?: string;
  readonly code?: string;
}

export type FieldName = keyof FieldErrors;

export interface SignInValues {
  readonly email: string;
  readonly password: string;
}

export interface SignUpValues {
  readonly email: string;
  readonly password: string;
  readonly confirm: string;
  readonly organisation: string;
}

export interface ResetValues {
  readonly email: string;
}

export interface PhoneValues {
  readonly phone: string;
}

export interface CodeValues {
  readonly code: string;
}

/** Optional-and-mutable counterpart to `FieldErrors`, so a validator can assemble its result
 * field by field and still hand back the readonly type. */
type Draft = { -readonly [K in FieldName]?: string };

/**
 * Pragmatic, not RFC 5322: one `@`, no whitespace either side, and a domain carrying a dot with
 * at least two characters after it. A maximal regex rejects addresses that genuinely deliver,
 * and the only authority on deliverability is the receiving mail server -- which this build
 * never contacts. Shape checking is all that is honestly available here.
 */
const EMAIL_PATTERN = /^[^\s@]+@[^\s@]+\.[^\s@]{2,}$/;

/**
 * Twelve rather than eight. Length is the only property of a password this form can enforce that
 * actually correlates with resistance to offline cracking; a character-class rule of the
 * "one symbol, one digit" kind mostly produces `Password1!` and a reused credential.
 */
export const PASSWORD_MIN_LENGTH = 12;

export const PASSWORD_HINT =
  `At least ${PASSWORD_MIN_LENGTH} characters. A short phrase you can remember is stronger ` +
  'than a short string you cannot.';

export const PHONE_HINT =
  'International format, starting with the country code — for example +14155550123.';

/** Separators people actually type when writing a number down. Stripped before matching so that
 * `+1 (415) 555-0123` and `+14155550123` are the same number, which they are. */
const PHONE_SEPARATORS = /[\s\-().]/g;

/** E.164 allows up to 15 digits including the country code; below 8 no assignable number
 * exists, so anything shorter is a typo rather than a short number. */
const PHONE_PATTERN = /^\+\d{8,15}$/;

export const CODE_LENGTH = 6;

const DIGITS_ONLY = /^\d+$/;

/** The canonical form handed to the auth adapter, so a provider never has to parse presentation
 * whitespace out of a number it is expected to dial. */
export function normalisePhone(raw: string): string {
  return raw.replace(PHONE_SEPARATORS, '');
}

function emailError(raw: string): string | undefined {
  const value = raw.trim();
  if (value.length === 0) return 'Enter an email address.';
  if (!EMAIL_PATTERN.test(value)) {
    return 'Enter a complete address with an @ and a domain — for example jdoe@institution.edu.';
  }
  return undefined;
}

export function validateSignIn(values: SignInValues): FieldErrors {
  const errors: Draft = {};

  const email = emailError(values.email);
  if (email !== undefined) errors.email = email;

  // No length rule on sign-in. The password already exists; rejecting it locally for being
  // shorter than today's policy would lock out an account this form cannot see.
  if (values.password.length === 0) errors.password = 'Enter your password.';

  return errors;
}

export function validateSignUp(values: SignUpValues): FieldErrors {
  const errors: Draft = {};

  const email = emailError(values.email);
  if (email !== undefined) errors.email = email;

  if (values.password.length === 0) {
    errors.password = `Choose a password of at least ${PASSWORD_MIN_LENGTH} characters.`;
  } else if (values.password.length < PASSWORD_MIN_LENGTH) {
    // The count is quoted back because "too short" leaves the reader counting characters.
    errors.password =
      `Use at least ${PASSWORD_MIN_LENGTH} characters. This one has ${values.password.length}.`;
  }

  if (values.confirm.length === 0) {
    errors.confirm = 'Re-enter the password to confirm it.';
  } else if (values.confirm !== values.password) {
    errors.confirm = 'The two passwords do not match.';
  }

  const organisation = values.organisation.trim();
  if (organisation.length === 0) {
    errors.organisation = 'Enter the institution, lab or company you are working with.';
  } else if (organisation.length < 2) {
    errors.organisation = 'Enter at least two characters.';
  }

  return errors;
}

export function validateReset(values: ResetValues): FieldErrors {
  const errors: Draft = {};

  const email = emailError(values.email);
  if (email !== undefined) errors.email = email;

  return errors;
}

export function validatePhone(values: PhoneValues): FieldErrors {
  const errors: Draft = {};
  const value = normalisePhone(values.phone);

  if (value.length === 0) {
    errors.phone = 'Enter a phone number, starting with the country code.';
  } else if (!value.startsWith('+')) {
    errors.phone = 'Start with + and the country code — for example +14155550123.';
  } else if (!PHONE_PATTERN.test(value)) {
    errors.phone = 'Use + followed by 8 to 15 digits, with no letters.';
  }

  return errors;
}

export function validateCode(values: CodeValues): FieldErrors {
  const errors: Draft = {};
  const value = values.code.trim();

  if (value.length === 0) {
    errors.code = `Enter the ${CODE_LENGTH}-digit code.`;
  } else if (!DIGITS_ONLY.test(value)) {
    errors.code = `The code is ${CODE_LENGTH} digits, with no letters or spaces.`;
  } else if (value.length !== CODE_LENGTH) {
    errors.code = `The code is ${CODE_LENGTH} digits. This one has ${value.length}.`;
  }

  return errors;
}

export function hasErrors(errors: FieldErrors): boolean {
  return Object.keys(errors).length > 0;
}

/**
 * First error in the caller's own visual order, which is what "move focus to the first invalid
 * field" means to a reader: the topmost one on screen, not the first key an object literal
 * happens to enumerate.
 */
export function firstErrorField(
  errors: FieldErrors,
  order: readonly FieldName[],
): FieldName | null {
  for (const name of order) {
    if (errors[name] !== undefined) return name;
  }
  return null;
}
