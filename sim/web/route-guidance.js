(() => {
  'use strict';
  const card=document.getElementById('routeTrialCard');
  const preset=document.getElementById('routePreset');
  const policy=document.getElementById('routePolicy');
  const status=document.getElementById('routeTrialStatus');
  const fit=document.getElementById('routeFit');
  const cost=document.getElementById('routeCost');
  let current=null;
  async function post(body) {
    try {
      const response=await fetch('/api/route-guidance',{method:'POST',
        headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
      const result=await response.json();
      if(!result.ok)throw new Error(result.error||'Route request failed');
      status.textContent='Applying route trial…';
    } catch(error) {status.textContent=error.message;}
  }
  document.getElementById('routeStart').onclick=()=>post({preset:preset.value,policy:policy.value});
  document.getElementById('routeDisable').onclick=()=>post({disable:true});
  policy.onchange=()=>{if(current?.enabled)post({policy:policy.value});};
  window.leafRouteGuidance={
    fit:()=>Boolean(current?.enabled&&fit.checked),
    bounds:()=>current?.cylinders||[],
    draw(ctx,point,scale) {
      if(!current?.enabled)return;
      const path=current.path||[],active=current.active||0;
      ctx.save();ctx.font='bold 12px system-ui';ctx.textAlign='center';
      const poly=(points,color,dashed=false)=>{
        if(!points.length)return;
        ctx.strokeStyle=color;ctx.lineWidth=2;ctx.setLineDash(dashed?[5,5]:[]);
        ctx.beginPath();points.forEach((q,i)=>{const p=point(q[0],q[1]);i?ctx.lineTo(p.x,p.y):ctx.moveTo(p.x,p.y);});ctx.stroke();
      };
      poly(current.cylinders.map(c=>[c[0],c[1]]),'#717f8a',true);
      current.cylinders.forEach((c,i)=>{
        const p=point(c[0],c[1]),credited=i<active||current.completed;
        ctx.setLineDash(credited?[5,5]:[]);ctx.lineWidth=i===active?2.5:1.5;
        ctx.strokeStyle=credited?'#9fa9a1':i===active?'#155e91':'#577a45';
        ctx.fillStyle=credited?'rgba(180,190,180,.12)':'rgba(155,195,110,.12)';
        const ring=current.outlines?.[i];
        ctx.beginPath();
        if(ring?.length){ring.forEach((q,j)=>{const v=point(q[0],q[1]);j?ctx.lineTo(v.x,v.y):ctx.moveTo(v.x,v.y);});ctx.closePath();}
        else ctx.arc(p.x,p.y,c[2]*scale,0,2*Math.PI);
        ctx.fill();ctx.stroke();
        const margin=path[i-active]?.[2]||0;
        if(!credited&&margin>0){ctx.setLineDash([3,4]);ctx.strokeStyle='#bc680f';
          ctx.beginPath();ctx.arc(p.x,p.y,Math.max(0,c[2]-margin)*scale,0,2*Math.PI);ctx.stroke();}
        ctx.fillStyle='#203b2c';ctx.fillText('TP'+(i+1)+' · '+c[2]+' m',p.x,p.y-c[2]*scale-9);
      });
      if(current.valid){
        poly([current.gps,...path],'#d77411');
        path.forEach((q,i)=>{const p=point(q[0],q[1]);ctx.setLineDash([]);
          ctx.fillStyle=i?'#e8a657':'#c35200';ctx.beginPath();ctx.arc(p.x,p.y,i?3:6,0,2*Math.PI);ctx.fill();});
        if(current.entry){const e=point(current.entry[0],current.entry[1]);
          ctx.setLineDash([]);ctx.strokeStyle='#155e91';ctx.lineWidth=2;
          ctx.beginPath();ctx.moveTo(e.x-5,e.y);ctx.lineTo(e.x+5,e.y);
          ctx.moveTo(e.x,e.y-5);ctx.lineTo(e.x,e.y+5);ctx.stroke();}
        const p=point(current.gps[0],current.gps[1]);ctx.fillStyle='#8b288c';
        ctx.beginPath();ctx.arc(p.x,p.y,3,0,2*Math.PI);ctx.fill();
      }
      ctx.restore();
    }
  };
  fit.onchange=()=>window.dispatchEvent(new Event('route-redraw'));
  addEventListener('leaf-status',e=>{
    const previousPreset=current?.preset;
    current=e.detail.routeGuidance;
    if(current?.enabled&&current.preset!==previousPreset)preset.value=current.preset;
    cost.hidden=!current?.enabled;
    if(!current?.enabled){
      status.textContent='Desktop experiment off. Start a route to compare the map with the real LCD.';
      return;
    }
    cost.textContent='From original start: selected margin '+Number(current.startPolicyM).toFixed(1)+
      ' m · ideal optimized (no margin) '+Number(current.startIdealM).toFixed(1)+' m · extra '+
      (current.startExtraPct>=0?'+':'')+Number(current.startExtraPct).toFixed(2)+'%';
    if(document.activeElement!==policy)policy.value=current.policy;
    status.textContent=current.valid
      ? (current.completed?'Goal reached · ':'TP '+(current.active+1)+' / '+current.cylinders.length+' · ')+
        Math.round(current.remainingM)+' m to goal · '+Math.round(current.cylinderRemainingM)+' m to cylinder entry · bearing '+Math.round(current.bearingDeg)+'° · '+
        Math.round(current.path[0]?.[2]||0)+' m inward margin'
      : 'Route loaded · waiting for a fresh GPS fix and flight logging';
  });
})();
