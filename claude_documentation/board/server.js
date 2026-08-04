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
const DECISION_LOG = path.join(BOARD_DIR, '..', 'documentation', '02-decision-log.md');

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
    complexity: field(md, 'Complexity') || '',
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


// --- architecture map ---------------------------------------------------------
//
// Tickets grouped into the architectural structure they build, which is a
// different cut from phases: a phase is *when*, a layer is *where in the stack*.
// Ticket ids are listed explicitly rather than derived from phase, because
// several subsystems deliberately span phases (profiling, gameplay, content).

const ARCHITECTURE = [
  { layer: 'Foundation', blurb: 'Build, toolchain and verification', groups: [
    { name: 'Build harness',      tickets: ['T0001','T0002','T0003','T0010','T0004','T0011'] },
    { name: 'Test & CI',          tickets: ['T0012','T0084'] },
    { name: 'Conventions',        tickets: ['T0055','T0056'] },
    { name: 'Crash handling',     tickets: ['T0099'] },
  ]},
  { layer: 'Engine core', blurb: 'Application lifetime and the services everything sits on', groups: [
    { name: 'App & window',       tickets: ['T0013','T0014','T0015'] },
    { name: 'Layers & events',    tickets: ['T0017','T0018'] },
    { name: 'Identity',           tickets: ['T0016'] },
    { name: 'Reflection',         tickets: ['T0053'] },
    { name: 'Diagnostics',        tickets: ['T0054','T0019'] },
    { name: 'Time',               tickets: ['T0057'] },
    { name: 'Frame lifecycle',    tickets: ['T0100'] },
    { name: 'Input',              tickets: ['T0068'] },
    { name: 'Threading',          tickets: ['T0026','T0050'] },
  ]},
  { layer: 'Data model', blurb: 'What a project, scene and asset actually are', groups: [
    { name: 'Serialization',      tickets: ['T0020','T0082'] },
    { name: 'Scene & ECS',        tickets: ['T0021','T0022','T0077'] },
    { name: 'Transforms',         tickets: ['T0101'] },
    { name: 'Assets',             tickets: ['T0023','T0058'] },
    { name: 'Project & settings', tickets: ['T0024','T0078'] },
    { name: 'Composition',        tickets: ['T0059','T0071'] },
    { name: 'Communication',      tickets: ['T0072','T0074','T0075'] },
  ]},
  { layer: 'Gameplay', blurb: 'How the developer attaches and runs their own code', groups: [
    { name: 'Hot-reload module',  tickets: ['T0048','T0095'] },
    { name: 'Behaviours',         tickets: ['T0062'] },
    { name: 'Autoloads',          tickets: ['T0076'] },
    { name: 'Utilities',          tickets: ['T0073'] },
  ]},
  { layer: 'Rendering', blurb: 'Device, passes, materials and light', groups: [
    { name: 'Device & stack',     tickets: ['T0025','T0027','T0046','T0047'] },
    { name: 'Submission',         tickets: ['T0028','T0045','T0081','T0085'] },
    { name: 'Materials',          tickets: ['T0060'] },
    { name: 'HDR & colour',       tickets: ['T0096'] },
    { name: 'Lighting',           tickets: ['T0079','T0086','T0087'] },
    { name: 'Effects',            tickets: ['T0080','T0061'] },
    { name: 'Visibility',         tickets: ['T0093'] },
    { name: 'Extensibility',      tickets: ['T0094'] },
  ]},
  { layer: 'Content pipeline', blurb: 'Getting authored assets into the engine', groups: [
    { name: 'Mesh import',        tickets: ['T0038','T0009'] },
    { name: 'Texture import',     tickets: ['T0097'] },
    { name: 'LOD',                tickets: ['T0039','T0040'] },
    { name: 'Animation',          tickets: ['T0041','T0049','T0005'] },
  ]},
  { layer: 'World & environment', blurb: 'Sky, weather and atmosphere', groups: [
    { name: 'Sky & time',         tickets: ['T0088'] },
    { name: 'Atmospherics',       tickets: ['T0089','T0091'] },
    { name: 'Weather',            tickets: ['T0090','T0092'] },
  ]},
  { layer: 'Simulation', blurb: 'Physics and audio', groups: [
    { name: 'Physics',            tickets: ['T0051'] },
    { name: 'Audio',              tickets: ['T0052'] },
    { name: 'Navigation',         tickets: ['T0098'] },
  ]},
  { layer: 'Editor', blurb: 'Authoring tools — never shipped with the game', groups: [
    { name: 'Shell & panels',     tickets: ['T0032','T0033','T0034','T0066','T0067'] },
    { name: 'Scene authoring',    tickets: ['T0035','T0036','T0063','T0064'] },
    { name: 'Editing model',      tickets: ['T0065','T0037'] },
    { name: 'Probe (retired)',    tickets: ['T0007'] },
  ]},
  { layer: 'Shipping', blurb: 'The second consumer of the engine, and the player', groups: [
    { name: 'Runtime & export',   tickets: ['T0042','T0043'] },
    { name: 'Player state',       tickets: ['T0083'] },
    { name: 'Game UI',            tickets: ['T0069'] },
    { name: 'Networking',         tickets: ['T0070'] },
  ]},
  { layer: 'Cross-cutting', blurb: 'Spans every layer', groups: [
    { name: 'Profiling',          tickets: ['T0029','T0030','T0031'] },
    { name: 'Deferred',           tickets: ['T0008'] },
  ]},
];

// Dependencies are read from the tickets themselves: any "T0053"-style mention
// in a ticket body is treated as a reference. Derived rather than hand-authored,
// so the graph cannot drift from the backlog.
function readArchitecture() {
  const board = readBoard();
  const byId = new Map();
  for (const c of board.columns) for (const t of c.tasks) byId.set(t.id, t);

  const edges = [];
  for (const [id, t] of byId) {
    const seen = new Set();
    for (const m of t.markdown.matchAll(/\bT(\d{4})\b/g)) {
      const other = 'T' + m[1];
      if (other !== id && byId.has(other) && !seen.has(other)) {
        seen.add(other);
        edges.push({ from: id, to: other });
      }
    }
  }

  const placed = new Set();
  const layers = ARCHITECTURE.map((L) => ({
    layer: L.layer,
    blurb: L.blurb,
    groups: L.groups.map((g) => {
      const tasks = g.tickets.filter((id) => byId.has(id)).map((id) => {
        placed.add(id);
        const t = byId.get(id);
        return { id, title: t.title, status: t.status, column: t.column,
                 complexity: t.complexity, priority: t.priority,
                 checksDone: t.checksDone, checksTotal: t.checksTotal, file: t.file };
      });
      return { name: g.name, tasks };
    }),
  }));

  // Anything not placed is a mapping bug -- surface it rather than hide it.
  const unplaced = [...byId.keys()].filter((id) => !placed.has(id)).sort();
  return { generated: new Date().toISOString(), layers, edges, unplaced };
}

// --- decision log --------------------------------------------------------------
//
// Unlike a ticket, a decision has no header table, no Status, no checkboxes --
// it's just prose under a "## D<N> -- title" heading. There is nothing here
// worth parsing server-side, so unlike readArchitecture() above, this does
// none: the raw file goes to the client as-is, and decisions.html splits it
// into per-decision sections and renders each with the same renderMarkdown()
// the ticket detail panel uses (see markdown.js). That keeps the "## D<N> --
// title" convention understood in exactly one place instead of two, and means
// a new decision appended to the file needs no change here at all.
function readDecisionLog() {
  return { generated: new Date().toISOString(), markdown: fs.readFileSync(DECISION_LOG, 'utf8') };
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

    if (url === '/api/architecture') {
      res.writeHead(200, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store' });
      return res.end(JSON.stringify(readArchitecture()));
    }

    if (url === '/api/decisions') {
      res.writeHead(200, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store' });
      return res.end(JSON.stringify(readDecisionLog()));
    }

    if (url === '/architecture' || url === '/architecture.html') {
      const html = fs.readFileSync(path.join(BOARD_DIR, 'architecture.html'));
      res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-store' });
      return res.end(html);
    }

    if (url === '/decisions' || url === '/decisions.html') {
      const html = fs.readFileSync(path.join(BOARD_DIR, 'decisions.html'));
      res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-store' });
      return res.end(html);
    }

    // markdown.js is shared by index.html and decisions.html (see its header
    // comment) -- the one static asset this board has, everything else being
    // either a full page or a JSON endpoint. No caching, same as every other
    // response here: this is a local dev tool, not a thing fronted by a CDN.
    if (url === '/markdown.js') {
      const js = fs.readFileSync(path.join(BOARD_DIR, 'markdown.js'));
      res.writeHead(200, { 'Content-Type': 'application/javascript; charset=utf-8', 'Cache-Control': 'no-store' });
      return res.end(js);
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
