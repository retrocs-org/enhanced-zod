'use strict';
const dialog = document.querySelector('#join-dialog');
let lastJoinButton = null, ticket = null, request = null;
function commandFor(platform) {
  if (!ticket) return '';
  return platform === 'windows'
    ? `$env:ZOD_JOIN_TICKET = '${ticket.token}'; $env:ZOD_PORT = '${ticket.port}'; .\\zod-ee.exe -c ${ticket.host}`
    : `ZOD_JOIN_TICKET=${ticket.token} ZOD_PORT=${ticket.port} ./zod-ee -c ${ticket.host}`;
}
if (dialog) {
  const feedback = dialog.querySelector('#join-feedback');
  const commands = dialog.querySelector('#join-commands');
  const platform = dialog.querySelector('#join-platform');
  const code = dialog.querySelector('#join-command');
  const signIn = dialog.querySelector('#join-login');
  const copy = dialog.querySelector('.copy-command');
  for (const button of document.querySelectorAll('.join-button')) {
    button.addEventListener('click', async () => {
      request?.abort(); const currentRequest = new AbortController(); request = currentRequest;
      lastJoinButton = button; ticket = null; commands.hidden = true; signIn.hidden = true;
      dialog.querySelector('#join-title').textContent = `Join ${button.dataset.name}`;
      feedback.textContent = 'Requesting your join ticket…'; copy.textContent = 'Copy command ↗';
      dialog.showModal(); button.disabled = true;
      try {
        const response = await fetch('/api/tickets', {signal:currentRequest.signal,method:'POST',credentials:'same-origin',headers:{'Content-Type':'application/json'},body:JSON.stringify({server:button.dataset.id})});
        const data = await response.json();
        if (currentRequest !== request || !dialog.open) return;
        if (!response.ok) { signIn.hidden = response.status !== 401; throw new Error(data.error || 'Could not issue a ticket.'); }
        ticket = {token:data.ticket,host:button.dataset.host,port:button.dataset.port};
        code.textContent = commandFor(platform.value); commands.hidden = false;
        feedback.textContent = 'Ticket ready. Connect within two minutes; this ticket works once.';
      } catch (error) {if (currentRequest !== request || error.name === 'AbortError') return; feedback.textContent = error.message || 'The service could not be reached. Please retry.';}
      finally {button.disabled = false;}
    });
  }
  platform.addEventListener('change', () => {code.textContent=commandFor(platform.value);copy.textContent='Copy command ↗';});
  copy.addEventListener('click',async () => {
    try {await navigator.clipboard.writeText(code.textContent);copy.textContent='Command copied ✓';}
    catch {feedback.textContent='Copy is unavailable here. Select the command text and copy it manually.';}
  });
  dialog.querySelector('.close-dialog').addEventListener('click',()=>dialog.close());
  dialog.addEventListener('close',()=>{request?.abort();request=null;ticket=null;code.textContent='';lastJoinButton?.focus();});
}

// Public GET explorer. Endpoint values are fixed by the PHP catalog.
const explorer = document.querySelector('#api-explorer');
if (explorer) {
  const endpoint = explorer.querySelector('#api-endpoint');
  const output = document.querySelector('#api-output');
  const status = document.querySelector('#api-status');
  const raw = document.querySelector('#api-raw');
  const button = explorer.querySelector('button');
  const allowed = new Set(Array.from(endpoint.options, option => option.value));
  endpoint.addEventListener('change', () => {raw.href = endpoint.value;});
  explorer.addEventListener('submit', async event => {
    event.preventDefault();
    const path = endpoint.value;
    if (!allowed.has(path) || !path.startsWith('/') || path.startsWith('//')) return;
    button.disabled = true; status.textContent = 'Requesting ' + path + '…';
    output.textContent = ''; const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 10000);
    try {
      const response = await fetch(path, {method:'GET', credentials:'omit', cache:'no-store', redirect:'error', headers:{Accept:'application/json'}, signal:controller.signal});
      const body = await response.text();
      if (!response.headers.get('content-type')?.includes('application/json')) throw new Error('The endpoint did not return JSON.');
      output.textContent = JSON.stringify(JSON.parse(body), null, 2);
      status.textContent = 'GET ' + path + ' · HTTP ' + response.status;
    } catch (error) {
      status.textContent = error.name === 'AbortError' ? 'Request timed out. Please retry.' : 'Request failed. ' + error.message;
      output.textContent = 'No JSON response available.';
    } finally {clearTimeout(timeout);button.disabled = false;}
  });
}
const navigationToggle = document.querySelector('.nav-toggle');
const mainNavigation = document.querySelector('#main-nav');
if (navigationToggle && mainNavigation) navigationToggle.addEventListener('click', () => {
  const expanded = navigationToggle.getAttribute('aria-expanded') === 'true';
  navigationToggle.setAttribute('aria-expanded', String(!expanded));
  mainNavigation.classList.toggle('is-open', !expanded);
});
