import { useEffect, useId, useMemo, useRef, useState } from 'react';
import type { FormEvent, MouseEvent, RefObject } from 'react';

import { ActionButton, StatusDot, ToastStack, useToasts } from '../marketing/components';
import { useRoute } from '../routes/RouteContext';
import { AuthNotConfiguredError, isAuthConfigured, runAuth } from '../services/authService';
import type { AuthProvider, AuthResult } from '../services/authService';
import { PasswordField } from './PasswordField';
import { SocialAuthButtons } from './SocialAuthButtons';
import {
  CODE_LENGTH,
  PASSWORD_HINT,
  PHONE_HINT,
  firstErrorField,
  hasErrors,
  normalisePhone,
  validateCode,
  validatePhone,
  validateReset,
  validateSignIn,
  validateSignUp,
} from './validation';
import type { FieldErrors, FieldName } from './validation';

/**
 * The account form: two modes (sign in / create account) and three sub-flows within each
 * (email, phone verification, password reset), all driven from one state machine so that a
 * reader can walk every branch without the page reloading or the URL lying about where they are.
 *
 * The central design constraint is honesty. This repository ships no identity provider, so the
 * form validates locally, calls `runAuth()` and renders whatever comes back -- which here is an
 * `AuthNotConfiguredError` carrying the service's own explanation. It is presented as an
 * informational integration state, never as a failure the reader caused and never as a success.
 * The same code path renders a real result the moment `configureAuth()` is called at bootstrap,
 * which is why all three outcomes (success, provider error, not-configured) are handled rather
 * than only the one this build can produce.
 */
export interface AuthFormProps {
  readonly mode: 'signin' | 'signup';
}

type Flow = 'email' | 'phone' | 'reset';
type PhoneStep = 'number' | 'code';
type PendingTarget = 'form' | AuthProvider;
type OutcomeKind = 'info' | 'error' | 'success';

interface Outcome {
  readonly kind: OutcomeKind;
  readonly title: string;
  readonly body: string;
}

interface Values {
  readonly email: string;
  readonly password: string;
  readonly confirm: string;
  readonly organisation: string;
  readonly phone: string;
  readonly code: string;
}

interface RequestSpec {
  readonly target: PendingTarget;
  /** Only reachable with a real adapter attached; kept per-operation so a wired deployment does
   * not inherit a generic "OK" for six different outcomes. */
  readonly successTitle: string;
  readonly task: () => Promise<AuthResult>;
  readonly onSettled?: (kind: OutcomeKind) => void;
}

/** Which validator applies is a function of the mode and the sub-flow, and of nothing else.
 * Keeping the mapping module-level and pure means the re-validation effect below can depend on
 * it without a stale closure. */
function validateFor(
  mode: AuthFormProps['mode'],
  flow: Flow,
  phoneStep: PhoneStep,
  values: Values,
): FieldErrors {
  if (flow === 'reset') return validateReset(values);
  if (flow === 'phone') return phoneStep === 'code' ? validateCode(values) : validatePhone(values);
  if (mode === 'signup') return validateSignUp(values);
  return validateSignIn(values);
}

/** Visual order, which is the order "the first invalid field" means to the reader. It must be
 * kept in step with the JSX below; a mismatch sends focus to a field further down the column. */
function fieldOrderFor(
  mode: AuthFormProps['mode'],
  flow: Flow,
  phoneStep: PhoneStep,
): readonly FieldName[] {
  if (flow === 'reset') return ['email'];
  if (flow === 'phone') return phoneStep === 'code' ? ['code'] : ['phone'];
  if (mode === 'signup') return ['email', 'organisation', 'password', 'confirm'];
  return ['email', 'password'];
}

interface FieldProps {
  readonly id: string;
  readonly label: string;
  readonly type: 'email' | 'text' | 'tel';
  readonly value: string;
  readonly onChange: (next: string) => void;
  readonly autoComplete: string;
  readonly inputRef: RefObject<HTMLInputElement>;
  readonly error?: string;
  readonly hint?: string;
  readonly inputMode?: 'email' | 'tel' | 'numeric';
  readonly maxLength?: number;
}

/** The non-password field. Its whole job is to keep the label / input / hint / error wiring
 * identical everywhere, because `aria-describedby` assembled by hand at six call sites is
 * `aria-describedby` that is wrong at one of them. */
function Field({
  id,
  label,
  type,
  value,
  onChange,
  autoComplete,
  inputRef,
  error,
  hint,
  inputMode,
  maxLength,
}: FieldProps) {
  const errorId = `${id}-error`;
  const hintId = `${id}-hint`;
  const describedBy = [hint ? hintId : null, error ? errorId : null]
    .filter((entry): entry is string => entry !== null)
    .join(' ');

  return (
    <div className="mk-auth-field">
      <label className="mk-auth-field__label" htmlFor={id}>
        {label}
      </label>
      <input
        id={id}
        ref={inputRef}
        className="mk-auth-field__input"
        type={type}
        value={value}
        onChange={(event) => onChange(event.currentTarget.value)}
        autoComplete={autoComplete}
        autoCapitalize="none"
        autoCorrect="off"
        spellCheck={false}
        inputMode={inputMode}
        maxLength={maxLength}
        aria-invalid={error ? true : undefined}
        aria-describedby={describedBy.length > 0 ? describedBy : undefined}
      />
      {hint ? (
        <p className="mk-auth-field__hint" id={hintId}>
          {hint}
        </p>
      ) : null}
      {error ? (
        <p className="mk-auth-field__error" id={errorId}>
          {error}
        </p>
      ) : null}
    </div>
  );
}

/** Cmd/Ctrl/Shift-click and middle-click belong to the browser: intercepting them would break
 * "open in a new tab" on what is, in the markup, a real link to a real URL. */
function isModifiedClick(event: MouseEvent<HTMLAnchorElement>): boolean {
  return event.metaKey || event.ctrlKey || event.shiftKey || event.altKey || event.button !== 0;
}

export function AuthForm({ mode }: AuthFormProps) {
  const { navigate } = useRoute();
  const { toasts, push, dismiss } = useToasts();
  const uid = useId();

  const [flow, setFlow] = useState<Flow>('email');
  const [phoneStep, setPhoneStep] = useState<PhoneStep>('number');
  const [pending, setPending] = useState<PendingTarget | null>(null);
  const [outcome, setOutcome] = useState<Outcome | null>(null);
  const [errors, setErrors] = useState<FieldErrors>({});
  const [notice, setNotice] = useState<'terms' | 'privacy' | null>(null);

  /** Live re-validation is armed only by a failed submit. Validating every keystroke from the
   * first character tells someone their email is invalid while they are still typing the local
   * part, which is hostile; saying nothing after they have already been told once is worse. */
  const [revalidate, setRevalidate] = useState(false);

  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [confirm, setConfirm] = useState('');
  const [organisation, setOrganisation] = useState('');
  const [phone, setPhone] = useState('');
  const [code, setCode] = useState('');

  const headingRef = useRef<HTMLHeadingElement>(null);
  const emailRef = useRef<HTMLInputElement>(null);
  const passwordRef = useRef<HTMLInputElement>(null);
  const confirmRef = useRef<HTMLInputElement>(null);
  const organisationRef = useRef<HTMLInputElement>(null);
  const phoneRef = useRef<HTMLInputElement>(null);
  const codeRef = useRef<HTMLInputElement>(null);
  const submitWrapRef = useRef<HTMLDivElement>(null);
  const dialogRef = useRef<HTMLDialogElement>(null);

  /** Monotonic request counter. A response is applied only if it belongs to the newest request:
   * switching tab, changing sub-flow or submitting again while one is in flight must not be
   * overwritten by whatever the abandoned attempt eventually returns. */
  const requestVersionRef = useRef(0);
  const mountedRef = useRef(true);
  const restoreFocusRef = useRef(false);

  const values = useMemo<Values>(
    () => ({ email, password, confirm, organisation, phone, code }),
    [email, password, confirm, organisation, phone, code],
  );

  const authConfigured = isAuthConfigured();
  const isSignUp = mode === 'signup';
  const busy = pending !== null;

  useEffect(() => {
    mountedRef.current = true;
    return () => {
      mountedRef.current = false;
    };
  }, []);

  // A mode change is a new view of the same page. Focus is moved to the heading because nothing
  // else tells a screen-reader user that the form they were on has been replaced; `preventScroll`
  // keeps the console column from jumping under a mouse user who never left the viewport.
  useEffect(() => {
    requestVersionRef.current += 1;
    setPending(null);
    setFlow('email');
    setPhoneStep('number');
    setErrors({});
    setRevalidate(false);
    setOutcome(null);
    // The address survives the switch -- someone who mistyped their way into the wrong tab should
    // not have to retype it. Secrets do not: they belong to the attempt that was abandoned.
    setPassword('');
    setConfirm('');
    setCode('');
    headingRef.current?.focus({ preventScroll: true });
  }, [mode]);

  useEffect(() => {
    if (!revalidate) return;
    setErrors(validateFor(mode, flow, phoneStep, values));
  }, [revalidate, mode, flow, phoneStep, values]);

  // Focus restoration after a form request settles. The busy state disables the fieldset, which
  // drops focus to the document body; this puts it back on the control that was pressed, but
  // only if nothing else has claimed it in the meantime.
  useEffect(() => {
    if (pending !== null || !restoreFocusRef.current) return;
    restoreFocusRef.current = false;
    const active = document.activeElement;
    if (active !== null && active !== document.body) return;
    submitWrapRef.current?.querySelector('button')?.focus();
  }, [pending]);

  // Declared after the restoration effect so that on the transition into the code step this one
  // runs last and wins: the code field is where the reader has to act next.
  useEffect(() => {
    if (phoneStep !== 'code') return;
    codeRef.current?.focus({ preventScroll: true });
  }, [phoneStep]);

  // `showModal()` rather than the `open` attribute, so the notice gets the top layer, the
  // backdrop and the focus trap the platform already implements. The fallback exists for
  // environments without dialog support (jsdom in the test suite), where an inline panel is
  // still better than a button that does nothing.
  useEffect(() => {
    const dialog = dialogRef.current;
    if (dialog === null) return;

    if (notice === null) {
      if (dialog.open) dialog.close();
      return;
    }
    if (dialog.open) return;

    if (typeof dialog.showModal === 'function') {
      try {
        dialog.showModal();
        return;
      } catch {
        // Already open in another mode, or unsupported: fall through to the inline form.
      }
    }
    dialog.open = true;
  }, [notice]);

  function refFor(name: FieldName): RefObject<HTMLInputElement> | null {
    switch (name) {
      case 'email':
        return emailRef;
      case 'password':
        return passwordRef;
      case 'confirm':
        return confirmRef;
      case 'organisation':
        return organisationRef;
      case 'phone':
        return phoneRef;
      case 'code':
        return codeRef;
      default:
        return null;
    }
  }

  function goToFlow(next: Flow): void {
    // Abandons any in-flight request for the flow being left, so its response cannot be
    // rendered against the fields of the flow being entered.
    requestVersionRef.current += 1;
    setPending(null);
    setFlow(next);
    setPhoneStep('number');
    setErrors({});
    setRevalidate(false);
    setOutcome(null);
  }

  async function execute(spec: RequestSpec): Promise<void> {
    const version = requestVersionRef.current + 1;
    requestVersionRef.current = version;
    if (spec.target === 'form') restoreFocusRef.current = true;
    setPending(spec.target);
    setOutcome(null);

    let next: Outcome;
    let redirectTo: string | undefined;

    try {
      const result = await spec.task();
      next = { kind: 'success', title: spec.successTitle, body: result.message };
      redirectTo = result.redirectTo;
    } catch (error) {
      if (error instanceof AuthNotConfiguredError) {
        next = { kind: 'info', title: 'Integration required', body: error.message };
      } else {
        next = {
          kind: 'error',
          title: 'Request failed',
          body:
            error instanceof Error && error.message.length > 0
              ? error.message
              : 'The request did not complete. Nothing was changed.',
        };
      }
    }

    if (!mountedRef.current || requestVersionRef.current !== version) return;

    setPending(null);
    setOutcome(next);
    // `OutcomeKind` and the toast's tone are the same three states by construction; if they
    // ever diverge this call is where the compiler will say so.
    //
    // The same strings deliberately land in two places in one commit, but only ONE of them
    // announces. `.mk-auth-feedback` below is the live region; `ToastStack` is not one, for the
    // reason its own header gives. Making the toast a live region again would read every
    // outcome twice, and these bodies run to several sentences.
    push({ tone: next.kind, title: next.title, body: next.body });
    spec.onSettled?.(next.kind);

    // Only a configured adapter can produce a redirect, and the destination is that
    // deployment's own; the hash router picks the change up from here.
    if (next.kind === 'success' && redirectTo !== undefined && typeof window !== 'undefined') {
      window.location.hash = redirectTo;
    }
  }

  function handleSubmit(event: FormEvent<HTMLFormElement>): void {
    event.preventDefault();
    if (busy) return;

    const nextErrors = validateFor(mode, flow, phoneStep, values);
    setErrors(nextErrors);

    if (hasErrors(nextErrors)) {
      setRevalidate(true);
      setOutcome(null);
      const first = firstErrorField(nextErrors, fieldOrderFor(mode, flow, phoneStep));
      if (first !== null) refFor(first)?.current?.focus();
      return;
    }

    if (flow === 'reset') {
      void execute({
        target: 'form',
        successTitle: 'Reset requested',
        task: () => runAuth('resetPassword', { email: email.trim() }),
      });
      return;
    }

    if (flow === 'phone') {
      const number = normalisePhone(phone);
      if (phoneStep === 'code') {
        void execute({
          target: 'form',
          successTitle: 'Number verified',
          task: () => runAuth('verifyPhoneCode', { phone: number, code: code.trim() }),
        });
        return;
      }
      void execute({
        target: 'form',
        successTitle: 'Code sent',
        task: () => runAuth('requestPhoneCode', { phone: number }),
        // The code step is entered on the honest not-connected outcome as well as on success.
        // A second half that is unreachable in this build would be a state machine that exists
        // only in the source, and the step itself says plainly that no code was sent.
        onSettled: (kind) => {
          if (kind === 'error') return;
          setErrors({});
          setRevalidate(false);
          setPhoneStep('code');
        },
      });
      return;
    }

    if (isSignUp) {
      void execute({
        target: 'form',
        successTitle: 'Account created',
        task: () =>
          runAuth('signUp', {
            email: email.trim(),
            password,
            organisation: organisation.trim(),
          }),
      });
      return;
    }

    void execute({
      target: 'form',
      successTitle: 'Signed in',
      task: () => runAuth('signIn', { email: email.trim(), password }),
    });
  }

  function handleProvider(provider: AuthProvider): void {
    if (busy) return;
    void execute({
      target: provider,
      successTitle: 'Provider accepted',
      task: () => runAuth('social', { provider }),
    });
  }

  function handleTabClick(event: MouseEvent<HTMLAnchorElement>, target: 'signin' | 'signup'): void {
    if (isModifiedClick(event)) return;
    event.preventDefault();
    navigate(target);
  }

  const heading = isSignUp ? 'Create an account' : 'Sign in';
  const subtitle = isSignUp
    ? 'Request access to the ORCA research platform.'
    : 'Return to the orbital rescue planning workspace.';

  const legend =
    flow === 'reset'
      ? 'Password reset'
      : flow === 'phone'
        ? phoneStep === 'code'
          ? 'Verification code'
          : 'Phone verification'
        : isSignUp
          ? 'Account details'
          : 'Email and password';

  const submitLabel =
    flow === 'reset'
      ? 'Send reset link'
      : flow === 'phone'
        ? phoneStep === 'code'
          ? 'Verify code'
          : 'Request verification code'
        : isSignUp
          ? 'Create account'
          : 'Sign in';

  return (
    <div className="mk-auth-console__inner">
      <header className="mk-auth-head">
        <h1 className="mk-auth-title" ref={headingRef} tabIndex={-1}>
          {heading}
        </h1>
        <p className="mk-auth-sub">{subtitle}</p>

        <nav className="mk-auth-tabs" aria-label="Account access">
          <a
            className="mk-auth-tab"
            href="#/signin"
            aria-current={mode === 'signin' ? 'page' : undefined}
            onClick={(event) => handleTabClick(event, 'signin')}
          >
            Sign in
          </a>
          <a
            className="mk-auth-tab"
            href="#/signup"
            aria-current={mode === 'signup' ? 'page' : undefined}
            onClick={(event) => handleTabClick(event, 'signup')}
          >
            Create account
          </a>
        </nav>
      </header>

      {/* House rule: this build must never imply an account exists. The notice is rendered from
          the service's own configuration state, so it disappears by itself the moment a real
          adapter is registered -- it cannot go stale. */}
      {authConfigured ? null : (
        <div className="mk-auth-notice">
          <p className="mk-auth-notice__label mk-mono">
            <StatusDot tone="warm" />
            Not connected
          </p>
          <p className="mk-auth-notice__body">
            Authentication is not wired to a provider in this build. Every route below validates
            locally and then stops: nothing is transmitted, no account is created and no session
            exists. The workspace is unaffected — it runs locally against the C++ engine.
          </p>
        </div>
      )}

      {flow === 'email' ? (
        <SocialAuthButtons
          busyProvider={pending === 'form' ? null : pending}
          onProvider={handleProvider}
          onPhone={() => goToFlow('phone')}
          disabled={busy}
        />
      ) : null}

      <form
        className="mk-auth-form"
        onSubmit={handleSubmit}
        noValidate
        aria-busy={busy || undefined}
      >
        {/* `noValidate`: the browser's own bubbles would pre-empt the messages above, which are
            specific about the repair, and they are neither styled nor announced consistently. */}
        <fieldset className="mk-auth-fieldset" disabled={busy}>
          <legend className="mk-auth-legend mk-mono">{legend}</legend>

          {flow === 'reset' ? (
            <>
              <p className="mk-auth-explain">
                Enter the address on the account. A reset link would be sent to it; nothing else
                about the account is disclosed by this form.
              </p>
              <Field
                id={`${uid}-email`}
                label="Email address"
                type="email"
                inputMode="email"
                value={email}
                onChange={setEmail}
                autoComplete="email"
                inputRef={emailRef}
                error={errors.email}
              />
            </>
          ) : null}

          {flow === 'phone' && phoneStep === 'number' ? (
            <Field
              id={`${uid}-phone`}
              label="Phone number"
              type="tel"
              inputMode="tel"
              value={phone}
              onChange={setPhone}
              autoComplete="tel"
              inputRef={phoneRef}
              hint={PHONE_HINT}
              error={errors.phone}
            />
          ) : null}

          {flow === 'phone' && phoneStep === 'code' ? (
            <>
              <p className="mk-auth-explain">
                Number entered: <span className="mk-mono">{normalisePhone(phone)}</span>.{' '}
                {authConfigured
                  ? `Enter the ${CODE_LENGTH}-digit code that was sent to it.`
                  : 'No message was sent, because phone verification is not connected. This ' +
                    'step is shown so the flow can be inspected end to end; submitting it ' +
                    'reports the same integration gap.'}
              </p>
              <Field
                id={`${uid}-code`}
                label={`${CODE_LENGTH}-digit verification code`}
                type="text"
                inputMode="numeric"
                maxLength={CODE_LENGTH}
                value={code}
                onChange={setCode}
                autoComplete="one-time-code"
                inputRef={codeRef}
                error={errors.code}
              />
            </>
          ) : null}

          {flow === 'email' ? (
            <>
              <Field
                id={`${uid}-email`}
                label="Email address"
                type="email"
                inputMode="email"
                value={email}
                onChange={setEmail}
                autoComplete="email"
                inputRef={emailRef}
                error={errors.email}
              />

              {isSignUp ? (
                <Field
                  id={`${uid}-organisation`}
                  label="Institution or organisation"
                  type="text"
                  value={organisation}
                  onChange={setOrganisation}
                  autoComplete="organization"
                  inputRef={organisationRef}
                  hint="ORCA is a research platform; access is granted to a named group."
                  error={errors.organisation}
                />
              ) : null}

              <PasswordField
                id={`${uid}-password`}
                label="Password"
                value={password}
                onChange={setPassword}
                autoComplete={isSignUp ? 'new-password' : 'current-password'}
                inputRef={passwordRef}
                hint={isSignUp ? PASSWORD_HINT : undefined}
                error={errors.password}
              />

              {isSignUp ? (
                <PasswordField
                  id={`${uid}-confirm`}
                  label="Confirm password"
                  value={confirm}
                  onChange={setConfirm}
                  autoComplete="new-password"
                  inputRef={confirmRef}
                  error={errors.confirm}
                />
              ) : null}
            </>
          ) : null}
        </fieldset>

        <div className="mk-auth-submit" ref={submitWrapRef}>
          <ActionButton type="submit" variant="primary" fullWidth busy={pending === 'form'}>
            {submitLabel}
          </ActionButton>
        </div>
      </form>

      <div className="mk-auth-links">
        {flow === 'email' && !isSignUp ? (
          <button type="button" className="mk-auth-link" onClick={() => goToFlow('reset')}>
            Forgot your password?
          </button>
        ) : null}
        {flow !== 'email' ? (
          <button type="button" className="mk-auth-link" onClick={() => goToFlow('email')}>
            Back to email {isSignUp ? 'sign-up' : 'sign-in'}
          </button>
        ) : null}
        {flow === 'phone' && phoneStep === 'code' ? (
          <button
            type="button"
            className="mk-auth-link"
            onClick={() => {
              setPhoneStep('number');
              setCode('');
              setErrors({});
              setOutcome(null);
            }}
          >
            Use a different number
          </button>
        ) : null}
      </div>

      {/* Always mounted, even when empty: a polite live region has to exist in the document
          before its content changes, or the first message is frequently not announced at all. */}
      <div className="mk-auth-feedback" role="status" aria-live="polite">
        {outcome === null ? null : (
          <div className={`mk-auth-outcome mk-auth-outcome--${outcome.kind}`}>
            <p className="mk-auth-outcome__title mk-mono">{outcome.title}</p>
            <p className="mk-auth-outcome__body">{outcome.body}</p>

            {outcome.kind === 'info' ? (
              <div className="mk-auth-outcome__escape">
                <p className="mk-auth-outcome__note">
                  The workspace runs locally against the C++ engine over the Node bridge and is
                  not access-controlled in this build, so it opens without an account.
                </p>
                <ActionButton variant="quiet" onClick={() => navigate('workspace')}>
                  Continue to the local workspace
                </ActionButton>
              </div>
            ) : null}
          </div>
        )}
      </div>

      <p className="mk-auth-legal">
        {isSignUp
          ? 'Creating an account would be subject to the '
          : 'Use of this demonstrator is described in the '}
        <button type="button" className="mk-auth-link" onClick={() => setNotice('terms')}>
          terms of use
        </button>
        {' and the '}
        <button type="button" className="mk-auth-link" onClick={() => setNotice('privacy')}>
          privacy notice
        </button>
        {'. Both are placeholder notices for a research demonstrator.'}
      </p>

      <dialog className="mk-auth-dialog" ref={dialogRef} onClose={() => setNotice(null)}>
        <h2 className="mk-auth-dialog__title">
          {notice === 'privacy' ? 'Privacy notice' : 'Terms of use'}
        </h2>
        {notice === 'privacy' ? (
          <>
            <p>
              This build collects nothing. There is no analytics script, no identity provider and
              no server-side account store; anything typed into the form above stays in the
              browser tab and is discarded when it closes.
            </p>
            <p>
              This is a placeholder notice for a research demonstrator, not a privacy policy. A
              production deployment must publish a real policy — naming a controller, a lawful
              basis, retention periods and a contact route — before it collects any personal
              data, including an email address.
            </p>
          </>
        ) : (
          <>
            <p>
              ORCA is research software. It plans against simplified orbital models, makes no
              claim of flight readiness, certification or operational qualification, and is not a
              mission-planning authority.
            </p>
            <p>
              These are placeholder terms for a research demonstrator, not an agreement. A
              production deployment must publish real terms, and a real privacy policy, before it
              collects personal data or grants an account.
            </p>
          </>
        )}
        <form method="dialog" className="mk-auth-dialog__actions">
          <button type="submit" className="mk-auth-dialog__close">
            Close
          </button>
        </form>
      </dialog>

      <ToastStack toasts={toasts} onDismiss={dismiss} />
    </div>
  );
}
