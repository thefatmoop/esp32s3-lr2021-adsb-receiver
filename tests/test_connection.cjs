// Test the actual browser polling functions without a network or browser.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),path=require('node:path');
const root=path.resolve(__dirname,'../main');
const ui=fs.readFileSync(path.join(root,'ui.js'),'utf8'),live=fs.readFileSync(path.join(root,'live.js'),'utf8');
const polling=ui.slice(ui.indexOf('let pollTimer=null'),ui.indexOf('document.onvisibilitychange'));
const fast=live.slice(live.indexOf('async function livePoll()'),live.indexOf('\nlivePoll();'));
let now=20000,timers=[],requests=0;
const box={Date:{now:()=>now},state:{uptime:1},online:true,lastFetch:now,liveBusy:false,trailBusy:false,liveSupported:true,liveFailures:0,liveTimer:null,
 document:{hidden:false},AbortSignal:{timeout:()=>null},setTimeout:(fn,ms)=>{timers.push(ms);return 1;},
 setText:()=>{},gnssUI:()=>{},table:()=>{},draw:()=>{},$:()=>({classList:{remove:()=>{}}}),
 fetch:async()=>{requests++;throw Error('transient HTTP error');}};
vm.createContext(box);vm.runInContext(polling+'\n'+fast,box);
(async()=>{
 await vm.runInContext('poll()',box);assert.equal(box.online,true,'diagnostics failure must not invalidate fresh live data');
 now+=10001;await vm.runInContext('poll()',box);assert.equal(box.online,false,'a real outage becomes disconnected');
 box.online=true;box.lastFetch=now;box.liveBusy=true;requests=0;
 await vm.runInContext('poll()',box);assert.equal(requests,0,'diagnostics must yield to live request');
 box.liveBusy=false;box.trailBusy=true;
 await vm.runInContext('livePoll()',box);assert.equal(requests,0,'live request must yield to trail download');
 box.trailBusy=false;box.fetch=async()=>({ok:true,json:async()=>({uptime:2})});box.render=()=>{};timers=[];
 await vm.runInContext('livePoll()',box);assert.equal(timers.at(-1),500,'live cadence is 2 Hz');
 assert.equal(box.liveFailures,0);
 box.fetch=async()=>{throw Error('offline');};
 for(let i=0;i<4;i++)await vm.runInContext('livePoll()',box);
 assert.equal(timers.at(-1),2000,'outages back off instead of hammering receiver');
 console.log('PASS: transient failures, real outage, serialized polling, 2 Hz cadence, bounded retry backoff');
})().catch(e=>{console.error(e);process.exitCode=1;});
