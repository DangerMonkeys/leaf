import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import '../public/labs/thermal-training/friend-training.js';
import '../public/labs/thermal-training/friend-colors.js';

const F = globalThis.FriendTraining, C = F.config, dt = 1 / 60;
const radius = C.airspeedMps / (C.idealTurnDps * Math.PI / 180);
const user = () => ({x: 0, y: radius, z: 1000, heading: 270, turnRate: -20});
const near = (a, b, tolerance = 1e-7) => assert.ok(Math.abs(a - b) < tolerance, `${a} != ${b}`);

test('starts exactly opposite, same radius, altitude and ideal rate; exact integration preserves circle', () => {
  const u = user(), f = F.createFriend(u, 3);
  near(f.x, 0); near(f.y, -radius); near(f.z, u.z); near(f.turnRate, -20);
  near(f.heading, 90); near(f.bankDeg, -19.59, 0.02);
  for (let i = 0; i < 1080; i++) { F.advancePilot(u, dt); F.advancePilot(f, dt); }
  near(Math.hypot(u.x, u.y), radius); near(Math.hypot(f.x, f.y), radius);
  near(u.x, -f.x); near(u.y, -f.y);
});

test('NPC forecast follows the exact current-rate arc, starts ahead, and never changes the pilot', () => {
  for(const turnRate of [-28,-20,0,20,28]) for(const heading of [0,90,359]) {
    const pilot={x:15,y:-20,z:1000,heading,turnRate};
    const before={...pilot}, points=F.forecastPilot(pilot);
    assert.deepEqual(pilot,before);
    const first={...pilot},last={...pilot};
    F.advancePilot(first,C.topViewForecastStartSeconds);
    F.advancePilot(last,C.topViewForecastSeconds);
    near(points[0].x,first.x); near(points[0].y,first.y);
    near(points.at(-1).x,last.x); near(points.at(-1).y,last.y);
    assert.ok(points.length>40);
    assert.ok(points.every(p=>Number.isFinite(p.x)&&Number.isFinite(p.y)));
  }
});

test('NPCs use traced skins with no forecast, while the player keeps its turn preview', () => {
  const html=fs.readFileSync(new URL('../public/labs/thermal-training/index.html',import.meta.url),'utf8');
  const icon=html.match(/function drawParaglider\([^]*?\n}/)[0];
  assert.ok(icon.includes('ctx.drawImage(skin'));
  assert.ok(icon.includes("canopy === 'arrival' ? arrivingCanopySkin : friendCanopySkin"));
  assert.ok(!icon.includes('#ff7900') && !icon.includes('#ef6c00'));
  const skin=fs.readFileSync(new URL('../public/labs/thermal-training/canopy-pattern-uv.svg',import.meta.url),'utf8');
  for(const color of ['#ffffff','#171919','#c4ef00']) assert.ok(skin.includes(color));
  const draw=html.match(/function drawMainMap\([^]*?\n}/)[0];
  assert.ok(!draw.includes('drawFriendTurnForecast'));
  assert.ok(draw.includes('drawTurnPreview(p.x, p.y, state.heading'));
  assert.ok(html.includes("createFriendRenderer(FriendTraining.config, 'orange-red')"));
  const arrival=F.createArrivingFriend(user(),F.createFriend(user(),4),42);
  assert.equal(arrival.color,'arrival');
  assert.equal(F.createFriend(user(),4).color,'friend');
});

test('orange-red palette preserves white, black, skin, blue and alpha plus fabric shading', () => {
  const pixels=new Uint8ClampedArray([196,239,0,255,98,120,0,128,255,255,255,255,
    23,25,25,255,0,0,0,0,200,140,100,255,50,100,150,255]);
  const before=pixels.slice();
  FriendColors.orangeRedPixels(pixels);
  assert.deepEqual([...pixels.slice(0,4)],[255,79,42,255]);
  assert.deepEqual([...pixels.slice(4,8)],[128,40,21,128]);
  assert.deepEqual(pixels.slice(8),before.slice(8));
  const renderer=fs.readFileSync(new URL('../public/labs/thermal-training/friend-renderer.js',import.meta.url),'utf8');
  assert.ok(renderer.includes('new THREE.Source(FriendColors.orangeRedCanvas(source.image))'));
  assert.ok(renderer.includes('styledMaterials.has(material)'));
});

test('player leading arc uses standard turn colors when lesson turn-rate bands are enabled', () => {
  const html=fs.readFileSync(new URL('../public/labs/thermal-training/index.html',import.meta.url),'utf8');
  const draw=html.match(/function drawTurnPreview\([^]*?\n}/)[0];
  const calls=[];
  const scope=vm.createContext({tutorial:{active:true,showTargetMarkers:false,step:0},
    tutorialSteps:[{showTurnRateBands:true}],baseAirspeed:10,
    turnRateGlowColor:(rate,alpha)=>{calls.push([rate,alpha]);return {active:true,color:'test-color'};},
    ctx:new Proxy({}, {get:()=>()=>{}})});
  vm.runInContext(draw,scope);
  for(const rate of [-30,-20,0,20,30]) scope.drawTurnPreview(0,0,0,rate,2);
  assert.deepEqual(calls,[[-30,1],[-20,1],[0,1],[20,1],[30,1]]);
  scope.tutorialSteps[0].showTurnRateBands=false;
  scope.drawTurnPreview(0,0,0,20,2); assert.equal(calls.length,5);
});

test('weather stays inside speed, turn and strength limits for 20 minutes', () => {
  const w = F.createWeather(42), twin = F.createWeather(42);
  near(w.bearingDeg, 0); near(w.speedMps, 0); near(w.strengthFactor, 1);
  let min = 1, max = 1;
  for (let i = 0; i < 60 * 1200; i++) {
    const previous = {...w};
    F.advanceWeather(w, dt); F.advanceWeather(twin, dt);
    assert.ok(Math.abs(w.bearingDeg - previous.bearingDeg) <= 10 * dt + 1e-10);
    assert.ok(Math.abs(w.strengthFactor - previous.strengthFactor) <= 0.02 * dt + 1e-10);
    assert.ok(w.strengthFactor >= .75 && w.strengthFactor <= 1.25);
    assert.ok(w.speedMps <= .44704);
    if (i === 299) near(w.speedMps, .44704 / 2);
    if (i === 599) near(w.speedMps, .44704);
    min = Math.min(min, w.strengthFactor); max = Math.max(max, w.strengthFactor);
  }
  near(w.x, twin.x); near(w.y, twin.y); near(w.strengthFactor, twin.strengthFactor);
  assert.ok(max - min > .3);
  const stopped = {...w};
  F.advanceWeather(w, 0);
  assert.deepEqual(w, stopped);
});

test('real lesson integration: reset, pause, both pilots update once, and other stages keep legacy weather', () => {
  const html = fs.readFileSync(new URL('../public/labs/thermal-training/index.html', import.meta.url), 'utf8');
  const lesson=html.match(/successType: "thermalFriendsTogether",[^]*?stageComplete:/)[0];
  assert.match(lesson,/showThermal: false/);
  assert.match(lesson,/showRings: false/);
  assert.match(html,/completeTitle: "Working together complete",\s*showTargetMarkers: false,\s*showTurnRateBands: true/);
  assert.match(html,/!tutorial\.showTargetMarkers && !tutorialSteps\[tutorial\.step\]\?\.showTurnRateBands/);
  const names = ['clamp','lerp','seededRandom','smoothWave','readParams','makeThermal','freshState',
    'windVector','thermalWindDriftRatio','thermalCenterAt','thermalProfileAt','liftAt',
    'setupFriendTogetherScenario','updateFriendTogetherScenario','updatePhysics'];
  const functions = names.map(name => {
    const match = html.match(new RegExp('function '+name+'\\([^]*?\\n}', 'm'));
    assert.ok(match, name);
    return match[0];
  }).join('\n');
  const values = {seed:12345,duration:480,wind:0,diameter:140,diameterVariance:0,strength:6,strengthVariance:0,texture:0,sink:1,volume:0};
  const ui = Object.fromEntries(Object.entries(values).map(([key,value])=>[key,{value}]));
  ui.showThermal={checked:false};
  const context = vm.createContext({FriendTraining:F,ui,console});
  vm.runInContext(`
    const mphToMps=.44704, baseAirspeed=10, nominalTargetTurnRateDps=20, maxTurnRateDps=30;
    const maxTurnRateSinkMps=.5, maxSpiralSinkMps=7;
    let activeTargetTurnRateDps=20;
    const tutorial={active:true,showTargetMarkers:false};
    const keys=new Set(), steeringHoldSeconds={left:0,right:0};
    let state;
    function requestFriendModel(){}
    function requestArrivingModel(){}
    function updateAudio(){}
    function updateTurnDirectionBankCue(){}
    function applyTurnDirectionPull(){}
    function sampleNow(){}
    function updateTutorial(){}
    function finishTrial(){}
    ${functions}
    setupFriendTogetherScenario();
    globalThis.snapshot=()=>({t:state.t,x:state.x,y:state.y,z:state.z,rate:state.turnRate,vario:state.vario,
      weather:state.thermal.friendWeather ? {...state.thermal.friendWeather,random:null}:null,
      friend:{...tutorial.friendGliders[0]}});
    globalThis.tick=updatePhysics;
    globalThis.run=()=>{state.running=true;};
    globalThis.pause=()=>{state.running=false;};
    globalThis.reset=setupFriendTogetherScenario;
    globalThis.bankSinkProbe=(rate)=>{
      setupFriendTogetherScenario(); state.running=true; state.turnRate=rate;
      tutorial.friendGliders[0].turnRate=rate;
      updatePhysics(1/60);
      return {playerVario:state.vario,playerLift:state.lift,turnSink:state.turnRateSink,
        spiralSink:state.spiralSink,friendVario:tutorial.friendGliders[0].vario,friendLift:tutorial.friendTogetherLift};
    };
    globalThis.legacy=()=>{ui.strength.value=6;state=freshState(12345);return thermalProfileAt(0,1000,readParams());};
    globalThis.hardReset=()=>setupFriendTogetherScenario(true,1500);
    globalThis.hardSnapshot=()=>({startAlt:state.startAlt,wind:readParams().windMph,
      windDir:state.thermal.windDir,settings:state.thermal.friendConfig,
      count:tutorial.friendGliders.length,spawned:tutorial.arrivalSpawned,
      center:thermalCenterAt(10,state.z,readParams()),windVector:windVector(readParams()),
      wander:{x:state.thermal.friendWeather.x,y:state.thermal.friendWeather.y}});
    globalThis.forceGain=(gain)=>{state.z=state.startAlt+gain;updateFriendTogetherScenario(readParams(),1/60);};
    globalThis.setDirection=(direction)=>{tutorial.friendTurnDirection=direction;setupFriendTogetherScenario();};
  `,context);
  const initial = context.snapshot();
  near(initial.y, radius); near(initial.friend.y,-radius); near(initial.rate,-20);
  near(initial.vario, initial.friend.vario); near(initial.vario, 4.5676,.001);
  near(ui.strength.max,6);
  context.tick(dt); assert.deepEqual(context.snapshot(),initial);
  context.run();
  for(let i=0;i<600;i++) context.tick(dt);
  const flight=context.snapshot(); near(flight.t,10); near(flight.weather.elapsed,10);
  assert.notEqual(flight.friend.x, initial.friend.x);
  context.pause(); context.tick(dt); assert.deepEqual(context.snapshot(),flight);
  context.reset(); assert.deepEqual(context.snapshot(),initial);
  for(const rate of [-30,-28,-20,0,20,28,30]) {
    const probe=context.bankSinkProbe(rate);
    near(probe.turnSink,0); near(probe.spiralSink,0);
    near(probe.playerVario,probe.playerLift-C.sinkMps);
    near(probe.friendVario,probe.friendLift-C.sinkMps);
  }
  const legacy=context.legacy(); near(legacy.radius,70); near(legacy.strength,7);
  assert.equal(context.snapshot().weather,null);
  context.hardReset();
  const hard=context.hardSnapshot();
  near(hard.startAlt,1500); near(hard.wind,6); assert.equal(hard.count,1);
  near(Math.hypot(hard.windVector.x,hard.windVector.y),6*.44704);
  near(hard.center.x-hard.wander.x,hard.windVector.x*10*6/7);
  near(hard.center.y-hard.wander.y,hard.windVector.y*10*6/7);
  context.forceGain(199.99); assert.equal(context.hardSnapshot().count,1);
  context.forceGain(200); assert.equal(context.hardSnapshot().count,2);
  context.forceGain(201); assert.equal(context.hardSnapshot().count,2);
  context.hardReset(); assert.equal(context.hardSnapshot().count,1);
  near(context.hardSnapshot().windDir,hard.windDir);
  context.reset(); near(context.hardSnapshot().wind,0);
  for(const direction of [-1,1]) {
    context.setDirection(direction);
    const s=context.snapshot();
    near(s.rate,direction*20);near(s.friend.turnRate,direction*20);
    near(s.friend.heading,(180+direction*90+360)%360);
    near(context.hardSnapshot().settings.turnDirection,direction);
  }
});

test('harder weather doubles spatial wandering, strength range and rate; wind advects all pilots', () => {
  const c=F.harderConfig,w=F.createWeather(314,c),easy=F.createWeather(314);
  near(c.driftMaxMph,2); near(c.strengthRangeFraction,.50); near(c.strengthRateMaxFractionPerS,.04);
  for(let i=0;i<60*600;i++) {
    const before=w.strengthFactor;
    F.advanceWeather(w,dt);F.advanceWeather(easy,dt);
    assert.ok(w.strengthFactor>=.50 && w.strengthFactor<=1.50);
    assert.ok(Math.abs(w.strengthFactor-before)<=.04*dt+1e-10);
  }
  near(w.x,easy.x*2,1e-7); near(w.y,easy.y*2,1e-7);
  for(const rate of [-28,0,20]) {
    const calm={...user(),turnRate:rate},windy={...calm,windX:2,windY:-1};
    F.advancePilot(calm,3); F.advancePilot(windy,3);
    near(windy.x-calm.x,6);near(windy.y-calm.y,-3);
  }
});

test('step 3 continues from the completed altitude, restarts at its baseline and has direct navigation', () => {
  const html=fs.readFileSync(new URL('../public/labs/thermal-training/index.html',import.meta.url),'utf8');
  const calls=[],state={z:1500.1,startAlt:1000,t:115,thermal:{windDir:0,slantDir:.1}},tutorial={step:0,phase:'step-complete'};
  const lesson={successType:'thermalFriendsTogether',harderConditions:true,windMph:6,goal:'Goal: climb'};
  const ui={wind:{},showThermal:{},duration:{},start:{}};
  const scope=vm.createContext({state,tutorial,ui,tutorialSteps:[{},lesson],
    resetTutorialOrbitProgress:()=>{},resetFriendThermalState:()=>{},applyThermalPreset:()=>{},
    setupFriendTogetherScenario:(hard,alt)=>{calls.push([hard,alt]);state.z=state.startAlt=alt;state.t=0;},
    updateOutputs:()=>{},setTutorialText:()=>{},tutorialStartPrompt:()=>''});
  vm.runInContext(html.match(/function configureTutorialStep\([^]*?\n}/)[0],scope);
  scope.configureTutorialStep(1);assert.deepEqual(calls[0],[true,1500.1]);
  tutorial.phase='friend-goal-failed';state.z=1650;
  scope.configureTutorialStep(1);assert.deepEqual(calls[1],[true,1500.1]);
  tutorial.step=0;tutorial.phase='practice';
  scope.configureTutorialStep(1);assert.deepEqual(calls[2],[true,1000]);
  assert.match(html,/lesson'\) === 'harder-conditions'/);
  assert.match(html,/roadmapLabel: "3\. Harder conditions"/);
});

test('newcomer approaches outside, holds for a gap, joins continuously and stays clear in either turn direction', () => {
 for(const direction of [-1,1]) {
  const c={...F.harderConfig,turnDirection:direction};
  for(const seed of [1,42,12345,8743981]) {
    const u={...user(),heading:(direction*90+360)%360,turnRate:direction*20,vario:4,windX:1,windY:2},f={...F.createFriend(u,4,c),windX:1,windY:2};
    const p=F.createArrivingFriend(u,f,seed,c),same=F.createArrivingFriend(u,f,seed,c);
    near(p.x,same.x);near(p.y,same.y);assert.equal(p.phase,'approach');
    p.windX=1;p.windY=2;
    let held=false,merged=false;
    for(let i=0;i<60*100;i++) {
      F.advancePilot(u,dt);F.advancePilot(f,dt);u.z+=4*dt;f.z+=4*dt;
      const before={...p}; F.advanceArrivingFriend(p,[u,f],dt,c);
      assert.ok(Math.hypot(p.x-before.x,p.y-before.y)<13*dt);
      const center={x:(i+1)*dt,y:2*(i+1)*dt};
      p.vario=7*F.thermalLiftShape(Math.hypot(p.x-center.x,p.y-center.y)/70)-1;p.z+=p.vario*dt;
      if(p.phase==='holding') {held=true; assert.ok(Math.hypot(p.x-center.x,p.y-center.y)>radius+15);}
      if(p.joined)merged=true;
      for(const other of [u,f]) assert.ok(Math.hypot(p.x-other.x,p.y-other.y,p.z-other.z)>=c.trafficClearanceM);
    }
    assert.ok(held && merged,`Seed ${seed} did not hold then merge (${p.phase})`);
  }
 }
});

test('stage direction persists across continuation/retries; entry restart, fresh or direct entry can reroll; each windy step rerolls wind', () => {
  const html=fs.readFileSync(new URL('../public/labs/thermal-training/index.html',import.meta.url),'utf8');
  const lessons=[{successType:'other'}, {successType:'thermalFriends'},
    {successType:'thermalFriendsTogether'}, {successType:'thermalFriendsTogether',harderConditions:true,windMph:6},
    {successType:'wind',windMph:10},{successType:'wind',windMph:10}].map(l=>({windMph:0,goal:'Goal',...l}));
  let randomValue=.1,randomCalls=0;
  const math=Object.create(Math);math.random=()=>{randomCalls++;return randomValue;};
  const fresh=()=>({z:1000,startAlt:1000,t:0,thermal:{windDir:1,slantDir:1.2}});
  const scope=vm.createContext({Math:math,state:fresh(),tutorial:{step:0,phase:'practice',friendTurnDirection:null},
    tutorialSteps:lessons,ui:{wind:{value:0},showThermal:{},duration:{},start:{},seed:{value:42}},
    resetTutorialOrbitProgress:()=>{},resetFriendThermalState:()=>{},applyThermalPreset:()=>{},
    setupFriendThermalScenario:()=>{scope.state=fresh();},
    setupFriendTogetherScenario:()=>{scope.state=fresh();},freshState:fresh,
    updateOutputs:()=>{},setTutorialText:()=>{},tutorialStartPrompt:()=>''});
  vm.runInContext(html.match(/function configureTutorialStep\([^]*?\n}/)[0],scope);
  scope.configureTutorialStep(1);assert.equal(scope.tutorial.friendTurnDirection,-1);assert.equal(randomCalls,1);
  randomValue=.9;
  for(const step of [2,2,2]) {scope.configureTutorialStep(step);assert.equal(scope.tutorial.friendTurnDirection,-1);}
  assert.equal(randomCalls,1);
  scope.configureTutorialStep(3);near(scope.state.thermal.windDir,.9*Math.PI*2);assert.equal(randomCalls,2);
  randomValue=.3;scope.configureTutorialStep(3);
  assert.equal(scope.tutorial.friendTurnDirection,-1);near(scope.state.thermal.windDir,.3*Math.PI*2);
  randomValue=.9;scope.configureTutorialStep(1);assert.equal(scope.tutorial.friendTurnDirection,1);
  scope.configureTutorialStep(2);scope.configureTutorialStep(3);assert.equal(scope.tutorial.friendTurnDirection,1);
  scope.configureTutorialStep(0);assert.equal(scope.tutorial.friendTurnDirection,null);
  randomValue=.2;scope.configureTutorialStep(3);assert.equal(scope.tutorial.friendTurnDirection,-1);
  // A refreshed page has no session direction; direct Step 2 starts independently.
  scope.tutorial.friendTurnDirection=null;randomValue=.8;scope.configureTutorialStep(2);
  assert.equal(scope.tutorial.friendTurnDirection,1);
  for(const [step,random] of [[4,.15],[4,.75],[5,.4],[3,.6]]) {
    randomValue=random;scope.configureTutorialStep(step);
    near(scope.state.thermal.windDir,random*Math.PI*2);
    near(scope.state.thermal.slantDir-scope.state.thermal.windDir,.2);
  }
});

test('Thermal Entry mirrors approach, headings and NPC orbit spacing with the stage direction', () => {
  const html=fs.readFileSync(new URL('../public/labs/thermal-training/index.html',import.meta.url),'utf8');
  const functions=['setupFriendThermalScenario','updateFriendGliders'].map(name=>
    html.match(new RegExp('function '+name+'\\([^]*?\\n}'))[0]).join('\n');
  const flights=[];
  for(const direction of [-1,1]) {
    const tutorial={friendTurnDirection:direction,friendJoinedAt:null,friendSuccessfulOrbits:0};
    const scope=vm.createContext({tutorial,ui:{seed:{value:42}},baseAirspeed:10,nominalTargetTurnRateDps:20,
      friendInitialSeparationDeg:100,friendSeparationGrowthOrbits:3,friendSeparationPerOrbitDeg:10,friendSeparationCorrectionRateDps:2,
      clamp:(n,a,b)=>Math.max(a,Math.min(b,n)),angleDeltaDeg:(a,b)=>(b-a+540)%360-180,
      freshState:()=>({startAlt:1000,z:1000,t:0}),readParams:()=>({}),thermalCenterAt:()=>({x:0,y:0})});
    vm.runInContext(functions,scope);scope.setupFriendThermalScenario();
    near(scope.state.x,-direction*95);near(scope.state.y,radius);
    for(let i=0;i<600;i++) {scope.state.t+=dt;scope.updateFriendGliders({},dt);}
    for(const glider of tutorial.friendGliders) {
      assert.equal(Math.sign(glider.turnRate),direction);
      near((glider.heading-glider.angle+360)%360,(direction*90+360)%360);
    }
    flights.push(tutorial.friendGliders);
  }
  for(let i=0;i<2;i++) {near(flights[0][i].x,-flights[1][i].x);near(flights[0][i].y,flights[1][i].y);}
  const side=html.match(/function drawFriendTogetherSideView\([^]*?\n}/)[0];
  assert.ok(side.includes('state.thermal?.friendConfig || FriendTraining.config'));
  assert.ok(side.includes('viewObservation(friend, state, viewConfig)'));
  assert.ok(side.includes('Math.sign(viewConfig.turnDirection)'));
});

test('three-pilot goal drops phase/height tests but measures full 3D separation from either NPC', () => {
  const c=F.harderConfig,u={x:0,y:30,z:1250,vario:4},f={x:0,y:30,z:1290};
  const p={x:30,y:0,z:1250},g={baseZ:1000,gainM:249,valid:true};
  const center={x:0,y:0};
  assert.ok(F.updateClimbGoal(g,u,f,center,c).failed);
  assert.ok(!F.updateClimbGoal(g,u,f,center,c,[f,p]).failed);
  for(const offset of [[19.99,0,0],[0,19.99,0],[0,0,19.99],[11,11,11]]) {
    const nearPilot={x:u.x+offset[0],y:u.y+offset[1],z:u.z+offset[2]};
    for(const traffic of [[f,nearPilot],[nearPilot,p]]) {
      const result=F.updateClimbGoal(g,u,traffic[0],center,c,traffic);
      assert.ok(result.failed);assert.equal(result.reason,"Don't get too close to another glider.");near(result.gainM,249);
    }
  }
  assert.ok(!F.updateClimbGoal(g,u,f,center,c,[f,{...u,z:u.z+20}]).failed);
  assert.ok(F.updateClimbGoal(g,{...u,vario:0},f,center,c,[f,p]).failed);
  assert.ok(F.updateClimbGoal(g,{...u,z:1500},f,center,c,[f,p]).complete);
});

test('multiple side-view pilots share one head direction instead of recentering independently', () => {
  const u={...user(),heading:0},a={...F.createFriend(u,4),x:-50,y:10},b={...a,x:-50,y:-10};
  const first=F.viewObservation(a,u),second=F.viewObservation(b,u,C,first.headYawDeg);
  near(second.headYawDeg,first.headYawDeg);assert.notEqual(second.screenXFraction,first.screenXFraction);
});

test('work-together disables bank sink while leaving the legacy penalty branch intact', () => {
  near(C.turnSinkMaxMps,0); near(C.sinkMps,1);
  for(const rate of [-60,-30,-25,-20,0,20,25,30,60]) near(F.turnSink(rate),0);
  const html=fs.readFileSync(new URL('../public/labs/thermal-training/index.html',import.meta.url),'utf8');
  assert.match(html,/state\.turnRateSink = state\.thermal\.friendWeather\s*\? FriendTraining\.turnSink\(state\.turnRate\)\s*: maxTurnRateSinkMps \* turnRatePenalty/);
});

test('500m climb thresholds stop and latch the attempt without clearing earned meters', () => {
  const center={x:12,y:-8}, u={x:12,y:22,z:1000,vario:4};
  const at=(deg,z=1000)=>({x:12+30*Math.sin(deg*Math.PI/180),y:-8+30*Math.cos(deg*Math.PI/180),z});
  const initial={baseZ:1000,gainM:0,valid:true};
  for(const angle of [40,79,80,180,280,281,320]) for(const dz of [-25,0,25]) {
    assert.ok(F.updateClimbGoal(initial,u,at(angle,1000+dz),center).valid);
  }
  for(const angle of [0,10,350,359]) assert.ok(!F.updateClimbGoal(initial,u,at(angle),center).valid);
  for(const dz of [-25.01,25.01]) assert.ok(!F.updateClimbGoal(initial,u,at(180,1000+dz),center).valid);
  near(F.updateClimbGoal(initial,{...u,z:1499},at(180,1499),center).gainM,499);
  assert.ok(!F.updateClimbGoal(initial,{...u,z:1499},at(180,1499),center).complete);
  assert.ok(F.updateClimbGoal(initial,{...u,z:1500},at(180,1500),center).complete);
  for(const [vario,angle,dz] of [[0,180,0],[-1,180,0],[4,0,0],[4,180,26]]) {
    const broken=F.updateClimbGoal({...initial,gainM:489},{...u,z:1490,vario},at(angle,1490+dz),center);
    near(broken.gainM,489); assert.ok(broken.failed && !broken.complete);
    const recovered=F.updateClimbGoal(broken,{...u,z:1510},at(180,1510),center);
    assert.equal(recovered,broken);
    near(F.updateClimbGoal(recovered,{...u,z:1520},at(180,1520),center).gainM,489);
  }
  assert.ok(F.updateClimbGoal(initial,{...u,x:center.x,y:center.y},at(180),center).valid);
  assert.equal(F.relativeAltitudeLabel(1012,1000),'Friend: +12 m');
  assert.equal(F.relativeAltitudeLabel(988,1000),'Friend: -12 m');
  assert.equal(F.relativeAltitudeLabel(999.9,1000),'Friend: 0 m');
});

test('angular position no longer fails the goal, while above/below and lift limits remain', () => {
  const center={x:70,y:-40};
  for(const direction of [-1,1]) for(const bearing of [0,179,359]) {
    const f={x:center.x+30*Math.sin(bearing*Math.PI/180),y:center.y+30*Math.cos(bearing*Math.PI/180),z:1000};
    for(const gap of [79,281]) {
      const b=(bearing+direction*gap)*Math.PI/180;
      const u={x:center.x+30*Math.sin(b),y:center.y+30*Math.cos(b),z:1000,vario:4};
      assert.equal(F.updateClimbGoal(null,u,f,center,{...C,turnDirection:direction}).reason,null);
    }
  }
  const u={x:0,y:30,z:1000,vario:4},f={x:0,y:-30,z:1000},origin={x:0,y:0};
  assert.match(F.updateClimbGoal(null,{...u,z:1026},f,origin).reason,/above/);
  assert.match(F.updateClimbGoal(null,{...u,z:974},f,origin).reason,/below/);
  assert.match(F.updateClimbGoal(null,{...u,vario:0},f,origin).reason,/lift/);
});

test('20m 3D clearance applies in both steps, regardless of thermal drift or arrival phase', () => {
  const u={x:30,y:0,z:1200,vario:4},f={x:-30,y:0,z:1200};
  for(const c of [C,F.harderConfig]) {
    near(c.trafficClearanceM,20);
    for(const center of [{x:0,y:0},{x:0,y:-40},{x:30,y:0},{x:1000,y:-1000}]) {
      assert.equal(F.updateClimbGoal(null,u,f,center,c).reason,null);
      for(const offset of [[19.99,0,0],[0,19.99,0],[0,0,19.99],[11,11,11]]) {
        const p={x:u.x+offset[0],y:u.y+offset[1],z:u.z+offset[2]};
        assert.equal(F.updateClimbGoal(null,u,p,center,c).reason,"Don't get too close to another glider.");
        // An approaching second NPC is checked before the vertical limit is lifted.
        assert.equal(F.updateClimbGoal(null,u,f,center,c,[f,p],true).reason,"Don't get too close to another glider.");
      }
      for(const offset of [[20,0,0],[0,20,0],[0,0,20],[12,0,16]]) {
        const p={x:u.x+offset[0],y:u.y+offset[1],z:u.z+offset[2]};
        assert.equal(F.updateClimbGoal(null,u,p,center,c).reason,null);
      }
      const highFriend={...f,z:u.z+26};
      assert.match(F.updateClimbGoal(null,u,highFriend,center,c,[highFriend],true).reason,/below/);
      assert.equal(F.updateClimbGoal(null,u,highFriend,center,c,[highFriend],false).reason,null);
    }
  }
});

test('real lesson completes at 500m, not three orbits, and pauses without accumulating', () => {
  const html=fs.readFileSync(new URL('../public/labs/thermal-training/index.html',import.meta.url),'utf8');
  const funcs=['updateTutorial','tutorialGoalText'].map(name=>html.match(new RegExp('function '+name+'\\([^]*?\\n}'))[0]).join('\n');
  const state={x:0,y:30,z:1499,vario:4,t:100,running:true,varioTrace:[]};
  const friend={x:0,y:-30,z:1499};
  const tutorial={active:true,phase:'practice',step:0,orbitCount:99,friendGliders:[friend],
    friendClimbGoal:{baseZ:1000,gainM:499,valid:true},peakVario:4};
  const ui={tutorialGoal:{},start:{}};
  const messages=[];
  const scope=vm.createContext({FriendTraining:F,state,tutorial,ui,
    tutorialSteps:[{successType:'thermalFriendsTogether',completeTitle:'Complete',complete:'Climbed 500m'}],
    readParams:()=>({}),thermalCenterAt:()=>({x:0,y:0}),
    setTutorialText:(...args)=>messages.push(args),tutorialActionPrompt:action=>action});
  vm.runInContext(funcs,scope);
  scope.updateTutorial(dt); assert.equal(tutorial.phase,'practice');
  assert.match(ui.tutorialGoal.textContent,/499\/500 m/);
  state.z=1500; friend.z=1500; state.running=false;
  scope.updateTutorial(dt); assert.equal(tutorial.phase,'practice');
  state.running=true; scope.updateTutorial(dt);
  assert.equal(tutorial.phase,'step-complete'); assert.equal(state.running,false);
  assert.equal(messages[0][1],'Climbed 500m');
  assert.ok(tutorial.pauseForCallout);
  assert.match(html,/tutorial\.friendClimbGoal = \{baseZ: state\.z, gainM: 0/);
  // A failed attempt stops with explanatory text and preserves its last good gain.
  tutorial.phase='practice'; tutorial.pauseForCallout=false;
  tutorial.friendClimbGoal={baseZ:1000,gainM:321,valid:true};
  state.z=1322; friend.z=1290; state.running=true;
  scope.updateTutorial(dt);
  assert.equal(tutorial.phase,'friend-goal-failed');
  assert.equal(state.running,false); assert.ok(tutorial.pauseForCallout);
  near(tutorial.friendClimbGoal.gainM,321);
  assert.match(messages.at(-1)[1],/above your friend.*321 m/);
  scope.updateTutorial(dt); near(tutorial.friendClimbGoal.gainM,321);
  // The real restart path resets the attempt, then begins practice again.
  let restarts=0;
  scope.configureTutorialStep=()=>{restarts++; tutorial.phase='ready-practice';
    tutorial.friendClimbGoal={baseZ:1000,gainM:0,valid:true};};
  scope.resetTutorialOrbitProgress=()=>{};
  vm.runInContext(html.match(/function continueTutorial\([^]*?\n}/)[0],scope);
  scope.continueTutorial();
  assert.equal(restarts,1); assert.equal(tutorial.phase,'practice');
  assert.equal(state.running,true); near(tutorial.friendClimbGoal.gainM,0);
  const overlay=html.match(/function updateTutorialOverlayCards\([^]*?\n}/)[0];
  assert.ok(overlay.includes('"friend-goal-failed"'));
});

test('friend thermal has a 6 m/s core, half the interior slope, and a smooth steep edge', () => {
  const air=C.initialCoreClimbMps+C.sinkMps, radius=C.diameterM/2;
  near(C.initialCoreClimbMps,6);
  const lift=r=>air*F.thermalLiftShape(r/radius);
  near(lift(0)-C.sinkMps,6);
  for(const r of [5,14,28,40,48]) near((lift(r+.01)-lift(r))/.01,-.05);
  near(lift(49)-C.sinkMps,3.55);
  near(lift(70),0); near(lift(90),0);
  for(let r=.1;r<=80;r+=.1) assert.ok(lift(r)<=lift(r-.1)+1e-10 && lift(r)>=0);
  const h=.0001;
  near((lift(49)-lift(49-h))/h,(lift(49+h)-lift(49))/h,1e-5);
  near((lift(70)-lift(70-h))/h,0,1e-5);
  assert.ok((lift(61)-lift(60))<-.1);
});

test('NPC gains and cue caps are tripled while final turn limits stay unchanged', () => {
  near(C.trendGain,72); near(C.trendMaxCorrectionDps,18);
  near(C.socialGain,9); near(C.socialMaxCorrectionDps,7.5);
  near(C.separationMaxCorrectionDps,3);
  const old={...C,trendGain:24,trendMaxCorrectionDps:6,socialGain:3,socialMaxCorrectionDps:2.5};
  for(const vario of [2.99,3.01,0,6]) {
    const before=F.createFriend(user(),3),after=structuredClone(before);
    const observation={vario,userX:20,userY:0,userZ:1000.01};
    F.steer(before,observation,dt,old); F.steer(after,observation,dt);
    near(after.trendCorrection,3*before.trendCorrection);
    near(after.socialCorrection,3*before.socialCorrection);
    assert.ok(Math.abs(after.targetTurnRate)>=C.minTurnDps && Math.abs(after.targetTurnRate)<=C.maxTurnDps);
  }
  near(C.minTurnDps,12); near(C.maxTurnDps,28); near(C.wideningIncentiveScale,.5);
});

test('increasing vario widens; decreasing vario tightens; constant vario settles at ideal', () => {
  for (const slope of [-.1, 0, .1]) {
    const f = F.createFriend(user(), 3);
    for (let i = 0; i < 600; i++) {
      const previousRate = f.turnRate;
      F.steer(f, {vario: 3 + slope * i * dt, userX: 0, userY: radius, userZ: 1000}, dt);
      assert.ok(Math.abs(f.turnRate - previousRate) <= C.turnSlewDps2 * dt + 1e-10);
    }
    if (slope > 0) assert.ok(Math.abs(f.turnRate) < 19);
    else if (slope < 0) assert.ok(Math.abs(f.turnRate) > 21);
    else near(f.turnRate, -20);
  }
});

test('widening requests are exactly halved while tightening commands and response are unchanged', () => {
  for(const direction of [-1,1]) for(const slope of [-1,0,1]) for(const relative of [-3,0,3]) {
    const c={...C,turnDirection:direction}, old={...c,wideningIncentiveScale:1};
    const f=F.createFriend(user(),3,c);
    for(let i=1;i<=180;i++) {
      const original=structuredClone(f), half=structuredClone(f);
      const observation={vario:3+slope*i*dt,userX:f.x+40,userY:f.y+40,userZ:1000+relative*i*dt};
      F.steer(original,observation,dt,old); F.steer(half,observation,dt,c);
      const requested=Math.abs(original.targetTurnRate), actual=Math.abs(half.targetTurnRate);
      near(actual,requested<20 ? 20+(requested-20)*.5 : requested);
      if(requested>=20) near(half.turnRate,original.turnRate);
      Object.assign(f,half);
    }
  }
});

test('out-climbing partner still gets a fast response with reduced authority', () => {
  for(const direction of [-1,1]) {
    const c={...C,turnDirection:direction};
    const u=user(),f=F.createFriend(u,3,c);
    let at100ms=0,at500ms=0;
    for(let i=1;i<=60;i++) {
      F.advanceGuardedFriend(f,{vario:3,userX:u.x,userY:u.y,userZ:1000+i*dt},
        {x:0,y:0,radius:70,vx:0,vy:0},dt,c);
      if(i===6) at100ms=Math.abs(f.turnRate);
      if(i===30) at500ms=Math.abs(f.turnRate);
    }
    assert.ok(at100ms>20.1,`Slow onset: ${at100ms}`);
    assert.ok(at500ms>21,`Weak half-second response: ${at500ms}`);
    assert.ok(f.socialCorrection>2 && f.socialCorrection<=C.socialMaxCorrectionDps);
    for(let i=0;i<60*5;i++) F.steer(f,{vario:3,userX:u.x,userY:u.y,userZ:1001},dt,c);
    near(Math.abs(f.turnRate),20,.02);
  }
});

test('abeam correction reverses for falling or outside partners and vanishes beyond range', () => {
  for(const side of [-1,1]) for(const rising of [-1,1]) {
    const u=user(),f=F.createFriend(u,3);
    for(let i=1;i<=60;i++) F.steer(f,{vario:3,userX:f.x,userY:f.y+side*50,userZ:1000+rising*i*dt},dt);
    assert.ok((Math.abs(f.turnRate)-20)*side*rising>(side*rising>0 ? 1.8 : .9));
  }
  const f=F.createFriend(user(),3);
  for(let i=1;i<=60;i++) F.steer(f,{vario:3,userX:f.x,userY:f.y+300,userZ:1000+i*dt},dt);
  near(f.socialCorrection,0);near(f.turnRate,-20);
});

test('inside-only cue tapers to zero ahead/behind with identical forward and rear diagonals', () => {
  for (const direction of [-1,1]) for (const heading of [0,90,179,359]) {
    for (const rising of [-1,1]) for (const outside of [false,true]) {
      const c={...C,turnDirection:direction};
      const corrections=[];
      for (const offset of [-90,-45,0,45,90]) {
        const bearing=(heading+direction*(90+offset)+(outside?180:0))*Math.PI/180;
        const f={...F.createFriend(user(),3,c),x:0,y:0,heading,relativeClimb:rising};
        F.steer(f,{vario:3,userX:50*Math.sin(bearing),userY:50*Math.cos(bearing),userZ:1000+rising*dt},dt,c);
        const expected=C.socialGain*.96*C.socialInsideTurnWeight*Math.cos(offset*Math.PI/180)*rising*(outside?-1:1);
        near(f.socialCorrection,expected);
        near(Math.abs(f.targetTurnRate),20+expected*(expected<0?.5:1));
        corrections.push(f.socialCorrection);
      }
      near(corrections[0],0);near(corrections[4],0);
      near(corrections[1],corrections[3]);
      near(corrections[1],corrections[2]*Math.SQRT1_2);
    }
  }
});

test('own-vario trend vetoes conflicting climb and spacing cues independently', () => {
  for(const direction of [-1,1]) for(const slope of [-.1,.1]) {
    for(const relative of [-1,1]) for(const spacing of [-1,1]) {
      const c={...C,turnDirection:direction},u=user(),f=F.createFriend(u,3,c);
      for(let i=1;i<=300;i++) F.steer(f,{vario:3+slope*i*dt,userX:u.x,userY:u.y,
        userZ:1000+relative*i*dt,separationCorrection:spacing},dt,c);
      assert.ok(f.trendCorrection*slope<0);
      assert.ok(f.appliedSocialCorrection*f.trendCorrection>=0);
      assert.ok(f.appliedSeparationCorrection*f.trendCorrection>=0);
      if(f.socialCorrection*f.trendCorrection<0) near(f.appliedSocialCorrection,0);
      else near(f.appliedSocialCorrection,f.socialCorrection);
      if(spacing*f.trendCorrection<0) near(f.appliedSeparationCorrection,0);
      else near(f.appliedSeparationCorrection,spacing);
      assert.ok((Math.abs(f.targetTurnRate)-20)*f.trendCorrection>0);
    }
  }
});

test('angular spacing tightens behind and widens when catching up, using the moving center', () => {
  for(const direction of [-1,1]) for(const rotate of [0,150,359]) {
    const c={...C,turnDirection:direction},thermal={x:123,y:-67,radius:70};
    const bearing=rotate*Math.PI/180;
    const f={...F.createFriend(user(),3,c),x:thermal.x+30*Math.sin(bearing),y:thermal.y+30*Math.cos(bearing)};
    for(const gap of [0,45,90,135,180,225,270,315,359.99]) {
      const b=(rotate+direction*gap)*Math.PI/180;
      const observation={vario:3,userX:thermal.x+30*Math.sin(b),userY:thermal.y+30*Math.cos(b),userZ:1000};
      const s=F.separationObservation(f,observation,thermal,c);
      near(s.correction,C.separationMaxCorrectionDps*Math.sin((gap-180)*Math.PI/180));
      if(gap>0 && gap<180) assert.ok(s.correction<0);
      if(gap>180) assert.ok(s.correction>0);
      const pilot=structuredClone(f);
      F.steer(pilot,{...observation,separationCorrection:s.correction},dt,c);
      near(pilot.desiredIdealTurnDps,20+s.correction);
      near(Math.abs(pilot.targetTurnRate),20+s.correction*(s.correction<0?.5:1));
      const shifted=F.separationObservation({...f,x:f.x+500,y:f.y-20},
        {...observation,userX:observation.userX+500,userY:observation.userY-20},
        {...thermal,x:thermal.x+500,y:thermal.y-20},c);
      near(shifted.correction,s.correction);
    }
    near(F.separationObservation(f,{userX:thermal.x,userY:thermal.y},thermal,c).correction,0);
  }
});

test('small spacing bias reduces both leading and trailing phase error in steady lift', () => {
  for(const direction of [-1,1]) for(const gap of [135,225]) {
    const c={...C,turnDirection:direction},u={...user(),heading:(direction*90+360)%360,turnRate:direction*20};
    const f=F.createFriend(u,3,c),b=-direction*gap*Math.PI/180;
    f.x=radius*Math.sin(b);f.y=radius*Math.cos(b);
    f.heading=((-direction*gap+direction*90)%360+360)%360;
    const thermal={x:0,y:0,radius:70};
    for(let i=0;i<60*120;i++) {
      F.advanceGuardedFriend(f,{vario:3,userX:u.x,userY:u.y,userZ:u.z},thermal,dt,c);
      F.advancePilot(u,dt,c);
    }
    const finalGap=F.separationObservation(f,{userX:u.x,userY:u.y},thermal,c).gapDeg;
    assert.ok(Math.abs(finalGap-180)<25,`Gap ${gap} ended at ${finalGap}`);
  }
});

test('spacing filter is rate-stable and angular wrap produces no jump', () => {
  const f={...F.createFriend(user(),3),x:0,y:30},thermal={x:0,y:0,radius:70};
  let previous;
  for(let degrees=-1;degrees<=361;degrees+=.25) {
    const b=-degrees*Math.PI/180;
    const s=F.separationObservation(f,{userX:30*Math.sin(b),userY:30*Math.cos(b)},thermal);
    if(previous!==undefined) assert.ok(Math.abs(s.correction-previous)<.005*C.separationMaxCorrectionDps);
    previous=s.correction;
  }
  const g={...f};
  F.advanceGuardedFriend(g,{vario:3,userX:30,userY:0,userZ:1000},thermal,dt);
  assert.ok(Math.abs(g.separationCorrection)<=C.separationMaxCorrectionDps*(1-Math.exp(-dt/C.separationFilterS))+1e-8);
  assert.ok(Number.isFinite(g.angularSeparationDeg));
});

test('relative camera responds to heading, elevation and banking without nonfinite poses', () => {
  const f = F.createFriend(user(), 3), u = user();
  const initial = F.viewObservation(f, u);
  near(initial.elevation, 0); near(initial.distance, radius * 2);
  u.x += 30; u.z += 15;
  const changed = F.viewObservation(f, u);
  assert.ok(changed.elevation > 0);
  assert.notEqual(changed.localZ, initial.localZ);
  u.x = f.x; u.y = f.y; u.z = f.z + 1000;
  assert.ok(Object.values(F.viewObservation(f, u)).every(Number.isFinite));
});

test('spherical viewport maps true pilot elevation to full height, independent of range', () => {
  const u = {x:0,y:0,z:1000,heading:0};
  for (const range of [0.1, 10, 100, 1000]) {
    for (const degrees of [-80,-45,0,45,80]) {
      const f = {...u,x:range,z:1000+range*Math.tan(degrees*Math.PI/180),turnRate:-20};
      const v = F.viewObservation(f,u);
      near(v.viewElevationDeg,degrees);
      near(v.elevation,-degrees);
      near(v.screenYFraction,F.elevationScreenFraction(degrees));
    }
  }
  for (const [height, fraction, elevation] of [[20,0,90],[-20,1,-90],[0,.5,0]]) {
    const v = F.viewObservation({...u,z:u.z+height,turnRate:-20},u);
    near(v.screenYFraction,fraction); near(v.viewElevationDeg,elevation);
    assert.ok(Object.values(v).every(Number.isFinite));
  }
  const close = F.viewObservation({...u,x:10,z:1010,turnRate:-20},u);
  const distant = F.viewObservation({...u,x:100,z:1010,turnRate:-20},u);
  assert.ok(close.screenYFraction < distant.screenYFraction, 'Same altitude difference must appear higher when closer');
});

test('focused elevation scale expands the middle smoothly and compresses near both poles', () => {
  near(F.elevationScreenFraction(0),.5);
  near(F.elevationScreenFraction(90),0);
  near(F.elevationScreenFraction(-90),1);
  let previous = 1;
  for (let angle=-90;angle<=90;angle+=.25) {
    const y=F.elevationScreenFraction(angle);
    assert.ok(y>=0 && y<=1 && y<=previous);
    near(y+F.elevationScreenFraction(-angle),1);
    near(F.elevationScreenFraction(angle,{...C,sideViewElevationFocus:0}),(90-angle)/180);
    previous=y;
  }
  const middleMotion=F.elevationScreenFraction(0)-F.elevationScreenFraction(1);
  const edgeMotion=F.elevationScreenFraction(89)-F.elevationScreenFraction(90);
  assert.ok(middleMotion>7.9/180 && middleMotion<8.1/180);
  const epsilon=.0001;
  near((.5-F.elevationScreenFraction(epsilon))*180/epsilon,8,.001);
  assert.ok(edgeMotion<1/180);
  // Gain changes screen placement, not the physical angle used to orient the model.
  const u=user(),f={...F.createFriend(u,3),z:u.z+10};
  const linear=F.viewObservation(f,u,{...C,sideViewElevationFocus:0});
  const focused=F.viewObservation(f,u);
  near(linear.elevation,focused.elevation); near(linear.viewElevationDeg,focused.viewElevationDeg);
  assert.ok(focused.screenYFraction<linear.screenYFraction);
});

test('bounded head view centers abeam, allows exits, and keeps a stable viewing shoulder', () => {
  for (const side of [-1,1]) {
    for (const heading of [0,90,179,359]) {
      const u={x:0,y:0,z:1000,heading,turnRate:side*20};
      for (const offset of [-140,-90,-45,0,45,90,140]) {
        const bearing=(heading+side*90+offset)*Math.PI/180;
        const f={x:100*Math.sin(bearing),y:100*Math.cos(bearing),z:1000,heading:0,turnRate:-20};
        const v=F.viewObservation(f,u,{...C,turnDirection:side});
        const head=C.sideViewHeadYawMaxDeg*Math.sin(offset*Math.PI/180);
        near(v.headYawDeg,head);
        near(v.screenXFraction,.5+(offset-head)/C.sideViewHorizontalFovDeg);
        if(Math.abs(offset)>120) near(v.visibility,0);
        if(offset===0) {near(v.screenXFraction,.5);near(v.visibility,1);}
        const reversed=F.viewObservation(f,{...u,turnRate:-u.turnRate},{...C,turnDirection:side});
        near(reversed.screenXFraction,v.screenXFraction);
        near(reversed.headYawDeg,v.headYawDeg);
      }
    }
  }
  const u=user(); u.turnRate=0;
  near(F.viewObservation(F.createFriend(u,3),u).screenXFraction,.5);
  for (const fraction of [-1,0,.1,.5,.9,1,2]) {
    const left=F.spriteLeft(10,270,200,fraction,.45,{left:.2,right:.7});
    near(left+200*.45,10+270*fraction);
  }
  near(F.spriteLeft(10,270,200,.5,.45,{left:.2,right:.7})+200*.45,145);
});

test('full bearing sweep hides the rear wrap and keeps head and foreground continuous', () => {
  for(const side of [-1,1]) for(const heading of [0,179,359]) {
    const u={x:0,y:0,z:1000,heading,turnRate:side*20};
    let previous;
    for(let offset=-181;offset<=181;offset+=.25) {
      const bearing=(heading+side*90+offset)*Math.PI/180;
      const f={x:80*Math.sin(bearing),y:80*Math.cos(bearing),z:1000,heading:0,turnRate:-20};
      const v=F.viewObservation(f,u,{...C,turnDirection:side});
      assert.ok(Math.abs(v.headYawDeg)<=45+1e-7);
      assert.ok(v.visibility>=0 && v.visibility<=1);
      if(previous) {
        assert.ok(Math.abs(v.headYawDeg-previous.headYawDeg)<.2);
        const jump=Math.abs(v.screenXFraction-previous.screenXFraction);
        if(jump>1) {near(v.visibility,0);near(previous.visibility,0);}
        else assert.ok(jump<.005);
        assert.ok(Math.abs(F.wingtipPose(-20,v.headYawDeg).slewFraction-
          F.wingtipPose(-20,previous.headYawDeg).slewFraction)<.005);
      }
      previous=v;
    }
  }
});

test('friend apparent scale uses full slant range and can grow beyond its starting size', () => {
  const ref=C.sideViewReferenceRangeM;
  near(F.spriteSizeFraction(ref),.62);
  near(F.spriteSizeFraction(ref/2),1.24);
  near(F.spriteSizeFraction(ref*2),.31);
  near(F.spriteSizeFraction(ref,{...C,sideViewSizeScale:1.5}),.93);
  near(F.spriteSizeFraction(0),C.sideViewMaxSizeFraction);
  near(F.spriteSizeFraction(1e8),C.sideViewMinSizeFraction);
  const u={x:0,y:0,z:1000,heading:0,turnRate:-20};
  const v=F.viewObservation({...u,x:30,z:1040},u);
  near(v.distance,50); near(v.horizontalDistance,30);
  near(Math.hypot(v.localX,v.localZ),1);
  const html=fs.readFileSync(new URL('../public/labs/thermal-training/index.html',import.meta.url),'utf8');
  const draw=html.match(/function drawFriendTogetherSideView\([^]*?\n}/)[0];
  assert.ok(draw.includes('view.visibility > 0'));
  assert.ok(draw.includes('ctx.globalAlpha *= view.visibility'));
  assert.ok(draw.includes('spriteSizeFraction(view.distance)'));
  assert.ok(!draw.includes('Math.sqrt'));
});

test('work-together columns keep the enlarged side view centered and clear of HUD cards', () => {
  const html=fs.readFileSync(new URL('../public/labs/thermal-training/index.html',import.meta.url),'utf8');
  const fn=html.match(/function friendTogetherLayout\([^]*?\n}/)[0];
  const scope=vm.createContext({}); vm.runInContext(fn,scope);
  for(const [width,height,cardBottom] of [[320,500,90],[390,540,90],[760,480,95],[980,780,110],[1280,800,110]]) {
    const scale=width/980, map={x:0,y:0,w:980,h:height/scale};
    const layout=scope.friendTogetherLayout(map,(cardBottom+10)/scale);
    near(layout.side.y+layout.side.h/2,map.h/2);
    assert.ok(layout.side.y*scale>=cardBottom+10-1e-7);
    near(layout.topDown.x+layout.topDown.w/2,map.w*.325);
    assert.ok(layout.side.w*scale>width*.42);
    assert.ok(layout.side.x>=layout.topDown.x+layout.topDown.w);
    assert.ok(layout.side.h>0 && layout.side.y+layout.side.h<=map.h);
  }
  const draw=html.match(/function drawMainMap\([^]*?\n}/)[0];
  assert.ok(draw.includes('const layout = isWorkTogether ?'));
  assert.ok(html.includes("classList.toggle('friends-columns', workTogether)"));
});

test('top-down zoom doubles world spacing, not icons; crossing pilots draw by altitude', () => {
  const html=fs.readFileSync(new URL('../public/labs/thermal-training/index.html',import.meta.url),'utf8');
  const functions=['drawMainMap','friendTogetherLayout','mapPoint'].map(name=>
    html.match(new RegExp('function '+name+'\\([^]*?\\n}'))[0]).join('\n');
  for(const mobile of [false,true]) for(const altitudes of [[900,1100],[1100,900],[1000,1000]]) {
    const calls=[],scales=[];
    const friends=altitudes.map((z,i)=>({x:0,y:0,z,heading:0,turnRate:20,color:`friend${i}`}));
    const scope=vm.createContext({
      tutorial:{active:true,step:0,friendGliders:friends},
      tutorialSteps:[{successType:'thermalFriendsTogether'}],
      state:{x:0,y:0,z:1000,t:0,heading:0,bankCue:0,samples:[]},
      friendViewTopClearance:100,maxTurnRateDps:30,
      readParams:()=>({showThermal:false}), thermalCenterAt:()=>({x:0,y:0}),
      thermalProfileAt:()=>({radius:70}),isMobileLayout:()=>mobile,
      ctx:new Proxy({}, {get:()=>()=>{}}),
      drawTerrain:(_r,_x,_y,scale)=>scales.push(scale),
      drawFriendTurnForecast:()=>calls.push({type:'forecast'}),
      drawTurnPreview:()=>calls.push({type:'preview'}),actualTurnRateDps:()=>20,
      drawParaglider:(x,y,heading,rate,size,color)=>calls.push({type:'pilot',size,color})
    });
    vm.runInContext(functions,scope);
    scope.drawMainMap({x:0,y:0,w:980,h:780},null);
    near(scales[0],2*Math.min(980*.45/250,780/330));
    const icons=calls.filter(c=>c.type==='pilot');
    const expected=[...friends.map(f=>({color:f.color,z:f.z})),{color:'blue',z:1000}].sort((a,b)=>a.z-b.z);
    assert.deepEqual(icons.map(c=>c.color),expected.map(c=>c.color));
    for(const icon of icons) near(icon.size,(mobile?1.5:1)*(icon.color==='blue'?1:1.12));
    assert.equal(calls.filter(c=>c.type==='forecast').length,0);
    assert.equal(calls[0].type,'preview');
    assert.deepEqual(friends.map(f=>f.z),altitudes);
    // Other lessons keep the old world scale.
    scope.tutorial.active=false;
    scope.drawMainMap({x:0,y:0,w:980,h:780},null);
    near(scales[1],780/330*(mobile?1.5:1));
  }
});

test('both NPCs cross the full map behind overlays without column clipping', () => {
  const html=fs.readFileSync(new URL('../public/labs/thermal-training/index.html',import.meta.url),'utf8');
  const functions=['drawMainMap','friendTogetherLayout','mapPoint'].map(name=>
    html.match(new RegExp('function '+name+'\\([^]*?\\n}'))[0]).join('\n');
  for(const mobile of [false,true]) {
    const map={x:10,y:20,w:980,h:780},clips=[],pilots=[];
    let pathRect;
    const ctx=new Proxy({}, {get:(_,key)=>key==='rect' ? (...v)=>{pathRect=v;}
      : key==='clip' ? ()=>clips.push(pathRect) : ()=>{}});
    const scope=vm.createContext({ctx,
      tutorial:{active:true,step:0,friendGliders:[
        {x:-80,y:0,z:1000,heading:0,turnRate:20,color:'friend'},
        {x:100,y:0,z:1100,heading:0,turnRate:20,color:'arrival'}]},
      tutorialSteps:[{successType:'thermalFriendsTogether'}],
      state:{x:0,y:0,z:1000,t:0,heading:0,bankCue:0,samples:[]},
      friendViewTopClearance:100,maxTurnRateDps:30,
      readParams:()=>({showThermal:false}),thermalCenterAt:()=>({x:0,y:0}),
      thermalProfileAt:()=>({radius:70}),isMobileLayout:()=>mobile,
      drawTerrain:()=>{},drawTurnPreview:()=>{},actualTurnRateDps:()=>20,
      drawParaglider:(x,y,heading,rate,size,color)=>pilots.push({x,y,color})});
    vm.runInContext(functions,scope);
    scope.drawMainMap(map,null);
    assert.deepEqual(clips,[[map.x,map.y,map.w,map.h]]);
    const column=scope.friendTogetherLayout(map,100).topDown;
    const left=pilots.find(p=>p.color==='friend'),right=pilots.find(p=>p.color==='arrival');
    assert.ok(left.x<column.x && left.x>map.x);
    assert.ok(right.x>column.x+column.w && right.x<map.x+map.w);
    near(pilots.find(p=>p.color==='blue').x,map.x+map.w*.325);
  }
  const draw=html.match(/function draw\(\)[^]*?\n}/)[0];
  assert.ok(draw.indexOf('drawFriendTogetherSideView(map)')>draw.indexOf('drawMainMap(map'));
});

test('foreground inside wingtip descends with bank and mirrors with turn direction', () => {
  const level=F.wingtipPose(0), medium=F.wingtipPose(20), steep=F.wingtipPose(30);
  near(level.tipFraction,-C.sideViewWingtipLevelClearanceFraction);
  // Even the lowest outline control point and stroke stay above the viewport.
  for(const height of [250,360]) assert.ok(height*(level.tipFraction+.012)+1.05/2<0);
  assert.ok(medium.tipFraction>.09 && medium.tipFraction<.15);
  assert.ok(steep.tipFraction>.30 && steep.tipFraction<.39);
  assert.ok(F.wingtipPose(.01).tipFraction<0);
  assert.ok(level.tipFraction<medium.tipFraction && medium.tipFraction<steep.tipFraction);
  near(F.wingtipPose(-20).tipFraction,medium.tipFraction);
  assert.equal(F.wingtipPose(-20).side,-1); assert.equal(medium.side,1);
  const html=fs.readFileSync(new URL('../public/labs/thermal-training/index.html',import.meta.url),'utf8');
  const draw=html.match(/function drawFriendTogetherSideView\([^]*?\n}/)[0];
  assert.ok(draw.indexOf('drawUserInsideWingtip')>draw.indexOf('ctx.drawImage(image'));
});

test('hanging wingtip slews opposite the look direction, independent of bank', () => {
  for (const turnRate of [-30,-20,0,20,30]) {
    const centered=F.wingtipPose(turnRate);
    near(centered.slewFraction,0);
    const left=F.wingtipPose(turnRate,-60),right=F.wingtipPose(turnRate,60);
    assert.ok(left.slewFraction>0 && right.slewFraction<0);
    near(left.slewFraction,-right.slewFraction);
    near(left.tipFraction,centered.tipFraction); near(right.tipFraction,centered.tipFraction);
    near(F.wingtipPose(turnRate,180).slewFraction,-C.sideViewWingtipSlewFraction);
    // Even the widest mirrored outline clears either edge at a full look offset.
    assert.ok(.5+F.wingtipPose(turnRate,90).slewFraction+.36<0);
    assert.ok(.5+F.wingtipPose(turnRate,-90).slewFraction-.36>1);
  }
  const html=fs.readFileSync(new URL('../public/labs/thermal-training/index.html',import.meta.url),'utf8');
  const draw=html.match(/function drawUserInsideWingtip\([^]*?\n}/)[0];
  assert.ok(draw.includes('rib(cellY)'));
  assert.ok(draw.includes('ctx.moveTo(-w * .40, cellY + h * .012)'));
});

test('wingtip is visible only when banking toward the NPC side, never when level', () => {
  for(const heading of [0,45,179,270,359]) for(const relative of [-170,-90,-10,10,90,170]) {
    const bearing=(heading+relative)*Math.PI/180;
    const u={x:0,y:0,z:1000,heading,turnRate:0};
    const f={x:50*Math.sin(bearing),y:50*Math.cos(bearing),z:1000,heading:0,turnRate:-20};
    const view=F.viewObservation(f,u);
    for(const rate of [-20,-.01,0,.01,20]) {
      const opacity=F.wingtipVisibility(rate,view.relativeBearing,view.horizontalDistance);
      if(rate===0 || Math.sign(rate)!==Math.sign(relative)) near(opacity,0);
      else near(opacity,1);
    }
  }
  for(const rate of [-20,0,20]) {
    for(const bearing of [-180,0,180]) near(F.wingtipVisibility(rate,bearing,50),0);
    near(F.wingtipVisibility(rate,90,0),0);
    near(F.wingtipVisibility(rate,null,50),0);
  }
  near(F.wingtipVisibility(20,90,50),1);
  near(F.wingtipVisibility(.1,90,50),1);
  near(F.wingtipVisibility(1,90,50),1);
  const html=fs.readFileSync(new URL('../public/labs/thermal-training/index.html',import.meta.url),'utf8');
  const draw=html.match(/function drawUserInsideWingtip\([^]*?\n}/)[0];
  const scope=vm.createContext({FriendTraining:F,ctx:new Proxy({}, {get(){throw new Error('Hidden wing must not draw');}})});
  assert.ok(!draw.includes('globalAlpha'));
  vm.runInContext(draw,scope);
  scope.drawUserInsideWingtip({w:270,h:360},0,0,90,50);
  scope.drawUserInsideWingtip({w:270,h:360},20,0,-90,50);
  scope.drawUserInsideWingtip({w:270,h:360},-20,0,90,50);
  const panel=html.match(/function drawFriendTogetherSideView\([^]*?\n}/)[0];
  assert.ok(panel.includes('view?.relativeBearing'));
  assert.ok(panel.includes('view?.horizontalDistance'));
});

test('foreground cells have double spacing and travel with the wingtip fabric', () => {
  const html=fs.readFileSync(new URL('../public/labs/thermal-training/index.html',import.meta.url),'utf8');
  const draw=html.match(/function drawUserInsideWingtip\([^]*?\n}/)[0];
  const sample=(turnRate,height=360) => {
    const moves=[], curves=[];
    const ctx=new Proxy({}, {get:(_,key) => key==='createLinearGradient'
      ? () => ({addColorStop(){}})
      : key==='moveTo' ? (x,y) => {if(Math.abs(x+270*.4)<1e-8) moves.push(y);}
      : key==='bezierCurveTo' ? (...p) => curves.push(p)
      : () => {}});
    const scope=vm.createContext({ctx,FriendTraining:F,clamp:(n,a,b)=>Math.min(b,Math.max(a,n))});
    vm.runInContext(draw,scope);
    scope.drawUserInsideWingtip({x:0,y:0,w:270,h:height},turnRate,0,Math.sign(turnRate)*90,50);
    // Twelve curved shading bands and one seam per cell. Verify material
    // coordinates from actual drawing calls, including curvature and spacing.
    near(moves[1]-moves[0],height*C.sideViewWingtipCellHeightFraction/12);
    assert.ok(curves.some(p=>p[1]!==p[3] && p[3]!==p[5]), 'Ribs must curve, not be horizontal bars');
    return moves.filter((_,i)=>i%13===0).map(y=>[y-height*.012,y-height*.012+height*C.sideViewWingtipCellHeightFraction]);
  };
  for(const height of [250,360]) {
    const medium=sample(20,height),steep=sample(30,height);
    const spacing=height*.027*2;
    const travel=height*(F.wingtipPose(30).tipFraction-F.wingtipPose(20).tipFraction);
    near(medium[0][1],height*F.wingtipPose(20).tipFraction);
    for(let i=0;i<medium.length;i++) {
      near(medium[i][1]-medium[i][0],spacing);
      near(steep[i][0]-medium[i][0],travel);
      if(i) near(medium[i-1][0]-medium[i][0],spacing);
    }
  }
});

test('panorama skyline and equal-altitude pilot share the middle of the visual viewport', () => {
  for (const horizon of [.4,.61,.75]) {
    const crop = F.panoramaCrop(2172,724,{...C,sideViewPanoramaHorizonFraction:horizon});
    assert.ok(crop.sy >= 0 && crop.sy+crop.sh <= 724+1e-7);
    near((horizon*724-crop.sy)/crop.sh,.5);
  }
  const html = fs.readFileSync(new URL('../public/labs/thermal-training/index.html', import.meta.url),'utf8');
  const draw = html.match(/function drawFriendTogetherSideView\([^]*?\n}/)[0];
  assert.ok(!/YOUR ALTITUDE|viewElevationDeg\.toFixed|matched climb|m away|SIDE VIEW/.test(draw));
  const f = F.createFriend(user(),3);
  near(F.viewObservation(f,user()).screenYFraction,.5);
});

test('thermal guard enforces both radius fractions, including hostile boundary headings', () => {
  for(const radius of [40,70,100]) for(const side of [-1,1]) {
    const config={...C,turnDirection:side};
    const thermal={x:200,y:-100,radius,vx:.2,vy:-.1};
    for(const fraction of [.2,.7]) for(const heading of [0,90,180,270]) {
      const f={...F.createFriend(user(),3,config),x:thermal.x+radius*fraction,y:thermal.y,heading};
      const before=structuredClone(f);
      const observation={vario:3,userX:100,userY:100,userZ:f.z};
      F.advanceGuardedFriend(f,observation,thermal,0,config);
      assert.deepEqual(f,before);
      F.advanceGuardedFriend(f,observation,thermal,dt,config);
      const distance=Math.hypot(f.x-thermal.x,f.y-thermal.y);
      assert.ok(distance>=radius*.2-1e-8 && distance<=radius*.7+1e-8);
      assert.ok(Math.abs(f.turnRate-before.turnRate)<=C.turnSlewDps2*dt+1e-8);
    }
  }
});

test('guarded partner stays within the moving thermal band through changing lift and social cues', () => {
  let interventions=0,limits=0;
  for(const seed of [42,12345]) {
    const u=user(), f=F.createFriend(u,3.135), w=F.createWeather(seed);
    for(let i=0;i<60*360;i++) {
      F.advanceWeather(w,dt);
      F.advancePilot(u,dt);
      u.z=1000+50*Math.sin(i*dt/20);
      const previousRate=f.turnRate;
      F.advanceGuardedFriend(f,{vario:f.vario,userX:u.x,userY:u.y,userZ:u.z},
        {x:w.x,y:w.y,radius:70,vx:Math.sin(w.bearingDeg*Math.PI/180)*w.speedMps,
          vy:Math.cos(w.bearingDeg*Math.PI/180)*w.speedMps},dt);
      const r=Math.hypot(f.x-w.x,f.y-w.y);
      assert.ok(r>=14-1e-8 && r<=49+1e-8,`Radius ${r}`);
      assert.ok(Math.abs(f.turnRate-previousRate)<=C.turnSlewDps2*dt+1e-8);
      f.vario=(C.initialCoreClimbMps*w.strengthFactor+C.sinkMps)*F.thermalLiftShape(r/70)-C.sinkMps-F.turnSink(f.turnRate);
      f.z+=f.vario*dt;
      if(f.thermalGuardActive) interventions++;
      if(f.thermalBoundaryLimited) limits++;
    }
  }
  assert.ok(interventions>0);
  console.log('12-minute guard stress: predictive ticks',interventions,'boundary backstop ticks',limits);
});

test('eight-minute flight remains finite with bounded controls and no friend teleporting', () => {
  const u = user(), f = F.createFriend(u, 3.135), w = F.createWeather(12345);
  function vario(p) {
    return (C.initialCoreClimbMps * w.strengthFactor + C.sinkMps) * F.thermalLiftShape(Math.hypot(p.x-w.x,p.y-w.y)/(C.diameterM/2)) - C.sinkMps - F.turnSink(p.turnRate);
  }
  for (let i = 0; i < 60 * 480; i++) {
    F.advanceWeather(w, dt);
    F.steer(f, {vario: f.vario, userX:u.x,userY:u.y,userZ:u.z}, dt);
    const old = {x:f.x,y:f.y};
    F.advancePilot(f,dt); F.advancePilot(u,dt);
    f.vario=vario(f); u.vario=vario(u); f.z += f.vario*dt; u.z += u.vario*dt;
    assert.ok(Math.hypot(f.x-old.x,f.y-old.y) <= C.airspeedMps*dt + 1e-8);
    assert.ok(Math.abs(f.turnRate)>=C.minTurnDps && Math.abs(f.turnRate)<=C.maxTurnDps);
    assert.ok([f.x,f.y,f.z,f.heading,f.vario].every(Number.isFinite));
  }
  console.log('480s smoke: friend climb', (f.z-1000).toFixed(1), 'm; distance from thermal', Math.hypot(f.x-w.x,f.y-w.y).toFixed(1), 'm');
});
