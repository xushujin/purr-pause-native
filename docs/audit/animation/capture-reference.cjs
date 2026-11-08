const {_electron}=require('playwright');
const path=require('path');
const fs=require('fs');
const root=path.resolve(process.argv[2]||path.join(__dirname,'../../..'));
const electron=process.env.PURR_REFERENCE_ELECTRON;
if(!electron) throw new Error('Set PURR_REFERENCE_ELECTRON to the Electron 33.2.1 executable');
const output=path.resolve(process.env.PURR_REFERENCE_OUTPUT||__dirname);
fs.mkdirSync(output,{recursive:true});
const main=path.join(output,'reference-main.cjs');
fs.writeFileSync(main,"const {app,BrowserWindow}=require('electron');let win;app.whenReady().then(()=>{win=new BrowserWindow({width:1280,height:800,useContentSize:true,show:false,webPreferences:{contextIsolation:false,nodeIntegration:false}});win.loadURL('about:blank');});");
(async()=>{
const browser=await _electron.launch({executablePath:electron,args:['--no-sandbox',main]});
const page=await browser.firstWindow();
await page.addInitScript(()=>{ window.electronAPI={onStartAnimation(){},overlayShown(){},dismiss(){},snooze(){}};window.__raf=[];window.requestAnimationFrame=fn=>window.__raf.push(fn);Object.defineProperty(performance,'now',{value:()=>0});});
await page.goto(require('url').pathToFileURL(path.join(root,'tests/reference/renderer/index.html')).href);
const result=[];
for(const mode of ['walk-center','walk-zoom','walk-flat','fade-center']) for(const aspect of [16/9,1,0.5,3]) for(const progress of [0,0.25,0.5,0.75,1]){
result.push(await page.evaluate(({mode,aspect,progress})=>{
const walk=document.getElementById('catWalkVideo'),idle=document.getElementById('catIdleVideo');
for(const v of [walk,idle]){Object.defineProperty(v,'videoWidth',{configurable:true,value:200*aspect});Object.defineProperty(v,'videoHeight',{configurable:true,value:200});Object.defineProperty(v,'duration',{configurable:true,value:4});Object.defineProperty(v,'readyState',{configurable:true,value:4});v.load=()=>{};v.play=()=>Promise.resolve();}
window.__raf=[];startAnimation({animationMode:mode,breakMinutes:5,videos:{walk:'',idle:''}});
if(mode!=='fade-center') walk.dispatchEvent(new Event('loadedmetadata'));
if(mode!=='fade-center') {const initial=__raf.shift();if(initial)initial(0);}
const callback=__raf.shift();if(callback)callback((mode==='fade-center'?3000:4000)*progress);
const el=document.getElementById(mode==='fade-center'?'catIdle':'catContainer');
// fake intrinsic media size to preserve actual replaced-element aspect in the DOM.
walk.style.width=200*aspect+'px';
const r=el.getBoundingClientRect();
return{mode,aspect,progress,width:innerWidth,height:innerHeight,bounds:[r.x,r.y,r.width,r.height],opacity:Number(getComputedStyle(el).opacity),videoFit:getComputedStyle(idle).objectFit};
},{mode,aspect,progress}));}
require('fs').writeFileSync(path.join(output,'electron33-cat-geometry.json'),JSON.stringify(result,null,2));
await page.goto(require('url').pathToFileURL(path.join(root,'tests/reference/renderer/rocket-demo.html')).href+'?startY=380');
const rocket=[];
for(const ms of [0,120,560,1000,3640,5740,6500,7000]){
 await page.evaluate(ms=>{for(const a of document.getAnimations()){a.pause();a.currentTime=ms;}},ms);
 const v=await page.evaluate(()=>{const selectors=['.rocket-art','.rocket-group','.spark','.middle-flame','.badge-text','.rocket-label'];return selectors.map(s=>{const e=document.querySelector(s),r=e.getBoundingClientRect(),st=getComputedStyle(e);return{s,bounds:[r.x,r.y,r.width,r.height],matrix:st.transform,origin:st.transformOrigin,font:st.font,opacity:st.opacity,...(e.getBBox?{bbox:Object.fromEntries(["x","y","width","height"].map(k=>[k,e.getBBox()[k]]))}:{}),...(s==='.rocket-label'?{spans:[...e.querySelectorAll('tspan')].map(x=>({text:x.textContent,bbox:Object.fromEntries(["x","y","width","height"].map(k=>[k,x.getBBox()[k]]))}))}:{})};});});
 rocket.push({milliseconds:ms,elements:v});
 if(ms===1000)await page.screenshot({path:path.join(output,'electron33-rocket-1000.png'),omitBackground:true});
}
fs.writeFileSync(path.join(output,'electron33-rocket-geometry.json'),JSON.stringify(rocket,null,2)+'\n');
await browser.close();
fs.unlinkSync(main);
})();
