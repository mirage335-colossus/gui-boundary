// Generic DOM mechanics for the shared boundary. Application state, widget
// eligibility, layout, stable IDs and edit validation remain in C++.
export const identity = key => JSON.stringify([key.id, key.generation]);
const encoder = new TextEncoder();
export const byteOffset = (text, utf16) => encoder.encode(text.slice(0, utf16)).length;
export function utf16Offset(text, bytes) {
  let count = 0, offset = 0;
  for (const character of text) {
    const size = encoder.encode(character).length;
    if (count + size > bytes) break;
    count += size; offset += character.length;
  }
  return offset;
}

// Serial transport preserves command order and bounds pending work. On an
// uncertain delivery, retry uses the identical epoch/sequence/payload. Never
// reconnect by replaying commands into a fresh application session.
export class Client {
  constructor(exchange, apply, status = () => {}) {
    this.exchange = exchange; this.apply = apply; this.status = status;
    this.queue = []; this.running = false; this.failed = false;
    this.epoch = null; this.ack = 0n; this.state = null; this.inflight = null;
  }
  accept(state) {
    if (this.epoch !== null && state.epoch !== this.epoch) throw Error("Session changed; reload to start a new session.");
    const ack = BigInt(state.ack);
    if (ack < this.ack) return false;
    this.epoch = state.epoch; this.ack = ack; this.state = state;
    this.apply(state);
    if (state.error && state.error !== "Duplicate operation ignored") this.status(state.error);
    return true;
  }
  send(operation, coalesce = null) {
    return new Promise((resolve, reject) => {
      if (coalesce) {
        const previous = this.queue.find(task => task.coalesce === coalesce);
        if (previous) {
          if(previous.completions.length>=128){
            const error=Error("Input queue is full; wait for the backend.");this.status(error.message);reject(error);return;
          }
          previous.operation = operation; previous.completions.push({resolve, reject}); this.pump(); return;
        }
      }
      if (this.queue.length >= 128) {
        const error=Error("Input queue is full; wait for the backend.");this.status(error.message);reject(error);return;
      }
      this.queue.push({operation, coalesce, completions: [{resolve, reject}]});
      this.pump();
    });
  }
  async pump() {
    if (this.running || this.failed || !this.state) return;
    this.running = true;
    try {
      while (this.inflight || this.queue.length) {
        if (!this.inflight) {
          const task = this.queue.shift();
          const operation = typeof task.operation === "function" ? task.operation(this.state.snapshot) : task.operation;
          this.inflight = {task, envelope: {epoch: this.epoch, seq: String(this.ack + 1n), operation}};
        }
        const {task, envelope} = this.inflight;
        const state = await this.exchange(envelope);
        if (state.epoch !== this.epoch || BigInt(state.ack) < BigInt(envelope.seq)) throw Error(state.error || "Operation was not acknowledged.");
        this.accept(state); this.inflight = null;
        for (const completion of task.completions) completion.resolve(state);
      }
    } catch (error) {
      this.failed = true; this.status(`Connection paused: ${error.message}. Retry preserves operation identity.`, true);
    } finally { this.running = false; }
  }
  retry() { this.failed = false; this.status(""); this.pump(); }
}

export class Renderer {
  constructor(root, pages, send, document = globalThis.document) {
    this.root = root; this.pages = pages; this.send = send; this.document = document;
    this.nodes = new Map(); this.pendingEdits = new Map(); this.editVersion = 0;
    this.measurements = new Set(); this.lastFocus = null; this.snapshot = null;
    this.root.addEventListener("wheel", event => this.groupWheel(event), {passive: false});
    this.document.addEventListener("keydown", event => {
      if (event.defaultPrevented || event.isComposing || !this.snapshot) return;
      if(event.key==="Escape"&&this.snapshot.popup){event.preventDefault();this.operation({key:this.snapshot.popup.key},"popupClose");return;}
      const code = ["Escape","Enter","F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"].indexOf(event.key);
      if (this.snapshot.keyBindings.some(binding => binding.key === code && binding.control === event.ctrlKey && binding.shift === event.shiftKey && binding.alt === event.altKey)) {
        event.preventDefault(); this.send({type:"shortcut",key:code,control:event.ctrlKey,shift:event.shiftKey,alt:event.altKey}).catch(()=>{});
      }
    });
  }
  element(tag, className, parent) {
    const element = this.document.createElement(tag);
    if (className) element.className = className;
    if (parent) parent.append(element);
    return element;
  }
  operation(widget, type, values = {}, coalesce = null) {
    return this.send({type, key: widget.key, ...values}, coalesce).catch(() => {});
  }
  current(key) { return this.snapshot.widgets.find(widget => identity(widget.key) === identity(key)); }
  setFont(node, font) {
    node.style.fontSize = `${font.size}px`; node.style.fontWeight = font.bold ? "700" : "400";
    node.style.color = ["var(--text)", "var(--muted)", "var(--accent)", "var(--error)"][font.tone];
  }
  create(widget) {
    const node = this.element("div", `widget kind-${widget.kind}`, this.root);
    node.dataset.key = identity(widget.key);
    const entry = {node, widget, signature: null, lastSelection: null};
    const act = (type, values, coalesce) => this.operation(entry.widget, type, values, coalesce);
    // Stable outer and editor objects survive snapshots. Closures use the
    // current declaration so retained native objects never retain stale rows.
    if (widget.kind === 0) {
      entry.control = node;
      entry.spacer = this.element("div", "group-spacer", node);
      node.setAttribute("role", "group"); node.tabIndex = -1;
      node.addEventListener("scroll", () => {
        if (!entry.updating) act("scroll", {x: node.scrollLeft, y: node.scrollTop}, `scroll:${identity(entry.widget.key)}`);
      });
    } else if (widget.kind === 1) entry.control = this.element("div", "label", node);
    else if (widget.kind === 2) {
      entry.control = this.element("button", "control", node);
      entry.control.addEventListener("click", () => act("activate"));
    } else if (widget.kind === 3) {
      const label = this.element("label", "toggle", node);
      entry.control = this.element("input", "", label); entry.control.type = "checkbox";
      entry.label = this.element("span", "", label);
      entry.control.addEventListener("change", () => act("checked", {value: entry.control.checked}));
    } else if (widget.kind === 4 || widget.kind === 8) {
      entry.control = this.element("select", "control", node);
      if(widget.kind===4)entry.display=this.element("span","choice-display",node);
      entry.control.addEventListener("change", () => {
        const id = entry.control.value;
        if (id) act("choose", {id});
        if (entry.widget.kind === 8) entry.control.value = "";
      });
    } else if (widget.kind === 5) {
      entry.control = this.element(widget.multiline ? "textarea" : "input", "control editor", node);
      if (!widget.multiline) entry.control.type = "text";
      entry.suggestions = this.element("select", "suggestions", node);
      entry.suggestions.setAttribute("aria-label", "Text suggestions");
      entry.suggestions.addEventListener("change", () => {
        const id = entry.suggestions.value; if (id) act("choose", {id}); entry.suggestions.value = "";
      });
      entry.control.addEventListener("compositionstart", () => { entry.composing = true; });
      entry.control.addEventListener("compositionend", () => { entry.composing = false; this.edit(entry); });
      entry.control.addEventListener("input", event => { if (!event.isComposing && !entry.composing) this.edit(entry); });
      entry.control.addEventListener("keydown", event => {
        const submit = entry.widget.submit;
        if (event.key === "Enter" && !event.isComposing && !event.shiftKey &&
            ((submit === 1 && !event.ctrlKey) || (submit === 2 && event.ctrlKey))) {
          event.preventDefault(); act("submit");
        }
      });
      const selection = () => this.selection(entry);
      entry.control.addEventListener("select", selection);
      entry.control.addEventListener("keyup", selection);
      entry.control.addEventListener("pointerup", selection);
      entry.control.addEventListener("scroll", () => {
        if(!entry.updating&&(Math.abs(entry.control.scrollLeft-entry.widget.scroll[0])>.5||Math.abs(entry.control.scrollTop-entry.widget.scroll[1])>.5))
          act("scroll",{x:entry.control.scrollLeft,y:entry.control.scrollTop},`scroll:${identity(entry.widget.key)}`);
      });
    } else if (widget.kind === 6) {
      entry.control = this.element("div", "record-list control", node);
      entry.control.setAttribute("role", "listbox"); entry.control.tabIndex = 0;
      entry.rows = this.element("div", "records", entry.control);
      entry.control.addEventListener("scroll", () => {
        if (!entry.updating) act("scroll", {x: entry.control.scrollLeft, y: entry.control.scrollTop}, `scroll:${identity(entry.widget.key)}`);
      });
      entry.control.addEventListener("keydown", event => {
        const value={ArrowDown:"down",ArrowUp:"up",Enter:"enter"," ":"space"}[event.key];
        if(value){event.preventDefault();act("listKey",{value});}
      });
    } else if (widget.kind === 7) {
      entry.control = this.element("canvas", "bitmap", node); entry.control.tabIndex = 0;
      entry.actions = this.element("select", "bitmap-actions", node);
      entry.actions.setAttribute("aria-label", "Bitmap actions");
      entry.actions.addEventListener("change", () => {if (entry.actions.value) act("action", {id: entry.actions.value}); entry.actions.value = "";});
    }
    const pointer = (event, kind) => {
        if (!entry.widget.pointerInput) return;
        const bounds = this.root.getBoundingClientRect();
        if (kind === "wheel") event.preventDefault();
        act("pointer", {kind, x: event.clientX - bounds.left, y: event.clientY - bounds.top,
          wheelX: kind === "wheel" ? event.deltaX / 100 : 0, wheelY: kind === "wheel" ? -event.deltaY / 100 : 0,
          control: event.ctrlKey, shift: event.shiftKey, alt: event.altKey}, kind === "move" ? `pointer:${identity(entry.widget.key)}` : null);
    };
    node.addEventListener("click", event => pointer(event, "click"));
    node.addEventListener("dblclick", event => pointer(event, "double"));
    node.addEventListener("pointermove", event => pointer(event, "move"));
    node.addEventListener("wheel", event => pointer(event, "wheel"), {passive: false});
    node.addEventListener("contextmenu",event=>{if(entry.widget.kind===7&&entry.widget.actions.length){event.preventDefault();act("popupOpen");}});
    entry.control.addEventListener("focus", () => {
      if(entry.updating)return;
      const version=(this.focusVersion||0)+1;this.focusVersion=version;this.pendingFocus=version;
      this.operation(entry.widget,"focus").then(()=>{
        if(this.pendingFocus!==version)return;this.pendingFocus=null;this.syncFocus();
      });
    });
    return entry;
  }
  selection(entry) {
    if (entry.updating || this.pendingEdits.has(identity(entry.widget.key))) return;
    const editor = entry.control;
    const start = byteOffset(editor.value, editor.selectionStart || 0), end = byteOffset(editor.value, editor.selectionEnd || 0);
    const backward = editor.selectionDirection === "backward";
    const value = [backward ? end : start, backward ? start : end];
    if (JSON.stringify(entry.lastSelection) === JSON.stringify(value)) return;
    entry.lastSelection = value;
    const version = (entry.selectionVersion || 0) + 1; entry.selectionVersion = version; entry.pendingSelection = true;
    this.operation(entry.widget, "selection", {anchor: String(value[0]), caret: String(value[1])}, `selection:${identity(entry.widget.key)}`)
      .then(() => { if (entry.selectionVersion === version) entry.pendingSelection = false; });
  }
  edit(entry) {
    const key = entry.widget.key, id = identity(key), version = ++this.editVersion, value = entry.control.value;
    this.pendingEdits.set(id, version);
    this.send(snapshot => ({type: "edit", key, value,
      base: snapshot.widgets.find(widget => identity(widget.key) === id)?.text ?? ""}), `edit:${id}`)
      .then(() => {
        if (this.pendingEdits.get(id) !== version) return;
        this.pendingEdits.delete(id);
        const current = this.current(key);
        if (current && entry.control.value !== current.text) entry.control.value = current.text;
        this.selection(entry);
      }).catch(() => {if (this.pendingEdits.get(id) === version) this.pendingEdits.delete(id);});
  }
  fillOptions(select, options, selected, placeholder = null) {
    const signature = JSON.stringify([options, placeholder]);
    if (select.dataset.signature !== signature) {
      select.replaceChildren();
      if (placeholder !== null) {const option = this.element("option", "", select); option.value = ""; option.textContent = placeholder;}
      for (const value of options) {const option = this.element("option", "", select); option.value = value.id; option.textContent = value.label; option.disabled = !value.enabled;}
      select.dataset.signature = signature;
    }
    select.value = selected ?? "";
  }
  update(entry, widget) {
    entry.widget = widget; entry.updating = true;
    const {node, control} = entry, [x,y,width,height] = widget.bounds, [cx,cy,cw,ch] = widget.clip;
    node.style.left = `${x}px`; node.style.top = `${y}px`; node.style.width = `${width}px`; node.style.height = `${height}px`;
    node.style.clipPath = `inset(${Math.max(0,cy-y)}px ${Math.max(0,x+width-cx-cw)}px ${Math.max(0,y+height-cy-ch)}px ${Math.max(0,cx-x)}px)`;
    node.hidden = !widget.visible; node.title = widget.help;
    node.inert = !widget.inModal || !widget.enabled;
    control.disabled = !widget.enabled; control.setAttribute("aria-disabled", String(!widget.enabled));
    control.setAttribute("aria-label", widget.accessibleName || widget.label || widget.text);
    this.setFont(node, widget.font);
    this.setFont(control, widget.font);
    if (widget.kind === 0) {
      const clip = widget.contentClip ?? [0,0,width,height];
      entry.spacer.style.width = `${Math.max(width,width-clip[2]+widget.contentSize[0])}px`;
      entry.spacer.style.height = `${Math.max(height,height-clip[3]+widget.contentSize[1])}px`;
    } else if (widget.kind === 1) {
      control.textContent = widget.text; control.style.whiteSpace = widget.wrap ? "pre-wrap" : "pre";
    } else if (widget.kind === 2) control.textContent = widget.label;
    else if (widget.kind === 3) {control.checked = widget.checked; entry.label.textContent = widget.label;}
    else if (widget.kind === 4 || widget.kind === 8) {
      this.fillOptions(control, widget.options, widget.selected, widget.kind === 8 ? widget.label : null);
      if(entry.display){
        const selectedExists=widget.options.some(option=>option.id===widget.selected);
        entry.display.textContent=widget.displayText||widget.placeholder;
        entry.display.hidden=!(widget.displayText||(!selectedExists&&widget.placeholder));
      }
    }
    else if (widget.kind === 5) {
      const pending = this.pendingEdits.has(identity(widget.key)) || entry.composing;
      if (!pending && control.value !== widget.text) control.value = widget.text;
      control.readOnly = widget.readOnly; control.placeholder = widget.placeholder;
      if(widget.multiline){
        control.setAttribute("wrap",widget.wrap?"soft":"off");
        control.style.whiteSpace=widget.wrap?"pre-wrap":"pre";
        control.style.overflowWrap=widget.wrap?"anywhere":"normal";
        control.style.overflowX=widget.wrap?"hidden":"auto";
        control.style.overflowY="auto";
      }
      entry.suggestions.hidden = !widget.options.length; entry.suggestions.disabled = !widget.enabled || widget.readOnly;
      control.style.paddingRight = widget.options.length ? "28px" : "4px";
      this.fillOptions(entry.suggestions, widget.options, null, "▾");
      if (!pending && !entry.pendingSelection && JSON.stringify(entry.lastSelection) !== JSON.stringify(widget.selection)) {
        const [anchor, caret] = widget.selection;
        control.setSelectionRange(utf16Offset(control.value, Math.min(anchor,caret)), utf16Offset(control.value, Math.max(anchor,caret)), anchor > caret ? "backward" : "forward");
        entry.lastSelection = widget.selection;
      }
    } else if (widget.kind === 6) {
      entry.rows.style.height = `${widget.records.length * widget.rowHeight}px`;
      entry.rows.style.width = `${Math.max(width - 2, widget.contentSize[0])}px`;
      const signature = JSON.stringify([widget.records, widget.rowHeight, widget.selected, widget.enabled]);
      if (signature !== entry.signature) {
        entry.rows.replaceChildren();
        if (!widget.records.length) {const placeholder = this.element("div", "empty", entry.rows); placeholder.textContent = widget.placeholder;}
        widget.records.forEach((record,index) => {
          const row = this.element("div", "record", entry.rows); row.style.top = `${index * widget.rowHeight}px`; row.style.height = `${widget.rowHeight}px`;
          row.setAttribute("role", "option"); row.setAttribute("aria-label", record.text); row.setAttribute("aria-selected", String(record.id === widget.selected));
          row.setAttribute("aria-disabled", String(!record.enabled));
          row.addEventListener("click", () => this.operation(entry.widget, "select", {id: record.id}));
          row.addEventListener("dblclick", () => this.operation(entry.widget, "activateRecord", {id: record.id}));
          for (const cell of record.cells) {
            const text = this.element("span", "cell", row); text.textContent = cell.text;
            const [left,top,w,h] = cell.bounds;
            Object.assign(text.style, {left:`${left}px`,top:`${top}px`,width:`${w}px`,height:`${h}px`,whiteSpace:cell.wrap?"pre-wrap":"pre"});
            this.setFont(text, cell.font);
          }
        });
        entry.signature = signature;
      }
    } else if (widget.kind === 7) {
      control.tabIndex=widget.pointerInput||widget.actions.length?0:-1;
      entry.actions.hidden = !widget.actions.length; entry.actions.disabled = !widget.enabled;
      this.fillOptions(entry.actions, widget.actions, null, "Actions");
      if (widget.image) {
        const image = widget.image, signature = JSON.stringify([image.source,image.revision,image.width,image.height,image.rgb]);
        if (entry.signature !== signature) {
          control.width = image.width; control.height = image.height;
          const context = control.getContext("2d"), pixels = context.createImageData(image.width,image.height), bytes = atob(image.rgb);
          for (let source=0,target=0;source<bytes.length;source+=3,target+=4) {
            pixels.data[target]=bytes.charCodeAt(source);pixels.data[target+1]=bytes.charCodeAt(source+1);pixels.data[target+2]=bytes.charCodeAt(source+2);pixels.data[target+3]=255;
          }
          context.putImageData(pixels,0,0); entry.signature = signature;
        }
      }
    }
    if (widget.scroll && (widget.kind!==5 || entry.lastScrollRevision!==widget.scrollRevision)) {
      if (Math.abs(control.scrollLeft-widget.scroll[0])>.5) control.scrollLeft=widget.scroll[0];
      if (Math.abs(control.scrollTop-widget.scroll[1])>.5) control.scrollTop=widget.scroll[1];
      entry.lastScrollRevision=widget.scrollRevision;
    }
    entry.updating = false;
  }
  render(snapshot) {
    this.snapshot = snapshot; this.document.title = snapshot.title || "Boundary example";
    for(const [name,color] of Object.entries(snapshot.palette))this.root.style.setProperty(`--${name}`,`rgb(${color.join(',')})`);
    this.root.style.width = `${snapshot.width}px`; this.root.style.height = `${snapshot.height}px`;
    const pageSignature = JSON.stringify([snapshot.pages,snapshot.activePage]);
    if (pageSignature !== this.pageSignature) {
      this.pages.replaceChildren();
      for (const page of snapshot.pages) {
        const button = this.element("button", "page", this.pages);button.textContent=page.label;button.disabled=!page.enabled;
        const [x,y,w,h]=page.bounds;Object.assign(button.style,{left:`${x}px`,top:`${y}px`,width:`${w}px`,height:`${h}px`});
        button.setAttribute("aria-pressed",String(page.selected));
        button.addEventListener("click",()=>this.send({type:"page",id:page.id}).catch(()=>{}));
      }
      this.pageSignature=pageSignature;
    }
    const retained=new Set();
    for(const [index,widget] of snapshot.widgets.entries()){const id=identity(widget.key);retained.add(id);
      let entry=this.nodes.get(id);if(!entry){entry=this.create(widget);this.nodes.set(id,entry);}this.update(entry,widget);entry.node.style.zIndex=String(index+1);}
    for(const [id,entry] of this.nodes)if(!retained.has(id)){entry.node.remove();this.nodes.delete(id);this.pendingEdits.delete(id);}
    this.syncFocus();
    if(snapshot.closed){this.root.setAttribute("inert","");for(const dialog of this.document.querySelectorAll?.('.service-prompt')||[])dialog.close();}
    this.renderPopup(snapshot.popup);
    this.measure(snapshot.measurements);
  }
  syncFocus(){
    if(this.pendingFocus||!this.snapshot)return;
    const focus=this.snapshot.focus?identity(this.snapshot.focus):null;
    if(focus!==this.lastFocus){const entry=this.nodes.get(focus);if(entry){entry.updating=true;entry.control.focus({preventScroll:true});entry.updating=false;}
      else if(this.lastFocus)this.document.activeElement?.blur?.();this.lastFocus=focus;}
  }
  renderPopup(popup){
    const signature=popup?JSON.stringify([popup.key,popup.revision]):null;
    if(signature===this.popupSignature)return;
    const wasOpen=Boolean(this.popupNode);this.popupNode?.remove();this.popupNode=null;this.popupSignature=signature;
    if(!popup){if(wasOpen){this.lastFocus=null;this.syncFocus();}return;}
    const container=this.element("div","popup",this.root);this.popupNode=container;
    container.setAttribute("role","menu");const [x,y,width,height]=popup.bounds;
    Object.assign(container.style,{left:`${Math.min(x,Math.max(0,this.snapshot.width-180))}px`,top:`${Math.min(y+height,Math.max(0,this.snapshot.height-popup.options.length*28))}px`,width:`${Math.max(180,width)}px`});
    for(const option of popup.options){const button=this.element("button","popup-option",container);button.textContent=option.label;button.disabled=!option.enabled;button.setAttribute("role","menuitem");
      button.addEventListener("click",()=>this.operation({key:popup.key},"popupChoice",{id:option.id}));}
    const first=container.children[0];first?.focus();
  }
  measure(requests) {
    const results=[];
    for(const {id,request} of requests) {
      if(this.measurements.has(id))continue;this.measurements.add(id);
      const span=this.element("span","measure",this.document.body);this.setFont(span,request.font);
      span.textContent=request.text || "\u200b";
      span.style.whiteSpace=request.wrap?"pre-wrap":"pre";
      if(request.wrap)span.style.width=`${request.width}px`;
      const rect=span.getBoundingClientRect();results.push({id,width:request.text?rect.width:0,height:rect.height});span.remove();
    }
    if(results.length)this.send({type:"measure",values:results}).catch(()=>{});
  }
  groupWheel(event) {
    if(event.defaultPrevented||!this.snapshot)return;
    const node=event.target.closest?.(".widget"), entry=node?this.nodes.get(node.dataset.key):null;
    if(entry&&(entry.widget.kind===6||entry.widget.kind===5))return;
    let widget=entry?.widget;
    while(widget){
      if(widget.kind===0){event.preventDefault();this.operation(widget,"scroll",{x:widget.scroll[0]+event.deltaX,y:widget.scroll[1]+event.deltaY},`scroll:${identity(widget.key)}`);return;}
      widget=this.snapshot.widgets.find(value=>value.key.id===widget.parent);
    }
  }
}

export async function executeService(service, host = globalThis) {
  const base={type:"service",id:service.id,value:"",error:""};
  if(service.kind!==2)return {...base,status:"error",error:"This browser profile supports prompts only; file paths and external host services are unsupported."};
  const document=host.document;
  if(!document)return {...base,status:"error",error:"This host has no DOM dialog support."};
  return new Promise(resolve=>{
    const previous=document.activeElement,dialog=document.createElement("dialog");dialog.className="service-prompt";
    const form=document.createElement("form"),label=document.createElement("label"),title=document.createElement("span"),input=document.createElement("input");
    const error=document.createElement("p"),actions=document.createElement("div"),cancel=document.createElement("button"),accept=document.createElement("button");
    title.textContent=service.title;input.type="text";input.value=service.value;input.setAttribute("aria-label",service.title);
    error.setAttribute("role","status");actions.className="service-actions";cancel.type="button";cancel.textContent="Cancel";accept.type="submit";accept.textContent="OK";
    label.append(title,input);actions.append(cancel,accept);form.append(label,error,actions);dialog.append(form);
    let settled=false;
    const finish=(status,value="",failure="")=>{
      if(settled)return;settled=true;
      if(dialog.open)dialog.close();dialog.remove();
      if(previous?.isConnected)previous.focus({preventScroll:true});
      resolve({...base,status,value,error:failure});
    };
    const submit=()=>{
      const value=input.value;
      if(BigInt(encoder.encode(value).length)>BigInt(service.byteLimit)){error.textContent=`Text exceeds the ${service.byteLimit}-byte limit.`;input.focus();return;}
      finish("success",value);
    };
    form.addEventListener("submit",event=>{event.preventDefault();submit();});
    cancel.addEventListener("click",()=>finish("cancelled"));
    dialog.addEventListener("cancel",event=>{event.preventDefault();finish("cancelled");});
    dialog.addEventListener("close",()=>finish("cancelled"));
    dialog.addEventListener("keydown",event=>{
      event.stopPropagation();
      if(event.key==="Escape"){event.preventDefault();finish("cancelled");}
      else if(event.key==="Enter"&&event.target===input&&!event.isComposing){event.preventDefault();submit();}
    });
    try{
      (document.querySelector?.('#stage')||document.body).append(dialog);dialog.showModal();input.focus();input.select();
    }catch(failure){finish("error","",failure.message||"Could not open prompt");}
  });
}
