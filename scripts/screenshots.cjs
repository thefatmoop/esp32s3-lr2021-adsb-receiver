// Documentation only: entirely synthetic aircraft and receiver data.
const {chromium}=require('playwright');
const fs=require('fs'),http=require('http'),path=require('path'),assert=require('assert');
const root=path.resolve(__dirname,'..'),main=path.join(root,'main'),out=path.join(root,'docs/images');
const aircraft=[
 ['A00001','DEMO101',40.92,-74.12,31000,454,65,-64,8],
 ['A00002','DEMO202',40.70,-73.63,18000,360,245,-72,0],
 ['A00003','DEMO303',40.50,-74.20,4200,190,350,-81,0],
 ['A00004','DEMO404',41.02,-73.83,37000,480,110,-77,0],
 ['A00005','DEMO505',40.48,-73.72,900,110,30,-83,4],
 ['A00006','DEMO606',40.84,-74.52,25000,425,210,-68,0]
].map(([address,callsign,lat,lon,altitude,speed,track,rssi,age],i)=>({address,callsign,lat,lon,altitude,speed,track,rssi,age,positionAge:age,verticalRate:i%2?0:512,nonIcao:false,messages:900+i*120,cleanMessages:850+i*120,correctedMessages:50}));
function snapshot(){return {uptime:3600,valid:48750,corrected:2250,correctionMode:2,repairCandidates:3000,repairEligible:2250,repairRejected:750,repairConfirmed:690,hardwareManchester:true,candidates:120000,badCrc:71250,badPairs:0,bestBadPairs:0,nearPairs:0,selftest:true,error:0,overflows:0,radioErrors:0,heap:102400,lastValidAge:.1,hf:false,gain:13,boost:7,threshold:40,thresholdRaw:40,bandwidth:0,captureBytes:32,irqWake:true,irqWakes:120000,fifoPeak:21,fifoFull:0,partialResets:0,fifoOverflowFlags:0,fifoUnderflowFlags:0,recoveries:0,tuning:'fixed',lastFrame:'',rssiNow:-82,rssiPeak:-61,token:'documentation-demo',ip:'192.0.2.10',ssid:'Demo Wi-Fi',aircraft,gnss:{hasPosition:true,fix:true,saved:false,lat:40.75,lon:-74.0,age:1,satellites:12,hdop:.9,error:0},tcp:{enabled:true,port:30005,format:'beast',clients:1,dropped:0,slowClients:0,error:0}};}
const server=http.createServer((req,res)=>{let body,type='application/json';const u=new URL(req.url,'http://localhost');
 if(u.pathname==='/api/state'||u.pathname==='/api/live')body=JSON.stringify(snapshot());
 else if(u.pathname==='/api/trails'){const a=aircraft.find(a=>a.address===u.searchParams.get('address'))||aircraft[0],angle=a.track*Math.PI/180;body=JSON.stringify({uptime:3600,points:Array.from({length:16},(_,i)=>{const n=15-i;return [a.lat-n*.008*Math.cos(angle),a.lon-n*.011*Math.sin(angle),a.altitude,5+n*15];})});}
 else {const name=u.pathname==='/'?'ui.html':u.pathname.slice(1);if(!['ui.html','ui.js','live.js','style.css','world.json'].includes(name)){res.writeHead(404);res.end();return;}body=fs.readFileSync(path.join(main,name));type=name.endsWith('.html')?'text/html':name.endsWith('.js')?'text/javascript':name.endsWith('.css')?'text/css':'application/json';}
 res.setHeader('Content-Type',type);res.end(body);
});
(async()=>{fs.mkdirSync(out,{recursive:true});await new Promise(r=>server.listen(0,'127.0.0.1',r));
 const browser=await chromium.launch({headless:true,...(process.env.BROWSER_CHANNEL?{channel:process.env.BROWSER_CHANNEL}:{})});try{
  const page=await browser.newPage({viewport:{width:1440,height:900},deviceScaleFactor:1}),errors=[];
  page.on('pageerror',e=>errors.push(e.message));await page.route('**/*',r=>r.request().url().startsWith('http://127.0.0.1:')?r.continue():r.abort());
  await page.goto('http://127.0.0.1:'+server.address().port);await page.waitForFunction(()=>mapDetail&&state?.aircraft.length===6);
  await page.evaluate(()=>{center=[-74,40.75];zoomLevel=9;prefs.onlineDetails=false;document.querySelector('.brand small').textContent='DEMO · FICTIONAL LOCATION & AIRCRAFT';draw();});
  await page.waitForTimeout(4000);await page.screenshot({path:path.join(out,'webui-desktop.png')});
  await page.setViewportSize({width:430,height:880});await page.evaluate(()=>{center=[-74,40.75];zoomLevel=8.5;draw();});await page.waitForTimeout(300);await page.screenshot({path:path.join(out,'webui-mobile.png')});
  await page.locator('#aircraftTab').click();await page.screenshot({path:path.join(out,'webui-aircraft.png')});
  assert.deepEqual(errors,[]);console.log('PASS synthetic desktop, mobile map and aircraft screenshots; no external data requested');
 }finally{await browser.close();server.close();}
})().catch(e=>{console.error(e);server.close();process.exit(1)});
