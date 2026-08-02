#!/usr/bin/env node
//
// Kanban board for claude_documentation/backlog.
//
//   node claude_documentation/board/server.js [port]
//   -> http://localhost:871
//
// No dependencies, Node stdlib only.
//
// The folders are the source of truth: a task's column is the directory it sits
// in, not a field inside the file. Moving the file moves the card. That way the
// board can never disagree with the repository.

'use strict';

const http = require('http');
const fs = require('fs');
const path = require('path');

const PORT = Number(process.argv[2] || process.env.PORT || 8071);
const BOARD_DIR = __dirname;
const BACKLOG_DIR = path.join(BOARD_DIR, '..', 'backlog');

const COLUMNS = [
  { id: 'open', dir: 'open', title: 'Open' },
  { id: 'inprogress', dir: 'inprogress', title: 'In Progress' },
  { id: 'completed', dir: 'completed', title: 'Completed' },
];

// --- task parsing ------------------------------------------------------------

// Pull "| **Field** | value |" out of the header table.
function field(md, name) {
  const m = md.match(new RegExp(`\\|\\s*\\*\\*${name}\\*\\*\\s*\\|([^|]*)\\|`, 'i'));
  return m ? m[1].trim() : '';
}

function parseTask(file, column) {
  const md = fs.readFileSync(file, 'utf8');
  const heading = md.match(/^#\s+(.+)$/m);
  const full = heading ? heading[1].trim() : path.basename(file, '.md');

  // "T0001 — Run the exe under wine" -> id + title. Accept em dash or hyphen.
  const split = full.match(/^(T\d+)\s*[—–-]\s*(.*)$/);

  const done = (md.match(/^\s*-\s*\[x\]/gim) || []).length;
  const todo = (md.match(/^\s*-\s*\[ \]/gim) || []).length;

  const status = field(md, 'Status');

  // "1 — Harden the build" -> order 1, label "Harden the build". Tasks without
  // a Phase sort last, under a "No phase" group.
  const phaseRaw = field(md, 'Phase');
  const pm = phaseRaw.match(/^\s*(\d+)\s*[—–-]?\s*(.*)$/);

  return {
    id: split ? split[1] : full,
    title: split ? split[2] : full,
    column,
    status,
    phase: phaseRaw || null,
    phaseOrder: pm ? Number(pm[1]) : 999,
    phaseLabel: pm ? (pm[2] || `Phase ${pm[1]}`) : 'No phase',
    // A blocked task still lives in open/; surface it so it can be styled.
    blocked: /blocked/i.test(status),
    priority: field(md, 'Priority') || '—',
    created: field(md, 'Created'),
    closed: field(md, 'Closed'),
    checksDone: done,
    checksTotal: done + todo,
    file: path.relative(path.join(BOARD_DIR, '..', '..'), file),
    mtime: fs.statSync(file).mtimeMs,
    markdown: md,
  };
}

function readBoard() {
  const columns = COLUMNS.map((c) => {
    const dir = path.join(BACKLOG_DIR, c.dir);
    let tasks = [];
    if (fs.existsSync(dir)) {
      tasks = fs
        .readdirSync(dir)
        .filter((f) => f.endsWith('.md') && f.toLowerCase() !== 'readme.md')
        .map((f) => parseTask(path.join(dir, f), c.id))
        // Phase first, then id -- so a column reads in the order work happens.
        .sort((a, b) => a.phaseOrder - b.phaseOrder || a.id.localeCompare(b.id));
    }
    return { ...c, tasks };
  });
  return { generated: new Date().toISOString(), columns };
}

// --- http --------------------------------------------------------------------

const server = http.createServer((req, res) => {
  const url = (req.url || '/').split('?')[0];

  try {
    if (url === '/api/tasks') {
      const body = JSON.stringify(readBoard());
      res.writeHead(200, {
        'Content-Type': 'application/json; charset=utf-8',
        // The board polls; never let a proxy or the browser serve a stale board.
        'Cache-Control': 'no-store',
      });
      return res.end(body);
    }

    if (url === '/' || url === '/index.html') {
      const html = fs.readFileSync(path.join(BOARD_DIR, 'index.html'));
      res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-store' });
      return res.end(html);
    }

    res.writeHead(404, { 'Content-Type': 'text/plain' });
    res.end('not found');
  } catch (err) {
    res.writeHead(500, { 'Content-Type': 'text/plain' });
    res.end(String((err && err.stack) || err));
  }
});

server.listen(PORT, () => {
  const b = readBoard();
  const counts = b.columns.map((c) => `${c.title} ${c.tasks.length}`).join(' | ');
  console.log(`backlog board -> http://localhost:${PORT}`);
  console.log(`watching       ${path.resolve(BACKLOG_DIR)}`);
  console.log(`current        ${counts}`);
});
