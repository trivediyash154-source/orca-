/**
 * The account store's security properties, asserted rather than assumed.
 *
 * These are the tests that would catch the failures that actually matter here: a password
 * written to storage in a readable form, a wrong password accepted, a duplicate account
 * silently overwriting an existing one, and a Google sign-in clobbering the password an email
 * account was created with.
 *
 * They rely on the runtime's WebCrypto (`globalThis.crypto.subtle`, present in every Node the
 * toolchain supports). The store deliberately refuses to run without it rather than falling
 * back to a weaker hash, so a missing `subtle` fails these tests loudly instead of passing on a
 * downgraded path.
 */

import { beforeEach, describe, expect, it } from 'vitest';

import {
  AuthStorageError,
  clearSession,
  createAccount,
  findAccount,
  listAccounts,
  readSession,
  signInWithGoogleProfile,
  signInWithPassword,
} from '../src/auth/accountStore';

const PASSWORD = 'a reasonably long passphrase';

describe('accountStore', () => {
  beforeEach(() => {
    window.localStorage.clear();
  });

  it('creates an account and signs it in', async () => {
    const user = await createAccount({
      email: 'Jdoe@Institution.EDU',
      password: PASSWORD,
      organisation: 'Institution',
    });

    // The address is normalised on the way in, so a capitalised retype still finds the account.
    expect(user.email).toBe('jdoe@institution.edu');
    expect(findAccount('JDOE@INSTITUTION.EDU')).not.toBeNull();
    expect(readSession()?.id).toBe(user.id);
  });

  it('never stores the password, in any readable form', async () => {
    await createAccount({ email: 'a@b.co', password: PASSWORD, organisation: 'Lab' });

    const raw = window.localStorage.getItem('orca.accounts.v1') ?? '';
    expect(raw).not.toContain(PASSWORD);
    // Not even a fragment: a substring search would catch a naive "store the first N chars"
    // mistake that a whole-string search would miss.
    expect(raw).not.toContain('passphrase');

    const [account] = listAccounts();
    expect(account?.salt).toMatch(/^[0-9a-f]{32}$/);
    expect(account?.hash).toMatch(/^[0-9a-f]{64}$/);
  });

  it('gives every account its own salt, so identical passwords hash differently', async () => {
    await createAccount({ email: 'one@b.co', password: PASSWORD, organisation: 'Lab' });
    await createAccount({ email: 'two@b.co', password: PASSWORD, organisation: 'Lab' });

    const [first, second] = listAccounts();
    expect(first?.salt).not.toBe(second?.salt);
    expect(first?.hash).not.toBe(second?.hash);
  });

  it('accepts the right password and rejects a wrong one', async () => {
    await createAccount({ email: 'a@b.co', password: PASSWORD, organisation: 'Lab' });
    clearSession();

    const user = await signInWithPassword('a@b.co', PASSWORD);
    expect(user.email).toBe('a@b.co');

    await expect(signInWithPassword('a@b.co', `${PASSWORD} `)).rejects.toBeInstanceOf(
      AuthStorageError,
    );
  });

  it('reports an unknown address and a wrong password identically', async () => {
    await createAccount({ email: 'a@b.co', password: PASSWORD, organisation: 'Lab' });

    const wrongPassword = await signInWithPassword('a@b.co', 'nope').catch((e: Error) => e.message);
    const noSuchAccount = await signInWithPassword('zz@b.co', PASSWORD).catch(
      (e: Error) => e.message,
    );

    // Two different messages would tell an attacker which addresses are registered.
    expect(wrongPassword).toBe(noSuchAccount);
  });

  it('refuses to create a second account for the same address', async () => {
    await createAccount({ email: 'a@b.co', password: PASSWORD, organisation: 'Lab' });
    await expect(
      createAccount({ email: 'A@B.CO', password: 'another long passphrase', organisation: 'X' }),
    ).rejects.toBeInstanceOf(AuthStorageError);
    expect(listAccounts()).toHaveLength(1);
  });

  it('signs a Google account in without a password, and reuses it on return', () => {
    const first = signInWithGoogleProfile({ email: 'g@gmail.com', name: 'G User' });
    clearSession();
    const second = signInWithGoogleProfile({ email: 'g@gmail.com' });

    expect(second.id).toBe(first.id);
    expect(listAccounts()).toHaveLength(1);
    expect(listAccounts()[0]?.salt).toBeUndefined();
  });

  it('keeps the password when an email account later signs in with Google', async () => {
    await createAccount({ email: 'both@b.co', password: PASSWORD, organisation: 'Lab' });
    signInWithGoogleProfile({ email: 'both@b.co', name: 'Both' });

    // Dropping the credential here would silently lock the reader out of the route they
    // originally signed up with.
    await expect(signInWithPassword('both@b.co', PASSWORD)).resolves.toBeDefined();
  });

  it('directs a Google-only address away from the password form', async () => {
    signInWithGoogleProfile({ email: 'only@gmail.com' });
    await expect(signInWithPassword('only@gmail.com', PASSWORD)).rejects.toThrow(/Google/);
  });

  it('treats an unparseable session as signed out', () => {
    window.localStorage.setItem('orca.session.v1', '{not json');
    expect(readSession()).toBeNull();
  });
});
