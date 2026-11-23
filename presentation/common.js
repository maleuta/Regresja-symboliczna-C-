// Shared helpers of visualizer.html and pipeline.html: parsing and formatting formulas,
// marking what a variation changed, and drawing expression trees as SVG.
"use strict";
const SVGNS = "http://www.w3.org/2000/svg";

// ================================================================== formulas
// Parses Expression::to_string output, e.g. "+(*(2.5, x), 0.75)".
function parse(text) {
  let i = 0;
  const skip = () => { while (text[i] === " ") i++; };
  function node() {
    skip();
    const start = i;
    while (i < text.length && !"(),".includes(text[i])) i++;
    const token = text.slice(start, i).trim();
    if (text[i] === "(") {
      i++;
      const children = [node()];
      skip();
      while (text[i] === ",") { i++; children.push(node()); skip(); }
      i++;
      return { kind: children.length === 1 ? "unary" : "binary", name: token, children };
    }
    const value = Number(token);
    if (token !== "" && !Number.isNaN(value)) return { kind: "constant", value, children: [] };
    return { kind: "variable", name: token, children: [] };
  }
  return node();
}
const OPS = {
  "+": (a, b) => a + b, "-": (a, b) => a - b, "*": (a, b) => a * b,
  "/": (a, b) => (b === 0 ? NaN : a / b),
  pow: (a, b) => (a < 0 && Math.trunc(b) !== b) || (a === 0 && b < 0) ? NaN : Math.pow(a, b),
  hypot: (a, b) => Math.hypot(a, b), sin: Math.sin, cos: Math.cos, exp: Math.exp, square: (x) => x * x,
};
function evaluate(n, env) {
  if (n.kind === "constant") return n.value;
  if (n.kind === "variable") return env[n.name];
  const f = OPS[n.name];
  if (!f) return NaN;
  return n.kind === "unary" ? f(evaluate(n.children[0], env)) : f(evaluate(n.children[0], env), evaluate(n.children[1], env));
}
const sig = (n) => n.kind === "constant" ? String(n.value) : n.kind === "variable" ? n.name : `${n.name}(${n.children.map(sig).join(",")})`;
const size = (n) => 1 + n.children.reduce((s, c) => s + size(c), 0);
const depth = (n) => 1 + Math.max(0, ...n.children.map(depth));

const SUP = { "-": "⁻", "0": "⁰", "1": "¹", "2": "²", "3": "³", "4": "⁴", "5": "⁵", "6": "⁶", "7": "⁷", "8": "⁸", "9": "⁹" };
const pl = (s) => s.replace(".", ",");
function fmtConst(v, digits = 4) {
  if (v === 0) return "0";
  const a = Math.abs(v);
  if (a >= 1e-3 && a < 1e5) return pl(String(Number(v.toPrecision(digits))));
  const [m, e] = v.toExponential(digits - 1).split("e");
  return pl(String(Number(m))) + "·10" + String(Number(e)).split("").map((c) => SUP[c] || c).join("");
}
function fmtLoss(v) {
  if (v === null || v === undefined || !Number.isFinite(v)) return "∞";
  if (v === 0) return "0";
  return fmtConst(v, 3);
}
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
const varHtml = (name) => { const m = /^([A-Za-z]+)(\d+)$/.exec(name); return m ? `<i>${esc(m[1])}</i><sub>${m[2]}</sub>` : `<i>${esc(name)}</i>`; };
const PREC = { "+": 1, "-": 1, "*": 2, "/": 2 };
const atomic = (n) => (n.kind === "constant" && n.value >= 0) || n.kind === "variable";
// Infix HTML with minimal parentheses and Polish decimal commas.
function infix(n, parent = 0) {
  if (n.kind === "constant") { const s = fmtConst(n.value); return n.value < 0 && parent > 0 ? `(${s})` : s; }
  if (n.kind === "variable") return varHtml(n.name);
  const [a, b] = n.children;
  if (n.kind === "unary") {
    if (n.name === "square") return atomic(a) ? `${infix(a, 3)}²` : `(${infix(a)})²`;
    return `${esc(n.name)}(${infix(a)})`;
  }
  if (n.name === "pow") return `${atomic(a) ? infix(a, 3) : "(" + infix(a) + ")"}<sup>${infix(b)}</sup>`;
  if (!(n.name in PREC)) return `${esc(n.name)}(${infix(a)}; ${infix(b)})`;
  const p = PREC[n.name];
  let s;
  if ((n.name === "+" || n.name === "-") && b.kind === "constant" && b.value < 0) {
    s = `${infix(a, p)} ${n.name === "+" ? "−" : "+"} ${fmtConst(-b.value)}`;
  } else {
    s = infix(a, p) + { "+": " + ", "-": " − ", "*": "·", "/": " / " }[n.name] + infix(b, n.name === "-" || n.name === "/" ? p + 0.5 : p);
  }
  return p < parent ? `(${s})` : s;
}
const formulaHtml = (text) => (text ? infix(parse(text)) : "");

// ================================================================== what changed?
// Marks the region where parent `a` and child `b` differ. A mutation or crossover edits one
// place, so the first difference found top-down is the whole change.
function markAll(n) { n.mark = "changed"; n.children.forEach(markAll); }
function markDiff(a, b, op) {
  if (sig(a) === sig(b)) return;
  const sameNode = a.kind === b.kind && a.name === b.name && a.value === b.value && a.children.length === b.children.length;
  if (sameNode) { a.children.forEach((c, i) => markDiff(c, b.children[i], op)); return; }
  const childEquals = (n, target) => n.children.find((c) => sig(c) === sig(target));
  if (op === "insert_node" && childEquals(b, a)) {  // new node placed above a
    b.mark = "changed";
    b.children.forEach((c) => { if (sig(c) !== sig(a)) markAll(c); });
    a.mark = "place"; return;
  }
  if (op === "delete_node" && childEquals(a, b)) {  // node a removed, one child kept
    a.mark = "changed";
    a.children.forEach((c) => { if (sig(c) !== sig(b)) markAll(c); });
    b.mark = "place"; return;
  }
  if (op === "change_operator" && a.children.length === b.children.length && a.children.every((c, i) => sig(c) === sig(b.children[i]))) {
    a.mark = b.mark = "changed"; return;  // operator renamed, arguments kept
  }
  markAll(a); markAll(b); a.region = b.region = true;  // subtree, leaf or constant replaced
}
function findRegion(n) { if (n.region) return n; for (const c of n.children) { const r = findRegion(c); if (r) return r; } return null; }
function findSig(n, s) { if (sig(n) === s) return n; for (const c of n.children) { const r = findSig(c, s); if (r) return r; } return null; }

// ================================================================== tree drawing
const SYMBOL = { "+": "+", "-": "−", "*": "×", "/": "÷", pow: "^", square: "x²" };
const NODE_H = 30, GAP = 14, LEVEL = 62, PAD = 16;
function el(tag, attrs, parent) {
  const e = document.createElementNS(SVGNS, tag);
  for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
  if (parent) parent.appendChild(e);
  return e;
}
// Width-aware layout: leaves get slots as wide as their labels, parents sit centred over
// their children, and edges run from the bottom edge of a parent to the top edge of a child.
function drawTree(svg, root, showMarks) {
  svg.replaceChildren();
  if (!root) { svg.setAttribute("viewBox", "0 0 10 10"); svg.style.width = "10px"; return; }
  (function measure(n) {
    n.label = n.kind === "constant" ? fmtConst(n.value, 3) : n.kind === "variable" ? n.name : SYMBOL[n.name] || n.name;
    n.w = Math.max(n.kind === "variable" || n.kind === "constant" ? 34 : 32, n.label.length * 8.5 + 18);
    n.children.forEach(measure);
  })(root);
  let cursor = PAD, maxDepth = 0, prev = null;
  (function place(n, d) {
    n.d = d; maxDepth = Math.max(maxDepth, d);
    if (!n.children.length) {
      cursor += prev === null ? n.w / 2 : prev / 2 + GAP + n.w / 2;
      n.x = cursor; prev = n.w; return;
    }
    n.children.forEach((c) => place(c, d + 1));
    n.x = (n.children[0].x + n.children[n.children.length - 1].x) / 2;
  })(root, 0);
  let minX = Infinity, maxX = -Infinity;
  (function bounds(n) { minX = Math.min(minX, n.x - n.w / 2); maxX = Math.max(maxX, n.x + n.w / 2); n.children.forEach(bounds); })(root);
  const shift = PAD - minX, width = maxX - minX + 2 * PAD, height = maxDepth * LEVEL + NODE_H + 2 * PAD;
  const X = (n) => n.x + shift, Y = (n) => PAD + NODE_H / 2 + n.d * LEVEL;
  svg.setAttribute("viewBox", `0 0 ${width} ${height}`);
  svg.style.width = `${width}px`;

  const regions = el("g", {}, svg), edges = el("g", {}, svg), nodes = el("g", {}, svg);
  if (showMarks) {
    (function boxes(n) {
      if (n.region) {
        let x0 = Infinity, x1 = -Infinity, y1 = -Infinity;
        (function walk(m) { x0 = Math.min(x0, X(m) - m.w / 2); x1 = Math.max(x1, X(m) + m.w / 2); y1 = Math.max(y1, Y(m)); m.children.forEach(walk); })(n);
        el("rect", { x: x0 - 7, y: Y(n) - NODE_H / 2 - 7, width: x1 - x0 + 14, height: y1 - Y(n) + NODE_H + 14, rx: 12,
          fill: "var(--series-2-wash)", stroke: "var(--series-2)", "stroke-width": 1.5, "stroke-dasharray": "5 4" }, regions);
      }
      n.children.forEach(boxes);
    })(root);
  }
  (function draw(n) {
    for (const c of n.children) {
      el("line", { x1: X(n), y1: Y(n) + NODE_H / 2, x2: X(c), y2: Y(c) - NODE_H / 2, stroke: "var(--text-muted)", "stroke-width": 1.5 }, edges);
      draw(c);
    }
    const isOp = n.kind === "unary" || n.kind === "binary";
    const fill = n.kind === "constant" ? "var(--series-2-wash)" : n.kind === "variable" ? "var(--series-1-wash)" : "var(--node-op)";
    const marked = showMarks && n.mark === "changed", place = showMarks && n.mark === "place";
    el("rect", { x: X(n) - n.w / 2, y: Y(n) - NODE_H / 2, width: n.w, height: NODE_H, rx: isOp ? NODE_H / 2 : 6, fill,
      stroke: marked || place ? "var(--series-2)" : "var(--text-muted)", "stroke-width": marked ? 2.5 : place ? 2 : 1,
      "stroke-dasharray": place ? "4 3" : "none" }, nodes);
    const t = el("text", { x: X(n), y: Y(n) + 5, "text-anchor": "middle", fill: "var(--text-primary)", "font-size": 14,
      "font-weight": isOp ? 600 : 400, "font-style": n.kind === "variable" ? "italic" : "normal" }, nodes);
    t.textContent = n.label;
  })(root);
}

