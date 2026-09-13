"""Compile production whole-picture safe-region helpers, checking all supported layouts."""
from pathlib import Path
import os, subprocess,tempfile
s=(Path(__file__).resolve().parents[2]/'UT99-Xbox/XboxDrv/src/XboxViewport.cpp').read_text()
def extract(name):
    a=s.index(name);a=s.rfind('\n',0,a)+1;b=s.index('{',a);e=b+1;n=1
    while n:n+=(s[e]=='{')-(s[e]=='}');e+=1
    return s[a:e]
head=r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <initializer_list>
using INT=int;using FLOAT=float;using UBOOL=int;
template<class T>T Min(T a,T b){return std::min(a,b);}
template<class T>T Max(T a,T b){return std::max(a,b);}
template<class T>T Clamp(T x,T a,T b){return Max(a,Min(x,b));}
constexpr int XBOX_SCREEN_WIDTH=640,XBOX_SCREEN_HEIGHT=480;
struct UXboxClient {int SafeAreaSize,SafeAreaX,SafeAreaY;};
struct UViewport{};
struct UXboxViewport:UViewport {int ViewX,ViewY,ViewWidth,ViewHeight;};
'''
main=r'''
int main(){int checks=0;
for(int size=85;size<=100;size++)for(int dx:{-48,0,48})for(int dy:{-36,0,36})
for(int wide:{0,1})for(int count:{1,2,3,4})for(int p=0;p<count;p++){
 UXboxClient c{size,dx,dy};UXboxViewport v;
 int w=wide&&count>1?480:640,left=(640-w)/2;
 v.ViewWidth=count>2?w/2:w;v.ViewHeight=count>1?240:480;
 v.ViewX=left+(count>2?(p%2)*w/2:0);v.ViewY=count>2?(p/2)*240:count==2?p*240:0;
 int x=v.ViewX,y=v.ViewY,vw=v.ViewWidth,vh=v.ViewHeight;
 XboxViewportApplyPictureSafeArea(&c,x,y,vw,vh);
 int sx=0,sy=0,sw=640,sh=480;XboxViewportApplySafeArea(&c,sx,sy,sw,sh);
 assert(x>=sx&&y>=sy&&x+vw<=sx+sw&&y+vh<=sy+sh);
 assert(vw>0&&vh>0);
 if(size==100)assert(x==v.ViewX&&y==v.ViewY&&vw==v.ViewWidth&&vh==v.ViewHeight);
 // All tile edges follow one shared mapping; the centre joins with no gap.
 assert(x==sx+v.ViewX*sw/640&&y==sy+v.ViewY*sh/480);
 assert(x+vw==sx+(v.ViewX+v.ViewWidth)*sw/640);
 assert(y+vh==sy+(v.ViewY+v.ViewHeight)*sh/480);
 // Proportions are preserved to integer raster precision, including sidebars.
 assert(std::fabs(vw/(double)v.ViewWidth-sw/640.0)<.005);
 assert(std::fabs(vh/(double)v.ViewHeight-sh/480.0)<.005);
 checks++;
}
assert(checks==2880);
}
'''
parts=[extract(n) for n in ['static void XboxViewportApplySafeArea','static void XboxViewportApplyPictureSafeArea( UXboxClient* Client']]
with tempfile.TemporaryDirectory() as d:
    d=Path(d);(d/'t.cpp').write_text(head+'\n'.join(parts)+main)
    compiler=Path('C:/msys64/mingw64/bin/g++.exe');env=os.environ.copy();env['PATH']=str(compiler.parent)+';'+env['PATH']
    subprocess.run([str(compiler),'-std=c++11','-static',str(d/'t.cpp'),'-o',str(d/'t.exe')],check=True,env=env)
    subprocess.run([str(d/'t.exe')],check=True)
print('PASS 2880 safe-zone cases: 85-100%, extreme offsets, 1-4 players, both aspects, whole-picture scaling with gap-free tile joins')
