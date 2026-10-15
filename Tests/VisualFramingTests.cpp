#include "VisualFraming.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace
{
constexpr double pi = 3.14159265358979323846;
struct Vec { double x, y, z; };
struct Inputs { float low, mid, high, band, left, right; };

void require(bool passed, const std::string& message)
{
    if (!passed) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

Vec rotate(Vec p, char axis, double angle)
{
    const double c = std::cos(angle), s = std::sin(angle);
    if (axis == 'x') return {p.x, p.y*c-p.z*s, p.y*s+p.z*c};
    if (axis == 'y') return {p.x*c+p.z*s, p.y, -p.x*s+p.z*c};
    return {p.x*c-p.y*s, p.x*s+p.y*c, p.z};
}

Vec cross(Vec a, Vec b)
{
    return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
}

Vec normalized(Vec p)
{
    const double length = std::sqrt(p.x*p.x+p.y*p.y+p.z*p.z);
    require(length > 1.0e-10, "the knot frame never degenerates");
    return {p.x/length, p.y/length, p.z/length};
}

// Independent evaluation of the shader's actual geometry: this checks the
// analytical envelopes, rather than just testing a fit against its own box.
Vec shaderVertex(int mode, double x, double y, double phase, double gain,
                 double corr, Inputs in)
{
    const double u = x*2*pi, v = y*2*pi, strength = std::min(gain, 2.6);
    const bool closed = mode == 1 || mode == 2 || mode == 5;
    const double coord = closed ? std::abs(2*x-1) : x;
    const auto spectrum = [&](double t) { return in.band*(0.5+0.5*std::sin(t*8*pi+phase)); };
    const double b = spectrum(coord)*gain;
    const double waveCoord = closed ? 0.5+0.5*std::cos(u) : x;
    const double w = in.left*std::sin(waveCoord*11*pi+phase)*gain;
    Vec p;
    switch (mode)
    {
        case 0:
        {
            double px=(x-.5)*3.35, py=(y-.5)*1.95;
            double z=.23*std::sin(px*2+phase*.24)+.17*std::cos(py*3.2-phase*.17);
            z+=b*.36*std::sin(py*3.1+px*.7)+w*.09;
            py+=.18*std::sin(px*1.5+phase*.12);
            p=rotate(rotate({px,py,z},'x',.63),'z',-.22);
            p={p.x*.90,p.y*.90,p.z*.90}; break;
        }
        case 1:
        {
            double r=.92+.19*std::cos(v)+.13*b+.065*std::sin(7*u+phase*.4)*in.mid*strength;
            p={r*std::cos(u)*(1+.12*(1-corr)),r*std::sin(u),.27*std::sin(v)+w*.14};
            p=rotate(rotate(p,'x',.54),'z',.2); break;
        }
        case 2:
        {
            double theta=y*pi, r=.95+.10*std::sin(5*u+phase*.3)*std::sin(theta*6)+b*.19*std::sin(theta);
            p={r*std::sin(theta)*std::cos(u),r*std::cos(theta),r*std::sin(theta)*std::sin(u)};
            p.x+=.16*std::sin(theta*3+phase*.13)*in.low*strength;
            p=rotate(p,'y',phase*.09+.6); break;
        }
        case 3:
        {
            double towers=spectrum(std::floor(x*40)/40)*(.5+.5*std::sin(std::floor(y*34)*1.8+phase*.3));
            double z=std::pow(std::abs(std::sin(x*40*pi)),.3)*towers*.85*strength;
            p=rotate(rotate({(x-.5)*3.2,(y-.5)*2.8,z-.3},'x',-.90),'z',-.26);
            p={p.x*.80,p.y*.80-.08,p.z*.80}; break;
        }
        case 4:
        {
            double px=(x-.5)*3.2, py=(y-.5)*2.1;
            double z=.19*std::sin(px*2.2+py*1.1+phase*.2)+.16*std::cos(py*3.1-px);
            z+=b*.26*std::sin(px*3-py*2)+w*.10+in.right*std::cos(y*13*pi-phase)*gain*.15;
            p=rotate(rotate({px,py,z},'x',.34),'z',.2);
            p={p.x*.82,p.y*.82,p.z*.82}; break;
        }
        default:
        {
            double radius=.78+.23*std::cos(3*u+phase*.16);
            Vec centre={radius*std::cos(2*u),radius*std::sin(2*u),.32*std::sin(3*u+phase*.16)};
            double dr=-.69*std::sin(3*u+phase*.16);
            Vec tangent=normalized({dr*std::cos(2*u)-2*radius*std::sin(2*u),
                                    dr*std::sin(2*u)+2*radius*std::cos(2*u),.96*std::cos(3*u+phase*.16)});
            Vec binormal=normalized(cross(tangent,{std::cos(2*u),std::sin(2*u),0}));
            Vec normal=normalized(cross(binormal,tangent));
            double tube=.12+.065*b+.025*in.high*strength;
            p={centre.x+(normal.x*std::cos(v)+binormal.x*std::sin(v))*tube,
               centre.y+(normal.y*std::cos(v)+binormal.y*std::sin(v))*tube,
               centre.z+(normal.z*std::cos(v)+binormal.z*std::sin(v))*tube};
            p.x*=1+.10*(1-corr);
            p=rotate(rotate(p,'x',.38),'y',.3+phase*.04); break;
        }
    }
    const double attenuation=1/(1+std::max(0.0,gain-1)*.08);
    return {p.x*attenuation,p.y*attenuation,p.z*attenuation};
}

double projectedExtent(Vec p, const VisualFraming& f, float aspect, const std::string& context)
{
    const double w=f.distance-p.z;
    const double x=f.fit*(p.x/(aspect*w)-f.centerX);
    const double y=f.fit*(p.y/w-f.centerY);
    const double nearPlane=.1, farPlane=f.distance+f.zBound+1;
    const double z=((farPlane+nearPlane)*w-2*farPlane*nearPlane)/((farPlane-nearPlane)*w);
    require(w > 0 && std::isfinite(x) && std::isfinite(y) && std::isfinite(z), context+": finite projection ahead of eye");
    require(std::abs(x) <= .86001 && std::abs(y) <= .86001, context+": seven percent margin");
    require(std::abs(z) < 1, context+": depth stays inside clip planes");
    return std::max(std::abs(x),std::abs(y));
}

void sweep()
{
    const Inputs profiles[]={{0,0,0,0,0,0},{.2f,.6f,.9f,.45f,.3f,.8f},
                             {1,1,1,1,1,1},{1,.3f,.7f,4,4,4}};
    std::size_t frames=0,vertices=0;
    for (int mode=0;mode<6;++mode)
    for (float gain : {.25f,1.f,2.6f,4.f})
    for (float aspect : {.25f,.5f,1.f,2.f,4.f,8.f})
    for (float phase : {-10000.f,-17.3f,0.f,13.7f,10000.f})
    for (float corr : {-1.f,0.f,1.f})
    for (auto in : profiles)
    {
        const std::string label="mode "+std::to_string(mode)+", gain "+std::to_string(gain)+", aspect "+std::to_string(aspect);
        const auto fit=computeVisualFraming(mode,aspect,phase,gain,in.low,in.mid,in.high,corr,in.band,in.left,in.right);
        for (float value : {fit.distance,fit.fit,fit.centerX,fit.centerY,fit.zBound})
            require(std::isfinite(value),label+": finite uniforms");
        require(fit.fit>0 && fit.distance>=3.5 && fit.zBound>=1,label+": valid projection parameters");
        double limitingExtent=0;
        const auto corners=visual_framing_detail::enclosingCorners(mode,phase,gain,in.low,in.mid,in.high,corr,in.band,in.left,in.right);
        for (const auto& p : corners)
            limitingExtent=std::max(limitingExtent,projectedExtent({p.x,p.y,p.z},fit,aspect,label));
        require(std::abs(limitingExtent-.86)<.00001,label+": envelope uses the available frame");
        // Non-grid-aligned UVs also exercise interior displacement extrema.
        for (int y=0;y<=10;++y)
        for (int x=0;x<=18;++x)
        {
            auto p=shaderVertex(mode,double(x)/18,double(y)/10,phase,gain,corr,in);
            projectedExtent(p,fit,aspect,label);
            ++vertices;
        }
        ++frames;
    }
    std::cout<<"PASS "<<frames<<" framing cases, every envelope corner and "<<vertices<<" independent shader vertices\n";
}

void knownProjectionAndInvalidInput()
{
    auto wide=computeVisualFraming(0,8,0,1,0,0,0,0,0,0,0);
    auto tall=computeVisualFraming(0,.25f,0,1,0,0,0,0,0,0,0);
    require(wide.fit>tall.fit,"a wide viewport uses a larger focal length than a tall viewport");
    const float nan=std::numeric_limits<float>::quiet_NaN();
    auto bad=computeVisualFraming(0,nan,nan,nan,nan,nan,nan,nan,nan,nan,nan);
    auto zero=computeVisualFraming(0,0,0,1,0,0,0,0,0,0,0);
    for (auto f : {bad,zero})
        require(std::isfinite(f.fit) && f.fit>0 && std::isfinite(f.distance),"invalid control values have finite fallbacks");
    auto overload=computeVisualFraming(4,1,0,4,1,1,1,-1,1,4,4);
    require(overload.distance>3.5f,"large legal audio amplitudes move the eye in front of all geometry");
    std::cout<<"PASS aspect adaptation, invalid input and overloaded waveform bounds\n";
}
}

int main()
{
    sweep();
    knownProjectionAndInvalidInput();
    const Inputs input{1,1,1,1,1,1};
    int orbitCases=0;
    for(int mode=0;mode<6;mode++)
    for(float yaw : {-3.14159f,-1.5708f,-.7f,0.f,.7f,1.5708f,3.14159f})
    for(float pitch : {-3.14159f,-1.5708f,-.7f,0.f,.7f,1.5708f,3.14159f})
    for(float aspect : {.25f,1.f,2.f,8.f}){
        const auto fit=computeVisualFraming(mode,aspect,7,4,1,1,1,-1,1,1,1,yaw,pitch);
        for(int y=0;y<=12;y++)for(int x=0;x<=24;x++){
            auto p=shaderVertex(mode,double(x)/24,double(y)/12,7,4,-1,input);
            p=rotate(rotate(p,'y',yaw),'x',pitch);
            projectedExtent(p,fit,aspect,"360 orbit");
        }
        orbitCases++;
    }
    std::cout<<"PASS "<<orbitCases<<" 360-degree camera framing cases\n";
}
