import { fireEvent, render, screen, within } from '@testing-library/react';
import type { RenderResult } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it } from 'vitest';

import { AuthForm } from '../src/auth/AuthForm';
import { RouteProvider } from '../src/routes/RouteContext';

/** The account form. Two things are being protected here and they pull in opposite directions.
 *
 * House rule 5: with no adapter registered — which is what this repository ships — a submitted
 * form must arrive at an explicit "not connected" state and must never render a success title, a
 * session or an account. That outcome is reachable only by driving the real form through the
 * real service, so these tests do exactly that rather than asserting on copy in isolation.
 *
 * House rule 7: the errors that outcome is reached through have to be usable. Each one is
 * checked for the wiring a screen-reader user depends on — `aria-invalid` on the control it
 * belongs to and an `aria-describedby` that actually resolves to the message — because an error
 * rendered next to a field and an error attached to a field look identical on screen. */

function renderAuthForm(mode: 'signin' | 'signup'): RenderResult {
  // `AuthForm` reads `useRoute()` for its tab links and the workspace escape hatch, so the
  // provider is required. It is added here rather than in `test/testUtils.tsx`, which the
  // workspace suites share and which has no reason to know about routing.
  return render(
    <RouteProvider>
      <AuthForm mode={mode} />
    </RouteProvider>,
  );
}

/** The outcome is rendered twice by design — once in the persistent panel, once as a transient
 * toast — so that it survives both a reader who is looking at the form and one who is not. */
function outcomeTexts(pattern: string | RegExp): readonly string[] {
  return screen.getAllByText(pattern).map((node) => node.textContent ?? '');
}

const VALID_PASSWORD = 'correct horse battery';

/** `RouteProvider` resets the scroll position when a view mounts. jsdom's `window.scrollTo` is a
 * stub that reports "not implemented" to its virtual console rather than throwing, so the
 * provider's own try/catch never sees it and every render here would print a stack trace over
 * the suite's output. `RouteContext.test.tsx` owns the assertions about that reset. */
let originalScrollTo: typeof window.scrollTo;

beforeEach(() => {
  originalScrollTo = window.scrollTo;
  window.scrollTo = (() => undefined) as typeof window.scrollTo;
});

afterEach(() => {
  window.scrollTo = originalScrollTo;
});

describe('AuthForm: the not-connected notice', () => {
  it('states the integration gap before anything is typed', () => {
    renderAuthForm('signin');
    expect(screen.getByText('Not connected')).toBeInTheDocument();
    expect(
      screen.getByText(/Authentication is not wired to a provider in this build\./),
    ).toBeInTheDocument();
  });

  it('says plainly that nothing is transmitted and no session exists', () => {
    renderAuthForm('signup');
    const notice = screen.getByText(/nothing is transmitted, no account is created and no session/);
    expect(notice).toBeInTheDocument();
  });
});

describe('AuthForm: validation errors are attached to their fields, not merely near them', () => {
  it('reports both empty fields on an empty sign-in submit', () => {
    renderAuthForm('signin');
    fireEvent.click(screen.getByRole('button', { name: 'Sign in' }));

    expect(screen.getByText('Enter an email address.')).toBeInTheDocument();
    expect(screen.getByText('Enter your password.')).toBeInTheDocument();
  });

  it('marks the invalid controls with aria-invalid', () => {
    renderAuthForm('signin');
    fireEvent.click(screen.getByRole('button', { name: 'Sign in' }));

    expect(screen.getByLabelText('Email address')).toHaveAttribute('aria-invalid', 'true');
    expect(screen.getByLabelText('Password')).toHaveAttribute('aria-invalid', 'true');
  });

  it('points aria-describedby at a message that actually resolves', () => {
    renderAuthForm('signin');
    fireEvent.click(screen.getByRole('button', { name: 'Sign in' }));

    const email = screen.getByLabelText('Email address');
    const describedBy = email.getAttribute('aria-describedby');
    expect(describedBy).toBeTruthy();
    const described = (describedBy ?? '')
      .split(' ')
      .map((id) => document.getElementById(id)?.textContent ?? '')
      .join(' ');
    expect(described).toContain('Enter an email address.');
  });

  it('moves focus to the first invalid field in visual order', () => {
    renderAuthForm('signin');
    fireEvent.change(screen.getByLabelText('Email address'), {
      target: { value: 'jdoe@institution.edu' },
    });
    fireEvent.click(screen.getByRole('button', { name: 'Sign in' }));
    // Email is valid, so the password field is the first one on screen that is not.
    expect(screen.getByLabelText('Password')).toHaveFocus();
  });

  it('clears the marking once the field is corrected, without a second submit', () => {
    renderAuthForm('signin');
    fireEvent.click(screen.getByRole('button', { name: 'Sign in' }));
    expect(screen.getByLabelText('Email address')).toHaveAttribute('aria-invalid', 'true');

    fireEvent.change(screen.getByLabelText('Email address'), {
      target: { value: 'jdoe@institution.edu' },
    });
    expect(screen.getByLabelText('Email address')).not.toHaveAttribute('aria-invalid');
    expect(screen.queryByText('Enter an email address.')).not.toBeInTheDocument();
  });

  it('does not validate before the reader has submitted once', () => {
    renderAuthForm('signin');
    fireEvent.change(screen.getByLabelText('Email address'), { target: { value: 'j' } });
    expect(screen.queryByText(/Enter a complete address/)).not.toBeInTheDocument();
  });

  it('marks every wrong field on an empty sign-up, not just the first', () => {
    renderAuthForm('signup');
    fireEvent.click(screen.getByRole('button', { name: 'Create account' }));

    for (const label of [
      'Email address',
      'Institution or organisation',
      'Password',
      'Confirm password',
    ]) {
      expect(screen.getByLabelText(label)).toHaveAttribute('aria-invalid', 'true');
    }
    // Focus lands on the topmost of them, which is what "the first invalid field" means to a
    // reader looking at the column.
    expect(screen.getByLabelText('Email address')).toHaveFocus();
  });
});

describe('AuthForm: a valid sign-in submit (house rule 5)', () => {
  it('reaches the honest not-connected outcome', async () => {
    renderAuthForm('signin');
    fireEvent.change(screen.getByLabelText('Email address'), {
      target: { value: 'jdoe@institution.edu' },
    });
    fireEvent.change(screen.getByLabelText('Password'), { target: { value: VALID_PASSWORD } });
    fireEvent.click(screen.getByRole('button', { name: 'Sign in' }));

    await screen.findAllByText('Integration required', undefined, { timeout: 3000 });
    expect(
      outcomeTexts(/No credentials were transmitted and no session was created\./).length,
    ).toBeGreaterThan(0);
  });

  it('never renders a success title, a session or an account', async () => {
    const { container } = renderAuthForm('signin');
    fireEvent.change(screen.getByLabelText('Email address'), {
      target: { value: 'jdoe@institution.edu' },
    });
    fireEvent.change(screen.getByLabelText('Password'), { target: { value: VALID_PASSWORD } });
    fireEvent.click(screen.getByRole('button', { name: 'Sign in' }));

    await screen.findAllByText('Integration required', undefined, { timeout: 3000 });

    // `Signed in` is the success title this code path would use if an adapter were attached.
    expect(screen.queryByText('Signed in')).not.toBeInTheDocument();
    expect(container.querySelector('.mk-auth-outcome--success')).toBeNull();
    expect(container.querySelector('.mk-auth-outcome--info')).not.toBeNull();
    expect(screen.queryByText(/welcome/i)).not.toBeInTheDocument();
  });

  it('offers the workspace as the honest way forward, which needs no account', async () => {
    renderAuthForm('signin');
    fireEvent.change(screen.getByLabelText('Email address'), {
      target: { value: 'jdoe@institution.edu' },
    });
    fireEvent.change(screen.getByLabelText('Password'), { target: { value: VALID_PASSWORD } });
    fireEvent.click(screen.getByRole('button', { name: 'Sign in' }));

    const escape = await screen.findByRole(
      'button',
      { name: 'Continue to the local workspace' },
      { timeout: 3000 },
    );
    expect(escape).toBeInTheDocument();
  });

  it('announces the outcome through a live region rather than only in colour', async () => {
    const { container } = renderAuthForm('signin');
    // The region exists before its first message; one mounted with its content is frequently
    // not announced at all.
    const feedback = container.querySelector('.mk-auth-feedback');
    expect(feedback).toHaveAttribute('aria-live', 'polite');

    fireEvent.change(screen.getByLabelText('Email address'), {
      target: { value: 'jdoe@institution.edu' },
    });
    fireEvent.change(screen.getByLabelText('Password'), { target: { value: VALID_PASSWORD } });
    fireEvent.click(screen.getByRole('button', { name: 'Sign in' }));

    await screen.findAllByText('Integration required', undefined, { timeout: 3000 });
    expect(within(feedback as HTMLElement).getByText('Integration required')).toBeInTheDocument();
  });
});

describe('AuthForm: the password visibility toggle', () => {
  it('flips the input type and its own pressed state', () => {
    renderAuthForm('signin');
    const password = screen.getByLabelText('Password');
    expect(password).toHaveAttribute('type', 'password');

    const toggle = screen.getByRole('button', { name: 'Show password' });
    expect(toggle).toHaveAttribute('aria-pressed', 'false');

    fireEvent.click(toggle);
    expect(password).toHaveAttribute('type', 'text');
    expect(screen.getByRole('button', { name: 'Hide password' })).toHaveAttribute(
      'aria-pressed',
      'true',
    );

    fireEvent.click(screen.getByRole('button', { name: 'Hide password' }));
    expect(password).toHaveAttribute('type', 'password');
  });

  it('is a real button of type=button, so revealing does not submit the form', () => {
    renderAuthForm('signin');
    const toggle = screen.getByRole('button', { name: 'Show password' });
    expect(toggle).toHaveAttribute('type', 'button');
    fireEvent.click(toggle);
    // A submit would have run validation and produced messages.
    expect(screen.queryByText('Enter an email address.')).not.toBeInTheDocument();
  });

  it('does not carry the typed value across a mode change', () => {
    const view = renderAuthForm('signin');
    fireEvent.change(screen.getByLabelText('Email address'), {
      target: { value: 'jdoe@institution.edu' },
    });
    fireEvent.change(screen.getByLabelText('Password'), { target: { value: VALID_PASSWORD } });

    view.rerender(
      <RouteProvider>
        <AuthForm mode="signup" />
      </RouteProvider>,
    );

    // The address survives a mistyped tab choice; the secret belongs to the abandoned attempt.
    expect(screen.getByLabelText('Email address')).toHaveValue('jdoe@institution.edu');
    expect(screen.getByLabelText('Password')).toHaveValue('');
  });
});

describe('AuthForm: sign-up mode', () => {
  it('adds the confirmation and organisation fields the sign-in mode does not have', () => {
    renderAuthForm('signup');
    expect(screen.getByLabelText('Confirm password')).toBeInTheDocument();
    expect(screen.getByLabelText('Institution or organisation')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Create account' })).toBeInTheDocument();
  });

  it('rejects a confirmation that does not match, on the confirmation field', () => {
    renderAuthForm('signup');
    fireEvent.change(screen.getByLabelText('Email address'), {
      target: { value: 'jdoe@institution.edu' },
    });
    fireEvent.change(screen.getByLabelText('Institution or organisation'), {
      target: { value: 'Orbital Dynamics Lab' },
    });
    fireEvent.change(screen.getByLabelText('Password'), { target: { value: VALID_PASSWORD } });
    fireEvent.change(screen.getByLabelText('Confirm password'), {
      target: { value: `${VALID_PASSWORD}!` },
    });
    fireEvent.click(screen.getByRole('button', { name: 'Create account' }));

    expect(screen.getByText('The two passwords do not match.')).toBeInTheDocument();
    expect(screen.getByLabelText('Confirm password')).toHaveAttribute('aria-invalid', 'true');
    expect(screen.getByLabelText('Password')).not.toHaveAttribute('aria-invalid');
    // A mismatch stops at validation: nothing is submitted, so no outcome is reported.
    expect(screen.queryByText('Integration required')).not.toBeInTheDocument();
  });

  it('states the length rule before the reader can violate it', () => {
    renderAuthForm('signup');
    expect(screen.getByText(/At least 12 characters\./)).toBeInTheDocument();
  });
});

describe('AuthForm: the phone route', () => {
  it('reveals the phone field, and puts the email fields away', () => {
    renderAuthForm('signin');
    expect(screen.queryByLabelText('Phone number')).not.toBeInTheDocument();

    fireEvent.click(screen.getByRole('button', { name: 'Continue with phone number' }));

    expect(screen.getByLabelText('Phone number')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Request verification code' })).toBeInTheDocument();
    expect(screen.queryByLabelText('Email address')).not.toBeInTheDocument();
    expect(screen.queryByLabelText('Password')).not.toBeInTheDocument();
  });

  it('validates the number against E.164 before it would send anything', () => {
    renderAuthForm('signin');
    fireEvent.click(screen.getByRole('button', { name: 'Continue with phone number' }));
    fireEvent.change(screen.getByLabelText('Phone number'), { target: { value: '4155550123' } });
    fireEvent.click(screen.getByRole('button', { name: 'Request verification code' }));

    expect(screen.getByText(/Start with \+ and the country code/)).toBeInTheDocument();
    expect(screen.getByLabelText('Phone number')).toHaveAttribute('aria-invalid', 'true');
  });

  it('reaches the code step, and says there that no code was sent', async () => {
    renderAuthForm('signin');
    fireEvent.click(screen.getByRole('button', { name: 'Continue with phone number' }));
    fireEvent.change(screen.getByLabelText('Phone number'), {
      target: { value: '+1 (415) 555-0123' },
    });
    fireEvent.click(screen.getByRole('button', { name: 'Request verification code' }));

    await screen.findAllByText(/No code was generated and no message was sent/, undefined, {
      timeout: 3000,
    });
    // The second half of the flow is reachable so it can be inspected end to end, and it says
    // plainly that nothing was sent rather than pretending a code is on its way.
    expect(screen.getByLabelText('6-digit verification code')).toBeInTheDocument();
    expect(screen.getByText(/No message was sent, because phone verification is not connected/))
      .toBeInTheDocument();
    // The number is echoed back in its normalised form, which is what an adapter would dial.
    expect(screen.getByText('+14155550123')).toBeInTheDocument();
  });

  it('returns to the email route without carrying the phone error with it', () => {
    renderAuthForm('signin');
    fireEvent.click(screen.getByRole('button', { name: 'Continue with phone number' }));
    fireEvent.click(screen.getByRole('button', { name: 'Request verification code' }));
    expect(screen.getByText(/Enter a phone number/)).toBeInTheDocument();

    fireEvent.click(screen.getByRole('button', { name: 'Back to email sign-in' }));
    expect(screen.getByLabelText('Email address')).toBeInTheDocument();
    expect(screen.queryByText(/Enter a phone number/)).not.toBeInTheDocument();
  });
});

describe('AuthForm: a request that was abandoned before it settled', () => {
  it('never renders its response against the flow the reader moved on to', async () => {
    const { container } = renderAuthForm('signin');

    fireEvent.click(screen.getByRole('button', { name: 'Continue with phone number' }));
    fireEvent.change(screen.getByLabelText('Phone number'), { target: { value: '+14155550123' } });
    fireEvent.click(screen.getByRole('button', { name: 'Request verification code' }));

    // Leaving the flow while its request is still in flight.
    fireEvent.click(screen.getByRole('button', { name: 'Back to email sign-in' }));
    fireEvent.change(screen.getByLabelText('Email address'), {
      target: { value: 'jdoe@institution.edu' },
    });
    fireEvent.change(screen.getByLabelText('Password'), { target: { value: VALID_PASSWORD } });
    fireEvent.click(screen.getByRole('button', { name: 'Sign in' }));

    // Both requests were started within a few milliseconds of each other and share the same
    // delay, so waiting for the second one's outcome guarantees the first has also settled.
    await screen.findAllByText(
      /No credentials were transmitted and no session was created\./,
      undefined,
      { timeout: 3000 },
    );

    expect(screen.queryByText(/No code was generated/)).not.toBeInTheDocument();
    // One toast, from the request the reader is actually waiting on.
    expect(container.querySelectorAll('.mk-toast')).toHaveLength(1);
  });
});

describe('AuthForm: federated sign-in', () => {
  it('announces the attempt, then reports the same integration gap', async () => {
    const { container } = renderAuthForm('signin');
    fireEvent.click(screen.getByRole('button', { name: 'Continue with Google' }));

    // The announcement sits beside the button rather than inside it: content nested in a button
    // joins its accessible name, so a spinner label there would rename the control mid-press.
    expect(container.querySelector('.mk-auth-vh')?.textContent).toBe('Contacting Google.');

    await screen.findAllByText(
      /No authorisation request was started and no profile data was requested\./,
      undefined,
      { timeout: 3000 },
    );
    expect(container.querySelector('.mk-auth-outcome--success')).toBeNull();
  });
});
