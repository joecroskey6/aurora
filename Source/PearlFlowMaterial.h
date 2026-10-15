#pragma once

// Original optical treatment of persistent pigment fields. The simulation holds
// concentrations; palette and lighting are applied here, so colour edits are instant.
inline const char* pearlFlowMaterial = R"GLSL(
uniform sampler2D flowDye;
uniform sampler2D flowVelocity;
uniform vec2 flowTexel;
uniform int flowReady;
uniform vec3 cameraOrbit;
uniform float pearlViewFit;
uniform float onset;
vec3 pearlRotateX(vec3 p,float a){return vec3(p.x,cos(a)*p.y-sin(a)*p.z,sin(a)*p.y+cos(a)*p.z);}
vec3 pearlRotateY(vec3 p,float a){return vec3(cos(a)*p.x+sin(a)*p.z,p.y,-sin(a)*p.x+cos(a)*p.z);}
vec3 pearlToObject(vec3 p){return pearlRotateY(pearlRotateX(p,-cameraOrbit.y),-cameraOrbit.x);}
vec3 pearlToView(vec3 p){return pearlRotateX(pearlRotateY(p,cameraOrbit.x),cameraOrbit.y);}

float pearlDensity(vec2 p) {
    vec3 d=texture2D(flowDye,clamp(p,vec2(.001),vec2(.999))).rgb;
    return dot(d,vec3(.38,.42,.20));
}
vec3 renderLivingPearlFlat(vec2 uv,vec3 background,vec3 a,vec3 b,vec3 c,
                     vec3 energy,float phase,float gain,float aspect,int theme) {
    float ratio=clamp(aspect,.25,8.);
    float fit=.84*min(ratio/1.06,1./.64);
    vec2 p=(uv-.5)*vec2(2.*ratio,2.)/fit;
    // All layers, including their highlights and haze, share this safe support.
    if(abs(p.x)>=1.06||abs(p.y)>=.64)return background;
    float support=(1.-smoothstep(.91,1.06,abs(p.x)))*(1.-smoothstep(.48,.64,abs(p.y)));
    support*=1.-smoothstep(.72,1.18,dot(p/vec2(1.04,.66),p/vec2(1.04,.66)));
    vec2 st=p/vec2(2.12,1.28)+.5;
    vec2 motion=texture2D(flowVelocity,st).xy;
    vec2 refractOffset=clamp(motion*vec2(.0005),vec2(-.024),vec2(.024));
    vec3 result=background;
    float halo=exp(-dot(p*vec2(1.2,2.),p*vec2(1.2,2.))*2.);
    result=mix(result,mix(b,c,.55),halo*(theme==3?.07:theme==1?.15:.055));
    // Separated optical strata reveal different portions of the same evolving
    // flow, with parallax, transmission, edge light, and a finer foreground glaze.
    for(int layer=0;layer<3;layer++) {
        float z=float(layer);
        vec2 q=(st-.5)*(1.+.047*(2.-z))+.5;
        q+=vec2(-.013,.016)*(2.-z)+refractOffset*(2.-z);
        vec3 pigment=max(texture2D(flowDye,q).rgb,vec3(0.));
        float total=dot(pigment,vec3(1.));
        float d=dot(pigment,vec3(.38,.42,.20));
        vec2 stepUV=flowTexel*(1.+(2.-z)*1.8);
        float dx=pearlDensity(q+vec2(stepUV.x,0.))-pearlDensity(q-vec2(stepUV.x,0.));
        float dy=pearlDensity(q+vec2(0.,stepUV.y))-pearlDensity(q-vec2(0.,stepUV.y));
        vec2 slope=vec2(dx,dy)/max(stepUV,vec2(.001))*.055;
        vec3 n=normalize(vec3(-slope,1.));
        vec3 light=normalize(vec3(-.45,.68,1.));
        float lambert=max(0.,dot(n,light));
        float edge=clamp(length(slope)*.35,0.,1.);
        float fresnel=pow(1.-n.z,2.);
        float specular=pow(max(0.,dot(n,normalize(light+vec3(0.,0.,1.)))),theme==2?62.:38.);
        vec3 weights=pow(pigment+vec3(.00001),vec3(3.1));
        vec3 tint=(a*weights.x+b*weights.y+c*weights.z)/max(.0001,dot(weights,vec3(1.)));
        // Optical thickness produces broad coloured shadows beneath fine pearl edges.
        vec3 colour=tint*(.25+.64*lambert);
        float transmission=exp(-d*2.6);
        colour=mix(colour,mix(b,c,.6),transmission*.07);
        colour+=c*specular*(.25+.66*edge)+mix(b,c,.7)*fresnel*.44;
        colour=mix(colour,a*.36,edge*.21);
        float nacre=.5+.5*sin(d*19.+pigment.y*8.+n.y*2.);
        colour=mix(colour,colour*mix(b,c,nacre)*1.2,.15);
        float opacity=(1.-exp(-total*(5.+z*2.)))*smoothstep(.035,.19,total)*(.72+z*.12);
        result=mix(result,colour,clamp(opacity,0.,.9));
        result+=c*edge*specular*opacity*(.055+.09*clamp(energy.z*gain,0.,1.));
    }
    return mix(background,result,support);
}
// An actual three-dimensional stratified volume. Pigment is transported in the
// fluid plane, while curved density strata occupy depth and are ray integrated
// from the orbiting view. No billboard or screen-image rotation is involved.
vec3 renderLivingPearl(vec2 uv,vec3 background,vec3 a,vec3 b,vec3 c,
                     vec3 energy,float phase,float gain,float aspect,int theme) {
    float ratio=clamp(aspect,.25,8.);
    float fit=pearlViewFit>0.?pearlViewFit:.84*min(ratio/1.06,1./.64);
    float zoom=cameraOrbit.z>0.?cameraOrbit.z:1.;
    vec2 screen=(uv-.5)*vec2(2.*ratio,2.)/(fit*zoom);
    vec3 origin=pearlToObject(vec3(screen,3.));
    vec3 ray=pearlToObject(vec3(0.,0.,-1.));
    float pulse=1.+.035*clamp(energy.x*gain,0.,1.5)+.065*onset;
    vec3 bounds=vec3(1.06,.64,.68)*pulse;
    vec3 safeRay=sign(ray+vec3(.0000001))*max(abs(ray),vec3(.000001));
    vec3 t0=(-bounds-origin)/safeRay,t1=(bounds-origin)/safeRay;
    vec3 enter=min(t0,t1),leave=max(t0,t1);
    float nearT=max(max(enter.x,enter.y),enter.z),farT=min(min(leave.x,leave.y),leave.z);
    if(farT<=max(nearT,0.))return background;
    nearT=max(nearT,0.);
    float stepSize=(farT-nearT)/128.;
    float jitter=.375+.25*fract(sin(dot(gl_FragCoord.xy,vec2(12.9898,78.233)))*43758.5453);
    vec3 sum=vec3(0.);float transmittance=1.;
    for(int i=0;i<128;i++){
        vec3 p=(origin+ray*(nearT+(float(i)+jitter)*stepSize))/pulse;
        float support=(1.-smoothstep(.91,1.06,abs(p.x)))*(1.-smoothstep(.48,.64,abs(p.y)));
        support*=1.-smoothstep(.72,1.18,dot(p.xy/vec2(1.04,.66),p.xy/vec2(1.04,.66)));
        vec2 st=p.xy/vec2(2.12,1.28)+.5;
        vec2 motion=texture2D(flowVelocity,st).xy;
        vec2 q=st+vec2(-.08+.16*sin(p.y*3.),.055+.12*cos(p.x*4.))*p.z+clamp(motion*.0004,vec2(-.018),vec2(.018))*p.z;
        vec3 pigment=max(texture2D(flowDye,q).rgb,vec3(0.));
        float total=dot(pigment,vec3(1.));
        float d=dot(pigment,vec3(.38,.42,.20));
        float bend=(d-.12)*.35+.16*sin(p.x*3.2+phase*.08)+.08*sin(p.y*5.1);
        vec3 strata=(p.z-bend-vec3(-.17,0.,.17))/vec3(.052,.044,.038);
        vec3 sheets=exp(-strata*strata);
        float layerDensity=dot(sheets,vec3(.78,.9,1.));
        float density=total*smoothstep(.035,.19,total)*support*layerDensity;
        float alpha=1.-exp(-density*stepSize*95.);
        if(alpha>.001){
            vec2 delta=flowTexel*1.5;
            float dx=pearlDensity(q+vec2(delta.x,0.))-pearlDensity(q-vec2(delta.x,0.));
            float dy=pearlDensity(q+vec2(0.,delta.y))-pearlDensity(q-vec2(0.,delta.y));
            vec2 slope=vec2(dx,dy)/max(delta,vec2(.001))*.055;
            float tilt=1.-abs(ray.z);
            slope+=tilt*vec2(.512*cos(p.x*3.2+phase*.08),.408*cos(p.y*5.1));
            vec3 n=normalize(pearlToView(vec3(-slope,1.)));
            if(n.z<0.)n=-n;
            vec3 light=normalize(vec3(-.45,.68,1.));
            float lambert=max(0.,dot(n,light));
            float edge=clamp(length(slope)*.35,0.,1.);
            float specular=pow(max(0.,dot(n,normalize(light+vec3(0.,0.,1.)))),theme==2?62.:38.);
            float fresnel=pow(1.-abs(n.z),2.);
            vec3 weights=pow(pigment+vec3(.00001),vec3(3.1));
            vec3 tint=(a*weights.x+b*weights.y+c*weights.z)/max(.0001,dot(weights,vec3(1.)));
            vec3 colour=tint*(.25+.64*lambert);
            colour=mix(colour,mix(b,c,.6),exp(-d*2.6)*.07);
            colour+=c*specular*(.25+.66*edge)+mix(b,c,.7)*fresnel*.32;
            colour=mix(colour,a*.36,edge*.21);
            float nacre=.5+.5*sin(d*19.+pigment.y*8.+n.y*2.+p.z*3.);
            colour=mix(colour,colour*mix(b,c,nacre)*1.2,.15);
            colour+=c*edge*specular*(.055+.09*clamp(energy.z*gain,0.,1.));
            sum+=transmittance*alpha*colour;
            transmittance*=1.-alpha;
        }
        if(transmittance<.015)break;
    }
    return sum+transmittance*background;
}
)GLSL";
