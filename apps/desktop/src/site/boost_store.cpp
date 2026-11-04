#include "site/boost_store.h"
#include <algorithm>
#include <cctype>

namespace red {
namespace {
bool ValidKey(const std::string& key) {
  if (key.empty() || key.size() > 253 || key.front() == '.' || key.back() == '.' || key.find("..") != std::string::npos) return false;
  const bool dotted = key.find('.') != std::string::npos;
  if (!dotted && key != "localhost") return false;
  bool letter = false;
  for (const char c : key) {
    if (std::isalpha(static_cast<unsigned char>(c))) letter = true;
    else if (!std::isdigit(static_cast<unsigned char>(c)) && c != '.' && c != '-') return false;
  }
  return letter;  // Rejects IPv4 literals such as 127.0.0.1.
}
constexpr const char* kFonts[kBoostFontCount] = {
  "", "'Segoe UI', sans-serif", "Arial, sans-serif", "Verdana, sans-serif", "Tahoma, sans-serif", "'Trebuchet MS', sans-serif",
  "Candara, sans-serif", "Georgia, serif", "'Times New Roman', serif", "'Palatino Linotype', serif", "Cambria, serif",
  "Consolas, monospace", "'Courier New', monospace", "'Comic Sans MS', cursive", "Impact, sans-serif"};
}  // namespace

bool BoostLook::Valid() const {
  auto in = [](int value, int low, int high) { return value >= low && value <= high; };
  return in(hue, 0, 359) && in(tint, 0, 100) && in(hue2, 0, 359) && in(tint2, 0, 100) && in(brightness, -50, 50) && in(contrast, -50, 50) && in(saturation, -50, 50) &&
         in(dark, 0, 1) && in(font, 0, kBoostFontCount - 1) && in(size, -30, 50) && in(caps, 0, 3);
}

bool ValidBoostSelector(const std::string& selector) {
  if (selector.empty() || selector.size() > kMaxBoostSelector || selector.find("/*") != std::string::npos) return false;
  for (const char c : selector)
    if (static_cast<unsigned char>(c) < 0x20 || c == '{' || c == '}' || c == ';' || c == '"' || c == '\'' || c == '<' || c == '@') return false;
  return true;
}

std::string BoostCss(const Boost& boost) {
  const auto& l = boost.look;
  std::string css;
  if (l.dark || l.brightness || l.contrast || l.saturation) {
    // Root filter. Dark mode inverts the page and turns hues back; media get the same pair again
    // so photos and video look as they are.
    css += "html{filter:";
    if (l.dark) css += " invert(1) hue-rotate(180deg)";
    if (l.brightness) css += " brightness(" + std::to_string(100 + l.brightness) + "%)";
    if (l.contrast) css += " contrast(" + std::to_string(100 + l.contrast) + "%)";
    if (l.saturation) css += " saturate(" + std::to_string(100 + l.saturation) + "%)";
    css += " !important}\n";
    if (l.dark) css += ":where(html){background-color:#fff}\nimg,video,picture,canvas,iframe,embed,object,svg image,[style*=\"background-image\"]{filter:invert(1) hue-rotate(180deg) !important}\n";
  }
  // Tint: a top layer in the "color" blend mode gives everything the chosen hue (a filter would
  // also recolour the accents below). Kept under 60% so the page's own colour shows through.
  if (l.tint)
    css += "html::after{content:\"\";position:fixed;inset:0;z-index:2147483646;pointer-events:none;background:hsl(" + std::to_string(l.hue) +
           " 75% 50%);mix-blend-mode:color;opacity:" + std::to_string(l.tint * 55 / 100) + "% !important}\n";
  if (l.tint2) {
    const auto accent = [&](int light, const char* alpha = "") {
      return "hsl(" + std::to_string(l.hue2) + " " + std::to_string(45 + l.tint2 / 2) + "% " + std::to_string(light) + "%" + alpha + ")";
    };
    css += "a,a *{color:" + accent(62) + " !important}\n::selection{background-color:" + accent(55, " / .45") + " !important}\n"
           ":root{accent-color:" + accent(60) + " !important;caret-color:" + accent(60) + " !important}\n";
  }
  if (l.font > 0 && l.font < kBoostFontCount)
    css += std::string("body :not(i):not(code):not(pre):not(kbd):not(samp):not([class*=icon i]):not([class*=fa-]):not([class*=material i]){font-family:") + kFonts[l.font] + " !important}\n";
  if (l.size) css += "html{zoom:" + std::to_string(100 + l.size) + "%}\n";
  if (l.caps) css += std::string("body *{text-transform:") + (l.caps == 1 ? "uppercase" : l.caps == 2 ? "lowercase" : "capitalize") + " !important}\n";
  // Each selector in its own rule: one that no longer parses drops only itself.
  for (const auto& zap : boost.zaps) css += zap + "{display:none !important}\n";
  if (!boost.focus.empty())
    css += "body :not(" + boost.focus + "):not(" + boost.focus + " *):not(:has(" + boost.focus + ")):not(script):not(style){display:none !important}\n";
  return css + boost.css;
}

std::string BoostPicker(const std::string& mode) {
  // The page shares the DOM but not this world's globals or the binding; the overlay is a closed
  // shadow root built without innerHTML, so page CSP and Trusted Types do not apply to it.
  return "(()=>{const g=globalThis,M=" + JsString(mode) + R"JS(;if(g.__redPick)g.__redPick();if(!M)return;
const send=o=>{try{redBoostPick(JSON.stringify(o))}catch(e){}};
const host=document.createElement('red-boost-picker'),root=host.attachShadow({mode:'closed'});
const sheet=new CSSStyleSheet();sheet.replaceSync(':host{all:initial}.box{position:fixed;z-index:2147483647;pointer-events:none;border:2px solid #f43f5e;background:#f43f5e2e;border-radius:4px;display:none}.tip{position:fixed;left:50%;top:14px;transform:translateX(-50%);z-index:2147483647;pointer-events:none;font:600 13px "Segoe UI",sans-serif;color:#fff;background:#18181bf0;padding:8px 14px;border-radius:999px;box-shadow:0 6px 24px #0006}');
root.adoptedStyleSheets=[sheet];
const box=document.createElement('div');box.className='box';const tip=document.createElement('div');tip.className='tip';
tip.textContent=M==='zap'?'Click an element to zap it \u00b7 Esc to finish':'Click the part of the page to focus on \u00b7 Esc to cancel';
if(M==='zap')box.style.borderColor='#f43f5e';else{box.style.borderColor='#60a5fa';box.style.background='#60a5fa2e'}
root.append(box,tip);(document.body||document.documentElement).append(host);
const unique=s=>{try{return document.querySelectorAll(s).length===1}catch(e){return false}};
const name=/^-?[A-Za-z_][\w-]*$/;
const selector=e=>{const parts=[];for(let n=e;n&&n.nodeType===1;n=n.parentElement){
if(n.id&&name.test(n.id)&&unique('#'+n.id)){parts.unshift('#'+n.id);break}
let s=n.localName;if(s==='html'||s==='body'){parts.unshift(s);break}
const c=[...n.classList].filter(x=>name.test(x)).slice(0,3);if(c.length)s+='.'+c.join('.');
const p=n.parentElement;if(p){const same=[...p.children].filter(x=>x.localName===n.localName);if(same.length>1)s+=':nth-of-type('+(same.indexOf(n)+1)+')'}
parts.unshift(s);if(unique(parts.join(' > '))||parts.length>=12)break}
return parts.join(' > ')};
let target=null;
const at=ev=>{const e=ev.composedPath?ev.composedPath()[0]:ev.target;return e&&e.nodeType===1&&e!==host&&e!==document.documentElement&&e!==document.body?e:null};
const move=ev=>{target=at(ev);if(!target){box.style.display='none';return}const r=target.getBoundingClientRect();
Object.assign(box.style,{display:'block',left:r.left-2+'px',top:r.top-2+'px',width:r.width+'px',height:r.height+'px'})};
const block=ev=>{if(ev.isTrusted){ev.preventDefault();ev.stopImmediatePropagation()}};
const calm=matchMedia('(prefers-reduced-motion: reduce)').matches;
const clear=c=>c&&c!=='transparent'&&!/rgba\(.*,\s*0\)$/.test(c);
// Zap: the element blurs away while dust in its own colours flies off its area.
const dust=(e,r)=>{const cs=getComputedStyle(e);const colors=[cs.color,cs.backgroundColor,cs.borderTopColor].filter(clear);
if(colors.length<2)colors.push('#f43f5e','#fb923c','#fde68a');
// Its own host: the picker overlay is rebuilt when the saved Zap renews the loader.
const holder=document.createElement('red-boost-dust'),layer=holder.attachShadow({mode:'closed'});
(document.body||document.documentElement).append(holder);setTimeout(()=>holder.remove(),1400);
const n=Math.min(56,Math.max(18,Math.round(Math.sqrt(r.width*r.height)/5)));
for(let i=0;i<n;i++){const d=document.createElement('i'),z=2+Math.random()*5;
Object.assign(d.style,{position:'fixed',left:r.left+Math.random()*r.width+'px',top:r.top+Math.random()*r.height+'px',width:z+'px',height:z+'px',
borderRadius:Math.random()<.5?'50%':'1px',background:colors[i%colors.length],zIndex:'2147483647',pointerEvents:'none'});
layer.append(d);const a=Math.random()*Math.PI*2,v=24+Math.random()*96;
d.animate([{transform:'translate(0,0) rotate(0) scale(1)',opacity:1},
{transform:'translate('+Math.cos(a)*v+'px,'+(Math.sin(a)*v-30-Math.random()*40)+'px) rotate('+Math.random()*540+'deg) scale(.15)',opacity:0}],
{duration:520+Math.random()*420,delay:Math.random()*120,easing:'cubic-bezier(.15,.7,.3,1)',fill:'both'}).onfinish=()=>d.remove()}};
const click=ev=>{if(!ev.isTrusted)return;block(ev);const e=at(ev);if(!e)return;const s=selector(e);if(!s)return;
if(M!=='zap'){send({mode:M,selector:s});return}
box.style.display='none';
// Reported after the animation: the saved Zap's CSS would hide the element at once.
const gone=()=>{e.style.setProperty('display','none','important');send({mode:M,selector:s})};
if(calm||!e.animate){gone();return}
dust(e,e.getBoundingClientRect());
const an=e.animate([{opacity:1,filter:'blur(0)',transform:'scale(1)'},{opacity:0,filter:'blur(6px)',transform:'scale(.94)'}],
{duration:300,easing:'cubic-bezier(.4,0,1,1)',fill:'forwards'});
an.onfinish=()=>{gone();an.cancel()}};
const key=ev=>{if(ev.isTrusted&&ev.key==='Escape'){block(ev);send({cancel:true});stop()}};
const types=['pointerdown','pointerup','mousedown','mouseup','auxclick','dblclick','contextmenu'];
const stop=()=>{removeEventListener('pointermove',move,true);removeEventListener('click',click,true);removeEventListener('keydown',key,true);
types.forEach(t=>removeEventListener(t,block,true));host.remove();if(g.__redPick===stop)g.__redPick=null};
addEventListener('pointermove',move,true);addEventListener('click',click,true);addEventListener('keydown',key,true);
types.forEach(t=>addEventListener(t,block,true));g.__redPick=stop})())JS";
}

std::string BoostKeyFromOrigin(const std::string& origin) {
  size_t start = 0;
  if (origin.starts_with("https://")) start = 8; else if (origin.starts_with("http://")) start = 7; else return "";
  auto end = origin.find_first_of("/:?#", start);
  std::string host = origin.substr(start, end == std::string::npos ? std::string::npos : end - start);
  std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (host.starts_with("www.")) host.erase(0, 4);
  return ValidKey(host) ? host : "";
}

std::string JsString(const std::string& text) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out = "\"";
  for (size_t i = 0; i < text.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else if (c < 0x20 || c == '<' || c == '>' || c == '&') { out += "\\u00"; out += kHex[c >> 4]; out += kHex[c & 15]; }
    else if (c == 0xE2 && i + 2 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0x80 &&
             (static_cast<unsigned char>(text[i + 2]) == 0xA8 || static_cast<unsigned char>(text[i + 2]) == 0xA9)) {
      out += text[i + 2] == static_cast<char>(0xA8) ? "\\u2028" : "\\u2029"; i += 2;  // Line separators end JS strings.
    } else out += static_cast<char>(c);
  }
  return out + "\"";
}

const Boost* BoostStore::Find(const std::string& key) const {
  const auto at = boosts_.find(key);
  return at == boosts_.end() ? nullptr : &at->second;
}

int BoostStore::Set(const std::string& key, Boost boost, std::string& error) {
  if (!ValidKey(key)) { error = "Boosts work on websites with a regular address"; return 400; }
  if (boost.css.size() > kMaxBoostCss) { error = "This Boost is too long"; return 413; }
  if (boost.css.find('\0') != std::string::npos) { error = "Unsupported character in the Boost"; return 400; }
  if (!boost.look.Valid()) { error = "Unsupported Boost settings"; return 400; }
  if (boost.zaps.size() > kMaxBoostZaps) { error = "Too many zapped elements"; return 413; }
  for (const auto& zap : boost.zaps) if (!ValidBoostSelector(zap)) { error = "Unsupported element selector"; return 400; }
  if (!boost.focus.empty() && !ValidBoostSelector(boost.focus)) { error = "Unsupported element selector"; return 400; }
  if (boost.Empty()) { boosts_.erase(key); return 0; }
  if (boosts_.size() >= kMaxBoosts && !boosts_.contains(key)) { error = "Too many Boosts"; return 409; }
  boosts_[key] = std::move(boost);
  return 0;
}

std::string BoostStore::Loader() const {
  std::string rules;
  for (const auto& [key, boost] : boosts_) {
    const auto css = BoostCss(boost);
    if (!boost.enabled || css.empty()) continue;
    if (!rules.empty()) rules += ',';
    rules += '[' + JsString(key) + ',' + JsString(css) + ']';
  }
  return "(()=>{const R=[" + rules + "];const k='__redBoost',g=globalThis;"
         "const http=/^https?:$/.test(location.protocol);"
         "const h=location.hostname.toLowerCase().replace(/^www\\./,'');"
         "const css=http?R.filter(r=>h===r[0]||h.endsWith('.'+r[0])).map(r=>r[1]).join('\\n'):'';"
         // A renewal with the same CSS leaves the live sheet alone: swapping it would restyle the whole page.
         "if(g[k]&&g[k].css===css&&document.adoptedStyleSheets.includes(g[k].sheet))return;"
         "if(g[k]){document.adoptedStyleSheets=document.adoptedStyleSheets.filter(s=>s!==g[k].sheet);g[k]=null}"
         "if(!css)return;"
         "const s=new CSSStyleSheet();s.replaceSync(css);g[k]={css,sheet:s};document.adoptedStyleSheets=[...document.adoptedStyleSheets,s]})()";
}
}  // namespace red
