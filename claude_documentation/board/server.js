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
const https = require('https');
const fs = require('fs');
const path = require('path');

const PORT = Number(process.argv[2] || process.env.PORT || 8071);
const BOARD_DIR = __dirname;
const BACKLOG_DIR = path.join(BOARD_DIR, '..', 'backlog');
const REPO_DIR = path.join(BOARD_DIR, '..', '..');
const BACKLOG_README = path.join(BACKLOG_DIR, 'README.md');
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


// --- current ticket sequence ---------------------------------------------------
//
// The board answers "what is there" for every ticket. It does not answer "what
// is happening now", and the folders cannot: a sequence is an ordering *across*
// tickets, so it lives in exactly one place a folder cannot express -- the
// `## Current ticket sequence` section at the top of backlog/README.md.
//
// It is parsed out of that prose rather than restated here, and that is the
// whole design. A copy in this file would be a second source that drifts the
// first time someone edits the README and not this, and the section's own
// "Keeping it true" rule is that a stale sequence is worse than none because it
// is trusted. A board that could disagree with the document would be exactly
// that failure, wearing the authority of a rendered page.
//
// So the parser bends to the prose, not the other way round: the section is
// written for people first, and the only thing this depends on is the shape it
// already had -- a `**Set <date>` line and a numbered table whose second cell is
// a link to the ticket.
//
// Parse failure is reported in the payload rather than thrown, the same way
// readCI() reports a GitHub outage. An endpoint that 500s leaves the client
// with nothing to say, and "nothing to say" renders as an empty panel, which
// reads as "nothing planned" -- the one meaning this must never accidentally
// have.

const SEQUENCE_HEADING = '## Current ticket sequence';
// "| 1 | [T0060.6](completed/0060-material-system.md) | code | why... |"
const SEQ_LINK = /^\[(T\d{4})(\.\d+)?\]\((open|inprogress|completed)\/(.+\.md)\)$/;

function readSequence() {
  const source = path.relative(REPO_DIR, BACKLOG_README);
  const out = {
    generated: new Date().toISOString(),
    source, heading: SEQUENCE_HEADING,
    ok: false, error: null, problems: [], setDate: null, items: [],
  };

  let md;
  try {
    md = fs.readFileSync(BACKLOG_README, 'utf8');
  } catch (err) {
    out.error = `cannot read ${source} — ${err.message}`;
    return out;
  }

  const start = md.indexOf(SEQUENCE_HEADING);
  if (start < 0) {
    out.error = `${source} has no "${SEQUENCE_HEADING}" section. It is what a session `
              + `reads first after a context reset — restore it rather than deleting it.`;
    return out;
  }
  let section = md.slice(start + SEQUENCE_HEADING.length);
  const end = section.indexOf('\n## ');
  if (end >= 0) section = section.slice(0, end);

  const date = section.match(/^\*\*Set (\d{4}-\d{2}-\d{2})/m);
  if (date) out.setDate = date[1];
  else out.problems.push('No "**Set YYYY-MM-DD" line, so there is no way to tell how stale this is.');

  for (const line of section.split('\n')) {
    if (!line.trim().startsWith('|')) continue;
    const cells = line.trim().replace(/^\||\|$/g, '').split('|').map((c) => c.trim());
    // The header row and the `|---|` separator both fail this, which is what
    // keeps them out without having to recognise them.
    if (cells.length < 4 || !/^\d+$/.test(cells[0])) continue;

    const m = cells[1].match(SEQ_LINK);
    if (!m) {
      out.problems.push(`Row ${cells[0]}: "${cells[1]}" is not a [T####](folder/file.md) link, so it cannot be linked to its ticket.`);
      continue;
    }
    const [, base, sub, folder, fname] = m;
    const label = base + (sub || '');

    // Not a duplicate of tools/check_backlog.py so much as the visible half of
    // it: that script is the gate and runs when somebody runs it, this is what
    // a person staring at the board sees without being told. check_backlog.py
    // stays authoritative -- if the two ever disagree, believe it.
    if (!fs.existsSync(path.join(BACKLOG_DIR, folder, fname))) {
      out.problems.push(`${label}: points at ${folder}/${fname}, which does not exist.`);
    } else if (folder === 'completed') {
      out.problems.push(`${label}: listed as upcoming, but it sits in completed/. Drop the row and re-date the sequence.`);
    }

    const kindLabel = cells[2].replace(/\*\*/g, '').replace(/^\*|\*$/g, '').trim();
    out.items.push({
      position: Number(cells[0]),
      // What the row calls it ("T0060.6") and what the board can open ("T0060")
      // are different strings, and the card only exists for the second.
      id: label,
      ticket: base,
      kind: /decision/i.test(kindLabel) ? 'decision' : 'code',
      kindLabel,
      href: `${folder}/${fname}`,
      file: path.relative(REPO_DIR, path.join(BACKLOG_DIR, folder, fname)),
      // Cells cannot contain an unescaped pipe in markdown, but rejoining
      // rather than taking cells[3] means a stray one truncates nothing.
      why: cells.slice(3).join(' | '),
    });
  }

  out.items.sort((a, b) => a.position - b.position);

  if (!out.items.length) {
    out.error = `The "${SEQUENCE_HEADING}" section in ${source} lists no tickets. `
              + `If there genuinely is no sequence, say so there in a sentence — `
              + `an empty table reads as an oversight rather than a state.`;
    return out;
  }

  out.ok = true;
  return out;
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

// --- CI status ------------------------------------------------------------------
//
// This used to be two <img src=".../badge.svg"> tags in index.html, and the
// badge is wrong for this repository in a way that matters.
//
// `ci.yml` sets `cancel-in-progress: true`, so pushing again while a run is in
// flight cancels the previous one. That is routine here -- it is the intended
// behaviour, not an incident. GitHub's badge renders the newest *completed*
// run on the default branch and paints anything that is not `success` red, so
// a cancelled run makes the badge say "CI - failing" in exactly the situation
// where nothing at all is known about the code. Measured, 2026-08-04: run
// 30943449243 was cancelled by the next push, and badge.svg served
// `<title>CI - failing</title>` with the red gradient while the last run that
// actually finished on its merits (30941522246) had succeeded. CLAUDE.md
// already warns "`cancelled` is not `failed`" because reading it that way has
// produced a confident, wrong conclusion here before -- and the board was
// repeating the mistake in the header.
//
// The badge also has no in-flight state: while a run is executing it keeps
// showing the previous verdict, so "building" and "finished" look identical.
//
// Neither is fixable in an <img>, so the status is computed here from the
// Actions API instead, where `status` and `conclusion` are separate fields.

const GH_REPO = process.env.HP_BOARD_REPO || 'ywadi/hollow_point';
// Scoped to one branch for the same reason GitHub's badge is: the header
// answers "is the project green", and a run on a scratch branch is not that.
const GH_BRANCH = process.env.HP_BOARD_BRANCH || 'main';
// Two requests per refresh, one per workflow. Anonymous is 60 requests/hour
// per IP, so a 60s cache would exhaust it in half an hour of the board simply
// being open -- hence the slower default when there is no token. Conditional
// requests (see the ETag note below) make the idle case free either way; this
// is only the ceiling for when they miss.
const CI_TTL_DEFAULT = () => (ghToken ? 60000 : 120000);

const CI_WORKFLOWS = [
  { id: 'ci',   file: 'ci.yml',         label: 'CI' },
  { id: 'full', file: 'full-build.yml', label: 'Full build' },
];

// Optional -- the repo is public and all of this works anonymously -- but with
// a token the rate limit goes from 60 requests/hour to 5000 and stops being a
// consideration at all. Environment first, then the gh CLI, which is what is
// actually logged in on this machine.
//
// Three details here were each wrong on the first attempt:
//   - `command -v gh` returns nothing in a non-interactive shell here
//     (CLAUDE.md says so), so PATH alone loses a token that is sitting right
//     there -- hence the absolute paths.
//   - `gh auth token` only exists from gh 2.9; this machine has 2.4.0, where
//     `auth status --show-token` is the only way to get it.
//   - that command exits 0 and prints the token on *stderr*, which
//     execFileSync discards on success -- hence spawnSync and both streams.
let ghToken = process.env.GH_TOKEN || process.env.GITHUB_TOKEN || '';
if (!ghToken) {
  const run = (bin, args) => {
    const r = require('child_process').spawnSync(bin, args, { encoding: 'utf8', timeout: 5000 });
    return `${r.stdout || ''}\n${r.stderr || ''}`;
  };
  const findToken = (s) => {
    const m = String(s).match(/\bgh[pousr]_[A-Za-z0-9]{16,}\b/);
    return m ? m[0] : '';
  };
  for (const bin of [process.env.HP_BOARD_GH_BIN, 'gh', '/usr/bin/gh', '/usr/local/bin/gh'].filter(Boolean)) {
    try {
      ghToken = findToken(run(bin, ['auth', 'token']))
             || findToken(run(bin, ['auth', 'status', '--show-token']));
    } catch { /* no gh here -- anonymous is fine for a public repo */ }
    if (ghToken) break;
  }
}

// A 304 from a conditional request does not count against GitHub's primary
// rate limit, and a board left open on a quiet afternoon asks the same question
// over and over -- so remembering the ETag is the difference between an idle
// board costing nothing and an anonymous one exhausting 60 requests/hour and
// putting "unavailable" in the header. It stops helping while a run is in
// flight, because `updated_at` moves and the response genuinely changes, which
// is exactly when spending a request is worth it.
const etags = new Map(); // url -> { etag, body }

function githubJSON(pathAndQuery) {
  return new Promise((resolve, reject) => {
    const prior = etags.get(pathAndQuery);
    const req = https.request({
      hostname: 'api.github.com',
      path: pathAndQuery,
      method: 'GET',
      headers: {
        'Accept': 'application/vnd.github+json',
        'X-GitHub-Api-Version': '2022-11-28',
        // GitHub rejects requests without one.
        'User-Agent': 'hollowpoint-board',
        ...(ghToken ? { Authorization: `Bearer ${ghToken}` } : {}),
        ...(prior ? { 'If-None-Match': prior.etag } : {}),
      },
    }, (res) => {
      let body = '';
      res.setEncoding('utf8');
      res.on('data', (c) => { body += c; });
      res.on('end', () => {
        if (res.statusCode === 304 && prior) return resolve(prior.body);
        if (res.statusCode !== 200) {
          const e = new Error(`GitHub API ${res.statusCode}`);
          e.statusCode = res.statusCode;
          // Exhausted rate limit: remaining is 0 and reset is a unix time.
          if (res.headers['x-ratelimit-remaining'] === '0') e.rateLimitReset = Number(res.headers['x-ratelimit-reset']) * 1000;
          return reject(e);
        }
        let parsed;
        try { parsed = JSON.parse(body); } catch (err) { return reject(err); }
        if (res.headers.etag) etags.set(pathAndQuery, { etag: res.headers.etag, body: parsed });
        resolve(parsed);
      });
    });
    // The board must never hang on a slow network: this endpoint is polled.
    req.setTimeout(8000, () => req.destroy(new Error('GitHub API timed out')));
    req.on('error', reject);
    req.end();
  });
}

// GitHub splits this across two fields and the whole bug is collapsing them:
// `status` is queued|in_progress|completed, `conclusion` is null until the run
// completes and only then says success|failure|cancelled|skipped|timed_out|...
// Treating "conclusion !== 'success'" as failure buckets both a cancelled run
// and a running one (null) as red.
function runState(run) {
  if (!run) return 'none';
  if (run.status !== 'completed') {
    // queued, waiting, requested and pending all mean "not started yet".
    return run.status === 'in_progress' ? 'in_progress' : 'queued';
  }
  switch (run.conclusion) {
    case 'success': return 'success';
    case 'failure': case 'timed_out': case 'startup_failure': return 'failure';
    case 'cancelled': return 'cancelled';
    case 'skipped': return 'skipped';
    case 'action_required': return 'action_required';
    case 'neutral': return 'neutral';
    default: return 'unknown';
  }
}

// Which conclusions are a statement *about the code*. A cancelled run was
// killed before it could say anything, and a skipped one never ran -- neither
// is evidence of pass or fail, which is precisely why they must not be allowed
// to overwrite the last run that did reach a verdict.
const VERDICT_STATES = new Set(['success', 'failure', 'action_required']);

// How long a run may sit in `queued` before the header stops calling it "in
// flight". GitHub's hosted runners normally pick a job up in seconds, so an
// hour is far outside the legitimate range even when the queue is busy.
//
// **This exists because a run wedged for 38 hours.** Run 31122445705 --
// `run_attempt: 3` of a re-run, with **zero jobs** -- has reported `queued`
// since 2026-08-06 and GitHub refuses to clear it: `gh run cancel` says the run
// is completed, and force-cancel says it "has not yet queued". The two guards
// disagree about which end of the lifecycle it is stuck at, so there is no
// supported way to resolve it from this side. Meanwhile it was the only
// non-completed run on `main`, so `runs.find(...)` selected it every refresh
// and the header said "queued" for two days while six later runs went green.
//
// **`in_progress` is deliberately exempt, at any age.** A run in that state is
// demonstrably executing, and `full-build.yml` is ~1100 targets — timing it out
// would report a healthy nightly build as wedged, which is the same class of
// error in the other direction.
const CI_QUEUE_STALE_MS = Number(process.env.HP_BOARD_CI_QUEUE_STALE_MS) || 3600000;

// A run GitHub still calls `queued` long after any real queue would have
// cleared. Not a verdict and not in flight -- it is a stuck record, and the
// header must not treat it as the thing happening now.
function isStalledQueue(run) {
  if (!run || run.status === 'completed' || run.status === 'in_progress') return false;
  const started = Date.parse(run.run_started_at || run.created_at || '');
  return Number.isFinite(started) && (Date.now() - started) > CI_QUEUE_STALE_MS;
}

function summarizeRuns(wf, runs) {
  const latest = runs[0] || null;
  // "In flight" is the newest run that has not completed. There can be more
  // than one queued at once; the newest is the one the header is about --
  // except for a queue that has stalled, which is a stuck record rather than
  // work in progress and is reported separately below.
  const running = runs.find((r) => r.status !== 'completed' && !isStalledQueue(r)) || null;
  // Surfaced rather than hidden: something is genuinely wrong on GitHub's side
  // and silently dropping it would trade a wrong header for an incomplete one.
  const stalled = runs.find(isStalledQueue) || null;
  // The pass/fail signal comes from the newest run that actually reached a
  // verdict, skipping past any cancellations. With cancel-in-progress this is
  // frequently not the newest run, and using the newest is the bug.
  const verdictRun = runs.find((r) => r.status === 'completed' && VERDICT_STATES.has(runState(r))) || null;

  const brief = (r) => r && {
    id: r.id, url: r.html_url, number: r.run_number, state: runState(r),
    status: r.status, conclusion: r.conclusion, branch: r.head_branch,
    sha: (r.head_sha || '').slice(0, 7), title: r.display_title || r.name,
    startedAt: r.run_started_at || r.created_at, updatedAt: r.updated_at,
  };

  // The state the header paints. A run in flight wins, because it is the thing
  // happening now; otherwise it is the last verdict. Only when there has never
  // been a verdict does a cancelled/skipped run get to be the state itself --
  // at that point there is genuinely nothing else to report.
  const state = running ? runState(running) : (verdictRun ? runState(verdictRun) : runState(latest));

  return {
    ...wf,
    state,
    // True when the verdict being shown is not from the newest run -- i.e. the
    // newest run was cancelled, or one is still going. The UI says so rather
    // than presenting a stale green as if it covered the latest push.
    verdictIsStale: !!(verdictRun && latest && verdictRun.id !== latest.id),
    latest: brief(latest),
    running: brief(running),
    // A run stuck in `queued` past CI_QUEUE_STALE_MS. Shown in the tooltip so
    // the wedge is visible without letting it claim the header.
    stalled: brief(stalled),
    verdict: brief(verdictRun),
    runsUrl: `https://github.com/${GH_REPO}/actions/workflows/${wf.file}`,
  };
}

let ciCache = { at: 0, payload: null };

// Freshness only matters while something is running -- that is the one state
// that changes on its own, and a "building" badge that lags a minute behind the
// run finishing defeats the point of showing it. Shortened only when there is a
// token: two workflows every 20s would burn an anonymous quota in ten minutes
// and leave the header saying "unavailable", which is a worse failure than
// being a minute stale.
function effectiveTTL(payload) {
  const base = Number(process.env.HP_BOARD_CI_TTL_MS) || CI_TTL_DEFAULT();
  const running = payload && (payload.workflows || []).some((w) => w.running);
  return (running && ghToken) ? Math.min(base, 20000) : base;
}

async function readCI() {
  const now = Date.now();
  if (ciCache.payload && now - ciCache.at < effectiveTTL(ciCache.payload)) {
    return { ...ciCache.payload, cached: true };
  }

  // 20 runs, not 5: with cancel-in-progress a burst of pushes can cancel
  // several in a row, and the last real verdict has to still be in the window.
  const q = `branch=${encodeURIComponent(GH_BRANCH)}&per_page=20&exclude_pull_requests=true`;
  const results = await Promise.all(CI_WORKFLOWS.map(async (wf) => {
    try {
      const data = await githubJSON(`/repos/${GH_REPO}/actions/workflows/${wf.file}/runs?${q}`);
      return summarizeRuns(wf, data.workflow_runs || []);
    } catch (err) {
      // One workflow failing must not blank the other, and a GitHub outage
      // must not 500 the board -- report it in the payload instead.
      return { ...wf, state: 'unavailable', error: String(err.message || err),
               retryAfter: err.rateLimitReset || null,
               runsUrl: `https://github.com/${GH_REPO}/actions/workflows/${wf.file}` };
    }
  }));

  const payload = {
    generated: new Date().toISOString(),
    repo: GH_REPO, branch: GH_BRANCH, authenticated: !!ghToken,
    workflows: results,
  };
  // The client polls on this, so it has to describe the cache it will actually
  // hit next -- computed after the fact, from what came back.
  payload.ttlMs = effectiveTTL(payload);
  // A rate-limit rejection is held until the limit resets -- dated forward so
  // the ordinary TTL check expires it exactly then -- so a board left open does
  // not keep hammering an endpoint that is already refusing it.
  const soonestReset = results.map((r) => r.retryAfter).filter(Boolean).sort()[0];
  ciCache = { at: soonestReset ? soonestReset - payload.ttlMs : now, payload };
  return payload;
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

    // Always 200, even when the section is missing or malformed: the failure is
    // in the payload (see readSequence). The client has to be able to render
    // "this is broken" prominently, and it cannot do that from a 500.
    if (url === '/api/sequence') {
      res.writeHead(200, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store' });
      return res.end(JSON.stringify(readSequence()));
    }

    if (url === '/api/architecture') {
      res.writeHead(200, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store' });
      return res.end(JSON.stringify(readArchitecture()));
    }

    // The only endpoint here that talks to the network, so the only one that
    // is async. It resolves even when GitHub does not -- readCI() puts the
    // failure in the payload rather than throwing -- but a bug in it must not
    // take the board down either, hence the catch.
    if (url === '/api/ci') {
      readCI().then((payload) => {
        res.writeHead(200, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store' });
        res.end(JSON.stringify(payload));
      }).catch((err) => {
        res.writeHead(200, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store' });
        res.end(JSON.stringify({ generated: new Date().toISOString(), workflows: [], error: String((err && err.message) || err) }));
      });
      return;
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

// Requiring this file instead of running it exposes the pure helpers without
// binding a port or touching the network, which is how the CI-state derivation
// gets checked against recorded API responses -- the cancelled-then-succeeded
// sequence that motivated it cannot be reproduced on demand against the live
// API, so it is replayed from captured JSON instead.
if (require.main === module) {
  server.listen(PORT, () => {
    const b = readBoard();
    const counts = b.columns.map((c) => `${c.title} ${c.tasks.length}`).join(' | ');
    const s = readSequence();
    console.log(`backlog board -> http://localhost:${PORT}`);
    console.log(`watching       ${path.resolve(BACKLOG_DIR)}`);
    console.log(`current        ${counts}`);
    // Printed at startup because a sequence that failed to parse is invisible
    // until someone opens the page, and the person starting the server is the
    // one who can fix it.
    console.log(`sequence       ${s.ok ? `${s.items.length} items, set ${s.setDate || '(undated)'}` : `NOT PARSED — ${s.error}`}`);
    for (const p of s.problems) console.log(`  sequence !    ${p}`);
    console.log(`ci status      ${GH_REPO}@${GH_BRANCH} (${ghToken ? 'authenticated' : 'anonymous'})`);
  });
} else {
  module.exports = { runState, summarizeRuns, readBoard, readArchitecture, readSequence };
}
