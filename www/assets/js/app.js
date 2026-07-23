/* ============================
   WEBSERV — Dashboard Logic
   ============================ */

(function () {
  'use strict';

  // ── Helpers ──────────────────────────────────────────

  const $ = (sel, ctx = document) => ctx.querySelector(sel);
  const $$ = (sel, ctx = document) => [...ctx.querySelectorAll(sel)];

  // Toast notifications
  function toast(message, type = 'info') {
    const container = $('.toast-container') || (() => {
      const el = document.createElement('div');
      el.className = 'toast-container';
      document.body.appendChild(el);
      return el;
    })();

    const el = document.createElement('div');
    el.className = `toast toast--${type}`;
    const icons = { success: '✓', error: '✕', info: 'ℹ' };
    el.innerHTML = `<span class="toast__icon">${icons[type] || icons.info}</span><span>${message}</span>`;
    container.appendChild(el);

    setTimeout(() => {
      el.style.opacity = '0';
      el.style.transform = 'translateX(40px)';
      el.style.transition = '0.3s ease';
      setTimeout(() => el.remove(), 300);
    }, 4000);
  }

  // Show response panel — supports optional media preview
  function showResponse(panelId, status, headers, body, mediaOpts) {
    const panel = document.getElementById(panelId);
    if (!panel) return;

    const statusEl = panel.querySelector('.response-panel__status');
    const bodyEl = panel.querySelector('.response-panel__body');

    const isOk = status >= 200 && status < 400;
    statusEl.textContent = `${status}`;
    statusEl.className = `response-panel__status ${isOk ? 'response-panel__status--success' : 'response-panel__status--error'}`;

    let output = '';
    if (headers) {
      output += '── Headers ──\n';
      for (const [k, v] of headers.entries()) {
        output += `${k}: ${v}\n`;
      }
      output += '\n';
    }

    // If media, render inline preview instead of raw bytes
    if (mediaOpts && mediaOpts.type === 'image') {
      bodyEl.innerHTML = output + '── Preview ──\n';
      const img = document.createElement('img');
      img.src = mediaOpts.url;
      img.alt = 'Response image';
      img.style.cssText = 'max-width:100%;border-radius:12px;margin-top:10px;';
      bodyEl.appendChild(img);
    } else if (mediaOpts && mediaOpts.type === 'video') {
      bodyEl.innerHTML = output + '── Preview ──\n';
      const vid = document.createElement('video');
      vid.src = mediaOpts.url;
      vid.controls = true;
      vid.style.cssText = 'max-width:100%;border-radius:12px;margin-top:10px;';
      bodyEl.appendChild(vid);
    } else if (mediaOpts && mediaOpts.type === 'html') {
      bodyEl.innerHTML = output + '── Preview ──\n';
      const iframe = document.createElement('iframe');
      iframe.srcdoc = body;
      iframe.style.cssText = 'width:100%;height:400px;border:none;border-radius:12px;margin-top:10px;background:#fff;';
      bodyEl.appendChild(iframe);
    } else {
      output += '── Body ──\n';
      output += body || '(empty)';
      bodyEl.textContent = output;
    }

    panel.classList.add('visible');
  }

  // Set button loading state
  function setLoading(btn, loading) {
    if (loading) {
      btn.dataset.originalText = btn.innerHTML;
      btn.innerHTML = `<span class="spinner"></span> Sending…`;
      btn.disabled = true;
    } else {
      btn.innerHTML = btn.dataset.originalText || btn.innerHTML;
      btn.disabled = false;
    }
  }

  // ── Tab Navigation ──────────────────────────────────

  function initTabs() {
    const btns = $$('.nav-tabs__btn');
    const sections = $$('.section');

    btns.forEach(btn => {
      btn.addEventListener('click', () => {
        btns.forEach(b => b.classList.remove('active'));
        sections.forEach(s => s.classList.remove('active'));
        btn.classList.add('active');
        const target = document.getElementById(btn.dataset.target);
        if (target) target.classList.add('active');
      });
    });
  }

  // ── GET Tester ──────────────────────────────────────

  function initGetTester() {
    const form = $('#get-form');
    if (!form) return;

    form.addEventListener('submit', async (e) => {
      e.preventDefault();
      const url = $('#get-url').value.trim();
      if (!url) return toast('Please enter a URL', 'error');

      const btn = form.querySelector('.btn');
      setLoading(btn, true);

      try {
        const res = await fetch(url);
        const ct = (res.headers.get('content-type') || '').toLowerCase();

        // Detect media and show a preview
        if (ct.startsWith('image/')) {
          showResponse('get-response', res.status, res.headers, '', { type: 'image', url });
        } else if (ct.startsWith('video/')) {
          showResponse('get-response', res.status, res.headers, '', { type: 'video', url });
        } else {
          const body = await res.text();
          showResponse('get-response', res.status, res.headers, body, { type: ct.includes('html') ? 'html' : undefined });
        }
        toast(`GET ${res.status} — ${res.statusText}`, res.ok ? 'success' : 'error');
      } catch (err) {
        showResponse('get-response', 0, null, `Error: ${err.message}`);
        toast(`Request failed: ${err.message}`, 'error');
      } finally {
        setLoading(btn, false);
      }
    });
  }

  // ── POST Tester ─────────────────────────────────────

  function initPostTester() {
    const textForm = $('#post-text-form');
    const fileForm = $('#post-file-form');

    // Post text / JSON
    if (textForm) {
      textForm.addEventListener('submit', async (e) => {
        e.preventDefault();
        const url = $('#post-url').value.trim();
        const contentType = $('#post-content-type').value;
        const body = $('#post-body').value;
        if (!url) return toast('Please enter a URL', 'error');

        const btn = textForm.querySelector('.btn');
        setLoading(btn, true);

        try {
          const res = await fetch(url, {
            method: 'POST',
            headers: { 'Content-Type': contentType },
            body: body,
          });
          const ct = (res.headers.get('content-type') || '').toLowerCase();
          const text = await res.text();
          showResponse('post-response', res.status, res.headers, text, { type: ct.includes('html') ? 'html' : undefined });
          toast(`POST ${res.status} — ${res.statusText}`, res.ok ? 'success' : 'error');
        } catch (err) {
          showResponse('post-response', 0, null, `Error: ${err.message}`);
          toast(`Request failed: ${err.message}`, 'error');
        } finally {
          setLoading(btn, false);
        }
      });
    }

    // Post file upload
    if (fileForm) {
      fileForm.addEventListener('submit', async (e) => {
        e.preventDefault();
        const url = $('#post-file-url').value.trim();
        const fileInput = $('#post-file-input');
        if (!url) return toast('Please enter an upload URL', 'error');
        if (!fileInput.files.length) return toast('Please select a file', 'error');

        const file = fileInput.files[0];
        let targetUrl = url;
        if (!targetUrl.endsWith('/')) targetUrl += '/';
        targetUrl += encodeURIComponent(file.name);

        const btn = fileForm.querySelector('.btn');
        setLoading(btn, true);

        try {
          const res = await fetch(targetUrl, { 
            method: 'POST', 
            body: file,
            headers: { 'Content-Type': file.type || 'application/octet-stream' }
          });
          const text = await res.text();
          showResponse('post-response', res.status, res.headers, text, { type: ct.includes('html') ? 'html' : undefined });
          toast(`Upload ${res.status} — ${res.statusText}`, res.ok ? 'success' : 'error');

        } catch (err) {
          showResponse('post-response', 0, null, `Error: ${err.message}`);
          toast(`Upload failed: ${err.message}`, 'error');
        } finally {
          setLoading(btn, false);
        }
      });

      // File input visual feedback
      const fileInput = $('#post-file-input');
      const fileName = $('#post-file-name');
      if (fileInput && fileName) {
        fileInput.addEventListener('change', () => {
          fileName.textContent = fileInput.files[0] ? fileInput.files[0].name : '';
        });
      }
    }
  }

  // ── DELETE Tester ───────────────────────────────────

  function initDeleteTester() {
    const form = $('#delete-form');
    if (!form) return;

    form.addEventListener('submit', async (e) => {
      e.preventDefault();
      const url = $('#delete-url').value.trim();
      if (!url) return toast('Please enter a URL', 'error');

      const btn = form.querySelector('.btn');
      setLoading(btn, true);

      try {
        const res = await fetch(url, { method: 'DELETE' });
        const ct = (res.headers.get('content-type') || '').toLowerCase();
        const body = await res.text();
        showResponse('delete-response', res.status, res.headers, body, { type: ct.includes('html') ? 'html' : undefined });
        toast(`DELETE ${res.status} — ${res.statusText}`, res.ok ? 'success' : 'error');

      } catch (err) {
        showResponse('delete-response', 0, null, `Error: ${err.message}`);
        toast(`Request failed: ${err.message}`, 'error');
      } finally {
        setLoading(btn, false);
      }
    });
  }

  // ── CGI Tester ──────────────────────────────────────

  function initCgiTester() {
    $$('.cgi-run-btn').forEach(btn => {
      btn.addEventListener('click', async () => {
        const card = btn.closest('.cgi-card');
        const script = card.dataset.script;
        const method = card.dataset.method || 'GET';
        const inputEl = card.querySelector('.cgi-input');
        const body = inputEl ? inputEl.value : '';

        setLoading(btn, true);

        try {
          const opts = { method };
          if (method === 'POST' && body) {
            opts.headers = { 'Content-Type': 'text/plain' };
            opts.body = body;
          }
          const res = await fetch(`/cgi-bin/${script}`, opts);
          const ct = (res.headers.get('content-type') || '').toLowerCase();
          const text = await res.text();
          showResponse('cgi-response', res.status, res.headers, text, { type: ct.includes('html') ? 'html' : undefined });
          toast(`CGI ${res.status} — ${script}`, res.ok ? 'success' : 'error');
        } catch (err) {
          showResponse('cgi-response', 0, null, `Error: ${err.message}`);
          toast(`CGI failed: ${err.message}`, 'error');
        } finally {
          setLoading(btn, false);
        }
      });
    });
  }


  // ── Lightbox ────────────────────────────────────────

  function openLightbox(src, type) {
    const lb = $('#lightbox');
    const content = $('#lightbox-content');
    if (!lb || !content) return;

    if (type === 'video') {
      content.innerHTML = `<video src="${src}" controls autoplay style="max-width:90vw;max-height:85vh;border-radius:22px;box-shadow:0 20px 60px rgba(0,0,0,.4)"></video>`;
    } else {
      content.innerHTML = `<img src="${src}" alt="" />`;
    }
    lb.classList.add('open');
  }

  function initLightbox() {
    const lb = $('#lightbox');
    if (!lb) return;

    lb.addEventListener('click', (e) => {
      if (e.target === lb || e.target.classList.contains('lightbox__close')) {
        lb.classList.remove('open');
        const vid = lb.querySelector('video');
        if (vid) vid.pause();
      }
    });

    document.addEventListener('keydown', (e) => {
      if (e.key === 'Escape' && lb.classList.contains('open')) {
        lb.classList.remove('open');
        const vid = lb.querySelector('video');
        if (vid) vid.pause();
      }
    });
  }

  // ── Drag & Drop ─────────────────────────────────────

  function initDragDrop() {
    $$('.file-drop').forEach(drop => {
      drop.addEventListener('dragover', (e) => { e.preventDefault(); drop.classList.add('dragover'); });
      drop.addEventListener('dragleave', () => drop.classList.remove('dragover'));
      drop.addEventListener('drop', (e) => {
        e.preventDefault();
        drop.classList.remove('dragover');
        const input = drop.querySelector('input[type="file"]');
        if (input && e.dataTransfer.files.length) {
          input.files = e.dataTransfer.files;
          input.dispatchEvent(new Event('change'));
        }
      });
    });
  }

  // ── Theme Toggle ─────────────────────────────────────

  function initThemeToggle() {
    const btn = $('#theme-toggle');
    if (!btn) return;

    // Restore saved preference
    const saved = localStorage.getItem('webserv-theme');
    if (saved === 'dark') {
      document.body.classList.add('dark');
      btn.textContent = '🌙';
    }

    btn.addEventListener('click', () => {
      const isDark = document.body.classList.toggle('dark');
      btn.textContent = isDark ? '🌙' : '☀️';
      localStorage.setItem('webserv-theme', isDark ? 'dark' : 'light');
    });
  }

  // ── Cookie & Session Tester ──────────────────────────

  function parseBrowserCookies() {
    const cookies = {};
    if (document.cookie) {
      document.cookie.split(';').forEach(c => {
        const [k, ...v] = c.trim().split('=');
        if (k) cookies[k.trim()] = v.join('=').trim();
      });
    }
    return cookies;
  }

  function renderCookieViewer() {
    const viewer = $('#cookie-viewer');
    if (!viewer) return;

    const cookies = parseBrowserCookies();
    const entries = Object.entries(cookies);

    if (entries.length === 0) {
      viewer.innerHTML = '<div class="cookie-empty">🍪 No cookies set for this domain</div>';
      return;
    }

    viewer.innerHTML = entries.map(([name, value]) => `
      <div class="cookie-item">
        <span class="cookie-item__name">${name}</span>
        <span class="cookie-item__value" title="${value}">${value}</span>
        <button class="cookie-item__delete" title="Delete ${name}" data-cookie="${name}">✕</button>
      </div>
    `).join('');

    // Attach individual delete handlers
    $$('.cookie-item__delete', viewer).forEach(btn => {
      btn.addEventListener('click', () => {
        const name = btn.dataset.cookie;
        document.cookie = `${name}=; Path=/; Max-Age=0`;
        toast(`Cookie "${name}" deleted`, 'success');
        renderCookieViewer();
      });
    });
  }

  async function callSessionCgi(action, params = {}) {
    const qs = new URLSearchParams({ action, ...params }).toString();
    try {
      const res = await fetch(`/cgi-bin/session.py?${qs}`);
      const ct = (res.headers.get('content-type') || '').toLowerCase();
      const text = await res.text();
      showResponse('cookie-response', res.status, res.headers, text, { type: ct.includes('html') ? 'html' : undefined });
      toast(`${action} — ${res.status}`, res.ok ? 'success' : 'error');
      // Refresh cookie viewer after CGI call
      setTimeout(renderCookieViewer, 200);
    } catch (err) {
      showResponse('cookie-response', 0, null, `Error: ${err.message}`);
      toast(`Request failed: ${err.message}`, 'error');
    }
  }

  function initCookieTester() {
    // Refresh viewer button
    const refreshBtn = $('#cookie-refresh-btn');
    if (refreshBtn) refreshBtn.addEventListener('click', renderCookieViewer);

    // Clear all cookies
    const clearAllBtn = $('#cookie-clear-all-btn');
    if (clearAllBtn) {
      clearAllBtn.addEventListener('click', () => {
        const cookies = parseBrowserCookies();
        const names = Object.keys(cookies);
        names.forEach(name => {
          document.cookie = `${name}=; Path=/; Max-Age=0`;
        });
        toast(`Cleared ${names.length} cookie(s)`, 'success');
        renderCookieViewer();
      });
    }

    // Set cookie form
    const setForm = $('#cookie-set-form');
    if (setForm) {
      setForm.addEventListener('submit', async (e) => {
        e.preventDefault();
        const name = $('#cookie-name').value.trim();
        const value = $('#cookie-value').value.trim();
        const maxAge = $('#cookie-maxage').value || '3600';
        if (!name) return toast('Please enter a cookie name', 'error');
        await callSessionCgi('set', { name, value, max_age: maxAge });
      });
    }

    // Delete cookie form
    const deleteForm = $('#cookie-delete-form');
    if (deleteForm) {
      deleteForm.addEventListener('submit', async (e) => {
        e.preventDefault();
        const name = $('#cookie-delete-name').value.trim();
        if (!name) return toast('Please enter a cookie name', 'error');
        await callSessionCgi('delete', { name });
      });
    }

    // Session create
    const createBtn = $('#session-create-btn');
    if (createBtn) createBtn.addEventListener('click', () => callSessionCgi('session'));

    // Session destroy
    const destroyBtn = $('#session-destroy-btn');
    if (destroyBtn) destroyBtn.addEventListener('click', () => callSessionCgi('destroy'));

    // Cookie status
    const statusBtn = $('#session-status-btn');
    if (statusBtn) statusBtn.addEventListener('click', () => callSessionCgi('status'));

    // Render cookies when tab is opened
    $$('.nav-tabs__btn').forEach(btn => {
      if (btn.dataset.target === 'section-cookies') {
        btn.addEventListener('click', renderCookieViewer);
      }
    });
  }

  // ── Init ────────────────────────────────────────────

  document.addEventListener('DOMContentLoaded', () => {
    initThemeToggle();
    initTabs();
    initGetTester();
    initPostTester();
    initDeleteTester();
    initCgiTester();
    initCookieTester();
    initLightbox();
    initDragDrop();
  });

})();
