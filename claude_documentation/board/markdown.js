'use strict';
//
// Shared client-side markdown rendering, used by index.html (ticket detail
// panel) and decisions.html (the decision log). Split out into its own file
// rather than left inline, because decisions.html needed the exact same
// renderer and a second, slightly-diverged copy pasted into a second file is
// exactly the kind of thing that quietly drifts -- one gets a bugfix, the
// other doesn't, and nobody notices until the output looks different for no
// reason.
//
// Loaded as a plain <script src="/markdown.js">, not a module -- this project
// has no bundler and no build step, and one page growing a <script
// type="module"> while the rest stays classic script tags would be a second
// convention to remember for no real benefit at this size. The functions
// below just land as globals, same as everything else here.
//
// It is a compact renderer -- enough for hand-written task and decision
// markdown: headings, tables, fenced code, task lists, inline code/bold/
// italic/links. Not a general CommonMark implementation, and it does not need
// to be; every file it renders is written by the same few people in the same
// house style.

function esc(s) {
  return String(s).replace(/[&<>"]/g, (c) => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
}

// Inline formatting only (code/bold/italic/links), no block structure. Kept
// as its own function rather than folded back into renderMarkdown() so a
// caller that already has a single line -- a heading pulled out for a table
// of contents, say -- can format it without running the whole line-by-line
// block parser over one line.
function mdInline(t) {
  return esc(t)
    .replace(/`([^`]+)`/g, '<code>$1</code>')
    .replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>')
    .replace(/(^|[^*])\*([^*\n]+)\*/g, '$1<em>$2</em>')
    .replace(/\[([^\]]+)\]\(([^)]+)\)/g, '<a href="$2" target="_blank" rel="noopener">$1</a>');
}

function renderMarkdown(md) {
  const lines = md.split('\n');
  let out = '', i = 0, inCode = false, codeBuf = [];

  while (i < lines.length) {
    const line = lines[i];

    if (/^```/.test(line)) {
      if (inCode) { out += '<pre><code>' + esc(codeBuf.join('\n')) + '</code></pre>'; codeBuf = []; inCode = false; }
      else inCode = true;
      i++; continue;
    }
    if (inCode) { codeBuf.push(line); i++; continue; }

    // table: header row followed by a separator row
    if (/^\s*\|/.test(line) && i + 1 < lines.length && /^\s*\|[\s:|-]+\|\s*$/.test(lines[i + 1])) {
      const cells = (r) => r.trim().replace(/^\||\|$/g, '').split('|').map((c) => c.trim());
      let html = '<table><thead><tr>' + cells(line).map((c) => '<th>' + mdInline(c) + '</th>').join('') + '</tr></thead><tbody>';
      i += 2;
      while (i < lines.length && /^\s*\|/.test(lines[i])) {
        html += '<tr>' + cells(lines[i]).map((c) => '<td>' + mdInline(c) + '</td>').join('') + '</tr>';
        i++;
      }
      out += html + '</tbody></table>';
      continue;
    }

    let m;
    if ((m = line.match(/^(#{1,6})\s+(.*)$/))) { const n = m[1].length; out += `<h${n}>${mdInline(m[2])}</h${n}>`; i++; continue; }
    if (/^\s*(-{3,}|\*{3,})\s*$/.test(line)) { out += '<hr>'; i++; continue; }
    if (/^>\s?/.test(line)) { out += '<blockquote>' + mdInline(line.replace(/^>\s?/, '')) + '</blockquote>'; i++; continue; }

    if (/^\s*[-*]\s+/.test(line)) {
      let html = '<ul>';
      while (i < lines.length && /^\s*[-*]\s+/.test(lines[i])) {
        let t = lines[i].replace(/^\s*[-*]\s+/, '');
        const box = t.match(/^\[([ xX])\]\s*(.*)$/);
        if (box) {
          const checked = box[1].toLowerCase() === 'x';
          html += `<li class="task">${checked ? '☑' : '☐'} ${checked ? '<span style="opacity:.6">' + mdInline(box[2]) + '</span>' : mdInline(box[2])}</li>`;
        } else html += '<li>' + mdInline(t) + '</li>';
        i++;
      }
      out += html + '</ul>';
      continue;
    }

    if (line.trim() === '') { i++; continue; }

    let para = [];
    while (i < lines.length && lines[i].trim() !== '' && !/^(#{1,6}\s|\s*[-*]\s|```|\s*\||>)/.test(lines[i])) {
      para.push(lines[i]); i++;
    }
    if (para.length) out += '<p>' + mdInline(para.join(' ')) + '</p>';
    else i++;
  }
  if (inCode && codeBuf.length) out += '<pre><code>' + esc(codeBuf.join('\n')) + '</code></pre>';
  return out;
}
