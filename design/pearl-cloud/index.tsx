import { createEffect, onCleanup, onMount } from 'solid-js';
import { useTicker, useResolution } from '@diffusionstudio/jsx';
import type { SceneNode } from '@diffusionstudio/jsx';
import pearlCloud from './shader';

/** @inspect color path="Palette/Depth" */
const depth = '#193f59';
/** @inspect color path="Palette/Body" */
const body = '#70bbc4';
/** @inspect color path="Palette/Light" */
const light = '#f0f7eb';
/** @inspect number min=25 max=400 step=5 path="Motion/Intensity" */
const intensity = 100;
/** @inspect number min=0 max=200 step=5 path="Motion/Drift" */
const drift = 65;
/** @inspect select options="Soft Club,Afterhours,Chrome" path="Material/Theme" */
const theme = 'Soft Club';
/** @inspect number min=0 max=100 step=1 path="Audio study/Bass" */
const bass = 46;
/** @inspect number min=0 max=100 step=1 path="Audio study/Mid" */
const mid = 32;
/** @inspect number min=0 max=100 step=1 path="Audio study/Treble" */
const treble = 20;

const width = 1440;
const height = 900;
const vertex = `attribute vec2 position; varying vec2 uv;
void main(){uv=position*.5+.5;gl_Position=vec4(position,0.,1.);}`;
const fragment = `precision highp float;
varying vec2 uv;
uniform vec3 colourA,colourB,colourC,energy;
uniform float phase,gain,aspect;
uniform vec2 viewportSize;
uniform int theme;
${pearlCloud}
void main(){
 vec2 p=uv-.5;
 vec3 base=theme==1?vec3(.020,.032,.065):theme==2?vec3(.82,.84,.86):vec3(.86,.92,.93);
 float haze=exp(-dot(p*vec2(1.1,1.8),p*vec2(1.1,1.8))*3.);
 vec3 background=mix(base,theme==1?colourA*.28:colourC*.96,haze*(theme==1?.55:.46));
 gl_FragColor=vec4(renderPearlCloud(uv,background,colourA,colourB,colourC,energy,phase,gain,aspect,theme,viewportSize),1.);
}`;

export default function PearlCloud() {
 const { time } = useTicker();
 const resolution = useResolution();
 let surface: SceneNode | undefined;
 onMount(() => {
   const canvas = surface!.element as HTMLCanvasElement;
   const gl = canvas.getContext('webgl', {alpha:false, antialias:false, preserveDrawingBuffer:true})!;
   if (!gl) throw new Error('Pearl Cloud needs WebGL');
   function compile(kind:number,source:string) {
     const shader=gl.createShader(kind)!;
     gl.shaderSource(shader,source);gl.compileShader(shader);
     if(!gl.getShaderParameter(shader,gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(shader)||'Shader compilation failed');
     return shader;
   }
   const vs=compile(gl.VERTEX_SHADER,vertex), fs=compile(gl.FRAGMENT_SHADER,fragment);
   const program=gl.createProgram()!;
   gl.attachShader(program,vs);gl.attachShader(program,fs);gl.linkProgram(program);
   if(!gl.getProgramParameter(program,gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(program)||'Shader linking failed');
   const buffer=gl.createBuffer()!;
   gl.bindBuffer(gl.ARRAY_BUFFER,buffer);
   gl.bufferData(gl.ARRAY_BUFFER,new Float32Array([-1,-1,1,-1,-1,1,-1,1,1,-1,1,1]),gl.STATIC_DRAW);
   const loc=(name:string)=>gl.getUniformLocation(program,name);
   const locations=Object.fromEntries(['colourA','colourB','colourC','energy','phase','gain','aspect','theme','viewportSize'].map(k=>[k,loc(k)]));
   function rgb(hex:string) {const n=parseInt(hex.slice(1),16);return [(n>>16&255)/255,(n>>8&255)/255,(n&255)/255];}
   createEffect(() => {
     const density=Math.min(2,resolution());
     const w=Math.round(width*density),h=Math.round(height*density);
     if(canvas.width!==w||canvas.height!==h){canvas.width=w;canvas.height=h;}
     gl.viewport(0,0,w,h);gl.useProgram(program);
     gl.bindBuffer(gl.ARRAY_BUFFER,buffer);
     const position=gl.getAttribLocation(program,'position');
     gl.enableVertexAttribArray(position);gl.vertexAttribPointer(position,2,gl.FLOAT,false,0,0);
     gl.uniform3fv(locations.colourA,rgb(depth));gl.uniform3fv(locations.colourB,rgb(body));gl.uniform3fv(locations.colourC,rgb(light));
     gl.uniform3f(locations.energy,bass*.01,mid*.01,treble*.01);
     gl.uniform1f(locations.phase,time()*drift*.01);gl.uniform1f(locations.gain,intensity*.01);gl.uniform1f(locations.aspect,width/height);
     gl.uniform2f(locations.viewportSize,w,h);
     gl.uniform1i(locations.theme,theme==='Afterhours'?1:theme==='Chrome'?2:0);
     gl.drawArrays(gl.TRIANGLES,0,6);gl.flush();
   });
   onCleanup(()=>{gl.deleteBuffer(buffer);gl.deleteProgram(program);gl.deleteShader(vs);gl.deleteShader(fs);gl.getExtension('WEBGL_lose_context')?.loseContext();});
 });
 return <stage background="#d5dce0" camera={[0.41,0,0,0.41,40,55]} id="g6sbqy">
   <scene id="pearl-cloud" name="Pearl Cloud" width={width} height={height} active workarea={[0,12]}>
     <surface id="pearl-material" width={width} height={height} start={0} end={12} ref={surface}/>
   </scene>
 </stage>;
}
