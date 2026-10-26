'use strict';
// A small 4 Hz position feed runs separately from slower receiver diagnostics.
// Play received fixes on one continuous receiver-time timeline. Restarting a
// tween per poll makes steady flight accelerate, stop, and accelerate again.
const motion=new Map(),trailLoads=new Map(),aircraftInfo=new Map();
const MOTION_DELAY=3, MOTION_MAX_GAP=10, MOTION_COAST=30;
let motionClock=null;
function motionTime(now=performance.now()){return motionClock?motionClock.uptime+(now-motionClock.local)/1000-MOTION_DELAY:0;}
let liveBusy=false,liveTimer=null,liveSupported=true,trailBusy=false,infoAbort=null,infoSelection='';
let backgroundCanvas=null,backgroundKey='',backgroundStatus={ready:0,failed:0},backgroundVersion=0;
let backgroundView=null;
let slowUIAt=0;
function renderSlowUI(){if(Date.now()-slowUIAt>=1000){table();details();correctionUI();slowUIAt=Date.now();}}
function updateMotion(s){
 if(state&&s.uptime<state.uptime){motion.clear();trailLoads.clear();motionClock=null;}
 if(!motionClock)motionClock={uptime:s.uptime,local:performance.now()};
 const ids=new Set();
 for(const a of s.aircraft){const id=key(a);ids.add(id);if(!hasPosition(a))continue;
  const samples=motion.get(id)||[],last=samples.at(-1),play=motionTime();
  const before=last&&play>last.at?motionPosition(id):null;
  const previousRaw=before?rawMotionPosition(samples,play):null;
  // Any received message refreshes contact, but only reported ground velocity
  // can drive prediction. Never fabricate speed from heading or a lone fix.
  const velocity=Number.isFinite(a.speed)&&Number.isFinite(a.track)?a:samples.report;
  samples.report={seen:s.uptime-(a.age??0),speed:velocity?.speed,track:velocity?.track,altitude:Number.isFinite(a.altitude)?a.altitude:samples.report?.altitude};
  // The API rounds ages to milliseconds. Repeated fixes (including heading-only
  // packets) must not create new position samples or restart movement.
  const at=Math.round((s.uptime-(a.positionAge||0))*1000)/1000;
  const reset=last&&(at-last.at>MOTION_MAX_GAP||Math.abs(wrap(a.lon-last.p[0]))>1||Math.abs(a.lat-last.p[1])>1);
  if(!last||at>last.at+.001){
   if(reset){samples.length=0;samples.correction=null;}
   samples.push({at,p:[a.lon,a.lat,a.track??last?.p[2]??0]});
  }
  // Ease prediction error away when a new report arrives; do not snap a
  // coasting marker backwards onto the newly measured path.
  if(before&&!reset){const after=rawMotionPosition(samples,play);
   if(after.some((v,i)=>Math.abs(v-previousRaw[i])>1e-9))samples.correction={at:play,delta:[wrap(before[0]-after[0]),before[1]-after[1],wrap(before[2]-after[2])]};
  }
  while(samples.length>2&&(samples.length>64||samples[1].at<motionTime()-2))samples.shift();
  motion.set(id,samples);
 }
 for(const id of motion.keys())if(!ids.has(id)){motion.delete(id);trailLoads.delete(id);}
}
function coastSeconds(samples){const altitude=samples.report?.altitude;return !Number.isFinite(altitude)||altitude<1000?5:altitude>5000?MOTION_COAST:10;}
function coastEnd(samples){const seconds=coastSeconds(samples);return Math.min(samples.at(-1).at+seconds,(samples.report?.seen??-Infinity)+seconds-MOTION_DELAY);}
function rawMotionPosition(samples,at){
 if(at<=samples[0].at)return samples[0].p;
 for(let i=1;i<samples.length;i++){const b=samples[i];if(at>b.at)continue;
  const a=samples[i-1],t=(at-a.at)/(b.at-a.at);
  return [wrap(a.p[0]+wrap(b.p[0]-a.p[0])*t),a.p[1]+(b.p[1]-a.p[1])*t,a.p[2]+wrap(b.p[2]-a.p[2])*t];
 }
 const last=samples.at(-1),r=samples.report;
 const seconds=Math.max(0,Math.min(at,coastEnd(samples))-last.at);
 if(!seconds||!Number.isFinite(r?.speed)||!Number.isFinite(r?.track)||r.speed<0||r.speed>1200)return last.p;
 // Great-circle dead reckoning; knots -> metres/sec -> angular distance.
 const rad=Math.PI/180,d=r.speed*.514444*seconds/6371000,b=r.track*rad,lat=last.p[1]*rad,lon=last.p[0]*rad;
 const lat2=Math.asin(Math.max(-1,Math.min(1,Math.sin(lat)*Math.cos(d)+Math.cos(lat)*Math.sin(d)*Math.cos(b))));
 const lon2=lon+Math.atan2(Math.sin(b)*Math.sin(d)*Math.cos(lat),Math.cos(d)-Math.sin(lat)*Math.sin(lat2));
 return [wrap(lon2/rad),lat2/rad,r.track];
}
function motionPosition(id,at=motionTime()){
 const samples=motion.get(id);if(!samples?.length)return null;
 const p=rawMotionPosition(samples,at),c=samples.correction;
 const weight=c?Math.max(0,Math.min(1,1-(at-c.at))):0;
 return weight?[wrap(p[0]+c.delta[0]*weight),p[1]+c.delta[1]*weight,p[2]+c.delta[2]*weight]:p;
}
function displayAircraft(a,at=motionTime()){const p=motionPosition(key(a),at);return p?{...a,lon:p[0],lat:p[1],track:p[2]}:a;}
function displayTrail(a,display,at=motionTime()){
 // Trail caches contain the newest received fixes, ahead of delayed playback.
 // Clip by receiver time, not browser arrival time, and join to the same
 // displayed position used by the icon (including bounded prediction).
 const points=(trails.get(key(a))||[]).filter(p=>{
  const fixAt=Number.isFinite(p.at)?p.at:state.uptime+(p.time-lastFetch)/1000;
  return fixAt<=at&&Date.now()-p.time<600000;
 });
 return [...points,{lon:display.lon,lat:display.lat,alt:a.altitude,at,time:Date.now()}];
}
function drawBaseCached(w,h){
 const cacheKey=[w,h,canvas.width,canvas.height,zoomLevel,prefs.basemap,world.length,backgroundVersion].join('/');
 const c=mercator(...center),worldSize=256*2**zoomLevel;
 let dx=backgroundView?((backgroundView.c[0]-c[0]+worldSize/2)%worldSize+worldSize)%worldSize-worldSize/2:0;
 let dy=backgroundView?backgroundView.c[1]-c[1]:0;
 if(!backgroundView||backgroundKey!==cacheKey||Math.abs(dx)>backgroundView.pad-32||Math.abs(dy)>backgroundView.pad-32){
  const pad=256,bw=w+pad*2,bh=h+pad*2,dpr=canvas.width/w;
  if(!backgroundCanvas)backgroundCanvas=document.createElement('canvas');
  backgroundCanvas.width=Math.round(bw*dpr);backgroundCanvas.height=Math.round(bh*dpr);
  const screen=ctx,viewport=mapViewport;
  try{
   ctx=backgroundCanvas.getContext('2d');ctx.setTransform(dpr,0,0,dpr,0,0);mapViewport={w:bw,h:bh};
   baseMap(bw,bh);offlineMap(bw,bh);backgroundStatus=prefs.basemap==='streets'?streetTiles(bw,bh):{ready:0,failed:0};
   ctx.fillStyle='#152d3c1a';ctx.fillRect(0,0,bw,bh);
  }finally{ctx=screen;mapViewport=viewport;}
  backgroundKey=cacheKey;backgroundView={c,pad,bw,bh};dx=0;dy=0;
 }
 const {pad,bw,bh}=backgroundView;
 ctx.drawImage(backgroundCanvas,dx-pad,dy-pad,bw,bh);return backgroundStatus;
}
function animateMap(now){
 if(!document.hidden&&online&&canvas.clientWidth){
  const at=motionTime(),moving=[...motion.values()].some(m=>at>=m[0].at&&(at<Math.max(m.at(-1).at,coastEnd(m))||at<(m.correction?.at??-Infinity)+1));
  if(moving){const a=selectedAircraft();if(follow&&a&&onMap(a)){const p=displayAircraft(a);center=[p.lon,p.lat];}if(!drawPending)draw();}
 }requestAnimationFrame(animateMap);
}
requestAnimationFrame(animateMap);
async function livePoll(){
 if(liveBusy)return;const start=Date.now();liveBusy=true;
 try{if(state&&liveSupported&&!document.hidden){const r=await fetch('/api/live',{cache:'no-store',signal:AbortSignal.timeout(2000)});if(r.status===404){liveSupported=false;return;}if(!r.ok)throw Error();const s=await r.json();
  // Full diagnostics and fast polls may finish out of order. Only apply newer snapshots.
  if(s.uptime>=state.uptime||s.uptime+5<state.uptime)render({...state,...s});setText('updateStatus','4 Hz live · 3 s buffer · prediction 5/10/30 s by altitude');
 }}catch(e){setText('updateStatus','Live feed reconnecting');}finally{liveBusy=false;liveTimer=setTimeout(livePoll,document.hidden?4000:Math.max(20,250-(Date.now()-start)));}
}
livePoll();
async function loadTrail(){
 if(trailBusy||document.hidden||!state)return;const now=Date.now();
 const a=observed().find(a=>hasPosition(a)&&(!trailLoads.has(key(a))||now-trailLoads.get(key(a))>60000));if(!a)return;
 const id=key(a);trailLoads.set(id,now);trailBusy=true;const boot=state.uptime;
 try{const r=await fetch('/api/trails?address='+a.address+'&nonIcao='+(a.nonIcao?1:0),{cache:'no-store',signal:AbortSignal.timeout(2500)});if(!r.ok)return;const data=await r.json();if(!state||state.uptime<boot)return;
  const points=data.points.filter(p=>Array.isArray(p)&&p.length===4&&Number.isFinite(p[0])&&Number.isFinite(p[1])&&p[3]>=0&&p[3]<600).map(p=>({lat:p[0],lon:p[1],alt:p[2],at:data.uptime-p[3],time:Date.now()-p[3]*1000}));
  const merged=[...points,...(trails.get(id)||[])].sort((a,b)=>a.time-b.time);trails.set(id,merged.filter((p,i)=>!i||p.lat!==merged[i-1].lat||p.lon!==merged[i-1].lon).filter(p=>now-p.time<600000).slice(-600));draw();
 }catch(e){}finally{trailBusy=false;}
}
setInterval(loadTrail,600);
function infoText(text){$('aircraftInfoText').textContent=text;}
function showInfo(data){
 infoText([data.manufacturer,data.type,data.registration].filter(Boolean).join(' · ')||'Aircraft model unavailable');
 const img=$('aircraftPhoto'),link=$('photoCredit');img.hidden=true;img.removeAttribute('src');link.hidden=true;
 // Only use provider-hosted HTTPS images and show their source beside them.
 try{const u=new URL(data.url_photo_thumbnail||data.url_photo);if(u.protocol==='https:'&&(u.hostname==='airport-data.com'||u.hostname.endsWith('.airport-data.com'))){img.alt=[data.registration,data.type].filter(Boolean).join(' ');img.src=u.href;img.hidden=false;link.href='https://www.airport-data.com/aircraft/'+encodeURIComponent(data.registration||'')+'.html';link.hidden=false;}}catch(e){}
}
async function lookupAircraft(a){
 const id=a?key(a):'';if(id===infoSelection)return;infoSelection=id;if(infoAbort)infoAbort.abort();$('aircraftPhoto').hidden=true;$('aircraftPhoto').removeAttribute('src');$('photoCredit').hidden=true;
 if(!a){infoText('');return;}if(a.nonIcao){infoText('Model lookup unavailable for non-ICAO addresses');return;}
 if(!prefs.onlineDetails){infoText('Online aircraft details are off');return;}
 const cached=aircraftInfo.get(id);if(cached&&Date.now()-cached.at<3600000){if(cached.data)showInfo(cached.data);else infoText(cached.message);return;}
 infoText('Looking up aircraft model…');infoAbort=new AbortController();const controller=infoAbort,timer=setTimeout(()=>controller.abort(),6000);
 try{const r=await fetch('https://api.adsbdb.com/v0/aircraft/'+encodeURIComponent(a.address),{signal:controller.signal,referrerPolicy:'no-referrer'});if(!r.ok)throw Error(r.status===404?'No model details found':'Online details unavailable');const data=(await r.json()).response?.aircraft;if(!data)throw Error('No model details found');aircraftInfo.set(id,{at:Date.now(),data});if(infoSelection===id)showInfo(data);
 }catch(e){if(controller.signal.aborted&&infoSelection!==id)return;const message='Model/photo unavailable · offline or no listing';aircraftInfo.set(id,{at:Date.now()-3540000,message});if(infoSelection===id)infoText(message);}finally{clearTimeout(timer);while(aircraftInfo.size>64)aircraftInfo.delete(aircraftInfo.keys().next().value);}
}
$('aircraftPhoto').onerror=()=>{$('aircraftPhoto').hidden=true;};
$('onlineDetails').checked=prefs.onlineDetails;$('onlineDetails').onchange=()=>{prefs.onlineDetails=$('onlineDetails').checked;savePrefs();infoSelection='';lookupAircraft(selectedAircraft());};
function tcpUI(){const t=state?.tcp;if(!t)return;setText('tcpStatus',(t.enabled?(t.error?'Listener error '+t.error:state.ip+':'+t.port+' · '+t.format.toUpperCase()+' · '+t.clients+'/2 clients'):'TCP output disabled')+' · '+t.dropped+' queue drops · '+t.slowClients+' slow clients disconnected');}
$('tcpForm').onsubmit=async e=>{e.preventDefault();try{await post('/api/tcp',{enabled:$('tcpEnabled').checked,port:Number($('tcpPort').value),format:$('tcpFormat').value});setText('tcpResult','Saved. Updating listener…');clearTimeout(pollTimer);setTimeout(poll,500);}catch(e){setText('tcpResult',e.message);}};
function correctionUI(){if(!state)return;const mode=['Off','Observe','Apply'][state.correctionMode??0];setText('correctionSummary','Correction: '+mode+' · '+fmt(state.corrected??0)+' applied');setText('correctionStats','Clean: '+fmt(state.valid)+' · Eligible recoveries: '+fmt(state.repairEligible??0)+' · Applied: '+fmt(state.corrected??0)+' · Rejected by safeguards: '+fmt(state.repairRejected??0)+' · Later clean confirmations: '+fmt(state.repairConfirmed??0)+(state.hardwareManchester?'':' · inactive: hardware Manchester is off'));}
$('correctionForm').onsubmit=async e=>{e.preventDefault();try{await post('/api/correction',{mode:Number($('correctionMode').value)});setText('correctionResult','Correction mode saved on the ESP32-S3.');clearTimeout(pollTimer);setTimeout(poll,300);}catch(e){setText('correctionResult',e.message);}};
