const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
// Runs the real bridge, history hook and Browser component logic without a DAW or DOM.
const root=path.resolve(__dirname,'..');
const ts=require(root+'/node_modules/typescript');
let hooks=[],index=0,effects=[],dirty=false,currentProps,currentTree,component,clock=0,timerSeq=0;
const timers=new Map(),listeners=new Map(),sends=[];
const setTimer=(fn,delay=0)=>{timers.set(++timerSeq,{fn,at:clock+delay});return timerSeq};
const clearTimer=id=>timers.delete(id);
const window={__JUCE__:{backend:{
  addEventListener(name,fn){const id=++timerSeq;listeners.set(id,{name,fn});return [name,id]},
  removeEventListener(handle){listeners.delete(handle[1])},
  emitEvent(name,data){sends.push(data)},
}},setTimeout:setTimer,clearTimeout:clearTimer};
const same=(a,b)=>a&&b&&a.length===b.length&&a.every((v,i)=>Object.is(v,b[i]));
const react={
 useState(initial){const i=index++;if(!hooks[i]){const value=typeof initial==='function'?initial():initial;hooks[i]={value,set(value){const next=typeof value==='function'?value(hooks[i].value):value;if(!Object.is(next,hooks[i].value)){hooks[i].value=next;dirty=true}}}}return[hooks[i].value,hooks[i].set]},
 useRef(value){const i=index++;if(!hooks[i])hooks[i]={current:value};return hooks[i]},
 useMemo(fn,deps){const i=index++;if(!hooks[i]||!same(hooks[i].deps,deps))hooks[i]={value:fn(),deps};return hooks[i].value},
 useCallback(fn,deps){return react.useMemo(()=>fn,deps)},
 useEffect(fn,deps){const i=index++;if(!hooks[i]||!same(hooks[i].deps,deps)){const old=hooks[i];hooks[i]={deps,cleanup:old?.cleanup};effects.push(()=>{old?.cleanup?.();hooks[i].cleanup=fn()})}},
 memo:fn=>fn,
};
const jsx=(type,props)=>({type,props:props||{}});
const context=vm.createContext({console,window,localStorage:{getItem:()=>null,setItem(){}},setTimeout:setTimer,clearTimeout:clearTimer,performance:{now:()=>clock},URLSearchParams,URL,File:global.File,AudioContext:class{}});
const cache=new Map();
function load(file){file=path.resolve(file);if(!path.extname(file))file+=fs.existsSync(file+'.ts')?'.ts':'.tsx';if(cache.has(file))return cache.get(file).exports;
 const module={exports:{}};cache.set(file,module);
 const requireLocal=id=>{
  if(id==='react')return react;
  if(id==='react/jsx-runtime')return{jsx,jsxs:jsx,Fragment:'fragment'};
  if(id==='@phosphor-icons/react')return new Proxy({},{get:(_,k)=>String(k)});
  if(id.endsWith('.css'))return{__esModule:true,default:new Proxy({},{get:(_,k)=>String(k)})};
  if(id.includes('/PadsView/'))return{PadFooterBar:'PadFooterBar'};
  if(id.includes('/Dropdown/'))return{Dropdown:'Dropdown'};
  if(id.includes('/Knob/'))return{Knob:'Knob'};
  return load(path.resolve(path.dirname(file),id));
 };
 const js=ts.transpileModule(fs.readFileSync(file,'utf8'),{compilerOptions:{module:ts.ModuleKind.CommonJS,target:ts.ScriptTarget.ES2022,jsx:ts.JsxEmit.ReactJSX,esModuleInterop:true}}).outputText;
 vm.runInContext(`(function(require,module,exports){${js}\n})`,context,{filename:file})(requireLocal,module,module.exports);return module.exports;
}
function render(props=currentProps){currentProps=props;let n=0;do{if(n++>30)throw Error('effect loop');dirty=false;index=0;effects=[];currentTree=component(currentProps);for(const f of effects)f()}while(dirty);return currentTree;}
function nodes(node,out=[]){if(arguments.length===0)node=currentTree;if(!node)return out;if(Array.isArray(node)){for(const n of node)nodes(n,out);return out;}if(typeof node==='object'){out.push(node);nodes(node.props?.children,out);}return out;}
function text(node){if(node==null)return '';if(Array.isArray(node))return node.map(text).join('');if(typeof node==='object')return text(node.props?.children);return String(node)}
function find(fn){const n=nodes().find(fn);if(!n)throw Error('node not found');return n}
function click(fn){const n=find(fn);if(n.props.disabled)throw Error('disabled');n.props.onClick();render();}
function emit(name,value){for(const l of [...listeners.values()])if(l.name===name)l.fn(value);render();}
async function tick(ms){clock+=ms;let n=0;while([...timers.values()].some(t=>t.at<=clock)){if(n++>100)throw Error('timer loop');for(const [id,t] of [...timers])if(t.at<=clock){timers.delete(id);await t.fn()}render();}}
function last(type){return sends.filter(x=>x.type===type).at(-1)?.payload;}
let passed=0,failed=0;
function check(ok,name,detail=''){console.log(`${ok?'PASS':'FAIL'} ${name} | ${detail}`);ok?passed++:failed++;}
const layers=load(root+'/src/utils/layerView.ts');const bridge=load(root+'/src/utils/juceBridge.ts');
const {INITIAL_PADS}=load(root+'/src/data/padData.ts');
const pad=structuredClone(INITIAL_PADS[0]);pad.layers=[layers.layerFromFlat(pad)];
const pad2=structuredClone(pad);pad2.layers.push({...pad2.layers[0],volume:.21,pan:-.31,pitch:3,hold:.12,decay:.6});pad2.selectedLayerIndex=1;
for(const patch of [{volume:.3},{pan:.5},{pitch:7},{fine:20},{hold:0},{decay:1}]){const r=layers.routeLayerPatch(pad2,patch);Object.assign(pad2,r);}
check(pad2.layers[1].volume===.3&&pad2.layers[1].pan===.5&&pad2.layers[1].pitch===7&&pad2.layers[1].fine===20,'sequential sibling Layer edits preserve all values');
check(pad2.layers[0].volume===pad.volume,'editing Layer 2 preserves MAIN');
let added=layers.patchAddLayer(pad2);check(added.layers.length===3&&!added.layers[2].sampleStock&&!added.layers[2].roundRobin,'new Layer clears variation bank and RR');
const mainSelected={...pad2,selectedLayerIndex:0};const fromMain=layers.patchAddLayer(mainSelected);
bridge.sendPadPatchToJuce(0,fromMain,mainSelected);
check(last('addLayer')?.copyFromIndex===0&&fromMain.layers[2].volume===mainSelected.layers[0].volume,'adding a layer copies the selected MAIN in both UI and native engine');
let capped={...pad2,layers:Array.from({length:8},()=>({...pad2.layers[0]}))};check(layers.patchAddLayer(capped)===null,'ninth layer rejected');
const remove=layers.patchRemoveLayer(pad2,0);check(remove.hold===pad2.layers[1].hold&&remove.decay===pad2.layers[1].decay,'remove MAIN mirrors new Hold/Decay into flat UI fields',JSON.stringify({hold:remove.hold,decay:remove.decay}));
const equal=load(root+'/src/utils/undoState.ts').undoPadsEqual;
const restored=structuredClone(pad2),current=structuredClone(restored);
restored.nativeUndoState='native-pad';restored.polyphony=3;restored.layers.forEach((layer,i)=>layer.nativeUndoState='native-layer-'+i);
current.layers[1].pitch=12;current.layers[1].hold=.9;current.layers[1].roundRobin=true;
check(bridge.sendUndoPadsToJuce([restored],[current]),'Undo accepts complete native snapshots');
const undoBatch=last('restoreUndo');
const command=(type,layerIndex)=>undoBatch.commands.find(c=>c.type===type&&(layerIndex===undefined||c.payload.layerIndex===layerIndex));
check(undoBatch.states[0].state==='native-pad'&&undoBatch.states[0].layers[1].state==='native-layer-1','Undo preserves opaque pad and movable layer sample-bank snapshots');
check(command('setLayerPitch',1)?.payload.value===restored.layers[1].pitch&&command('setLayerHold',1)?.payload.value===restored.layers[1].hold&&command('setLayerRoundRobin',1)?.payload.value===false,'Undo sends layer pitch, envelope and RR to native engine');
check(command('setLayerFxChain',1)&&command('setLayerEq',1)&&command('setLayerSampleTrim',1)&&command('setPitch')&&command('setPolyphony'),'Undo sends FX, EQ, trim, MAIN pitch and polyphony');
check(sends.at(-1).type==='restoreUndo'&&!undoBatch.commands.some(c=>['addLayer','removeLayer','clearPadSample'].includes(c.type)),'Undo sends one atomic transaction without destructive intermediate actions');
const missing={...restored,nativeUndoState:undefined};const count=sends.length;
check(!bridge.sendUndoPadsToJuce([missing],[current])&&sends.length===count,'incomplete Undo snapshot never sends partial changes');
const echo=structuredClone(restored);echo.nativeUndoState='refreshed-pad';echo.layers[1].nativeUndoState='refreshed-layer';echo.layers[1].volume=Math.fround(restored.layers[1].volume);echo.waveformPeaks=[.9];echo.selectedLayerIndex=0;
check(equal([restored],[echo]),'native float32 echo, waveform refresh and selection are not extra Undo actions');
const realEdit=structuredClone(echo);realEdit.layers[1].pitch+=1;check(!equal([restored],[realEdit]),'real layer parameter changes remain distinguishable');
const specs=load(root+'/src/data/parameterSpecs.ts');const trims=load(root+'/src/utils/sampleTrim.ts');
const longPad={...pad,sampleLengthMs:2000,startMs:100,endMs:1800};const reset=specs.resettablePadSettings(0,longPad.sampleLengthMs);const normalized=trims.normalizePadTrimPatch(longPad,reset);
bridge.sendPadPatchToJuce(0,{...reset,...normalized},longPad);const resetTrim=last('setSampleTrim');
check(normalized.endMs===2000&&resetTrim?.endPosition===1,'current UI Reset Pad Settings restores the full sample, not fixed 500ms',JSON.stringify({endMs:normalized.endMs,endPosition:resetTrim?.endPosition}));

// Mount the real SampleBrowser function with deterministic React hook scheduling.
component=load(root+'/src/components/SampleBrowser/SampleBrowser.tsx').SampleBrowser;
let props={active:true,pads:[pad],selectedIndex:0,onSelectPad(){},onSelectLayer(){},onSelectVariation(){},onImportFile:async()=>{},masterKnob:.75,onMasterKnobChange(){},masterClipHit:false,onResetMasterClip(){}};
render(props);check(!!last('browserInit'),'Browser requests preferences on activation');
emit('browserPreferences',{recentPaths:['/samples'],gain:.33,autoAudition:false});
check(last('browserOpenDirectory')?.path==='/samples','saved folder opens on activation');
function listing(req,path='/samples'){emit('browserDirectory',{requestId:req,path,name:'samples',parent:'/',entries:[{name:'kick.wav',path:'/samples/kick.wav',directory:false},{name:'snare.wav',path:'/samples/snare.wav',directory:false}]});}
listing(last('browserOpenDirectory').requestId);
click(n=>n.props.role==='option'&&text(n)==='kick.wav');
check(!last('browserPreview'),'AUTO off selects without starting audition');
click(n=>n.props['aria-label']==='Play selected sample');
(async()=>{
 await tick(0);const oldPreview=last('browserPreview').requestId;
 check(last('browserPreview').path==='/samples/kick.wav','manual audition targets selected sample');
 emit('browserResult',{kind:'preview',requestId:oldPreview});
 check(nodes().some(n=>n.props.className==='goldIcon'),'preview success marks selected file as playing');
 click(n=>n.props['aria-label']==='Stop audition');emit('browserResult',{kind:'preview',requestId:oldPreview});
 check(!nodes().some(n=>n.props.className==='goldIcon'),'late preview success after Stop is ignored');
 click(n=>n.type==='button'&&text(n)==='ADD AS VARIATION 1');const oldImport=last('browserImport').requestId;
 check(last('browserImport').index===0&&last('browserImport').layerIndex===0&&last('browserImport').expectedCount===0,'ADD captures exact destination and bank count');
 check(find(n=>n.type==='button'&&text(n)==='LOADING SAMPLE…').props.disabled,'loading disables a second import');
 render({...props,active:false});emit('browserResult',{kind:'import',requestId:oldImport});
 check(!text(currentTree).includes('Sample loaded'),'import success after leaving Browser is ignored');
 render(props);emit('browserPreferences',{recentPaths:['/samples'],gain:.33,autoAudition:false});listing(last('browserOpenDirectory').requestId);
 emit('browserResult',{kind:'import',requestId:oldImport,error:'old error'});
 check(!text(currentTree).includes('old error'),'old import error after reopening is ignored');
 click(n=>n.props.role==='option'&&text(n)==='snare.wav');click(n=>n.type==='button'&&text(n)==='ADD AS VARIATION 1');const fresh=last('browserImport').requestId;
 emit('browserResult',{kind:'import',requestId:fresh});check(text(currentTree).includes('Sample loaded into the selected layer.'),'fresh import success is accepted');
 emit('browserResult',{kind:'import',requestId:fresh,error:'duplicate error'});check(!text(currentTree).includes('duplicate error'),'duplicate completion is consumed once');
 const req=last('browserOpenDirectory').requestId;render({...props,active:false});render(props);emit('browserPreferences',{recentPaths:['/samples'],gain:.33,autoAudition:false});
 emit('browserDirectory',{requestId:req,path:'/wrong',name:'WRONG',parent:'',entries:[]});check(!text(currentTree).includes('WRONG'),'stale directory reply after reopening is ignored');
 listing(last('browserOpenDirectory').requestId);click(n=>n.props.role==='option'&&text(n)==='kick.wav');
 click(n=>n.props['aria-label']==='Play selected sample');await tick(0);const errReq=last('browserPreview').requestId;
 emit('browserResult',{kind:'preview',requestId:errReq,error:'decode failure'});check(text(currentTree).includes('decode failure'),'preview decode errors are surfaced');
 await tick(120);check(last('browserSettings')?.gain===.33&&last('browserSettings')?.autoAudition===false,'saved audition preferences retained when persisted');
 render({...props,active:false});for(const h of hooks)h?.cleanup?.();check(!listeners.size,'unmount removes all native subscriptions');
 // Exercise the real history hook, including Undo before its debounce expires.
 hooks=[];index=0;effects=[];timers.clear();dirty=false;
 const historyHook=load(root+'/src/utils/useUndoRedo.ts').useUndoRedo;let restoredValues=[];
 component=props=>historyHook(props.value,next=>{currentProps={...currentProps,value:next};dirty=true},{max:2,isEqual:equal,onRestore:next=>restoredValues.push(next)});
 const initial=[restored];render({value:initial});
 check(!currentTree.canUndo&&!currentTree.canRedo,'history starts empty');
 const edit=(value)=>render({value});
 const changed=structuredClone(initial);changed[0].layers[1].pitch=9;edit(changed);
 check(currentTree.canUndo,'pending gesture can be undone immediately');
 currentTree.undo();render();check(currentProps.value[0].layers[1].pitch===initial[0].layers[1].pitch&&restoredValues.length===1&&currentTree.canRedo,'immediate Undo restores once and offers Redo');
 await tick(500);check(!currentTree.canUndo,'cancelled gesture timer cannot reinsert an Undo entry');
 currentTree.redo();render();check(currentProps.value[0].layers[1].pitch===9&&restoredValues.length===2,'Redo restores the complete gesture once');
 const nativeEcho=structuredClone(currentProps.value);nativeEcho[0].nativeUndoState='native-echo';nativeEcho[0].layers[1].volume=Math.fround(nativeEcho[0].layers[1].volume);edit(nativeEcho);await tick(500);
 currentTree.undo();render();check(currentProps.value[0].layers[1].pitch===initial[0].layers[1].pitch,'native echo did not insert a duplicate history entry');
 const newEdit=structuredClone(currentProps.value);newEdit[0].layers[1].pitch=4;edit(newEdit);await tick(400);check(!currentTree.canRedo,'new edit after Undo discards abandoned Redo branch');
 currentTree.clearHistory(currentProps.value);render();check(!currentTree.canUndo&&!currentTree.canRedo,'clearHistory removes pending and committed entries');
 const afterClear=structuredClone(currentProps.value);afterClear[0].layers[1].pitch=8;edit(afterClear);currentTree.undo();render();check(currentProps.value[0].layers[1].pitch===4,'first edit after clearing history can be undone');
 currentTree.clearHistory(currentProps.value);render();
 for(const pitch of [5,6,7]){const value=structuredClone(currentProps.value);value[0].layers[1].pitch=pitch;edit(value);await tick(400);}
 currentTree.undo();render();currentTree.undo();render();check(currentProps.value[0].layers[1].pitch===5&&!currentTree.canUndo,'history cap keeps the latest two complete gestures');
 for(const h of hooks)h?.cleanup?.();timers.clear();
 console.log(`SUMMARY passed=${passed} failed=${failed}`);process.exitCode=failed?1:0;
})().catch(err=>{console.error(err);process.exitCode=1});
