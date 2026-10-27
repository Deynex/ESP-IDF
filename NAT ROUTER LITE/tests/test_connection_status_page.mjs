// Run with Node.js: node tests/test_connection_status_page.mjs
const assert = (await import('node:assert/strict')).default;
const fs = await import('node:fs');
const vm = await import('node:vm');
const { setImmediate } = await import('node:timers');

const html = fs.readFileSync(new URL('../web%20pages/saved.html', import.meta.url), 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
const baseStatus = {
    credentials_saved: true,
    connected: false,
    nat_enabled: false,
    dns_ready: false,
    subnet_conflict: false,
    wifi_reason: 0,
    connection_error: 'none'
};

async function page(initialStatus = baseStatus) {
    const elements = new Map();
    const timers = new Map();
    let timerId = 0;
    let clock = 0;
    let payload = { ...initialStatus };
    const element = id => {
        if (!elements.has(id)) elements.set(id, { dataset: {}, textContent: '' });
        return elements.get(id);
    };
    const context = vm.createContext({
        document: { getElementById: element },
        Date: { now: () => clock },
        AbortController,
        setTimeout: (callback, delay) => {
            const id = ++timerId;
            timers.set(id, { callback, delay });
            return id;
        },
        clearTimeout: id => timers.delete(id),
        fetch: async (url, options) => {
            assert.equal(url, '/api/status');
            assert.equal(options.cache, 'no-store');
            return { ok: true, json: async () => payload };
        }
    });
    vm.runInContext(script, context, { filename: 'saved.html' });
    await new Promise(resolve => setImmediate(resolve));

    return {
        element,
        delay: () => [...timers.values()].at(-1).delay,
        update: async (next, elapsed = 2000) => {
            clock += elapsed;
            payload = { ...baseStatus, ...next };
            await context.updateStatus();
        }
    };
}

async function run() {
    const empty = await page({ ...baseStatus, credentials_saved: false });
    assert.equal(empty.element('status').dataset.state, 'idle');
    assert.match(empty.element('status-title').textContent, /Faltan los datos/);

    const view = await page();
    assert.equal(view.element('status').dataset.state, 'waiting');
    assert.match(view.element('status-title').textContent, /Conectando al módem/);
    assert.equal(view.delay(), 2000);

    // The user's wrong-key case must replace the generic waiting notice.
    await view.update({ wifi_reason: 15, connection_error: 'handshake_timeout' }, 25000);
    assert.equal(view.element('status').dataset.state, 'error');
    assert.equal(view.element('status-title').textContent, 'No se pudo validar la contraseña');
    assert.match(view.element('status-text').textContent, /contraseña puede ser incorrecta/);
    assert.match(view.element('status-text').textContent, /seguirá reintentando/);
    const passwordError = view.element('status-text').textContent;
    await view.update({ wifi_reason: 15, connection_error: 'handshake_timeout' }, 60000);
    assert.equal(view.element('status').dataset.state, 'error');
    assert.equal(view.element('status-text').textContent, passwordError);
    assert.equal(view.delay(), 2000);

    // Corrected credentials clear the diagnosis and start a fresh wait.
    await view.update({ wifi_reason: 0, connection_error: 'none' });
    assert.equal(view.element('status').dataset.state, 'waiting');
    assert.match(view.element('status-title').textContent, /Conectando al módem/);
    assert.doesNotMatch(view.element('status-text').textContent, /incorrecta/);
    await view.update({ connected: true, nat_enabled: true, dns_ready: true });
    assert.equal(view.element('status').dataset.state, 'ready');
    assert.equal(view.element('status-title').textContent, 'Conexión al módem establecida');
    assert.equal(view.delay(), 10000);

    await view.update({ wifi_reason: 201, connection_error: 'network_not_found' });
    assert.equal(view.element('status-title').textContent, 'Red Wi-Fi no encontrada');
    assert.doesNotMatch(view.element('status-text').textContent, /contraseña puede ser incorrecta/);
    await view.update({ wifi_reason: 211, connection_error: 'security_mismatch' });
    assert.equal(view.element('status-title').textContent, 'Seguridad Wi-Fi incompatible');
    await view.update({ wifi_reason: 202, connection_error: 'authentication_failed' });
    assert.equal(view.element('status-title').textContent, 'Error de autenticación');

    // Unknown categories remain readable and never become DOM-supplied text.
    await view.update({ wifi_reason: 999, connection_error: '<unknown>' });
    assert.equal(view.element('status-title').textContent, 'No se pudo completar la conexión');
    await view.update({ wifi_reason: 999, connection_error: '__proto__' });
    assert.equal(view.element('status-title').textContent, 'No se pudo completar la conexión');
    await view.update({ wifi_reason: '15', connection_error: 'handshake_timeout' });
    assert.equal(view.element('status-title').textContent, 'No se pudo actualizar el estado');

    return 'connection status page: all regression checks passed';
}

export const result = await run();
