// Training artwork is shared with website/public/labs/thermal-training/index.html.
// The browser draws backend telemetry; all actual flight physics runs on the device clock.
(() => {
  'use strict';
  const canvas = document.getElementById('freeFlightCanvas');
  const ctx = canvas.getContext('2d');
  const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
  const lerp = (a, b, t) => a + (b - a) * t;
  const maxTurnRateDps = 30;
  const friendCanopySkin = null, arrivingCanopySkin = null;
  const terrainMapImage = new Image();
  terrainMapImage.src = '/free-flight-terrain.png';
  const mapPoint = (x, y, focusX, focusY, scale, rect) => ({
    x: rect.x + rect.w / 2 + (x - focusX) * scale,
    y: rect.y + rect.h / 2 - (y - focusY) * scale
  });
  let state = {x:0, y:0, z:1000, startAlt:1000, heading:0, turnRate:0};
  let latestStatus = null, active = false, initialized = false;
  async function post(body) {
    try {
      const response = await fetch('/api/free-flight', {method:'POST',
        headers:{'Content-Type':'application/json'}, body:JSON.stringify(body)});
      const result = await response.json();
      if (!result.ok) throw Error(result.error || 'Flight update failed');
      document.getElementById('freeFlightError').textContent = '';
    } catch (error) {
      document.getElementById('freeFlightError').textContent = 'Connection interrupted: ' + error.message;
    }
  }
  const fields = [
    ['diameterM', 'Nominal diameter', 40, 500, 5, 160, 'm'],
    ['strengthMps', 'Peak upward air speed', .5, 10, .1, 4, 'm/s'],
    ['peakiness', 'Peakiness · broad → concentrated', .3, 5, .05, 1.65, ''],
    ['windMps', 'Wind speed', 0, 15, .25, 2, 'm/s'],
    ['windFromDeg', 'Wind from', 0, 360, 5, 250, '°'],
    ['tiltDeg', 'Plume tilt from vertical', 0, 70, 1, 26.565, '°'],
    ['diameterVariance', 'Diameter variation with height', 0, .7, .01, 0, '%'],
    ['strengthVariance', 'Strength variation with height', 0, .7, .01, 0, '%'],
    ['texture', 'Thermal texture', 0, 1, .01, 0, '%'],
    ['sinkMps', 'Glider sink through air', .5, 2, .1, 1, 'm/s']
  ];
  const parameters = {seed:1, drifting:false};
  const inputs = new Map();
  let parameterTimer;
  const displayValue = (field, value) => {
    const unit = field[6];
    return (unit === '%' ? Math.round(value*100) :
      unit === '°' || unit === 'm' ? Math.round(value) :
      Number(value).toFixed(2).replace(/0$/, '')) + (unit ? ' '+unit : '');
  };
  for (const field of fields) {
    const [name, label, min, max, step, initial] = field;
    parameters[name] = initial;
    const wrap = document.createElement('div'); wrap.className = 'flight-control';
    const text = document.createElement('label'); text.htmlFor = 'flight-'+name;
    const title = document.createElement('span'); title.textContent = label;
    const output = document.createElement('output'); output.textContent = displayValue(field, initial);
    const input = document.createElement('input');
    input.type='range'; input.id='flight-'+name;
    input.min=min; input.max=max; input.step=step; input.value=initial;
    text.append(title, output); wrap.append(text, input);
    document.getElementById('flightParameters').append(wrap);
    inputs.set(name, {input, output, field});
    input.addEventListener('input', () => {
      parameters[name] = Number(input.value);
      output.textContent = displayValue(field, parameters[name]);
      clearTimeout(parameterTimer);
      parameterTimer = setTimeout(() => post({parameters}), 100);
    });
  }
  document.getElementById('flightDrifting').addEventListener('change', e => {
    parameters.drifting=e.target.checked; post({parameters});
  });
  document.getElementById('flightNewSeed').addEventListener('click', () => {
    parameters.seed = (parameters.seed+1) >>> 0; post({parameters});
  });
  const held = new Set();
  let touchSteering = 0;
  function sendControls() {
    if (!active) return;
    post({steering:touchSteering || (held.has('ArrowRight')?1:0)-(held.has('ArrowLeft')?1:0),
      speedInput:(held.has('ArrowUp')?1:0)-(held.has('ArrowDown')?1:0)});
  }
  function clearControls() { held.clear(); touchSteering=0; if (active) sendControls(); }
  document.getElementById('flightReset').addEventListener('click', () => {
    clearControls(); post({reset:true, parameters});
  });
  document.getElementById('flightStraight').addEventListener('click', () => {
    clearControls(); post({straighten:true});
  });
  for (const [id, direction] of [['flightLeft',-1],['flightRight',1]]) {
    const button = document.getElementById(id);
    button.addEventListener('pointerdown', e => {
      e.preventDefault(); button.setPointerCapture(e.pointerId); touchSteering=direction; sendControls();
    });
    button.addEventListener('pointerup', () => { touchSteering=0; sendControls(); });
    button.addEventListener('pointercancel', clearControls);
  }
  const editing = element => ['INPUT','SELECT','TEXTAREA'].includes(element.tagName) || element.isContentEditable;
  addEventListener('keydown', e => {
    if (!active || editing(e.target)) return;
    if (['ArrowLeft','ArrowRight','ArrowUp','ArrowDown'].includes(e.code)) {
      e.preventDefault(); e.stopImmediatePropagation();
      if (!held.has(e.code)) { held.add(e.code); sendControls(); }
    } else if (e.code === 'Space') {
      e.preventDefault(); e.stopImmediatePropagation();
      if (!e.repeat) fetch('/api/clock', {method:'POST',headers:{'Content-Type':'application/json'},
        body:JSON.stringify({paused:!latestStatus.paused})});
    } else if (e.code === 'KeyC' && !e.repeat) { clearControls(); post({straighten:true}); }
  }, true);
  addEventListener('keyup', e => {
    if (held.delete(e.code)) { e.preventDefault(); e.stopImmediatePropagation(); sendControls(); }
  }, true);
  addEventListener('blur', clearControls);
  document.addEventListener('visibilitychange', () => { if (document.hidden) clearControls(); });
  setInterval(() => { if (held.size || touchSteering) sendControls(); }, 150);
  const reveal = document.getElementById('showLiveThermal');
  const trailToggle = document.getElementById('showLiveTrail');
  const zoom = document.getElementById('flightZoom');
  for (const input of [reveal, trailToggle, zoom]) input.addEventListener('input', draw);
  terrainMapImage.addEventListener('load', draw);
  window.leafFreeFlight = {active:false};
  addEventListener('leaf-status', e => {
    latestStatus=e.detail;
    const flight=e.detail.freeFlight, wasActive=active;
    active=Boolean(flight?.active);
    window.leafFreeFlight.active=active;
    document.getElementById('freeFlightCard').hidden=!active;
    document.getElementById('recordedPathCard').hidden=active;
    document.getElementById('recordingHint').hidden=active;
    document.getElementById('seek').disabled=active;
    document.getElementById('keyboardHint').textContent=active
      ? 'Arrow keys fly the glider · click Leaf buttons to operate the device'
      : 'arrow keys + enter · a held key is a held button';
    if (!active) { if (wasActive) clearControls(); return; }
    state=flight;
    if (!initialized || !wasActive) {
      initialized=true; Object.assign(parameters, flight.parameters);
      for (const [name, item] of inputs) {
        item.input.value=parameters[name]; item.output.textContent=displayValue(item.field, parameters[name]);
      }
      document.getElementById('flightDrifting').checked=parameters.drifting;
      document.getElementById('scenarioList').value='@free-flight';
    }
    draw();
  });

  function drawTerrain(rect, focusX, focusY, mapScale, zoom = 1) {
  const altitudeSeparation = clamp((state.z - state.startAlt) / 700, 0, 1);
  const groundParallax = lerp(0.24, 0.16, altitudeSeparation);
  if (terrainMapImage.complete && terrainMapImage.naturalWidth) {
    const tileSize = 760 * zoom * lerp(1, 0.86, altitudeSeparation);
    const groundScale = mapScale * groundParallax;
    const offsetX = ((-focusX * groundScale) % tileSize + tileSize) % tileSize;
    const offsetY = ((focusY * groundScale) % tileSize + tileSize) % tileSize;
    const startX = rect.x + offsetX - tileSize;
    const startY = rect.y + offsetY - tileSize;
    for (let y = startY; y < rect.y + rect.h; y += tileSize) {
      for (let x = startX; x < rect.x + rect.w; x += tileSize) {
        // A one-pixel overlap avoids hairline seams from fractional canvas scaling.
        ctx.drawImage(terrainMapImage, x, y, tileSize + 1, tileSize + 1);
      }
    }
    ctx.fillStyle = "rgba(255,255,255,.14)";
    ctx.fillRect(rect.x, rect.y, rect.w, rect.h);
    return;
  }
  const offsetX = ((focusX * groundParallax) % 170 + 170) % 170;
  const offsetY = ((focusY * groundParallax) % 145 + 145) % 145;
  const base = ctx.createLinearGradient(rect.x, rect.y, rect.x + rect.w, rect.y + rect.h);
  base.addColorStop(0, "#e7eaeb");
  base.addColorStop(.52, "#cbd1d3");
  base.addColorStop(1, "#eef0f1");
  ctx.fillStyle = base;
  ctx.fillRect(rect.x, rect.y, rect.w, rect.h);

  ctx.save();
  ctx.lineWidth = 1;
  for (let layer = 0; layer < 7; layer++) {
    ctx.strokeStyle = `rgba(74, 86, 93, ${0.07 + layer * 0.012})`;
    const spacing = 52 + layer * 11;
    for (let y = rect.y - spacing; y < rect.y + rect.h + spacing; y += spacing) {
      ctx.beginPath();
      for (let x = rect.x - 30; x <= rect.x + rect.w + 30; x += 8) {
        const px = x + Math.sin((y + offsetY + layer * 31) * .018) * (13 + layer * 2);
        const py = y + Math.sin((x + offsetX) * .021 + layer * .9) * (13 + layer * 3);
        if (x === rect.x - 30) ctx.moveTo(px, py); else ctx.lineTo(px, py);
      }
      ctx.stroke();
    }
  }
  ctx.globalAlpha = .22;
  ctx.fillStyle = "#aab2b5";
  for (let i = 0; i < 10; i++) {
    const x = rect.x + ((i * 137 - offsetX * .45) % (rect.w + 100)) - 50;
    const y = rect.y + ((i * 83 - offsetY * .45) % (rect.h + 90)) - 45;
    ctx.beginPath();
    ctx.ellipse(x, y, 80 + (i % 3) * 25, 28 + (i % 4) * 9, i * .43, 0, Math.PI * 2);
    ctx.fill();
  }
  ctx.restore();
}

function drawParaglider(x, y, headingDeg, turnRateDps, visualScale = 1, canopy = "blue", danger = false) {
  const isFriend = canopy === 'friend' || canopy === 'arrival';
  const skin = canopy === 'arrival' ? arrivingCanopySkin : friendCanopySkin;
  const bank = clamp(turnRateDps / maxTurnRateDps, -1, 1);
  const bankAbs = Math.abs(bank);
  const span = 32;
  const wingShiftX = bank * 5.5;
  const pilotX = -bank * 10;

  // Project a point on the canopy. In a left bank (negative bank), the
  // right side grows and spreads toward the viewer while the left compresses.
  function wingPoint(u, edge = "trailing") {
    const isInsideWing = bank !== 0 && Math.sign(u) === Math.sign(bank);
    const foldAmount = isInsideWing ? Math.pow(bankAbs, .62) : 0;
    const compressedAbsU = Math.abs(u) <= .78
      ? Math.abs(u)
      : .78 + (Math.abs(u) - .78) * (1 - foldAmount * .94);
    const curvedU = Math.sign(u) * compressedAbsU;
    const perspective = 1 - bank * u * 0.28;
    const projectedU = curvedU * (1 - bank * curvedU * 0.13);
    const centerFullness = Math.pow(Math.max(0, 1 - u * u), 0.62);
    const chord = (4.2 + 14.8 * centerFullness) * perspective;
    const bankHeight = bank * u * 5.2;
    const trailingY = -7 + 2.1 * centerFullness + bankHeight;
    return {
      x: wingShiftX + projectedU * span,
      y: edge === "leading" ? trailingY - chord : trailingY,
      perspective
    };
  }

  ctx.save();
  ctx.translate(x, y);
  ctx.rotate(headingDeg * Math.PI / 180);
  ctx.scale(visualScale, visualScale);

  // The inflated canopy uses the same attachment geometry at every bank;
  // projection alone changes the apparent wingtip and rib spacing.
  const leftTrail = wingPoint(-1, "trailing");
  const leftLead = wingPoint(-1, "leading");
  const rightLead = wingPoint(1, "leading");
  const rightTrail = wingPoint(1, "trailing");
  const highlightPosition = .5 - bank * .28;
  const highlightSpread = .26;
  const leftTipLight = clamp(.28 + bank * .16, .08, .52);
  const rightTipLight = clamp(.28 - bank * .16, .08, .52);
  const palettes = {
    blue: {dark: [5, 75, 128], light: [220, 245, 255]},
    green: {dark: [20, 100, 42], light: [190, 255, 170]},
    yellow: {dark: [150, 98, 0], light: [255, 244, 120]},
    friend: {dark: [45, 65, 82], light: [248, 250, 252]},
    red: {dark: [125, 15, 20], light: [255, 220, 220]}
  };
  const palette = danger ? palettes.red : (isFriend ? palettes.friend : palettes[canopy] || palettes.blue);
  const penalty = canopy === "blue" && !danger ? clamp(state.spiralPenalty || 0, 0, 1) : 0;
  const canopyColor = light => {
    const shade = clamp(light, 0, 1);
    const base = palette.dark.map((channel, index) => lerp(channel, palette.light[index], shade));
    const red = [
      lerp(125, 255, shade),
      lerp(15, 220, shade),
      lerp(20, 220, shade)
    ];
    const mixed = base.map((channel, index) => Math.round(lerp(channel, red[index], penalty)));
    return `rgb(${mixed.join(", ")})`;
  };
  const wingGradient = ctx.createLinearGradient(leftTrail.x, 0, rightTrail.x, 0);
  wingGradient.addColorStop(0, canopyColor(leftTipLight));
  wingGradient.addColorStop(
    Math.max(.07, highlightPosition - highlightSpread),
    canopyColor(.62 + bank * .08)
  );
  wingGradient.addColorStop(highlightPosition, canopyColor(1));
  wingGradient.addColorStop(
    Math.min(.93, highlightPosition + highlightSpread),
    canopyColor(.62 - bank * .08)
  );
  wingGradient.addColorStop(1, canopyColor(rightTipLight));
  ctx.fillStyle = wingGradient;
  ctx.strokeStyle = "#142031";
  ctx.lineWidth = 1.65;
  ctx.lineJoin = "round";
  ctx.beginPath();
  ctx.moveTo(leftTrail.x, leftTrail.y);
  ctx.quadraticCurveTo(
    leftTrail.x - 1.8 * leftTrail.perspective,
    (leftTrail.y + leftLead.y) / 2,
    leftLead.x,
    leftLead.y
  );
  for (let i = 1; i < 32; i++) {
    const u = -1 + i / 16;
    const point = wingPoint(u, "leading");
    ctx.lineTo(point.x, point.y);
  }
  ctx.lineTo(rightLead.x, rightLead.y);
  ctx.quadraticCurveTo(
    rightTrail.x + 1.8 * rightTrail.perspective,
    (rightTrail.y + rightLead.y) / 2,
    rightTrail.x,
    rightTrail.y
  );
  for (let i = 31; i >= 0; i--) {
    const u = -1 + i / 16;
    const point = wingPoint(u, "trailing");
    ctx.lineTo(point.x, point.y);
  }
  ctx.closePath();
  ctx.fill();
  ctx.stroke();

  if (isFriend && !danger) {
    ctx.save();
    ctx.clip();
    if (skin?.width) {
      // The same traced UV skin as the 3D model, mapped along the banked span.
      const strips = 128, sw = skin.width / strips;
      for (let i = 0; i < strips; i++) {
        const u = -1 + 2 * i / strips, nextU = -1 + 2 * (i + 1) / strips;
        const lead = wingPoint(u, "leading"), next = wingPoint(nextU, "leading");
        const trail = wingPoint(u, "trailing");
        ctx.drawImage(skin, i * sw, 0, sw, skin.height,
          lead.x, lead.y, next.x - lead.x + .12, trail.y - lead.y);
      }
      const shade = ctx.createLinearGradient(leftTrail.x, 0, rightTrail.x, 0);
      shade.addColorStop(0, 'rgba(20, 36, 49, .24)');
      shade.addColorStop(.5, 'rgba(255, 255, 255, .06)');
      shade.addColorStop(1, 'rgba(20, 36, 49, .24)');
      ctx.fillStyle = shade;
      ctx.fillRect(-50, -50, 100, 70);
    }
    ctx.restore();
  }

  // Full-width ribs. Equal cell positions in 3D become deliberately unequal
  // on screen as the nearer half of the canopy expands.
  ctx.strokeStyle = isFriend && !danger ? "rgba(65, 80, 70, .22)" : "rgba(239, 245, 248, .72)";
  ctx.lineWidth = 1;
  for (let i = 1; i < 18; i++) {
    const u = -1 + i / 9;
    const lead = wingPoint(u, "leading");
    const trail = wingPoint(u, "trailing");
    ctx.beginPath();
    ctx.moveTo(lead.x, lead.y + 1);
    ctx.quadraticCurveTo(
      (lead.x + trail.x) / 2 - bank * u * 1.8,
      (lead.y + trail.y) / 2,
      trail.x,
      trail.y - .4
    );
    ctx.stroke();
  }

  // Suspension lines keep fixed attachment points on wing and harness. Their
  // changing lengths and angles make the pilot/wing counter-shift legible.
  ctx.strokeStyle = "rgba(20, 32, 49, .78)";
  ctx.lineWidth = 1.15;
  const suspensionGroups = [
    {u: -.82, harness: -3.8},
    {u: -.56, harness: -2.7},
    {u: -.30, harness: -1.4},
    {u: .30, harness: 1.4},
    {u: .56, harness: 2.7},
    {u: .82, harness: 3.8}
  ];
  for (const line of suspensionGroups) {
    const attach = wingPoint(line.u, "trailing");
    ctx.beginPath();
    ctx.moveTo(attach.x, attach.y);
    ctx.lineTo(pilotX + line.harness, 8.5 + bankAbs * 1.5);
    ctx.stroke();
  }

  // Harness/body, displaced opposite the wing center by the turn load.
  ctx.save();
  ctx.translate(pilotX, 10);
  ctx.rotate(bank * .24);
  ctx.fillStyle = "#142031";
  ctx.beginPath();
  ctx.ellipse(0, 3.8, 7.5, 4.6, 0, 0, Math.PI * 2);
  ctx.fill();
  if (isFriend && !danger) {
    ctx.fillStyle = "#39434a";
    ctx.beginPath();
    ctx.ellipse(0, 2.6, 4.8, 3.3, 0, 0, Math.PI * 2);
    ctx.fill();
  }

  // The pilot's head leans into the turn rather than staying over the harness.
  const headLean = bank * 4.2;
  ctx.fillStyle = "#142031";
  ctx.strokeStyle = "#ffffff";
  ctx.lineWidth = 1.5;
  ctx.beginPath();
  ctx.arc(headLean, -2.5, 3.6, 0, Math.PI * 2);
  ctx.fill();
  ctx.stroke();
  ctx.restore();

  // A small center mark makes the wing's counter-shift readable at a glance.
  ctx.fillStyle = canopy === 'arrival' ? '#ff4f2a' : canopy === "friend" ? "#c4ef00" : "#d8ff00";
  ctx.beginPath();
  ctx.arc(wingShiftX, wingPoint(0, "trailing").y - .8, 1.35, 0, Math.PI * 2);
  ctx.fill();
  ctx.restore();
}



  function draw() {
    if (!active) return;
    const rect={x:0,y:0,w:canvas.width,h:canvas.height};
    const scale=Math.min(rect.w,rect.h)/360*Number(zoom.value);
    ctx.clearRect(0,0,rect.w,rect.h);
    drawTerrain(rect,state.x,state.y,scale,Number(zoom.value));
    ctx.save(); ctx.beginPath(); ctx.rect(0,0,rect.w,rect.h); ctx.clip();
    const point=(x,y)=>mapPoint(x,y,state.x,state.y,scale,rect);
    if (reveal.checked) {
      const center=point(state.centerX,state.centerY), radius=state.radius*scale;
      const gradient=ctx.createRadialGradient(center.x,center.y,0,center.x,center.y,radius*1.8);
      for (let i=0;i<=16;i++) {
        const r=i/16;
        gradient.addColorStop(r,`rgba(114,172,45,${(Math.exp(-state.parameters.peakiness*(r*1.8)**2)*.52*(1-r**8)).toFixed(3)})`);
      }
      ctx.fillStyle=gradient; ctx.beginPath(); ctx.arc(center.x,center.y,radius*1.8,0,Math.PI*2); ctx.fill();
      ctx.strokeStyle='rgba(65,110,27,.6)'; ctx.setLineDash([5,6]); ctx.lineWidth=1.2;
      for (const fraction of [.25,.5,.75,1]) {
        ctx.beginPath(); ctx.arc(center.x,center.y,radius*fraction,0,Math.PI*2);ctx.stroke();
      }
      ctx.setLineDash([]); ctx.fillStyle='#385923';ctx.font='13px system-ui';ctx.textAlign='center';
      ctx.fillText('Thermal at '+Math.round(state.z)+' m',center.x,center.y-radius-12);
    }
    if (trailToggle.checked && state.trail.length>1) {
      const points=state.trail.map(p=>({...point(p[0],p[1]),climb:p[3]}));
      ctx.lineCap='round';ctx.lineJoin='round';ctx.strokeStyle='rgba(255,255,255,.85)';ctx.lineWidth=5;
      ctx.beginPath();points.forEach((p,i)=>i?ctx.lineTo(p.x,p.y):ctx.moveTo(p.x,p.y));ctx.stroke();
      ctx.lineWidth=2.5;
      for(let i=1;i<points.length;i++) {
        ctx.strokeStyle=points[i].climb>0?'#547b45':'#8b7771';
        ctx.beginPath();ctx.moveTo(points[i-1].x,points[i-1].y);ctx.lineTo(points[i].x,points[i].y);ctx.stroke();
      }
    }
    let x=state.x,y=state.y,h=state.heading;
    const windRad=(state.parameters.windFromDeg+180)*Math.PI/180;
    ctx.strokeStyle='rgba(50,70,85,.45)';ctx.lineWidth=2;ctx.setLineDash([4,7]);ctx.beginPath();
    for(let i=0;i<30;i++) {
      h+=state.turnRate*.2;
      x+=(Math.sin(h*Math.PI/180)*state.airspeed+Math.sin(windRad)*state.parameters.windMps)*.2;
      y+=(Math.cos(h*Math.PI/180)*state.airspeed+Math.cos(windRad)*state.parameters.windMps)*.2;
      const p=point(x,y); if(i)ctx.lineTo(p.x,p.y);else ctx.moveTo(p.x,p.y);
    }
    ctx.stroke();ctx.setLineDash([]);
    const pilot=point(state.x,state.y);
    drawParaglider(pilot.x,pilot.y,state.heading,state.turnRate,1.35);
    ctx.restore();
    const card=(x,y,w,h)=>{
      ctx.fillStyle='rgba(250,252,249,.92)';ctx.strokeStyle='rgba(90,105,93,.22)';ctx.lineWidth=1;
      ctx.beginPath();ctx.roundRect(x,y,w,h,12);ctx.fill();ctx.stroke();
    };
    card(18,18,220,70);ctx.textAlign='left';ctx.fillStyle='#405047';ctx.font='12px system-ui';
    ctx.fillText('FREE FLIGHT · '+(state.parameters.drifting?'DRIFTING COLUMN':'ANCHORED PLUME'),32,41);
    ctx.font='bold 26px system-ui';ctx.fillStyle='#23382d';ctx.fillText(Math.round(state.z)+' m',32,72);
    card(rect.w-238,18,220,70);ctx.textAlign='right';ctx.font='12px system-ui';ctx.fillStyle='#405047';
    ctx.fillText('AIR SPEED / NET CLIMB',rect.w-32,41);ctx.font='bold 22px system-ui';
    ctx.fillStyle=state.climb>0?'#426b28':'#7b5144';
    ctx.fillText((state.airspeed*3.6).toFixed(0)+' km/h   '+(state.climb>=0?'+':'')+state.climb.toFixed(1)+' m/s',rect.w-32,72);
    card(rect.w/2-185,rect.h-94,370,76);
    const barY=rect.h-55,barX=rect.w/2;
    ctx.strokeStyle='#b4c0b2';ctx.lineWidth=5;ctx.beginPath();ctx.moveTo(barX-135,barY);ctx.lineTo(barX+135,barY);ctx.stroke();
    ctx.strokeStyle='#294f39';ctx.lineWidth=3;ctx.beginPath();ctx.moveTo(barX,barY);ctx.lineTo(barX+state.turnRate/30*135,barY);ctx.stroke();
    ctx.fillStyle='#294f39';ctx.beginPath();ctx.arc(barX+state.turnRate/30*135,barY,6,0,Math.PI*2);ctx.fill();
    ctx.textAlign='center';ctx.font='12px system-ui';ctx.fillText('TURN RATE  '+state.turnRate.toFixed(1)+'°/s',barX,rect.h-72);
    ctx.fillStyle='#637167';ctx.font='11px system-ui';ctx.fillText('← left       C straight       right →',barX,rect.h-28);
    if (latestStatus?.paused) {
      card(rect.w/2-115,18,230,44);ctx.font='bold 17px system-ui';ctx.fillStyle='#23382d';
      ctx.fillText('Paused · Space resumes',rect.w/2,46);
    }
    ctx.textAlign='left';ctx.font='12px system-ui';ctx.fillStyle='#334739';
    ctx.fillText(state.parameters.windMps.toFixed(1)+' m/s wind from '+Math.round(state.parameters.windFromDeg)+'°',20,rect.h-18);
  }
})();
