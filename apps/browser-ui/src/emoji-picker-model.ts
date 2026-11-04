export type EmojiRecord=readonly [emoji:string,label:string,search:string,group:number];
export const EMOJI_GROUPS=['Faces','People','Nature','Food','Travel','Activities','Objects','Symbols','Flags'];
export const emojiSearch=(value:string)=>value.normalize('NFKD').replace(/\p{M}/gu,'').toLocaleLowerCase();
const searchIndex=new WeakMap<EmojiRecord,string>();
export function findEmoji(entries:readonly EmojiRecord[],query:string,group:number):EmojiRecord[] {
  const terms=emojiSearch(query).trim().split(/\s+/).filter(Boolean);
  return entries.filter(e=>{if(!terms.length)return e[3]===group;let text=searchIndex.get(e);if(text===undefined){text=emojiSearch(e[2]);searchIndex.set(e,text);}return terms.every(t=>text.includes(t));});
}
