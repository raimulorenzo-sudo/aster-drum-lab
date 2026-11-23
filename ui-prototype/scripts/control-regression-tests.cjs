const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const root=path.resolve(__dirname,'..');
const React=require('react');
const {create,act}=require('react-test-renderer');
const ts=require(root+'/node_modules/typescript');
let clock=100,passed=0,failed=0;const sends=[],hostNodes=[];
class Events { constructor(){this.events=new Map()} addEventListener(t,f){if(!this.events.has(t))this.events.set(t,new Set());this.events.get(t).add(f)} removeEventListener(t,f){this.events.get(t)?.delete(f)} emit(t,e={}){for(const f of [...(this.events.get(t)||[])])f({pointerId:1,clientY:100,preventDefault(){},stopPropagation(){},...e})} }
const win=new Events();win.setTimeout=setTimeout;win.clearTimeout=clearTimeout;win.location={search:''};win.__JUCE__={backend:{emitEvent(t,p){sends.push(p)},addEventListener(){return['test',1]},removeEventListener(){}}};
const context=vm.createContext({console,window:win,localStorage:{getItem:()=>null,setItem(){}},performance:{now:()=>clock},URLSearchParams,URL,setTimeout,clearTimeout,requestAnimationFrame:()=>0,cancelAnimationFrame(){},ResizeObserver:class{observe(){}disconnect(){}},document:{addEventListener(){},removeEventListener(){},elementFromPoint(){return null}},File:global.File});
const cache=new Map();
function load(file){file=path.resolve(file);if(!path.extname(file))file+=fs.existsSync(file+'.ts')?'.ts':'.tsx';if(cache.has(file))return cache.get(file).exports;const module={exports:{}};cache.set(file,module);
 const req=id=>{if(id==='react')return React;if(id==='react/jsx-runtime')return require('react/jsx-runtime');if(id.endsWith('.css'))return{__esModule:true,default:new Proxy({},{get:(_,k)=>String(k)})};if(id==='@phosphor-icons/react')return new Proxy({},{get:()=>()=>null});if(id.includes('AutomationAssign/AutomationAssign'))return{useAutomationAssign:()=>({openTargetMenu(){},assignedSlotForTarget(){}})};return load(path.resolve(path.dirname(file),id))};
 const js=ts.transpileModule(fs.readFileSync(file,'utf8'),{compilerOptions:{module:ts.ModuleKind.CommonJS,target:ts.ScriptTarget.ES2022,jsx:ts.JsxEmit.ReactJSX,esModuleInterop:true}}).outputText;
 vm.runInContext(`(function(require,module,exports){${js}\n})`,context,{filename:file})(req,module,module.exports);return module.exports;}
function mock(element){const o=new Events();hostNodes.push({element,node:o});return Object.assign(o,{style:{setProperty(){}},setPointerCapture(){},releasePointerCapture(){},hasPointerCapture:()=>true,getBoundingClientRect:()=>({x:0,y:0,left:0,top:0,width:600,height:200}),closest:()=>null,querySelector:()=>null,scrollTo(){},setAttribute(){}})}
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
let eq;
function EqHarness(){const [v,set]=React.useState(structuredClone(NEUTRAL_EQ));eq=v;return React.createElement(EqBox,{eq:v,patchEq:f=>set(f),padIndex:0,layerIndex:0})}
let r=mount(EqHarness,{});act(()=>r.root.findAllByProps({role:'tab'}).find(n=>n.children.join('')==='HIGH MID').props.onClick());
check(slider(r,'GAIN').props['data-automation-target-id'].includes('HighMid'),'EQ band switch updates automation target',slider(r,'GAIN').props['data-automation-target-id']);
number(r,'GAIN','6');check(eq.highMid.gain===6&&eq.lowMid.gain===0,'EQ equal-value band switch edits selected band',JSON.stringify({lowMid:eq.lowMid.gain,highMid:eq.highMid.gain}));act(()=>r.unmount());
for(const [type,label,input,parameter,expected] of [['DRIVE','DRIVE','25%','amount',.25],['DRIVE','TONE','50%','tone',.5],['DRIVE','MIX','50%','mix',.5],['COMPRESSOR','MIX','50%','mix',.5],['TRANSIENT','ATK','+50','attack',.5],['TRANSIENT','SUS','-50','sustain',-.5]]){
 let pad=structuredClone(INITIAL_PADS[0]);pad.layers=[{...pad,fxChain:[makeFxSlot(type)]}];pad.selectedLayerIndex=0;
 function Harness(){const [p,set]=React.useState(pad);pad=p;return React.createElement(FxTab,{pad:p,padIndex:0,onChange:patch=>set(old=>({...old,...patch,layers:[{...old.layers[0],...patch}]}))})}
 r=mount(Harness,{});number(r,label,input);check(Math.abs(pad.layers[0].fxChain[0].params[parameter]-expected)<1e-6,`${type} ${label} accepts displayed units`,String(pad.layers[0].fxChain[0].params[parameter]));act(()=>r.unmount());
}
let changesA=[],changesB=[];r=mount(Knob,{label:'TEST',ownerKey:'A',value:.5,onChange:v=>changesA.push(v)});
act(()=>slider(r,'TEST').props.onPointerDown({button:0,detail:1,clientY:100,pointerId:1,preventDefault(){},currentTarget:mock()}));clock+=20;act(()=>win.emit('pointermove',{clientY:80}));const before=changesA.length;check(before===1,'drag emits a value before changing owner');
act(()=>r.update(React.createElement(Knob,{label:'TEST',ownerKey:'B',value:.5,onChange:v=>changesB.push(v)})));clock+=20;act(()=>win.emit('pointermove',{clientY:60}));check(changesA.length===before&&changesB.length===0,'changing owner cancels the old drag');act(()=>r.unmount());check(!(win.events.get('pointermove')?.size),'unmount removes pointer listeners');act(()=>win.emit('pointerup'));
const numeric=load(root+'/src/utils/numericInput.ts');check(numeric.parseNumericText('+25 ct')===25,'Fine input accepts displayed ct suffix');
// Exercise the real shared Knob lifecycle with React's memo/hooks enabled.
function pointer(r,label,y=100,id=1,button=0){act(()=>slider(r,label).props.onPointerDown({button,detail:1,clientY:y,pointerId:id,preventDefault(){},currentTarget:mock()}))}
function move(y,id=1){clock+=20;act(()=>win.emit('pointermove',{clientY:y,pointerId:id}))}
function eventCount(){return ['pointermove','pointerup','pointercancel','blur'].reduce((n,t)=>n+(win.events.get(t)?.size||0),0)}
for(const ending of ['pointerup','pointercancel','blur']){
 const changes=[];r=mount(Knob,{label:'TEST',value:.5,onChange:v=>changes.push(v)});pointer(r,'TEST');move(82);
 clock+=1;act(()=>win.emit('pointermove',{clientY:64}));act(()=>win.emit(ending,{clientY:64}));
 check(Math.abs(changes.at(-1)-.7)<1e-8,`${ending} commits the final throttled drag value`);
 check(eventCount()===0,`${ending} releases window listeners`);act(()=>r.unmount());
}
{
 const changes=[];r=mount(Knob,{label:'TEST',value:.5,onChange:v=>changes.push(v)});pointer(r,'TEST');move(10,2);act(()=>win.emit('pointerup',{pointerId:2,clientY:10}));act(()=>win.emit('pointercancel',{pointerId:2}));
 check(changes.length===0&&eventCount()>0,'foreign pointer does not change or end the active drag');move(82);act(()=>win.emit('pointerup',{clientY:82}));check(Math.abs(changes.at(-1)-.6)<1e-8,'original pointer still completes its drag');act(()=>r.unmount());
}
{
 const changes=[];r=mount(Knob,{label:'TEST',value:.5,onChange:v=>changes.push(v)});pointer(r,'TEST',100,1,2);move(10);check(changes.length===0&&eventCount()===0,'right click does not start a value drag');act(()=>r.unmount());
}
{
 const changes=[];r=mount(Knob,{label:'TEST',value:.5,onChange:v=>changes.push(v)});pointer(r,'TEST');act(()=>r.unmount());move(10);act(()=>win.emit('pointerup',{clientY:10}));check(changes.length===0&&eventCount()===0,'unmount cancels the gesture without a later callback');
}
{
 const changes=[];let props={label:'TEST',ownerKey:'A',value:.5,onChange:v=>changes.push(v)};r=mount(Knob,props);
 act(()=>slider(r,'TEST').props.onDoubleClick({preventDefault(){},stopPropagation(){}}));act(()=>r.root.findByType('input').props.onChange({currentTarget:{value:'.8'}}));act(()=>r.update(React.createElement(Knob,{...props,ownerKey:'B'})));
 check(!r.root.findAllByType('input').length&&!changes.length,'changing owner discards an uncommitted numeric draft');act(()=>r.unmount());
}
{
 let a=[],b=[];const props={label:'TEST',value:.5,onChange:v=>a.push(v)};r=mount(Knob,props);
 act(()=>r.update(React.createElement(Knob,{...props,onChange:v=>b.push(v)})));number(r,'TEST','.6');check(!a.length&&b[0]===.6,'equal visual props still refresh the change callback');
 act(()=>r.update(React.createElement(Knob,{...props,defaultValue:.25,onReset:()=>b.push('new reset')})));act(()=>slider(r,'TEST').props.onPointerDown({button:0,detail:1,altKey:true,preventDefault(){}}));check(b.at(-1)==='new reset','reset uses the current reset callback');act(()=>r.unmount());
}
{
 let a=[],b=[];const props={label:'TEST',value:.5,onChange:v=>a.push(v)};r=mount(Knob,props);pointer(r,'TEST');move(82);act(()=>r.update(React.createElement(Knob,{...props,onChange:v=>b.push(v)})));move(64);act(()=>win.emit('pointerup',{clientY:64}));check(a.length===1&&Math.abs(b.at(-1)-.7)<1e-8,'an ongoing drag uses the latest callback for the same owner');act(()=>r.unmount());
}
{
 const changes=[];const props={label:'TEST',value:.5,onChange:v=>changes.push(v)};r=mount(Knob,props);
 const host=hostNodes.findLast(x=>x.element?.props.role==='slider'&&x.element.props['aria-label']==='TEST').node;
 act(()=>{for(let i=0;i<5;++i)host.emit('wheel',{deltaY:-1})});check(Math.abs(changes.at(-1)-.55)<1e-8,'rapid wheel events accumulate before the parent renders');
 act(()=>host.emit('wheel',{deltaY:1,shiftKey:true}));check(Math.abs(changes.at(-1)-.549)<1e-8,'wheel fine adjustment uses the smaller step');act(()=>r.unmount());
}
for(const [input,expected] of [['1.2k',1200],['2.4 kHz',2400],['500 Hz',500],['1,5K',1500],['NaN',null],['1e999 Hz',null]])check(numeric.parseFrequencyInput(input)===expected,`frequency input ${input}`);
for(const [input,expected] of [['4.0:1',4],['8',8],['-2',-2],['4:2',null],['Infinity',null]])check(numeric.parseRatioInput(input)===expected,`ratio input ${input}`);
for(const [input,expected] of [['-6 dB',-6],['-inf',-Infinity],['25 ct',25],['L 30',-.3],['R 20',.2],['C',0]])check((/^[LRC]/.test(input)?numeric.parsePanInput(input):numeric.parseNumericText(input))===expected,`existing numeric format ${input}`);
for(const [type,label,input,parameter,expected] of [['COMPRESSOR','RATIO','8:1','ratio',8],['FILTER','FREQ','1.2k','hpCutoff',1200]]){
 let pad=structuredClone(INITIAL_PADS[0]);pad.layers=[{...pad,fxChain:[makeFxSlot(type)]}];pad.selectedLayerIndex=0;
 function Harness(){const [p,set]=React.useState(pad);pad=p;return React.createElement(FxTab,{pad:p,padIndex:0,onChange:patch=>set(old=>({...old,...patch,layers:[{...old.layers[0],...patch}]}))})}
 r=mount(Harness,{});number(r,label,input);check(Math.abs(pad.layers[0].fxChain[0].params[parameter]-expected)<1e-4,`${type} ${label} accepts displayed format`,String(pad.layers[0].fxChain[0].params[parameter]));act(()=>r.unmount());
}
{
 let pad=structuredClone(INITIAL_PADS[0]);pad.layers=[{...pad,fxChain:[makeFxSlot('DRIVE')]}];pad.selectedLayerIndex=0;let current=pad;
 const props=i=>({pad:current,padIndex:i,onChange:p=>{current={...current,...p,layers:[{...current.layers[0],...p}]}}});
 r=mount(FxTab,props(0));sends.length=0;act(()=>r.update(React.createElement(FxTab,props(1))));number(r,'DRIVE','50%');
 const target=sends.find(x=>x.type==='setFxAutomationTargetValue')?.payload.targetId;
 check(target?.startsWith('pad02.')&&Math.abs(current.layers[0].fxChain[0].params.amount-.5)<1e-8,'equal-value FX Pad switch edits and automates the current Pad',target);act(()=>r.unmount());
}
{
 const changes=[];r=mount(Knob,{label:'TEST',value:.4,onChange:v=>changes.push(v)});
 for(const invalid of ['','garbage','NaN','Infinity'])number(r,'TEST',invalid);
 check(changes.length===0,'invalid numeric drafts leave the value unchanged');number(r,'TEST','999');number(r,'TEST','-999');check(changes[0]===1&&changes[1]===0,'numeric values are clamped at both ends');
 act(()=>slider(r,'TEST').props.onDoubleClick({preventDefault(){},stopPropagation(){}}));act(()=>r.root.findByType('input').props.onChange({currentTarget:{value:'.8'}}));act(()=>r.root.findByType('input').props.onKeyDown({key:'Escape'}));check(changes.length===2&&!r.root.findAllByType('input').length,'Escape cancels numeric editing');act(()=>r.unmount());
}
check(eventCount()===0,'all control cases leave no gesture listeners');
console.log(`SUMMARY passed=${passed} failed=${failed}`);process.exitCode=failed?1:0;
