import assert from 'node:assert/strict';
import {Client,Renderer,identity,byteOffset,utf16Offset,executeService} from '../backends/web/renderer.mjs';
const tick=()=>new Promise(resolve=>setTimeout(resolve,0));
assert.equal(byteOffset('aé😃z',4),7);assert.equal(utf16Offset('aé😃z',7),4);assert.equal(utf16Offset('aé😃z',5),2);
assert.notEqual(identity({id:'a',generation:'1'}),identity({id:'a',generation:'2'}));

let fail=true,effects=0,serverAck=0n;const requests=[],states=[];
const client=new Client(async envelope=>{
  requests.push(envelope);
  if(BigInt(envelope.seq)>serverAck){serverAck=BigInt(envelope.seq);++effects;}
  if(fail){fail=false;throw Error('lost response after commit');}
  return {epoch:'e',ack:String(serverAck),snapshot:{value:effects},error:''};
},state=>states.push(state));
client.accept({epoch:'e',ack:'0',snapshot:{value:0}});
const first=client.send({type:'activate'});await tick();assert.equal(client.failed,true);assert.equal(effects,1);
client.retry();await first;assert.deepEqual(requests[0],requests[1]);assert.equal(effects,1);
const second=client.send(snapshot=>({type:'edit',base:snapshot.value}));await second;
assert.equal(requests.at(-1).operation.base,1);assert.equal(effects,2);
assert.equal(client.accept({epoch:'e',ack:'1',snapshot:{value:99}}),false);
assert.throws(()=>client.accept({epoch:'new',ack:'2',snapshot:{}}),/Session changed/);
const resumed=[],backpressure=[];
const paused=new Client(async envelope=>{
  resumed.push(envelope);return {epoch:'paused',ack:envelope.seq,snapshot:{},error:''};
},()=>{},message=>backpressure.push(message));
paused.accept({epoch:'paused',ack:'0',snapshot:{}});paused.failed=true;
const flooded=Array.from({length:500},(_,value)=>paused.send({type:'edit',value},'same-editor').then(()=>true,()=>false));
assert.equal(paused.queue.length,1);assert.equal(paused.queue[0].completions.length,128);
assert.equal(paused.queue[0].operation.value,127,'rejected coalesced input must not replace the last accepted operation');
const otherTasks=Array.from({length:127},(_,value)=>paused.send({type:'other',value},`other-${value}`));
await assert.rejects(paused.send({type:'overflow'}),/Input queue is full/);
assert.equal(paused.queue.length,128);assert(paused.queue.every(task=>task.completions.length<=128));
assert.equal(backpressure.filter(message=>message.includes('Input queue is full')).length,373);
paused.retry();const accepted=await Promise.all(flooded);await Promise.all(otherTasks);
assert.equal(accepted.filter(Boolean).length,128);assert.equal(resumed.length,128);
assert.equal(resumed[0].operation.value,127);assert.equal(paused.queue.length,0);assert.equal(paused.inflight,null);
assert.equal((await executeService({id:'1',kind:0})).status,'error');
assert.equal((await executeService({id:'2',kind:2,title:'Prompt',value:'x'},{})).status,'error');

class Element {
  constructor(tag,document){this.tagName=tag;this.ownerDocument=document;this.children=[];this.dataset={};this.attributes={};this.events={};this.style={setProperty:(name,value)=>{this.style[name]=value;}};this.scrollLeft=0;this.scrollTop=0;this.value='';}
  append(...children){for(const child of children){this.children.push(child);child.parent=this;child.isConnected=true;}}
  replaceChildren(){this.children=[];}
  remove(){if(this.parent)this.parent.children=this.parent.children.filter(child=>child!==this);}
  setAttribute(key,value){this.attributes[key]=value;}
  addEventListener(name,callback){(this.events[name]??=[]).push(callback);}
  emit(name,event={}){for(const callback of this.events[name]??[])callback(event);}
  focus(){this.ownerDocument.activeElement=this;}
  select(){this.selectionStart=0;this.selectionEnd=this.value.length;}
  showModal(){this.open=true;}
  close(){this.open=false;this.emit('close');}
  setSelectionRange(start,end,direction){this.selectionStart=start;this.selectionEnd=end;this.selectionDirection=direction;}
  getBoundingClientRect(){return {left:0,top:0,width:100,height:18};}
  getContext(){return {createImageData:(w,h)=>({data:new Uint8ClampedArray(w*h*4)}),putImageData:()=>{}};}
}
const document={createElement(tag){return new Element(tag,this);},addEventListener(){}};document.body=document.createElement('body');
const promptResult=executeService({id:'3',kind:2,title:'Generic prompt',value:'',byteLimit:'3'},{document});
let dialog=document.body.children.at(-1),form=dialog.children[0],input=form.children[0].children[1];
input.value='éé';form.emit('submit',{preventDefault(){}});assert.equal(dialog.open,true);
assert.match(form.children[1].textContent,/byte limit/);
input.value='é';form.emit('submit',{preventDefault(){}});assert.equal((await promptResult).value,'é');
const cancelledPrompt=executeService({id:'4',kind:2,title:'Cancel prompt',value:'',byteLimit:'9'},{document});
dialog=document.body.children.at(-1);dialog.emit('cancel',{preventDefault(){}});assert.equal((await cancelledPrompt).status,'cancelled');
const root=document.createElement('main'),pages=document.createElement('nav'),operations=[];
const renderer=new Renderer(root,pages,async operation=>{operations.push(operation);return {};},document);
const common={key:{id:'any application id',generation:'9'},kind:5,bounds:[27,48,150,24],clip:[30,48,140,24],visible:true,enabled:true,inModal:true,label:'Editor',text:'héllo',font:{size:14,bold:false,tone:0},help:'',accessibleName:'',placeholder:'',options:[],actions:[],records:[],readOnly:false,selection:[0,0],scroll:[0,0],multiline:false,submit:1,contentSize:[0,0],checked:false};
const snapshot={title:'Fixture',width:640,height:480,palette:{text:[1,2,3]},pages:[{id:'arbitrary page',label:'One',bounds:[0,456,640,24],selected:true,enabled:true}],activePage:'arbitrary page',widgets:[common],focus:null,closed:false,measurements:[],keyBindings:[]};
renderer.render(snapshot);const node=renderer.nodes.get(identity(common.key)),editor=node.control;
assert.equal(node.node.style.left,'27px');assert.equal(node.node.style.clipPath,'inset(0px 7px 0px 3px)');assert.equal(pages.children[0].style.top,'456px');
let acknowledgeFocus;
const focusRoot=document.createElement('main'),focusPages=document.createElement('nav');
const focusRenderer=new Renderer(focusRoot,focusPages,()=>new Promise(resolve=>{acknowledgeFocus=resolve;}),document);
const earlier={...common,key:{id:'previous focus',generation:'1'},kind:2};
const focusSnapshot={...snapshot,widgets:[common,earlier]};
focusRenderer.render(focusSnapshot);const focusedEditor=focusRenderer.nodes.get(identity(common.key)).control;
focusedEditor.focus();focusedEditor.emit('focus');
focusRenderer.render({...focusSnapshot,focus:earlier.key});
assert.equal(document.activeElement,focusedEditor,'an old acknowledgment must not steal newly focused editor');
focusRenderer.render({...focusSnapshot,focus:common.key});acknowledgeFocus({});await tick();
assert.equal(document.activeElement,focusedEditor);
renderer.render({...snapshot,widgets:[{...common,text:'new'}]});assert.equal(renderer.nodes.get(identity(common.key)).control,editor);assert.equal(editor.value,'new');
renderer.pendingEdits.set(identity(common.key),7);editor.value='unacknowledged input';
renderer.render({...snapshot,widgets:[{...common,text:'server old'}]});assert.equal(editor.value,'unacknowledged input');
renderer.pendingEdits.clear();renderer.render({...snapshot,widgets:[{...common,key:{...common.key,generation:'10'}}]});
assert.notEqual(renderer.nodes.get(identity({...common.key,generation:'10'})).control,editor);
const kinds=[0,1,2,3,4,6,7,8].map((kind,index)=>({...common,key:{id:`generic ${kind}`,generation:'1'},kind,bounds:[index*20,20,20,24],contentClip:[0,0,20,24],rowHeight:24,records:kind===6?[{id:'stable-row',text:'Accessible row',enabled:true,cells:[{text:'Cell',bounds:[4,2,16,18],font:common.font,wrap:false}]}]:[],image:kind===7?{width:1,height:1,source:'owned',revision:'1',rgb:'AQID'}:undefined}));
renderer.render({...snapshot,widgets:kinds});assert.equal(renderer.nodes.size,8);
assert.equal(renderer.nodes.get(identity(kinds[5].key)).rows.children[0].children[0].style.left,'4px');
const emptyLabel={...common,key:{id:'blank literal label',generation:'1'},kind:1,text:'',label:'Accessible label only'};
const multiline={...common,key:{id:'multiline feature',generation:'1'},multiline:true,text:'A long logical line\nA second hard line',wrap:false};
const staleChoice={...common,key:{id:'placeholder choice',generation:'1'},kind:4,options:[{id:'present',label:'Present',enabled:true}],selected:'removed',placeholder:'Choose one',displayText:''};
const wideList={...kinds[5],contentSize:[240,0]};
renderer.render({...snapshot,widgets:[emptyLabel,multiline,staleChoice,wideList]});
assert.equal(renderer.nodes.get(identity(emptyLabel.key)).control.textContent,'');
const textarea=renderer.nodes.get(identity(multiline.key)).control;
assert.equal(textarea.tagName,'textarea');assert.equal(textarea.attributes.wrap,'off');assert.equal(textarea.style.whiteSpace,'pre');assert.equal(textarea.style.overflowX,'auto');
assert.equal(textarea.value,multiline.text);
assert.equal(renderer.nodes.get(identity(staleChoice.key)).display.hidden,false);
assert.equal(renderer.nodes.get(identity(staleChoice.key)).display.textContent,'Choose one');
renderer.render({...snapshot,widgets:[emptyLabel,{...multiline,wrap:true},staleChoice,{...wideList,contentSize:[360,0]}]});
assert.equal(renderer.nodes.get(identity(multiline.key)).control,textarea,'changing wrap must retain the editor');
assert.equal(textarea.attributes.wrap,'soft');assert.equal(textarea.style.whiteSpace,'pre-wrap');assert.equal(textarea.style.overflowWrap,'anywhere');assert.equal(textarea.style.overflowX,'hidden');
assert.equal(textarea.value,multiline.text,'soft wrapping must not insert hard line breaks into text');
assert.equal(renderer.nodes.get(identity(wideList.key)).rows.style.width,'360px','content extent updates without replacing row data');
console.log('web transport, retained DOM mechanics, shared rectangles and Unicode offsets passed');
