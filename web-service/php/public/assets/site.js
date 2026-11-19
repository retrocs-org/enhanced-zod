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
        const response = await fetch('/api/tickets', {signal:currentRequest.signal,method:'POST',credentials:'same-origin',headers:{'Content-Type':'application/json'},body:JSON.stringify({Server:button.dataset.id})});
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
