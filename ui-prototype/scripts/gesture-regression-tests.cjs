const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const root=path.resolve(__dirname,'..');
const React=require('react');
const {create,act}=require('react-test-renderer');
const ts=require(root+'/node_modules/typescript');
let clock=100,passed=0,failed=0;const sends=[];
class Events { constructor(){this.events=new Map()} addEventListener(t,f){if(!this.events.has(t))this.events.set(t,new Set());this.events.get(t).add(f)} removeEventListener(t,f){this.events.get(t)?.delete(f)} emit(t,e={}){for(const f of [...(this.events.get(t)||[])])f({pointerId:1,clientY:100,preventDefault(){},stopPropagation(){},...e})} }
const win=new Events();win.setTimeout=setTimeout;win.clearTimeout=clearTimeout;win.location={search:''};win.__JUCE__={backend:{emitEvent(t,p){sends.push(p)},addEventListener(){return['test',1]},removeEventListener(){}}};
const context=vm.createContext({console,window:win,localStorage:{getItem:()=>null,setItem(){}},performance:{now:()=>clock},URLSearchParams,URL,setTimeout,clearTimeout,requestAnimationFrame:()=>0,cancelAnimationFrame(){},ResizeObserver:class{observe(){}disconnect(){}},document:{addEventListener(){},removeEventListener(){},elementFromPoint(){return null}},File:global.File});
const cache=new Map();
function load(file){file=path.resolve(file);if(!path.extname(file))file+=fs.existsSync(file+'.ts')?'.ts':'.tsx';if(cache.has(file))return cache.get(file).exports;const module={exports:{}};cache.set(file,module);
 const req=id=>{if(id.includes('/SettingsMenu/'))return{SettingsMenu:()=>null};if(id.endsWith('/App'))return{MASTER_UNITY:.75};if(id==='react-dom')return {createPortal:()=>null};if(id==='react')return React;if(id==='react/jsx-runtime')return require('react/jsx-runtime');if(id.endsWith('.css'))return{__esModule:true,default:new Proxy({},{get:(_,k)=>String(k)})};if(id==='@phosphor-icons/react')return new Proxy({},{get:()=>()=>null});if(id.includes('AutomationAssign/AutomationAssign'))return{AutomationModeButton:()=>null,useAutomationAssign:()=>({openTargetMenu(){},assignedSlotForTarget(){}})};return load(path.resolve(path.dirname(file),id))};
 const js=ts.transpileModule(fs.readFileSync(file,'utf8'),{compilerOptions:{module:ts.ModuleKind.CommonJS,target:ts.ScriptTarget.ES2022,jsx:ts.JsxEmit.ReactJSX,esModuleInterop:true}}).outputText;
 vm.runInContext(`(function(require,module,exports){${js}\n})`,context,{filename:file})(req,module,module.exports);return module.exports;}
function mock(){const o=new Events();return Object.assign(o,{dataset:{},style:{setProperty(){}},setPointerCapture(){},releasePointerCapture(){},hasPointerCapture:()=>true,getBoundingClientRect:()=>({x:0,y:0,left:0,top:0,width:600,height:200,bottom:200,right:600}),closest:()=>null,querySelector:()=>null,querySelectorAll:()=>[],scrollTo(){},setAttribute(){}})}
function mount(type,props){let r;act(()=>r=create(React.createElement(type,props),{createNodeMock:mock}));return r;}
function check(ok,name,detail=''){console.log(`${ok?'PASS':'FAIL'} ${name} ${detail}`);ok?passed++:failed++;}
function slider(r,label){return r.root.findAll(n=>n.type==='div'&&n.props.role==='slider'&&n.props['aria-label']===label)[0]}
function number(r,label,text){act(()=>slider(r,label).props.onDoubleClick({preventDefault(){},stopPropagation(){}}));act(()=>r.root.findByType('input').props.onChange({currentTarget:{value:text}}));act(()=>r.root.findByType('input').props.onKeyDown({key:'Enter'}));}
const {Knob}=load(root+'/src/components/Knob/Knob.tsx');
const {EqBox}=load(root+'/src/components/PadEditorTabs/EqBox.tsx');
const {NEUTRAL_EQ}=load(root+'/src/types.ts');
const {FxTab}=load(root+'/src/components/PadEditorTabs/tabs/FxTab.tsx');
const {makeFxSlot}=load(root+'/src/utils/fxChain.ts');
const {INITIAL_PADS}=load(root+'/src/data/padData.ts');

const {WaveformEditor}=load(root+'/src/components/WaveformEditor/WaveformEditor.tsx');
const {VelocityRangeSlider}=load(root+'/src/components/VelocityRangeSlider/VelocityRangeSlider.tsx');
let changes=[];let pad={...structuredClone(INITIAL_PADS[0]),sampleFileName:'test.wav',sampleFilePath:'/test.wav',sampleLengthMs:2000,startMs:0,endMs:2000,waveformPeaks:[0,.5,.2,0]};
let r=mount(WaveformEditor,{pad,padIndex:0,onChange:p=>changes.push(p),previewPlayback:{active:false,triggerId:0},onPreviewFinished(){}});
const handle=r.root.findAll(n=>n.type==='rect'&&typeof n.props.onPointerDown==='function')[0];
act(()=>handle.props.onPointerDown({button:0,detail:1,clientX:100,pointerId:1,preventDefault(){},stopPropagation(){},currentTarget:mock()}));
let before=changes.length;act(()=>r.unmount());clock+=20;act(()=>win.emit('pointermove',{clientX:150}));check(changes.length===before,'Waveform unmount cancels trim drag');check(!win.events.get('pointermove')?.size,'Waveform unmount removes drag listener');act(()=>win.emit('pointerup',{clientX:150}));
changes=[];r=mount(VelocityRangeSlider,{padIndex:0,layers:[{velocityMin:0,velocityMax:127}],activeLayerIndex:0,onSelectLayer(){},onChangeRange:(...p)=>changes.push(p),onAutoSplit(){},liveVelocity:null});
const thumb=r.root.findByProps({'aria-label':'MAIN velocity min'});act(()=>thumb.props.onPointerDown({button:0,clientX:100,pointerId:1,preventDefault(){},stopPropagation(){},target:mock(),currentTarget:mock()}));
before=changes.length;act(()=>r.unmount());clock+=20;act(()=>win.emit('pointermove',{clientX:150}));check(changes.length===before,'Velocity range unmount cancels drag');check(!win.events.get('pointermove')?.size,'Velocity range unmount removes drag listener');act(()=>win.emit('pointerup',{clientX:150}));
const {MixerView}=load(root+'/src/components/MixerView/MixerView.tsx');
changes=[];r=mount(MixerView,{pads:structuredClone(INITIAL_PADS),page:'A',selectedIndex:0,onSelect(){},onAudition(){},onPageChange(){},onChangePads:p=>changes.push(p),onChangePad:(...p)=>changes.push(p),masterKnob:.75,onMasterKnobChange(){},masterClipHit:false,onResetMasterClip(){},outputMode:'48Outs',onOutputModeChange(){}});
const fader=r.root.findAll(n=>n.type==='div'&&n.props.className==='faderTrack')[0];act(()=>fader.props.onPointerDown({button:0,detail:1,clientY:100,pointerId:1,preventDefault(){},stopPropagation(){},currentTarget:mock()}));
before=changes.length;act(()=>r.unmount());clock+=20;act(()=>win.emit('pointermove',{clientY:50}));check(changes.length===before,'Mixer unmount cancels fader');check(!win.events.get('pointermove')?.size,'Mixer unmount removes fader listener');act(()=>win.emit('pointerup',{clientY:50}));

const mixerProps={pads:structuredClone(INITIAL_PADS),page:'A',selectedIndex:0,onSelect(){},onAudition(){},onPageChange(){},onChangePad:(...p)=>changes.push({single:p}),onChangePads:p=>changes.push({batch:p}),masterKnob:.75,onMasterKnobChange(){},masterClipHit:false,onResetMasterClip(){},outputMode:'48Outs',onOutputModeChange(){}};
changes=[];r=mount(MixerView,mixerProps);
act(()=>r.root.findAllByType('article')[1].props.onMouseDown({shiftKey:false,metaKey:true,ctrlKey:false,stopPropagation(){}}));
const capture=r.root.findAll(n=>n.type==='div'&&n.props.className==='panBlock')[0];
const pan=r.root.findAll(n=>n.type==='div'&&n.props.role==='slider'&&n.props['aria-label']===undefined)[0];
const e={button:0,detail:1,clientY:100,pointerId:1,preventDefault(){},stopPropagation(){},currentTarget:mock()};
act(()=>capture.props.onPointerDownCapture(e));act(()=>pan.props.onPointerDown(e));clock+=20;act(()=>win.emit('pointermove',{clientY:90}));clock+=1;act(()=>win.emit('pointermove',{clientY:80}));act(()=>win.emit('pointerup',{clientY:80}));
const finalBatch=changes.at(-1)?.batch;
check(finalBatch?.length===2&&finalBatch.every(c=>Math.abs(c.patch.padPan-2/9)<1e-8),'Mixer multi-selection receives the final throttled Pan value');act(()=>r.unmount());
for(const ending of ['pointercancel','blur']){
 changes=[];r=mount(MixerView,mixerProps);const f=r.root.findAll(n=>n.type==='div'&&n.props.className==='faderTrack')[0];
 act(()=>f.props.onPointerDown({...e,currentTarget:mock()}));clock+=20;act(()=>win.emit('pointermove',{clientY:80}));act(()=>win.emit(ending));
 check(!win.events.get('pointermove')?.size,`Mixer fader ${ending} ends its gesture`);act(()=>r.unmount());
}
changes=[];const waveProps={pad,padIndex:0,onChange:p=>changes.push(p),previewPlayback:{active:false,triggerId:0},onPreviewFinished(){}};r=mount(WaveformEditor,waveProps);
const h=r.root.findAll(n=>n.type==='rect'&&typeof n.props.onPointerDown==='function')[0];
act(()=>h.props.onPointerDown({button:0,clientX:100,pointerId:1,preventDefault(){},stopPropagation(){},currentTarget:mock()}));before=changes.length;
act(()=>r.update(React.createElement(WaveformEditor,{...waveProps,padIndex:1})));clock+=20;act(()=>win.emit('pointermove',{clientX:160}));check(changes.length===before&&!win.events.get('pointermove')?.size,'changing Waveform Pad cancels the old trim gesture');act(()=>r.unmount());
check(['pointermove','pointerup','pointercancel','blur'].every(t=>!win.events.get(t)?.size),'all gesture cases leave no window drag listeners');
console.log('SUMMARY passed='+passed+' failed='+failed);process.exitCode=failed?1:0;
