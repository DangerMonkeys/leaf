/* Work together: deterministic weather and a sensor-driven pilot, independent of rendering. */
(function (root) {
  'use strict';
  const RAD = Math.PI / 180;
  const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
  const approach = (value, target, maxDelta) => value + clamp(target - value, -maxDelta, maxDelta);
  const smooth = (value, target, dt, seconds) => value + (target - value) * (1 - Math.exp(-dt / seconds));
  const deadband = (v, width) => Math.sign(v) * Math.max(0, Math.abs(v) - width);
  const config = Object.freeze({
    diameterM: 140,
    initialCoreClimbMps: 6,
    thermalInteriorDropMps: 3.5,
    thermalEdgeStartFraction: 0.7,
    sinkMps: 1,
    airspeedMps: 10,
    idealTurnDps: 20,
    turnDirection: -1,
    initialSeparationDeg: 180,
    goalClimbM: 500,
    trafficClearanceM: 20,
    goalAltitudeToleranceM: 25,
    driftMaxMph: 1,
    driftRampS: 10,
    driftInitialBearingDeg: 0,
    driftBearingRateMaxDps: 10,
    driftBearingAccelerationDps2: 5,
    driftRetargetS: 2,
    strengthRangeFraction: 0.25,
    strengthRateMaxFractionPerS: 0.02,
    strengthRetargetS: 3,
    strengthRateFilterS: 1.5,
    varioFilterS: 0.6,
    trendFilterS: 0.8,
    trendDeadbandMps2: 0.015,
    trendGain: 72,
    trendMaxCorrectionDps: 18,
    relativeClimbFilterS: 0.25,
    relativeClimbDeadbandMps: 0.04,
    socialGain: 9,
    socialMaxCorrectionDps: 7.5,
    socialInsideTurnWeight: 0.8,
    wideningIncentiveScale: 0.5,
    socialFullRangeM: 100,
    socialCutoffRangeM: 250,
    separationTargetDeg: 180,
    separationMaxCorrectionDps: 3,
    separationFilterS: 1,
    separationMinRadiusM: 5,
    minTurnDps: 12,
    maxTurnDps: 28,
    turnResponseS: 0.2,
    turnSlewDps2: 20,
    thermalMinRadiusFraction: 0.20,
    thermalMaxRadiusFraction: 0.70,
    thermalGuardMarginM: 4,
    thermalGuardLookaheadS: 6,
    turnSinkStartDps: 20,
    turnSinkFullDps: 30,
    turnSinkMaxMps: 0,
    gravityMps2: 9.80665,
    topViewForecastSeconds: 6,
    topViewForecastStartSeconds: 0.8,
    topViewForecastStepSeconds: 0.1,
    sideViewMaxFps: 30,
    sideViewPixelRatio: 2,
    sideViewFovDeg: 32,
    sideViewMinSizeFraction: 0.02,
    sideViewMaxSizeFraction: 1.8,
    sideViewReferenceSizeFraction: 0.62,
    sideViewSizeScale: 1,
    sideViewReferenceRangeM: 57.3,
    sideViewPanoramaHorizonFraction: 0.61,
    sideViewElevationFocus: 11.895,
    sideViewHeadYawMaxDeg: 45,
    sideViewHorizontalFovDeg: 120,
    sideViewEdgeFadeDeg: 8,
    sideViewWingtipLevelElevationDeg: 30,
    sideViewWingtipSlewFraction: 0.95,
    sideViewWingtipCellHeightFraction: 0.054,
    sideViewWingtipLevelClearanceFraction: 0.016,
  });

  const harderConfig = Object.freeze({...config, driftMaxMph: 2,
    strengthRangeFraction: .50, strengthRateMaxFractionPerS: .04,
    windMph: 6, arrivalClimbM: 200,
    entryOutsideMarginM: 25, entryLeadInM: 65, entryGapDeg: 50});

  function random(seed) {
    let x = (seed ^ 0x5f3759df) >>> 0 || 1;
    return () => { x ^= x << 13; x ^= x >>> 17; x ^= x << 5; return (x >>> 0) / 4294967296; };
  }

  function createWeather(seed, c = config) {
    return { settings: c, x: 0, y: 0, elapsed: 0, speedMps: 0,
      bearingDeg: c.driftInitialBearingDeg, bearingRateDps: 0, bearingTargetDps: 0,
      strengthFactor: 1, strengthRate: 0, strengthTargetRate: 0,
      directionTimer: 0, strengthTimer: 0, random: random(seed) };
  }

  // Called exactly once per physics tick. Read-only lift/render queries never advance RNG.
  function advanceWeather(w, dt, c = w.settings || config) {
    if (!(dt > 0)) return;
    w.directionTimer -= dt;
    w.strengthTimer -= dt;
    if (w.directionTimer <= 0) {
      w.bearingTargetDps = (w.random() * 2 - 1) * c.driftBearingRateMaxDps;
      w.directionTimer += c.driftRetargetS;
    }
    if (w.strengthTimer <= 0) {
      w.strengthTargetRate = (w.random() * 2 - 1) * c.strengthRateMaxFractionPerS;
      w.strengthTimer += c.strengthRetargetS;
    }
    const previousBearing = w.bearingDeg;
    w.bearingRateDps = approach(w.bearingRateDps, w.bearingTargetDps, c.driftBearingAccelerationDps2 * dt);
    w.bearingDeg += w.bearingRateDps * dt;
    const speed = c.driftMaxMph * 0.44704 * clamp((w.elapsed + dt / 2) / c.driftRampS, 0, 1);
    const bearing = (previousBearing + w.bearingDeg) / 2 * RAD;
    w.x += Math.sin(bearing) * speed * dt;
    w.y += Math.cos(bearing) * speed * dt;
    w.elapsed += dt;
    w.speedMps = c.driftMaxMph * 0.44704 * clamp(w.elapsed / c.driftRampS, 0, 1);
    w.strengthRate = smooth(w.strengthRate, w.strengthTargetRate, dt, c.strengthRateFilterS);
    const low = 1 - c.strengthRangeFraction, high = 1 + c.strengthRangeFraction;
    w.strengthFactor = clamp(w.strengthFactor + w.strengthRate * dt, low, high);
    if ((w.strengthFactor <= low && w.strengthRate < 0) || (w.strengthFactor >= high && w.strengthRate > 0)) {
      w.strengthRate = -w.strengthRate;
      w.strengthTargetRate = -w.strengthTargetRate;
    }
  }

  function turnSink(rate, c = config) {
    return c.turnSinkMaxMps * clamp((Math.abs(rate) - c.turnSinkStartDps) / (c.turnSinkFullDps - c.turnSinkStartDps), 0, 1);
  }

  function createFriend(user, initialVario, c = config) {
    const radius = c.airspeedMps / (c.idealTurnDps * RAD);
    const bearing = c.initialSeparationDeg * RAD;
    return { color: 'friend', x: Math.sin(bearing) * radius, y: Math.cos(bearing) * radius,
      z: user.z, heading: (c.initialSeparationDeg + c.turnDirection * 90 + 360) % 360,
      turnRate: c.turnDirection * c.idealTurnDps, targetTurnRate: c.turnDirection * c.idealTurnDps,
      radius, vario: initialVario, filteredVario: initialVario, trend: 0,
      relativeClimb: 0, previousRelativeAltitude: 0,
      trendCorrection: 0, socialCorrection: 0, bankDeg: bankAngle(c.turnDirection * c.idealTurnDps, c) };
  }

  function bankAngle(turnDps, c = config) {
    return Math.atan(c.airspeedMps * turnDps * RAD / c.gravityMps2) / RAD;
  }

  // Own-vario steering has priority over both cooperative cues. A separate
  // helper supplies thermal-centered phase spacing; no user vario/control input.
  function steer(friend, observation, dt, c = config) {
    if (!(dt > 0)) return;
    const previous = friend.filteredVario;
    friend.filteredVario = smooth(previous, observation.vario, dt, c.varioFilterS);
    friend.trend = smooth(friend.trend, (friend.filteredVario - previous) / dt, dt, c.trendFilterS);
    const relativeAltitude = observation.userZ - friend.z;
    const relativeVelocity = (relativeAltitude - friend.previousRelativeAltitude) / dt;
    friend.previousRelativeAltitude = relativeAltitude;
    friend.relativeClimb = smooth(friend.relativeClimb, relativeVelocity, dt, c.relativeClimbFilterS);
    friend.trendCorrection = clamp(-c.trendGain * deadband(friend.trend, c.trendDeadbandMps2),
      -c.trendMaxCorrectionDps, c.trendMaxCorrectionDps);
    const dx = observation.userX - friend.x, dy = observation.userY - friend.y;
    const range = Math.hypot(dx, dy);
    const heading = friend.heading * RAD;
    const insideUser = c.turnDirection * (Math.cos(heading) * dx - Math.sin(heading) * dy) / Math.max(1, range);
    const visibility = clamp((c.socialCutoffRangeM - range) / (c.socialCutoffRangeM - c.socialFullRangeM), 0, 1);
    // Only the signed lateral position contributes: strongest abeam, smoothly
    // zero directly ahead/behind. There is no competing longitudinal cue.
    const socialSignal = c.socialGain * deadband(friend.relativeClimb, c.relativeClimbDeadbandMps);
    friend.socialCorrection = clamp(socialSignal * c.socialInsideTurnWeight * insideUser,
      -c.socialMaxCorrectionDps, c.socialMaxCorrectionDps) * visibility;
    const separation = (observation.separationCorrection || 0) * visibility;
    // Reject each conflicting partner cue independently, not merely the net sum.
    // Worsening lift cannot be cancelled by widening to follow/match the user;
    // improving lift likewise keeps its own-vario widening decision.
    const prioritize = cue => cue * friend.trendCorrection < 0 ? 0 : cue;
    friend.appliedSocialCorrection = prioritize(friend.socialCorrection);
    friend.appliedSeparationCorrection = prioritize(separation);
    friend.desiredIdealTurnDps = c.idealTurnDps + friend.appliedSeparationCorrection;
    const requestedMagnitude = clamp(friend.desiredIdealTurnDps + friend.trendCorrection + friend.appliedSocialCorrection, c.minTurnDps, c.maxTurnDps);
    // Scale the combined, bounded widening request only. Tightening commands
    // and their response timing remain identical, including mixed cue cases.
    const correction = requestedMagnitude - c.idealTurnDps;
    const magnitude = c.idealTurnDps + correction * (correction < 0 ? c.wideningIncentiveScale : 1);
    friend.targetTurnRate = c.turnDirection * magnitude;
    const response = smooth(friend.turnRate, friend.targetTurnRate, dt, c.turnResponseS);
    friend.turnRate = approach(friend.turnRate, response, c.turnSlewDps2 * dt);
    friend.radius = c.airspeedMps / (Math.abs(friend.turnRate) * RAD);
    friend.bankDeg = bankAngle(friend.turnRate, c);
  }

  // Exact circular-arc integration (stable radius, no Euler orbit drift).
  function advancePilot(pilot, dt, c = config) {
    const h0 = pilot.heading * RAD, omega = pilot.turnRate * RAD, h1 = h0 + omega * dt;
    if (Math.abs(omega) > 1e-8) {
      pilot.x += c.airspeedMps / omega * (Math.cos(h0) - Math.cos(h1));
      pilot.y += c.airspeedMps / omega * (Math.sin(h1) - Math.sin(h0));
    } else {
      pilot.x += c.airspeedMps * Math.sin(h0) * dt;
      pilot.y += c.airspeedMps * Math.cos(h0) * dt;
    }
    pilot.heading = ((h1 / RAD) % 360 + 360) % 360;
    pilot.x += (pilot.windX || 0) * dt;
    pilot.y += (pilot.windY || 0) * dt;
  }

  function separationObservation(friend, observation, thermal, c = config) {
    const fx = friend.x - thermal.x, fy = friend.y - thermal.y;
    const ux = observation.userX - thermal.x, uy = observation.userY - thermal.y;
    const nearestRadius = Math.min(Math.hypot(fx, fy), Math.hypot(ux, uy));
    if (nearestRadius < 1e-8) return {gapDeg: c.separationTargetDeg, correction: 0};
    // Forward angular distance from NPC to user, measured in the orbit direction.
    const gapDeg = ((c.turnDirection * (Math.atan2(ux, uy) - Math.atan2(fx, fy)) / RAD) % 360 + 360) % 360;
    // Periodic error avoids a +/-180 jump when one pilot laps the other. Positive
    // near the opposite-side target means NPC is falling behind: turn tighter.
    const correction = c.separationMaxCorrectionDps * Math.sin((gapDeg - c.separationTargetDeg) * RAD) *
      clamp(nearestRadius / c.separationMinRadiusM, 0, 1);
    return {gapDeg, correction};
  }

  function advanceGuardedFriend(friend, observation, thermal, dt, c = config) {
    if (!(dt > 0)) return;
    const previousRate = friend.turnRate;
    const separation = separationObservation(friend, observation, thermal, c);
    friend.angularSeparationDeg = separation.gapDeg;
    friend.separationCorrection = smooth(friend.separationCorrection || 0, separation.correction, dt, c.separationFilterS);
    steer(friend, {...observation, separationCorrection: friend.separationCorrection}, dt, c);
    const inner = thermal.radius * c.thermalMinRadiusFraction;
    const outer = thermal.radius * c.thermalMaxRadiusFraction;
    const margin = Math.min(c.thermalGuardMarginM, (outer - inner) * .2);
    const requested = friend.targetTurnRate;
    // This optional training assist deliberately knows the real core. Predict
    // bank response and center translation before a boundary is reached.
    const risk = target => {
      const p = {...friend, turnRate: previousRate};
      const steps = 20, step = c.thermalGuardLookaheadS / steps;
      let cost = 0;
      for (let i = 1; i <= steps; i++) {
        p.turnRate = approach(p.turnRate, smooth(p.turnRate, target, step, c.turnResponseS), c.turnSlewDps2 * step);
        advancePilot(p, step, c);
        const distance = Math.hypot(p.x - thermal.x - (thermal.vx || 0) * step * i,
          p.y - thermal.y - (thermal.vy || 0) * step * i);
        const error = Math.max(inner + margin - distance, distance - outer + margin, 0);
        cost += error * error / steps;
      }
      return cost;
    };
    let target = requested, bestRisk = risk(requested);
    friend.thermalGuardActive = bestRisk > 0;
    if (friend.thermalGuardActive) {
      let bestCost = bestRisk;
      for (let magnitude = c.minTurnDps; magnitude <= c.maxTurnDps; magnitude += 2) {
        const candidate = c.turnDirection * magnitude;
        const cost = risk(candidate) + .002 * (candidate - requested) ** 2;
        if (cost < bestCost) { bestCost = cost; target = candidate; }
      }
      friend.targetTurnRate = target;
      friend.turnRate = approach(previousRate, smooth(previousRate, target, dt, c.turnResponseS), c.turnSlewDps2 * dt);
    }
    friend.guardCorrectionDps = target - requested;
    advancePilot(friend, dt, c);
    const dx = friend.x - thermal.x, dy = friend.y - thermal.y;
    const distance = Math.hypot(dx, dy);
    const bounded = clamp(distance, inner, outer);
    friend.thermalBoundaryLimited = bounded !== distance;
    if (friend.thermalBoundaryLimited) {
      // Hard backstop for unexpected states/core motion. No bounce or heading
      // snap: remove only the prohibited radial displacement for this tick.
      const angle = distance > 1e-8 ? Math.atan2(dx, dy) : (friend.heading - c.turnDirection * 90) * RAD;
      friend.x = thermal.x + Math.sin(angle) * bounded;
      friend.y = thermal.y + Math.cos(angle) * bounded;
    }
    friend.radius = c.airspeedMps / (Math.abs(friend.turnRate) * RAD);
    friend.bankDeg = bankAngle(friend.turnRate, c);
  }

  function forecastPilot(pilot, c = config) {
    const preview = {...pilot}, points = [];
    // Copy the pilot: forecasts must never advance the real flight or steering.
    const start = Math.min(c.topViewForecastStartSeconds, c.topViewForecastSeconds);
    advancePilot(preview, start, c);
    points.push({x: preview.x, y: preview.y});
    const steps = Math.max(1, Math.ceil((c.topViewForecastSeconds - start) / c.topViewForecastStepSeconds));
    const dt = (c.topViewForecastSeconds - start) / steps;
    for (let i = 0; i < steps; i++) {
      advancePilot(preview, dt, c);
      points.push({x: preview.x, y: preview.y});
    }
    return points;
  }

  // Uniform-scale crop around the source skyline; no raster edits or stretching.
  function panoramaCrop(width, height, c = config) {
    const horizon = clamp(c.sideViewPanoramaHorizonFraction, 0.01, 0.99);
    const halfHeight = Math.min(horizon, 1 - horizon) * height;
    return {sx: 0, sy: horizon * height - halfHeight, sw: width, sh: 2 * halfHeight};
  }

  // Expand angular motion near the horizon without moving zero or either pole.
  // Focus 0 is linear; 11.895 gives ~8x sensitivity at the middle, smoothly tapering.
  function elevationScreenFraction(degrees, c = config) {
    const angle = clamp(degrees / 90, -1, 1);
    const focus = Math.max(0, c.sideViewElevationFocus ?? 0);
    const expanded = focus > 1e-6 ? Math.atan(focus * angle) / Math.atan(focus) : angle;
    return (1 - expanded) / 2;
  }

  function viewObservation(friend, user, c = config, headYawOverride = null) {
    const dx = user.x - friend.x, dy = user.y - friend.y;
    const h = friend.heading * RAD, horizontalDistance = Math.hypot(dx, dy);
    const distance = Math.max(1, Math.hypot(horizontalDistance, friend.z - user.z));
    const directionRange = Math.max(0.01, horizontalDistance);
    // Model coordinates: +X right, +Y up, -Z nose. Rotate the observer into body yaw.
    const localX = horizontalDistance < 0.01 ? directionRange : dx * Math.cos(h) - dy * Math.sin(h);
    const localZ = -(dx * Math.sin(h) + dy * Math.cos(h));
    // Use true horizontal range here, including zero at the poles. The 1 m
    // rendering-size floor must not distort an angular bearing.
    const viewElevationDeg = Math.atan2(friend.z - user.z, horizontalDistance) / RAD;
    const elevation = -viewElevationDeg; // Observer elevation seen from the model.
    const screenYFraction = elevationScreenFraction(viewElevationDeg, c);
    const bearingFromUser = Math.atan2(-dx, -dy) / RAD;
    const relativeBearing = ((bearingFromUser - user.heading + 540) % 360) - 180;
    // Keep the lesson's viewing shoulder, including when steering through level.
    // Switching shoulder with turn-rate sign would flip the camera 180 degrees.
    const insideSide = Math.sign(c.turnDirection);
    const offWingtipDeg = ((relativeBearing - insideSide * 90 + 540) % 360) - 180;
    // A smooth bounded head turn, continuous even across the unseen rear wrap.
    // Do not chase the friend all the way around or pin them to a screen edge.
    const headYawDeg = headYawOverride ?? c.sideViewHeadYawMaxDeg * Math.sin(offWingtipDeg * RAD);
    const viewOffsetDeg = offWingtipDeg - headYawDeg;
    const screenXFraction = 0.5 + viewOffsetDeg / c.sideViewHorizontalFovDeg;
    const edge = c.sideViewHorizontalFovDeg / 2;
    const fade = clamp((edge - Math.abs(viewOffsetDeg)) / c.sideViewEdgeFadeDeg, 0, 1);
    const visibility = fade * fade * (3 - 2 * fade);
    return { localX: localX / directionRange, localZ: localZ / directionRange, elevation, viewElevationDeg, screenYFraction,
      distance, horizontalDistance, relativeAltitude: friend.z - user.z, relativeBearing, insideSide,
      offWingtipDeg, headYawDeg, viewOffsetDeg, visibility, screenXFraction,
      bankDeg: bankAngle(friend.turnRate, c) };
  }

  function wingtipVisibility(turnDps, relativeBearingDeg, horizontalDistance) {
    if (!Number.isFinite(relativeBearingDeg) || !(horizontalDistance > 1e-8) || turnDps === 0) return 0;
    // Positive lateral direction is the user's right; negative is their left.
    // A bank toward the opposite side raises this tip out of the viewing area.
    const lateral = Math.sin(relativeBearingDeg * RAD);
    if (Math.abs(lateral) < 1e-8 || Math.sign(lateral) !== Math.sign(turnDps)) return 0;
    return 1;
  }

  function wingtipPose(turnDps, headYawDeg = 0, c = config) {
    const bankDeg = Math.abs(bankAngle(turnDps, c));
    return {side: Math.sign(turnDps), bankDeg,
      // Looking toward a friend on the right slides the near wing left, and vice versa.
      slewFraction: -clamp(headYawDeg / c.sideViewHeadYawMaxDeg, -1, 1) * c.sideViewWingtipSlewFraction,
      // Put the whole rounded hem above the viewport when level. Banking moves
      // opaque fabric down through the viewport edge; there is no opacity fade.
      tipFraction: elevationScreenFraction(c.sideViewWingtipLevelElevationDeg - bankDeg, c)
        - elevationScreenFraction(c.sideViewWingtipLevelElevationDeg, c)
        - c.sideViewWingtipLevelClearanceFraction};
  }

  function spriteLeft(paneX, paneWidth, size, fraction, anchorX) {
    return paneX + fraction * paneWidth - size * anchorX;
  }

  function spriteSizeFraction(distance, c = config) {
    // Inverse slant range: double the distance gives half the apparent size.
    // Broad safety limits only; nearby models are allowed to extend past the panel.
    return clamp(c.sideViewReferenceSizeFraction * c.sideViewSizeScale *
      c.sideViewReferenceRangeM / Math.max(1, distance),
      c.sideViewMinSizeFraction, c.sideViewMaxSizeFraction);
  }

  // A failed attempt is latched until the lesson explicitly starts a fresh goal.
  function updateClimbGoal(progress, user, friend, center, c = config, traffic = null, checkAltitude = traffic === null) {
    if (progress?.failed) return progress;
    // Formation safety is independent of the hidden, wandering thermal center.
    // Include approaching traffic too; altitude matching ends only after joining.
    const relativeAltitudeM = friend ? friend.z - user.z : null;
    const tooClose = (traffic || (friend ? [friend] : [])).some(p =>
      Math.hypot(p.x-user.x,p.y-user.y,p.z-user.z) < c.trafficClearanceM);
    const reason = !friend ? 'Friend unavailable'
      : user.vario <= 0 ? "Don't fall out of lift."
      : tooClose ? "Don't get too close to another glider."
      : !checkAltitude ? null
      : relativeAltitudeM < -c.goalAltitudeToleranceM ? "Don't get too far above your friend."
      : relativeAltitudeM > c.goalAltitudeToleranceM ? "Don't get too far below your friend." : null;
    const valid = reason === null;
    const baseZ = progress?.baseZ ?? user.z;
    const gainM = valid ? Math.max(0, user.z - baseZ) : (progress?.gainM || 0);
    return {baseZ, gainM, valid, failed: !valid, reason, relativeAltitudeM,
      complete: valid && gainM >= c.goalClimbM};
  }

  function relativeAltitudeLabel(friendZ, userZ) {
    const meters = Math.round(friendZ - userZ);
    return `Friend: ${meters > 0 ? '+' : ''}${meters === 0 ? 0 : meters} m`;
  }

  // Half the former interior gradient, with a smooth, steeper outer shoulder.
  function thermalLiftShape(normalizedRadius, c = config) {
    const r = Math.max(0, normalizedRadius);
    if (r >= 1) return 0;
    const slope = c.thermalInteriorDropMps / (c.initialCoreClimbMps + c.sinkMps);
    const edge = c.thermalEdgeStartFraction;
    if (r <= edge) return 1 - slope * r;
    const t = (r - edge) / (1 - edge);
    const start = 1 - slope * edge;
    return Math.max(0, (2*t*t*t - 3*t*t + 1) * start
      + (t*t*t - 2*t*t + t) * (-slope * (1 - edge)));
  }

  // Estimate the shared orbit from the two pilots' positions and current turns,
  // rather than handing the arriving pilot the hidden thermal center.
  function estimateOrbit(pilots, c = config) {
    const centers = pilots.map(p => {
      const rate = c.turnDirection * clamp(Math.abs(p.turnRate), 12, 28) * RAD;
      return {x:p.x + c.airspeedMps/rate*Math.cos(p.heading*RAD),
        y:p.y - c.airspeedMps/rate*Math.sin(p.heading*RAD)};
    });
    const x=centers.reduce((s,p)=>s+p.x,0)/centers.length;
    const y=centers.reduce((s,p)=>s+p.y,0)/centers.length;
    const radii=pilots.map(p=>Math.hypot(p.x-x,p.y-y));
    return {x,y,radius:clamp(radii.reduce((a,b)=>a+b,0)/radii.length,24,48),
      outerRadius:Math.max(...radii)+c.entryOutsideMarginM};
  }

  function createArrivingFriend(user, partner, seed, c = harderConfig) {
    const orbit=estimateOrbit([user,partner],c), rng=random(seed ^ 0x3279);
    const bearing=rng()*Math.PI*2, radius=orbit.outerRadius+c.entryLeadInM;
    const entry={...createFriend(user,user.vario,c), id:'arrival', color:'arrival', phase:'approach',
      x:orbit.x+Math.sin(bearing)*radius,y:orbit.y+Math.cos(bearing)*radius,
      z:user.z+(c.entryLeadInM/c.airspeedMps+6)*(Math.max(0,user.vario)+c.sinkMps),
      heading:(bearing/RAD+180)%360,turnRate:0,targetTurnRate:0,
      holdSeconds:0,orbit, targetRadius:orbit.outerRadius,joined:false};
    return entry;
  }

  function advanceArrivingFriend(pilot, others, dt, c = harderConfig) {
    if (!(dt>0)) return;
    const estimate=estimateOrbit(others,c);
    for(const key of ['x','y','radius','outerRadius']) pilot.orbit[key]=smooth(pilot.orbit[key],estimate[key],dt,2);
    const orbit=pilot.orbit, dx=pilot.x-orbit.x,dy=pilot.y-orbit.y;
    const radius=Math.max(.01,Math.hypot(dx,dy)), bearing=Math.atan2(dx,dy);
    const outer=Math.max(orbit.outerRadius,...others.map(p=>Math.hypot(p.x-orbit.x,p.y-orbit.y)+c.entryOutsideMarginM));
    const angleGap=p=>Math.abs(((Math.atan2(p.x-orbit.x,p.y-orbit.y)-bearing)/RAD+540)%360-180);
    const gapClear=others.every(p=>angleGap(p)>=c.entryGapDeg);
    if(pilot.phase==='approach' && radius<=outer+8) pilot.phase='holding';
    if(pilot.phase==='holding') {
      pilot.holdSeconds+=dt;
      if(pilot.holdSeconds>=3 && gapClear) pilot.phase='joining';
    }
    // Merge gradually. Re-open the orbit if the adjacent gap closes.
    if(pilot.phase==='joining' && others.some(p=>angleGap(p)<25)) {pilot.phase='holding';pilot.holdSeconds=0;}
    const desiredRadius=['joining','joined'].includes(pilot.phase)?orbit.radius:outer;
    pilot.targetRadius=approach(pilot.targetRadius,desiredRadius,6*dt);
    const radial=clamp((pilot.targetRadius-radius)*.35,-c.airspeedMps*.85,c.airspeedMps*.85);
    let vx=c.turnDirection*Math.cos(bearing)*c.airspeedMps+Math.sin(bearing)*radial;
    let vy=-c.turnDirection*Math.sin(bearing)*c.airspeedMps+Math.cos(bearing)*radial;
    // Predict closest approach with each pilot. An unsafe merge yields outward.
    pilot.avoiding=false;
    for(const other of others) {
      const rx=pilot.x-other.x,ry=pilot.y-other.y,rz=pilot.z-other.z;
      const rvx=Math.sin(pilot.heading*RAD)*c.airspeedMps-Math.sin(other.heading*RAD)*c.airspeedMps;
      const rvy=Math.cos(pilot.heading*RAD)*c.airspeedMps-Math.cos(other.heading*RAD)*c.airspeedMps;
      const rvz=(pilot.vario||0)-(other.vario||0);
      const t=clamp(-(rx*rvx+ry*rvy+rz*rvz)/Math.max(.01,rvx*rvx+rvy*rvy+rvz*rvz),0,5);
      const closest=Math.hypot(rx+rvx*t,ry+rvy*t,rz+rvz*t);
      if(closest<c.trafficClearanceM+10) {
        pilot.avoiding=true;
        vx+=Math.sin(bearing)*c.airspeedMps*2; vy+=Math.cos(bearing)*c.airspeedMps*2;
        if(!pilot.joined) {pilot.phase='holding';pilot.holdSeconds=0;pilot.targetRadius=outer;}
      }
    }
    const desiredHeading=Math.atan2(vx,vy)/RAD;
    const error=((desiredHeading-pilot.heading+540)%360)-180;
    pilot.targetTurnRate=clamp(c.turnDirection*c.airspeedMps/radius/RAD+error*1.5,-30,30);
    pilot.turnRate=approach(pilot.turnRate,smooth(pilot.turnRate,pilot.targetTurnRate,dt,.35),c.turnSlewDps2*dt);
    advancePilot(pilot,dt,c);
    pilot.bankDeg=bankAngle(pilot.turnRate,c);
    if(pilot.phase==='joining' && !pilot.avoiding && Math.abs(radius-orbit.radius)<5 &&
      others.every(p=>Math.hypot(p.x-pilot.x,p.y-pilot.y,p.z-pilot.z)>=c.trafficClearanceM+5)) {
      pilot.phase='joined';pilot.joined=true;
    }
  }

  root.FriendTraining = { config, harderConfig, createWeather, advanceWeather, turnSink, createFriend, steer, advancePilot, separationObservation, advanceGuardedFriend, forecastPilot, bankAngle, viewObservation, panoramaCrop, elevationScreenFraction, wingtipPose, wingtipVisibility, spriteLeft, spriteSizeFraction, thermalLiftShape, updateClimbGoal, relativeAltitudeLabel, estimateOrbit, createArrivingFriend, advanceArrivingFriend };
})(globalThis);
