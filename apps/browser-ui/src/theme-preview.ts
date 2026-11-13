/** One request in flight. Replace obsolete previews, retain explicit save fences. */
export function createThemePreviewQueue<T>(
  send:(value:T,save:boolean)=>Promise<unknown>,
  settled:(error?:unknown)=>void=()=>{},
) {
  const pending:{value:T;save:boolean;id:number}[]=[];
  let sending=false,latest=0;
  let lastError:Error|undefined;
  const waiting:{resolve:()=>void;reject:(error:Error)=>void}[]=[];
  const flush=async()=>{
    if(sending||!pending.length)return;
    sending=true;const next=pending.shift()!;
    try{await send(next.value,next.save);lastError=undefined;if(next.id===latest)settled();}
    catch(error){lastError=error instanceof Error?error:new Error(String(error));if(next.id===latest)settled(error);}
    finally{sending=false;if(pending.length)void flush();else for(const waiter of waiting.splice(0)){if(lastError!==undefined)waiter.reject(lastError);else waiter.resolve();}}
  };
  const enqueue=(value:T,save:boolean)=>{
    const next={value,save,id:++latest},tail=pending.at(-1);
    if(tail&&!tail.save)pending[pending.length-1]=next;
    else pending.push(next);
    void flush();
  };
  return Object.assign(enqueue,{drain:()=>new Promise<void>((resolve,reject)=>{
    if(sending||pending.length)waiting.push({resolve,reject});else if(lastError!==undefined)reject(lastError);else resolve();
  })});
}
