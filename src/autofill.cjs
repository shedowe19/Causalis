'use strict';

// This function is serialized into an isolated JavaScript world in the main
// frame. The web page receives only values the user explicitly chose to fill;
// no vault API, IPC bridge, item list or session token exists in that frame.
function fillInPage(credential) {
  const fail = (reason) => ({ ok: false, reason });
  const exactOrigin = () => window.top === window && window.location.protocol === 'https:' &&
    window.location.origin === credential.origin;
  if (!exactOrigin()) return fail('origin-changed');
  const inputNodes = document.querySelectorAll('input');
  if (inputNodes.length > 2000) return fail('too-many-fields');
  const inputs = Array.from(inputNodes);
  const autocompleteHas = (input, token) => String(input.autocomplete || '').toLowerCase().split(/\s+/).includes(token);
  const visible = (input) => {
    if (!input.isConnected || input.disabled || input.readOnly || input.hidden) return false;
    if ((typeof input.matches === 'function' && input.matches(':disabled')) ||
      (typeof input.closest === 'function' && input.closest('[inert]'))) return false;
    if (typeof input.checkVisibility === 'function' &&
      !input.checkVisibility({ checkOpacity: true, checkVisibilityCSS: true, contentVisibilityAuto: true })) return false;
    const style = window.getComputedStyle(input);
    const bounds = input.getBoundingClientRect();
    return style.display !== 'none' && style.visibility !== 'hidden' && style.visibility !== 'collapse' &&
      Number(style.opacity) !== 0 && bounds.width >= 2 && bounds.height >= 2 &&
      bounds.right > 0 && bounds.bottom > 0 && bounds.left < window.innerWidth && bounds.top < window.innerHeight;
  };
  const safeForm = (input) => {
    if (!input.form) return true;
    try {
      const safeAction = (value) => {
        const action = new URL(value || window.location.href, document.baseURI || window.location.href);
        return action.protocol === 'https:' && action.origin === credential.origin && !action.username && !action.password;
      };
      if (!safeAction(input.form.getAttribute('action'))) return false;
      const formControls = input.form.elements || [];
      if (formControls.length > 2000) return false;
      const controls = Array.from(formControls);
      return controls.every((control) => !['submit', 'image'].includes(control.type) ||
        control.getAttribute('formaction') === null || safeAction(control.getAttribute('formaction')));
    } catch { return false; }
  };
  const passwords = inputs.filter((input) => input.type === 'password' && visible(input) &&
    !autocompleteHas(input, 'new-password') && safeForm(input));
  if (passwords.length !== 1) return fail(passwords.length ? 'ambiguous-password-fields' : 'no-safe-password-field');
  const passwordField = passwords[0];
  const passwordIndex = inputs.indexOf(passwordField);
  const candidates = inputs.filter((input, index) => index < passwordIndex && input.form === passwordField.form &&
    ['text', 'email', 'tel'].includes(input.type) && visible(input) && safeForm(input));
  const rank = (input) => autocompleteHas(input, 'username') ? 4 :
    /^(username|user|login|email|e-mail)$/i.test(input.name || input.id || '') ? 3 :
      input.type === 'email' ? 2 : input.form ? 1 : 0;
  const usernameField = candidates.sort((left, right) => rank(right) - rank(left))[0];
  const setter = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set;
  if (typeof setter !== 'function') return fail('no-native-input-setter');
  let fields = 0;
  const assign = (input, value) => {
    if (!exactOrigin() || !visible(input) || !safeForm(input)) return false;
    setter.call(input, value);
    input.dispatchEvent(new Event('input', { bubbles: true }));
    if (!exactOrigin()) return false;
    input.dispatchEvent(new Event('change', { bubbles: true }));
    fields += 1;
    return true;
  };
  if (usernameField && rank(usernameField) > 0 && credential.username && !assign(usernameField, credential.username))
    return fail('page-changed-during-fill');
  if (!assign(passwordField, credential.password)) return fail('page-changed-during-fill');
  // Input/change events notify the site; submitting remains the user's action.
  return { ok: true, fields };
}

function buildAutofillScript({ origin, username = '', password }) {
  if (typeof origin !== 'string' || typeof username !== 'string' || typeof password !== 'string' ||
      username.length > 16384 || password.length > 16384 || !password)
    throw new Error('Invalid credential for autofill.');
  let parsed;
  try { parsed = new URL(origin); } catch { throw new Error('Autofill requires an exact HTTPS origin.'); }
  if (parsed.protocol !== 'https:' || parsed.origin !== origin || parsed.username || parsed.password)
    throw new Error('Autofill requires an exact HTTPS origin.');
  const payload = JSON.stringify({ origin, username, password }).replace(/</g, '\\u003c')
    .replace(/\u2028/g, '\\u2028').replace(/\u2029/g, '\\u2029');
  return `(${fillInPage.toString()})(${payload})`;
}

module.exports = { buildAutofillScript };
