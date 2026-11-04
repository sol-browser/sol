import { validSpaceIcon } from '../../../packages/browser-contracts/src/index.ts';

const graphemes=new Intl.Segmenter(undefined,{granularity:'grapheme'});
/** Preserve a whole family/flag/skin-tone sequence, never a UTF-16 slice. */
export function spaceIconFromText(value:string):string|undefined {
  const icon=graphemes.segment(value)[Symbol.iterator]().next().value?.segment??'';
  return validSpaceIcon(icon)?icon:undefined;
}
