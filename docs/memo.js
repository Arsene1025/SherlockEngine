/*
 * memo.js — SherlockEngine 문서용 절 단위 메모 기능
 *
 * 각 문서 끝에 <script src="memo.js"></script> 한 줄만 넣으면 동작한다.
 * 페이지의 모든 h2/h3 옆에 메모 버튼을 붙이고, 입력한 내용은 브라우저의
 * localStorage에 "sherlock-memo:<문서 파일명>" 키로 저장한다. 서버가 필요 없고
 * HTML 파일은 건드리지 않으므로 문서를 갱신하거나 git으로 커밋해도 메모가 섞이지 않는다.
 *
 * 한계: 메모는 이 PC의 이 브라우저에만 남는다. 다른 곳으로 옮기려면 우측 하단
 * 패널의 「내보내기」로 JSON을 저장한 뒤 「가져오기」로 불러온다.
 *
 * 절 키 규칙
 *   - 제목에 id가 있으면 그 id를 키로 쓴다 (예: "s3-2"). 가장 안정적이다.
 *   - id가 없으면 "<가장 가까운 앞쪽 h2 id>/<제목 텍스트>"를 키로 쓴다.
 *     제목 문구가 바뀌면 그 메모는 고아가 되므로, 패널의 「고아 메모」 항목에서
 *     내용을 확인하고 지울 수 있다.
 */
(function () {
  'use strict';

  var PREFIX = 'sherlock-memo:';
  var VERSION = 1;

  // ---------- 저장소 ----------
  function docName() {
    var seg = location.pathname.split('/').pop() || 'index.html';
    try { seg = decodeURIComponent(seg); } catch (e) { /* 그대로 둔다 */ }
    return seg;
  }
  var KEY = PREFIX + docName();
  var storageOk = true;

  function loadDoc() {
    try {
      var raw = localStorage.getItem(KEY);
      if (!raw) return { version: VERSION, notes: {} };
      var obj = JSON.parse(raw);
      if (!obj || typeof obj !== 'object' || !obj.notes) return { version: VERSION, notes: {} };
      return obj;
    } catch (e) {
      storageOk = false;
      return { version: VERSION, notes: {} };
    }
  }
  function saveDoc(d) {
    try {
      localStorage.setItem(KEY, JSON.stringify(d));
      storageOk = true;
    } catch (e) {
      storageOk = false;
      setStatus('저장 실패: 브라우저 저장소를 쓸 수 없습니다 (시크릿 창이거나 저장소가 차단됨).', true);
    }
  }

  var data = loadDoc();

  // ---------- 절 키 ----------
  function normText(el) {
    var clone = el.cloneNode(true);
    var btns = clone.querySelectorAll('.memo-btn');
    for (var i = 0; i < btns.length; i++) btns[i].parentNode.removeChild(btns[i]);
    return (clone.textContent || '').replace(/\s+/g, ' ').trim().slice(0, 80);
  }
  function collectHeadings() {
    var all = document.querySelectorAll('h2, h3');
    var seen = {};
    var list = [];
    var lastH2 = 'top';
    for (var i = 0; i < all.length; i++) {
      var h = all[i];
      if (h.closest && h.closest('.memo-box, .memo-panel')) continue;
      if (h.tagName === 'H2' && h.id) lastH2 = h.id;
      var key = h.id ? h.id : (lastH2 + '/' + normText(h));
      var base = key, n = 2;
      while (seen[key]) key = base + '#' + (n++);
      seen[key] = true;
      list.push({ el: h, key: key });
    }
    return list;
  }

  // ---------- 스타일 ----------
  var css = '' +
    '.memo-btn{display:inline-block;vertical-align:middle;margin-left:10px;padding:1px 8px;border:1px solid var(--line,#ccc);' +
    'border-radius:999px;background:var(--surface,#fff);color:var(--muted,#888);font:inherit;font-size:11px;font-weight:500;line-height:1.6;' +
    'letter-spacing:0;cursor:pointer;opacity:.55;transition:opacity .15s,background .15s;user-select:none}' +
    'h2:hover .memo-btn,h3:hover .memo-btn,.memo-btn:focus{opacity:1}' +
    '.memo-btn.has{opacity:1;background:var(--amber-soft,#fbf0db);border-color:var(--amber,#b8781a);color:var(--amber,#b8781a)}' +
    '.memo-box{margin:8px 0 18px;padding:10px 12px 8px;border:1px solid var(--amber,#b8781a);border-left-width:4px;' +
    'border-radius:8px;background:var(--amber-soft,#fbf0db);color:var(--ink,#222);font-size:14px;line-height:1.6}' +
    '.memo-box textarea{display:block;width:100%;box-sizing:border-box;min-height:64px;resize:vertical;padding:8px 10px;' +
    'border:1px solid var(--line,#ccc);border-radius:6px;background:var(--surface,#fff);color:var(--ink,#222);' +
    'font:inherit;font-size:14px;line-height:1.6;outline:none}' +
    '.memo-box textarea:focus{border-color:var(--amber,#b8781a)}' +
    '.memo-box .memo-foot{display:flex;align-items:center;gap:12px;margin-top:6px;font-size:12px;color:var(--muted,#888)}' +
    '.memo-box .memo-foot .sp{flex:1}' +
    '.memo-box .memo-foot button,.memo-panel button,.memo-panel label.btn{padding:2px 9px;border:1px solid var(--line,#ccc);border-radius:6px;' +
    'background:var(--surface,#fff);color:var(--ink-2,#555);font:inherit;font-size:12px;cursor:pointer}' +
    '.memo-box .memo-foot button:hover,.memo-panel button:hover,.memo-panel label.btn:hover{border-color:var(--line-strong,#999)}' +
    '.memo-box .memo-foot button.del{color:var(--crit,#b23a3a)}' +
    '.memo-panel{position:fixed;right:16px;bottom:16px;z-index:9999;font-size:12px;line-height:1.5;color:var(--ink-2,#555)}' +
    '.memo-panel .toggle{display:flex;align-items:center;gap:6px;padding:6px 12px;border:1px solid var(--line,#ccc);border-radius:999px;' +
    'background:var(--surface,#fff);box-shadow:var(--shadow,0 2px 8px rgba(0,0,0,.15));cursor:pointer;color:var(--ink,#222);font-weight:500}' +
    '.memo-panel .toggle .cnt{min-width:18px;padding:0 6px;border-radius:999px;background:var(--amber-soft,#fbf0db);color:var(--amber,#b8781a);text-align:center}' +
    '.memo-panel .body{display:none;position:absolute;right:0;bottom:40px;width:280px;padding:12px;border:1px solid var(--line,#ccc);' +
    'border-radius:10px;background:var(--surface,#fff);box-shadow:var(--shadow,0 2px 8px rgba(0,0,0,.15))}' +
    '.memo-panel.open .body{display:block}' +
    '.memo-panel .row{display:flex;flex-wrap:wrap;gap:6px;margin-top:8px}' +
    '.memo-panel .status{margin-top:8px;color:var(--muted,#888);word-break:break-all}' +
    '.memo-panel .status.err{color:var(--crit,#b23a3a)}' +
    '.memo-panel .orphans{margin-top:8px;padding-top:8px;border-top:1px dashed var(--line,#ccc)}' +
    '.memo-panel .orphans .orow{display:flex;gap:6px;align-items:baseline;margin-top:4px}' +
    '.memo-panel .orphans .k{flex:1;font-family:"IBM Plex Mono",monospace;font-size:11px;color:var(--muted,#888);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}' +
    '.memo-panel input[type=file]{display:none}' +
    '.memo-panel .hint{margin-top:8px;color:var(--muted,#888)}' +
    '@media print{.memo-btn,.memo-panel{display:none!important}.memo-box{break-inside:avoid}}';
  var style = document.createElement('style');
  style.textContent = css;
  document.head.appendChild(style);

  // ---------- 절별 UI ----------
  var boxes = {};   // key -> {box, ta, btn, saved, h}
  var headings = collectHeadings();

  function fmtTime(ts) {
    if (!ts) return '';
    var d = new Date(ts);
    var p = function (n) { return (n < 10 ? '0' : '') + n; };
    return d.getFullYear() + '-' + p(d.getMonth() + 1) + '-' + p(d.getDate()) + ' ' + p(d.getHours()) + ':' + p(d.getMinutes());
  }
  function autoSize(ta) {
    ta.style.height = 'auto';
    ta.style.height = Math.max(64, ta.scrollHeight + 2) + 'px';
  }
  function refreshBtn(key) {
    var b = boxes[key];
    var has = !!(data.notes[key] && data.notes[key].text);
    b.btn.className = 'memo-btn' + (has ? ' has' : '');
    b.btn.textContent = has ? '메모 ✎' : '메모 +';
    b.btn.title = has ? '메모 있음 (' + fmtTime(data.notes[key].updated) + ')' : '이 절에 메모 남기기';
  }
  function openBox(key, focus) {
    var b = boxes[key];
    b.box.style.display = 'block';
    b.ta.value = (data.notes[key] && data.notes[key].text) || '';
    b.saved.textContent = data.notes[key] ? '저장됨 ' + fmtTime(data.notes[key].updated) : '';
    autoSize(b.ta);
    if (focus) b.ta.focus();
  }
  function closeBox(key) { boxes[key].box.style.display = 'none'; }
  function isOpen(key) { return boxes[key].box.style.display !== 'none'; }

  function commit(key) {
    var b = boxes[key];
    var text = b.ta.value;
    if (text.trim() === '') {
      if (data.notes[key]) { delete data.notes[key]; saveDoc(data); }
      b.saved.textContent = '';
    } else {
      var prev = data.notes[key];
      if (!prev || prev.text !== text) data.notes[key] = { text: text, updated: Date.now() };
      saveDoc(data);
      b.saved.textContent = storageOk ? '저장됨 ' + fmtTime(data.notes[key].updated) : '저장 실패';
    }
    refreshBtn(key);
    updateCount();
  }

  headings.forEach(function (it) {
    var h = it.el, key = it.key;
    var btn = document.createElement('button');
    btn.type = 'button';
    btn.setAttribute('aria-label', '메모');
    h.appendChild(btn);

    var box = document.createElement('div');
    box.className = 'memo-box';
    box.style.display = 'none';
    var ta = document.createElement('textarea');
    ta.placeholder = '이 절에 대한 메모… (입력하면 자동 저장)';
    ta.spellcheck = false;
    var foot = document.createElement('div');
    foot.className = 'memo-foot';
    var saved = document.createElement('span');
    var sp = document.createElement('span'); sp.className = 'sp';
    var del = document.createElement('button'); del.type = 'button'; del.className = 'del'; del.textContent = '삭제';
    var close = document.createElement('button'); close.type = 'button'; close.textContent = '접기';
    foot.appendChild(saved); foot.appendChild(sp); foot.appendChild(del); foot.appendChild(close);
    box.appendChild(ta); box.appendChild(foot);
    h.parentNode.insertBefore(box, h.nextSibling);

    boxes[key] = { box: box, ta: ta, btn: btn, saved: saved, h: h };

    var timer = null;
    ta.addEventListener('input', function () {
      autoSize(ta);
      clearTimeout(timer);
      timer = setTimeout(function () { commit(key); }, 400);
    });
    ta.addEventListener('blur', function () { clearTimeout(timer); commit(key); });
    del.addEventListener('click', function () {
      if (ta.value.trim() && !confirm('이 절의 메모를 삭제할까요?')) return;
      ta.value = ''; commit(key); closeBox(key);
    });
    close.addEventListener('click', function () { clearTimeout(timer); commit(key); closeBox(key); });
    btn.addEventListener('click', function (e) {
      e.preventDefault();
      if (isOpen(key)) { commit(key); closeBox(key); } else { openBox(key, true); }
    });

    refreshBtn(key);
    if (data.notes[key] && data.notes[key].text) openBox(key, false);
  });

  // ---------- 우측 하단 패널 ----------
  var panel = document.createElement('div');
  panel.className = 'memo-panel';
  panel.innerHTML =
    '<div class="toggle" title="이 문서의 메모 관리"><span>메모</span><span class="cnt">0</span></div>' +
    '<div class="body">' +
      '<div><b>' + docName().replace(/</g, '&lt;') + '</b></div>' +
      '<div class="row">' +
        '<button type="button" data-act="expand">모두 펼치기</button>' +
        '<button type="button" data-act="collapse">모두 접기</button>' +
      '</div>' +
      '<div class="row">' +
        '<button type="button" data-act="export">내보내기</button>' +
        '<button type="button" data-act="export-all">전체 문서 내보내기</button>' +
        '<label class="btn">가져오기<input type="file" accept="application/json,.json"></label>' +
        '<button type="button" data-act="clear">이 문서 메모 전체 삭제</button>' +
      '</div>' +
      '<div class="orphans" style="display:none"><div class="ohead" style="color:var(--muted,#888)">고아 메모 (제목이 바뀌어 붙일 곳이 없음)</div></div>' +
      '<div class="status"></div>' +
      '<div class="hint">메모는 이 브라우저의 localStorage에만 저장됩니다. 다른 PC로 옮기려면 내보내기 → 가져오기를 쓰세요.</div>' +
    '</div>';
  document.body.appendChild(panel);

  var statusEl = panel.querySelector('.status');
  var cntEl = panel.querySelector('.cnt');
  var orphansEl = panel.querySelector('.orphans');
  var fileInput = panel.querySelector('input[type=file]');

  function setStatus(msg, isErr) {
    if (!statusEl) return;
    statusEl.textContent = msg || '';
    statusEl.className = 'status' + (isErr ? ' err' : '');
  }
  function updateCount() {
    var n = 0, k;
    for (k in data.notes) if (data.notes[k] && data.notes[k].text) n++;
    cntEl.textContent = n;
    renderOrphans();
  }
  function renderOrphans() {
    var rows = [];
    for (var k in data.notes) if (!boxes[k] && data.notes[k] && data.notes[k].text) rows.push(k);
    var old = orphansEl.querySelectorAll('.orow');
    for (var i = 0; i < old.length; i++) orphansEl.removeChild(old[i]);
    orphansEl.style.display = rows.length ? 'block' : 'none';
    rows.forEach(function (key) {
      var row = document.createElement('div'); row.className = 'orow';
      var kk = document.createElement('span'); kk.className = 'k'; kk.textContent = key; kk.title = data.notes[key].text;
      var show = document.createElement('button'); show.type = 'button'; show.textContent = '보기';
      var rm = document.createElement('button'); rm.type = 'button'; rm.textContent = '삭제';
      show.addEventListener('click', function () { alert('[' + key + ']\n\n' + data.notes[key].text); });
      rm.addEventListener('click', function () {
        if (!confirm('고아 메모를 삭제할까요?\n\n' + data.notes[key].text.slice(0, 200))) return;
        delete data.notes[key]; saveDoc(data); updateCount();
      });
      row.appendChild(kk); row.appendChild(show); row.appendChild(rm);
      orphansEl.appendChild(row);
    });
  }

  function download(name, text) {
    var blob = new Blob([text], { type: 'application/json' });
    var url = URL.createObjectURL(blob);
    var a = document.createElement('a');
    a.href = url; a.download = name;
    document.body.appendChild(a); a.click(); document.body.removeChild(a);
    setTimeout(function () { URL.revokeObjectURL(url); }, 1000);
  }
  function stamp() {
    var d = new Date(), p = function (n) { return (n < 10 ? '0' : '') + n; };
    return d.getFullYear() + p(d.getMonth() + 1) + p(d.getDate()) + '-' + p(d.getHours()) + p(d.getMinutes());
  }
  function exportThis() {
    download(docName().replace(/\.html?$/i, '') + '-memo-' + stamp() + '.json',
      JSON.stringify({ format: 'sherlock-memo', version: VERSION, doc: docName(), exportedAt: new Date().toISOString(), notes: data.notes }, null, 2));
    setStatus('이 문서의 메모를 내보냈습니다.');
  }
  function exportAll() {
    var docs = {}, n = 0;
    try {
      for (var i = 0; i < localStorage.length; i++) {
        var k = localStorage.key(i);
        if (k && k.indexOf(PREFIX) === 0) {
          try { docs[k.slice(PREFIX.length)] = JSON.parse(localStorage.getItem(k)).notes || {}; n++; } catch (e) { /* 손상된 항목은 건너뜀 */ }
        }
      }
    } catch (e) { setStatus('저장소를 읽을 수 없습니다.', true); return; }
    download('sherlock-memo-all-' + stamp() + '.json',
      JSON.stringify({ format: 'sherlock-memo-bundle', version: VERSION, exportedAt: new Date().toISOString(), docs: docs }, null, 2));
    setStatus(n + '개 문서의 메모를 내보냈습니다.');
  }
  function mergeInto(target, notes) {
    var n = 0;
    for (var k in notes) {
      var v = notes[k];
      if (!v || typeof v.text !== 'string' || !v.text.trim()) continue;
      var cur = target[k];
      if (cur && cur.updated && v.updated && cur.updated >= v.updated && cur.text === v.text) continue;
      target[k] = { text: v.text, updated: v.updated || Date.now() };
      n++;
    }
    return n;
  }
  function refreshAll() {
    for (var key in boxes) {
      refreshBtn(key);
      if (data.notes[key] && data.notes[key].text) openBox(key, false); else if (isOpen(key)) closeBox(key);
    }
    updateCount();
  }
  function importFile(file) {
    var r = new FileReader();
    r.onload = function () {
      var obj;
      try { obj = JSON.parse(r.result); } catch (e) { setStatus('JSON을 읽을 수 없습니다.', true); return; }
      var applied = 0, docsTouched = 0;
      if (obj && obj.format === 'sherlock-memo-bundle' && obj.docs) {
        for (var name in obj.docs) {
          var k = PREFIX + name, cur;
          try { cur = JSON.parse(localStorage.getItem(k) || 'null'); } catch (e) { cur = null; }
          if (!cur || !cur.notes) cur = { version: VERSION, notes: {} };
          var n = mergeInto(cur.notes, obj.docs[name]);
          if (name === docName()) data = cur;
          try { localStorage.setItem(k, JSON.stringify(cur)); } catch (e) { setStatus('저장 실패', true); return; }
          applied += n; docsTouched++;
        }
        setStatus(docsTouched + '개 문서에서 ' + applied + '건을 가져왔습니다.');
      } else if (obj && obj.format === 'sherlock-memo' && obj.notes) {
        if (obj.doc && obj.doc !== docName() &&
            !confirm('이 파일은 「' + obj.doc + '」의 메모입니다. 현재 문서(' + docName() + ')에 붙일까요?')) { setStatus('가져오기를 취소했습니다.'); return; }
        applied = mergeInto(data.notes, obj.notes);
        saveDoc(data);
        setStatus(applied + '건을 가져왔습니다.');
      } else {
        setStatus('memo.js가 만든 파일이 아닙니다.', true); return;
      }
      refreshAll();
    };
    r.readAsText(file);
  }

  panel.querySelector('.toggle').addEventListener('click', function () { panel.classList.toggle('open'); });
  panel.addEventListener('click', function (e) {
    var act = e.target.getAttribute && e.target.getAttribute('data-act');
    if (!act) return;
    if (act === 'expand') { for (var k in boxes) openBox(k, false); }
    else if (act === 'collapse') { for (var k2 in boxes) { if (isOpen(k2)) { commit(k2); closeBox(k2); } } }
    else if (act === 'export') exportThis();
    else if (act === 'export-all') exportAll();
    else if (act === 'clear') {
      if (!confirm('이 문서의 메모를 모두 삭제할까요? 되돌릴 수 없습니다.')) return;
      data = { version: VERSION, notes: {} }; saveDoc(data);
      refreshAll(); setStatus('모두 삭제했습니다.');
    }
  });
  fileInput.addEventListener('change', function () {
    if (fileInput.files && fileInput.files[0]) importFile(fileInput.files[0]);
    fileInput.value = '';
  });
  document.addEventListener('click', function (e) {
    if (panel.classList.contains('open') && !panel.contains(e.target)) panel.classList.remove('open');
  });

  updateCount();
  if (!storageOk) setStatus('브라우저 저장소를 쓸 수 없어 메모가 저장되지 않습니다.', true);
})();
