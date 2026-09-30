import {Client,Renderer,executeService} from './renderer.mjs';
const status=document.querySelector('#status'), retry=document.querySelector('#retry');
const setStatus=(message,paused=false)=>{status.textContent=message;retry.hidden=!paused;};
async function post(path,body,token){
  const response=await fetch(path,{method:'POST',headers:{'Content-Type':'application/json',...(token?{'X-Gui-Token':token}:{})},body:JSON.stringify(body)});
  const value=await response.json();if(!response.ok)throw Error(value.error||response.statusText);return value;
}
try{
  let initial,exchange;
  const wasm=new URL(location.href).searchParams.get('mode')==='wasm';
  if(wasm){
    const {default:createModule}=await import('./gui_web_wasm.js');const module=await createModule();
    initial=JSON.parse(module.ccall('gui_web_create','string',['string'],[crypto.randomUUID()]));
    exchange=async envelope=>JSON.parse(module.ccall('gui_web_receive','string',['string'],[JSON.stringify(envelope)]));
  }else{
    const session=await post('/api/session',{});initial=session.state;
    exchange=envelope=>post('/api/event',envelope,session.token);
    window.addEventListener('pagehide',()=>{
      fetch('/api/release',{method:'POST',keepalive:true,headers:{'Content-Type':'application/json','X-Gui-Token':session.token},body:'{}'}).catch(()=>{});
    });
  }
  let renderer,serviceId=null;
  const client=new Client(exchange,state=>{
    renderer.render(state.snapshot);
    if(state.service&&state.service.id!==serviceId){serviceId=state.service.id;
      // Defer the DOM prompt until the current snapshot has been painted.
      setTimeout(async()=>{
        let result;
        try{result=await executeService(state.service);}catch(error){result={type:'service',id:state.service.id,status:'error',value:'',error:error.message||'Prompt failed'};}
        client.send(result).catch(error=>setStatus(error.message));
      },0);
    }
  },setStatus);
  renderer=new Renderer(document.querySelector('#stage'),document.querySelector('#pages'),(operation,coalesce)=>client.send(operation,coalesce));
  client.accept(initial);client.pump();retry.addEventListener('click',()=>client.retry());
  const viewport=document.querySelector('#viewport');
  let previousSize='';
  new ResizeObserver(()=>{
    const width=Math.min(4096,Math.max(0,Math.round(viewport.clientWidth))),height=Math.min(4096,Math.max(0,Math.round(viewport.clientHeight)));
    const scale=Math.min(4,window.devicePixelRatio||1),signature=JSON.stringify([width,height,scale]);
    if(signature!==previousSize){previousSize=signature;client.send({type:'resize',width,height,scale},'resize').catch(error=>setStatus(error.message));}
  }).observe(viewport);
  setStatus(`${wasm?'Wasm':'Hosted C++'} · shared layout, retained browser controls`);
}catch(error){setStatus(`Could not start: ${error.message}`);}
