/**
 * Store layout template generators.
 * Each template function takes (widthFt, depthFt, scheduleMeta) and returns
 * a full layout JSON ready for layoutStore.loadWizardLayout().
 *
 * scheduleMeta: { name, daysOfOperation, hours, productsFile, posCsvName, aisleLayout }
 *
 * Coordinate convention:
 *   x increases rightward, y increases downward (canvas coords).
 *   Front of store = bottom wall (y = depthFt - margin).
 *   Back of store  = top wall   (y = margin).
 *
 * Angle convention (used for checkout queue waypoint direction):
 *   angle is degrees, dx = cos(angle°), dy = sin(angle°).
 *   Front-wall nodes use angle=270: dy = sin(270°) = -1 → waypoints extend upward into store.
 *   Back-wall nodes  use angle=90:  dy = sin(90°)  = +1 → faces toward store interior.
 */

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

let _nextId = 1;
function uid() { return _nextId++; }

function resetIds() { _nextId = 1; }

function makeSchedule({ daysOfOperation, hours }) {
  return { daysOfOperation, hours };
}

function baseLayout(name, widthFt, depthFt, meta) {
  return {
    units:        "feet",
    productsFile: meta.productsFile ?? null,
    posCsvName:   meta.posCsvName ?? null,
    shape:        "rectangle",
    boundary: [
      { x: 0,       y: 0       },
      { x: widthFt, y: 0       },
      { x: widthFt, y: depthFt },
      { x: 0,       y: depthFt },
    ],
    nodes:          [],
    edges:          [],
    shelves:        [],
    freeObjects:    [],
    planogram:      {},
    checkoutQueues: [],
    schedule:       makeSchedule(meta),
    _wizardNew:     true,
  };
}

function node(id, type, x, y, overrides = {}) {
  return {
    id, type, x, y,
    angle:           overrides.angle           ?? 0,
    width:           overrides.width           ?? (type === "Entrance" || type === "Exit" ? 10 : 6),
    length:          overrides.length          ?? (type === "Entrance" || type === "Exit" ? 8  : 6),
    shelfLeft:       0, shelfRight: 0,
    blockedFraction: 0, personalSpace: 1.0,
    jamDensity:      3.5, dwellTime: 0,
    serviceRate:     type === "Register" ? 0.5 : 0,
    entryRate:       type === "Entrance" ? 1.0 : 0.0,
    exitRate:        type === "Exit"     ? 1.0 : 0.0,
    ...overrides,
  };
}

function edge(id, nodeA, nodeB, lengthFt, overrides = {}) {
  return {
    id, nodeA, nodeB,
    length:          parseFloat(lengthFt.toFixed(2)),
    width:           overrides.width    ?? 10,
    shelfLeft:       overrides.shelfLeft  ?? 0,
    shelfRight:      overrides.shelfRight ?? 0,
    freeSpeed:       1.2, jamDensity: 3.5, blockedFraction: 0,
    flow:            "bi", orientation: "fwd",
    edgeType:        overrides.edgeType ?? "aisle",
    ...overrides,
  };
}

/**
 * Build a straight-line checkout queue for a register node.
 * Waypoints extend inward (away from the nearest wall) at 6 ft spacing.
 *
 * @param {number} registerId
 * @param {object} regNode        - { x, y, angle } of the register
 * @param {number} numWaypoints   - how many queue positions
 * @param {number} processingTime - seconds per customer
 */
function checkoutQueue(registerId, regNode, numWaypoints, processingTime) {
  // The register faces inward; angle points into the store.
  // Waypoints extend in that same direction so customers queue inside the store.
  const angleRad = ((regNode.angle ?? 0) * Math.PI) / 180;
  const dx = Math.cos(angleRad);
  const dy = Math.sin(angleRad);
  const spacing = 6;
  const waypoints = [];
  for (let i = 1; i <= numWaypoints; i++) {
    waypoints.push({
      x: parseFloat((regNode.x + dx * spacing * i).toFixed(2)),
      y: parseFloat((regNode.y + dy * spacing * i).toFixed(2)),
    });
  }
  return { registerId, processingTime, waypoints };
}

// ---------------------------------------------------------------------------
// Aisle layout builders
// All builders receive a config object and return:
//   { botNodes, topNodes, nodes, edges, leftAnchor, rightAnchor, nTL?, nTR? }
//
// botNodes[i] — junction at bottom of aisle i (on the front spine, y = yFront)
// topNodes[i] — junction at top  of aisle i (on the back  spine, y = yBack)
// leftAnchor  — node to connect Entrance → (leftmost front point)
// rightAnchor — node to connect (rightmost front point) → Registers
// nTL, nTR    — racetrack only: top-left / top-right perimeter corners
// ---------------------------------------------------------------------------

/**
 * Parallel — N vertical aisles with a bottom spine and a top spine.
 */
function buildParallelAisles({ aisleCount, aisleXs, yFront, yBack, aisleWidth, shelfDepth, spineWidth }) {
  const botNodes = Array.from({ length: aisleCount }, () => uid());
  const topNodes = Array.from({ length: aisleCount }, () => uid());
  const aisleLen = parseFloat((yFront - yBack).toFixed(2));

  const nodes = [
    ...botNodes.map((id, i) => node(id, "Junction", aisleXs[i], yFront)),
    ...topNodes.map((id, i) => node(id, "Junction", aisleXs[i], yBack)),
  ];
  const edges = [];

  // Vertical aisles with shelves
  for (let i = 0; i < aisleCount; i++) {
    edges.push(edge(uid(), botNodes[i], topNodes[i], aisleLen,
      { edgeType: "aisle", width: aisleWidth, shelfLeft: shelfDepth, shelfRight: shelfDepth }));
  }
  // Bottom spine (between adjacent aisle feet)
  for (let i = 0; i < aisleCount - 1; i++) {
    const dx = parseFloat((aisleXs[i + 1] - aisleXs[i]).toFixed(2));
    edges.push(edge(uid(), botNodes[i], botNodes[i + 1], dx, { edgeType: "aisle", width: spineWidth }));
  }
  // Top spine (between adjacent aisle heads)
  for (let i = 0; i < aisleCount - 1; i++) {
    const dx = parseFloat((aisleXs[i + 1] - aisleXs[i]).toFixed(2));
    edges.push(edge(uid(), topNodes[i], topNodes[i + 1], dx, { edgeType: "aisle", width: spineWidth }));
  }

  return { botNodes, topNodes, nodes, edges, leftAnchor: botNodes[0], rightAnchor: botNodes[aisleCount - 1] };
}

/**
 * Racetrack — outer perimeter loop with inner vertical aisles.
 *
 * The Entrance and Exit nodes (provided by the caller) act as the bottom-left
 * and bottom-right corners of the loop. This builder adds:
 *   - Top-left (nTL) and top-right (nTR) corners
 *   - Top perimeter: nTL → topNodes[0..N-1] → nTR
 *   - Bottom inner segments: botNodes[0..N-2] (caller adds Entrance→B0 and BN-1→Exit)
 *   - Inner vertical aisles with shelves
 *
 * The caller adds the vertical left/right walls: Entrance→nTL, Exit→nTR.
 */
function buildRacetrackAisles({ aisleCount, aisleXs, yFront, yBack, xLeft, xRight, aisleWidth, shelfDepth, spineWidth }) {
  const nTL = uid(), nTR = uid();
  const botNodes = Array.from({ length: aisleCount }, () => uid());
  const topNodes = Array.from({ length: aisleCount }, () => uid());
  const aisleLen = parseFloat((yFront - yBack).toFixed(2));

  const nodes = [
    node(nTL, "Junction", xLeft,  yBack),
    node(nTR, "Junction", xRight, yBack),
    ...botNodes.map((id, i) => node(id, "Junction", aisleXs[i], yFront)),
    ...topNodes.map((id, i) => node(id, "Junction", aisleXs[i], yBack)),
  ];
  const edges = [];

  // Inner vertical aisles with shelves
  for (let i = 0; i < aisleCount; i++) {
    edges.push(edge(uid(), botNodes[i], topNodes[i], aisleLen,
      { edgeType: "aisle", width: aisleWidth, shelfLeft: shelfDepth, shelfRight: shelfDepth }));
  }
  // Bottom inner perimeter segments (caller adds Entrance→B0 and BN-1→Exit)
  for (let i = 0; i < aisleCount - 1; i++) {
    const dx = parseFloat((aisleXs[i + 1] - aisleXs[i]).toFixed(2));
    edges.push(edge(uid(), botNodes[i], botNodes[i + 1], dx, { edgeType: "aisle", width: spineWidth }));
  }
  // Top perimeter: TL → T0 → ... → TN-1 → TR
  edges.push(edge(uid(), nTL, topNodes[0],
    parseFloat((aisleXs[0] - xLeft).toFixed(2)), { edgeType: "aisle", width: spineWidth }));
  for (let i = 0; i < aisleCount - 1; i++) {
    const dx = parseFloat((aisleXs[i + 1] - aisleXs[i]).toFixed(2));
    edges.push(edge(uid(), topNodes[i], topNodes[i + 1], dx, { edgeType: "aisle", width: spineWidth }));
  }
  edges.push(edge(uid(), topNodes[aisleCount - 1], nTR,
    parseFloat((xRight - aisleXs[aisleCount - 1]).toFixed(2)), { edgeType: "aisle", width: spineWidth }));

  return { botNodes, topNodes, nodes, edges, nTL, nTR, leftAnchor: botNodes[0], rightAnchor: botNodes[aisleCount - 1] };
}

/**
 * Grid — N vertical × crossCount horizontal rows forming a full grid.
 * row 0 = bottom (yFront), row crossCount-1 = top (yBack).
 * Vertical segments carry shelves; horizontal cross-aisles are open walkways.
 */
function buildGridAisles({ aisleCount, aisleXs, yFront, yBack, aisleWidth, shelfDepth, spineWidth, crossCount }) {
  // y coordinate for each horizontal row
  const rows = Array.from({ length: crossCount }, (_, r) =>
    parseFloat((yFront - (r / (crossCount - 1)) * (yFront - yBack)).toFixed(2)));

  // grid[r][c] = node id; r=0 is front, r=crossCount-1 is back
  const grid = Array.from({ length: crossCount }, () =>
    Array.from({ length: aisleCount }, () => uid()));

  const nodes = [];
  for (let r = 0; r < crossCount; r++) {
    for (let c = 0; c < aisleCount; c++) {
      nodes.push(node(grid[r][c], "Junction", aisleXs[c], rows[r]));
    }
  }

  const edges = [];
  // Vertical segments (with shelves)
  for (let r = 0; r < crossCount - 1; r++) {
    const segLen = parseFloat((rows[r] - rows[r + 1]).toFixed(2)); // rows[r] > rows[r+1]
    for (let c = 0; c < aisleCount; c++) {
      edges.push(edge(uid(), grid[r][c], grid[r + 1][c], segLen,
        { edgeType: "aisle", width: aisleWidth, shelfLeft: shelfDepth, shelfRight: shelfDepth }));
    }
  }
  // Horizontal cross-aisles (no shelves, wider)
  for (let r = 0; r < crossCount; r++) {
    for (let c = 0; c < aisleCount - 1; c++) {
      edges.push(edge(uid(), grid[r][c], grid[r][c + 1],
        parseFloat((aisleXs[c + 1] - aisleXs[c]).toFixed(2)),
        { edgeType: "aisle", width: spineWidth }));
    }
  }

  return {
    botNodes: grid[0],
    topNodes: grid[crossCount - 1],
    nodes, edges,
    leftAnchor:  grid[0][0],
    rightAnchor: grid[0][aisleCount - 1],
  };
}

function buildAisles(cfg, aisleLayout) {
  switch (aisleLayout) {
    case "racetrack": return buildRacetrackAisles(cfg);
    case "grid":      return buildGridAisles(cfg);
    default:          return buildParallelAisles(cfg);
  }
}

// ---------------------------------------------------------------------------
// Template: Blank
// ---------------------------------------------------------------------------

export function buildBlank(widthFt, depthFt, meta) {
  resetIds();
  const layout = baseLayout(meta.name, widthFt, depthFt, meta);
  const cx = widthFt / 2, cy = depthFt / 2;
  const n1 = uid(), n2 = uid(), n3 = uid();
  layout.nodes = [
    node(n1, "Entrance", 0,       cy, { angle: 0   }),
    node(n2, "Junction", cx,      cy),
    node(n3, "Exit",     widthFt, cy, { angle: 180 }),
  ];
  layout.edges = [
    edge(uid(), n1, n2, cx, { edgeType: "wall", width: 12 }),
    edge(uid(), n2, n3, cx, { edgeType: "wall", width: 12 }),
  ];
  return layout;
}

// ---------------------------------------------------------------------------
// Template: Convenience Store (~50×38 ft)
//   3 aisles, 1 register, 1 stockroom
//   Front wall (bottom): Entrance, Register, Exit
//   Back wall  (top):    Stockroom (accessible from back spine)
// ---------------------------------------------------------------------------

export function buildConvenienceStore(widthFt, depthFt, meta) {
  resetIds();
  const layout = baseLayout(meta.name, widthFt, depthFt, meta);
  const aisleLayout = meta.aisleLayout ?? "parallel";

  const margin     = 4;
  const stockDepth = 8; // stockroom sits this far inside from the back wall
  const yFront = parseFloat((depthFt - margin).toFixed(2));
  const yBack  = parseFloat(margin.toFixed(2));
  const yStock = parseFloat((yBack + stockDepth).toFixed(2));

  // 3 aisles spread across left 65% of the store width
  const aisleCount   = 3;
  const aisleZoneEnd = parseFloat((margin + (widthFt - 2 * margin) * 0.65).toFixed(2));
  const aisleSpacing = (aisleZoneEnd - margin) / (aisleCount + 1);
  const aisleXs = Array.from({ length: aisleCount }, (_, i) =>
    parseFloat((margin + aisleSpacing * (i + 1)).toFixed(2)));

  // Front-wall x positions
  const xLeft = parseFloat(margin.toFixed(2));
  const xReg  = parseFloat((widthFt * 0.78).toFixed(2));
  const xExit = parseFloat((widthFt - margin).toFixed(2));

  const nEntr  = uid(), nExit = uid(), nReg = uid(), nStock = uid();

  const cfg = {
    aisleCount, aisleXs, yFront, yBack,
    xLeft, xRight: xExit,
    aisleWidth: 8, shelfDepth: 3, spineWidth: 6,
    crossCount: 3,
  };
  const aisle = buildAisles(cfg, aisleLayout);
  const { leftAnchor, rightAnchor } = aisle;

  layout.nodes = [
    node(nEntr,  "Entrance",  xLeft, yFront, { angle: 270 }),
    node(nExit,  "Exit",      xExit, yFront, { angle: 270 }),
    node(nReg,   "Register",  xReg,  yFront, { angle: 270, width: 6, length: 6 }),
    node(nStock, "Stockroom", aisleXs[aisleCount - 1], yStock, { angle: 90, width: 10, length: 8 }),
    ...aisle.nodes,
  ];

  const edges = [...aisle.edges];

  // Front spine: Entrance → leftAnchor … (bot spine) … rightAnchor → Reg → Exit
  edges.push(edge(uid(), nEntr, leftAnchor,
    parseFloat((aisleXs[0] - xLeft).toFixed(2)), { edgeType: "aisle", width: 8 }));
  edges.push(edge(uid(), rightAnchor, nReg,
    parseFloat((xReg - aisleXs[aisleCount - 1]).toFixed(2)), { edgeType: "aisle", width: 8 }));
  edges.push(edge(uid(), nReg, nExit,
    parseFloat((xExit - xReg).toFixed(2)), { edgeType: "aisle", width: 8 }));

  // Stockroom spur (vertical, from last aisle's back end)
  edges.push(edge(uid(), aisle.topNodes[aisleCount - 1], nStock,
    stockDepth, { edgeType: "aisle", width: 6 }));

  // Racetrack only: vertical left/right perimeter walls
  if (aisleLayout === "racetrack" && aisle.nTL != null) {
    const wallLen = parseFloat((yFront - yBack).toFixed(2));
    edges.push(edge(uid(), nEntr, aisle.nTL, wallLen, { edgeType: "aisle", width: 6 }));
    edges.push(edge(uid(), nExit, aisle.nTR, wallLen, { edgeType: "aisle", width: 6 }));
  }

  layout.edges = edges;
  layout.checkoutQueues = [
    checkoutQueue(nReg, layout.nodes.find(n => n.id === nReg), 3, 45),
  ];
  return layout;
}

// ---------------------------------------------------------------------------
// Template: Small Grocery (~150×100 ft)
//   6 aisles, 2 registers, 1 stockroom
// ---------------------------------------------------------------------------

export function buildSmallGrocery(widthFt, depthFt, meta) {
  resetIds();
  const layout = baseLayout(meta.name, widthFt, depthFt, meta);
  const aisleLayout = meta.aisleLayout ?? "parallel";

  const margin     = 6;
  const stockDepth = 10;
  const yFront = parseFloat((depthFt - margin).toFixed(2));
  const yBack  = parseFloat(margin.toFixed(2));
  const yStock = parseFloat((yBack + stockDepth).toFixed(2));

  // 6 aisles spread across left 72% of the store width
  const aisleCount   = 6;
  const aisleZoneEnd = parseFloat((margin + (widthFt - 2 * margin) * 0.72).toFixed(2));
  const aisleSpacing = (aisleZoneEnd - margin) / (aisleCount + 1);
  const aisleXs = Array.from({ length: aisleCount }, (_, i) =>
    parseFloat((margin + aisleSpacing * (i + 1)).toFixed(2)));

  // Front-wall x positions (2 registers in the checkout zone, right of aisles)
  const xLeft = parseFloat(margin.toFixed(2));
  const xReg1 = parseFloat((widthFt * 0.80).toFixed(2));
  const xReg2 = parseFloat((widthFt * 0.90).toFixed(2));
  const xExit = parseFloat((widthFt - margin).toFixed(2));

  const nEntr  = uid(), nExit = uid();
  const nReg1  = uid(), nReg2 = uid();
  const nStock = uid();

  const cfg = {
    aisleCount, aisleXs, yFront, yBack,
    xLeft, xRight: xExit,
    aisleWidth: 10, shelfDepth: 4, spineWidth: 8,
    crossCount: 3,
  };
  const aisle = buildAisles(cfg, aisleLayout);
  const { leftAnchor, rightAnchor } = aisle;

  layout.nodes = [
    node(nEntr,  "Entrance",  xLeft, yFront, { angle: 270 }),
    node(nExit,  "Exit",      xExit, yFront, { angle: 270 }),
    node(nReg1,  "Register",  xReg1, yFront, { angle: 270, width: 6, length: 6 }),
    node(nReg2,  "Register",  xReg2, yFront, { angle: 270, width: 6, length: 6 }),
    node(nStock, "Stockroom", aisleXs[aisleCount - 1], yStock, { angle: 90, width: 12, length: 10 }),
    ...aisle.nodes,
  ];

  const edges = [...aisle.edges];

  // Front spine
  edges.push(edge(uid(), nEntr, leftAnchor,
    parseFloat((aisleXs[0] - xLeft).toFixed(2)), { edgeType: "aisle", width: 10 }));
  edges.push(edge(uid(), rightAnchor, nReg1,
    parseFloat((xReg1 - aisleXs[aisleCount - 1]).toFixed(2)), { edgeType: "aisle", width: 10 }));
  edges.push(edge(uid(), nReg1, nReg2,
    parseFloat((xReg2 - xReg1).toFixed(2)), { edgeType: "aisle", width: 10 }));
  edges.push(edge(uid(), nReg2, nExit,
    parseFloat((xExit - xReg2).toFixed(2)), { edgeType: "aisle", width: 10 }));

  // Stockroom spur
  edges.push(edge(uid(), aisle.topNodes[aisleCount - 1], nStock,
    stockDepth, { edgeType: "aisle", width: 6 }));

  // Racetrack walls
  if (aisleLayout === "racetrack" && aisle.nTL != null) {
    const wallLen = parseFloat((yFront - yBack).toFixed(2));
    edges.push(edge(uid(), nEntr, aisle.nTL, wallLen, { edgeType: "aisle", width: 8 }));
    edges.push(edge(uid(), nExit, aisle.nTR, wallLen, { edgeType: "aisle", width: 8 }));
  }

  layout.edges = edges;
  layout.checkoutQueues = [
    checkoutQueue(nReg1, layout.nodes.find(n => n.id === nReg1), 5, 60),
    checkoutQueue(nReg2, layout.nodes.find(n => n.id === nReg2), 5, 60),
  ];
  return layout;
}

// ---------------------------------------------------------------------------
// Template: Supermarket (~250×150 ft)
//   10 aisles, 4 registers, 1 stockroom
// ---------------------------------------------------------------------------

export function buildSupermarket(widthFt, depthFt, meta) {
  resetIds();
  const layout = baseLayout(meta.name, widthFt, depthFt, meta);
  const aisleLayout = meta.aisleLayout ?? "parallel";

  const margin     = 8;
  const stockDepth = 12;
  const yFront = parseFloat((depthFt - margin).toFixed(2));
  const yBack  = parseFloat(margin.toFixed(2));
  const yStock = parseFloat((yBack + stockDepth).toFixed(2));

  // 10 aisles spread across left 72% of the store width
  const aisleCount   = 10;
  const aisleZoneEnd = parseFloat((margin + (widthFt - 2 * margin) * 0.72).toFixed(2));
  const aisleSpacing = (aisleZoneEnd - margin) / (aisleCount + 1);
  const aisleXs = Array.from({ length: aisleCount }, (_, i) =>
    parseFloat((margin + aisleSpacing * (i + 1)).toFixed(2)));

  // Front-wall x positions (4 registers in the checkout zone)
  const xLeft = parseFloat(margin.toFixed(2));
  const xReg1 = parseFloat((widthFt * 0.75).toFixed(2));
  const xReg2 = parseFloat((widthFt * 0.82).toFixed(2));
  const xReg3 = parseFloat((widthFt * 0.88).toFixed(2));
  const xReg4 = parseFloat((widthFt * 0.93).toFixed(2));
  const xExit = parseFloat((widthFt - margin).toFixed(2));

  const nEntr  = uid(), nExit = uid();
  const nReg1  = uid(), nReg2 = uid(), nReg3 = uid(), nReg4 = uid();
  const nStock = uid();

  const cfg = {
    aisleCount, aisleXs, yFront, yBack,
    xLeft, xRight: xExit,
    aisleWidth: 10, shelfDepth: 5, spineWidth: 10,
    crossCount: 4,
  };
  const aisle = buildAisles(cfg, aisleLayout);
  const { leftAnchor, rightAnchor } = aisle;

  layout.nodes = [
    node(nEntr,  "Entrance",  xLeft, yFront, { angle: 270 }),
    node(nExit,  "Exit",      xExit, yFront, { angle: 270 }),
    node(nReg1,  "Register",  xReg1, yFront, { angle: 270, width: 6, length: 6 }),
    node(nReg2,  "Register",  xReg2, yFront, { angle: 270, width: 6, length: 6 }),
    node(nReg3,  "Register",  xReg3, yFront, { angle: 270, width: 6, length: 6 }),
    node(nReg4,  "Register",  xReg4, yFront, { angle: 270, width: 6, length: 6 }),
    node(nStock, "Stockroom", aisleXs[aisleCount - 1], yStock, { angle: 90, width: 16, length: 12 }),
    ...aisle.nodes,
  ];

  const edges = [...aisle.edges];

  // Front spine
  edges.push(edge(uid(), nEntr, leftAnchor,
    parseFloat((aisleXs[0] - xLeft).toFixed(2)), { edgeType: "aisle", width: 12 }));
  edges.push(edge(uid(), rightAnchor, nReg1,
    parseFloat((xReg1 - aisleXs[aisleCount - 1]).toFixed(2)), { edgeType: "aisle", width: 12 }));
  edges.push(edge(uid(), nReg1, nReg2, parseFloat((xReg2 - xReg1).toFixed(2)), { edgeType: "aisle", width: 10 }));
  edges.push(edge(uid(), nReg2, nReg3, parseFloat((xReg3 - xReg2).toFixed(2)), { edgeType: "aisle", width: 10 }));
  edges.push(edge(uid(), nReg3, nReg4, parseFloat((xReg4 - xReg3).toFixed(2)), { edgeType: "aisle", width: 10 }));
  edges.push(edge(uid(), nReg4, nExit, parseFloat((xExit - xReg4).toFixed(2)), { edgeType: "aisle", width: 10 }));

  // Stockroom spur
  edges.push(edge(uid(), aisle.topNodes[aisleCount - 1], nStock,
    stockDepth, { edgeType: "aisle", width: 8 }));

  // Racetrack walls
  if (aisleLayout === "racetrack" && aisle.nTL != null) {
    const wallLen = parseFloat((yFront - yBack).toFixed(2));
    edges.push(edge(uid(), nEntr, aisle.nTL, wallLen, { edgeType: "aisle", width: 10 }));
    edges.push(edge(uid(), nExit, aisle.nTR, wallLen, { edgeType: "aisle", width: 10 }));
  }

  layout.edges = edges;
  layout.checkoutQueues = [nReg1, nReg2, nReg3, nReg4].map(rid => {
    const rn = layout.nodes.find(n => n.id === rid);
    return checkoutQueue(rid, rn, 6, 75);
  });
  return layout;
}

// ---------------------------------------------------------------------------
// Aisle layout metadata (for the wizard picker UI)
// ---------------------------------------------------------------------------

export const AISLE_LAYOUTS = [
  {
    id:          "parallel",
    label:       "Parallel",
    icon:        "⊟",
    description: "Straight aisles from front to back. Classic grocery layout.",
  },
  {
    id:          "racetrack",
    label:       "Racetrack",
    icon:        "⬭",
    description: "Outer loop with inner aisles. Natural circular customer flow.",
  },
  {
    id:          "grid",
    label:       "Grid",
    icon:        "⊞",
    description: "Crossing horizontal and vertical aisles. Maximum shelf density.",
  },
];

// ---------------------------------------------------------------------------
// Template metadata (for the wizard picker UI)
// ---------------------------------------------------------------------------

export const TEMPLATES = [
  {
    id:          "blank",
    label:       "Blank",
    description: "Clean canvas with one entrance, one exit. Build from scratch.",
    icon:        "□",
    defaultW:    120,
    defaultD:    80,
    build:       buildBlank,
  },
  {
    id:          "convenience",
    label:       "Convenience Store",
    description: "Compact layout. 3 aisles, 1 register, 1 stockroom.",
    icon:        "⊞",
    defaultW:    63,   // 50 × 1.25
    defaultD:    48,   // 38 × 1.25
    build:       buildConvenienceStore,
  },
  {
    id:          "small-grocery",
    label:       "Small Grocery",
    description: "Neighborhood market. 6 aisles, 2 registers, 1 stockroom.",
    icon:        "⊟",
    defaultW:    188,  // 150 × 1.25
    defaultD:    125,  // 100 × 1.25
    build:       buildSmallGrocery,
  },
  {
    id:          "supermarket",
    label:       "Supermarket",
    description: "Full-size store. 10 aisles, 4 registers, 1 stockroom.",
    icon:        "⊠",
    defaultW:    313,  // 250 × 1.25
    defaultD:    188,  // 150 × 1.25
    build:       buildSupermarket,
  },
];
