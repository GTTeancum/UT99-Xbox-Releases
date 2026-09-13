"""Compile actual split layout helpers and verify aspect/player-mask transitions."""
from pathlib import Path
import os, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
s=(ROOT/'UT99-Xbox/XboxDrv/src/XboxViewport.cpp').read_text(encoding='utf-8')
def extract(name):
    a=s.index(name); a=s.rfind('\n',0,a)+1; b=s.index('{',a); depth=1; end=b+1
    while depth:
        depth+=(s[end]=='{')-(s[end]=='}'); end+=1
    return s[a:end]
parts=[extract(n) for n in ('static INT XboxSplitActiveOrderForSlot','static UBOOL XboxSplitPillarboxed','extern "C" FLOAT XboxViewportProjectionWidth','static void XboxSplitApplyActiveViewRegion')]
head=r"""
#include <cassert>
#include <cmath>
#include <algorithm>
using INT=int;using UBOOL=int;using FLOAT=float;
#define XBOX_SCREEN_WIDTH 640
#define XBOX_SCREEN_HEIGHT 480
template<class T>T Clamp(T x,T a,T b){return std::max(a,std::min(x,b));}
int GXboxSplitActive=1,GXboxSplitActivePlayerCount=4,GXboxSplitActiveMask=15;
struct Ren{float aspect=1;float GetPixelAspectRatio(){return aspect;}};
struct UViewport{Ren* RenDev;};
struct UXboxViewport:UViewport{int ViewX,ViewY,ViewWidth,ViewHeight,SizeX,SizeY;};
"""
main=r"""
int main(){Ren ren;UXboxViewport v;v.RenDev=&ren;int checks=0;
for(int wide=0;wide<2;wide++)for(int mask=1;mask<16;mask++){
 GXboxSplitActiveMask=mask;int n=0;for(int i=0;i<4;i++)n+=(mask>>i)&1;
 GXboxSplitActivePlayerCount=n;ren.aspect=wide?4.f/3.f:1.f;
 int region=(wide&&n>1)?480:640, left=(640-region)/2, area=0;
 for(int slot=0;slot<4;slot++)if(mask&(1<<slot)){
  GXboxSplitActive=0;XboxSplitApplyActiveViewRegion(&v,slot); GXboxSplitActive=1;
  assert(v.ViewX>=left&&v.ViewX+v.ViewWidth<=left+region);
  assert(v.ViewY>=0&&v.ViewY+v.ViewHeight<=480);
  assert(v.SizeX==v.ViewWidth&&v.SizeY==v.ViewHeight);
  assert(v.ViewWidth==(n>2?region/2:region));
  assert(v.ViewHeight==(n>1?240:480));area+=v.ViewWidth*v.ViewHeight;
  float projection= XboxViewportProjectionWidth(&v,float(v.ViewWidth));
  if(wide&&n>1)assert(std::fabs(projection-(n>2?320.f:640.f))<.001f);
  GXboxSplitActive=0;assert(XboxViewportProjectionWidth(&v,640.f)==640.f);checks++;
 }
 assert(area==region*480*(n==3?3:4)/4);
}
}
"""
with tempfile.TemporaryDirectory() as d:
    d=Path(d);(d/'test.cpp').write_text(head+'\n'.join(parts)+main)
    compiler='C:/msys64/mingw64/bin/g++.exe';env=os.environ.copy();env['PATH']=str(Path(compiler).parent)+';'+env['PATH']
    subprocess.run([compiler,'-std=c++11','-static',str(d/'test.cpp'),'-o',str(d/'test.exe')],check=True,env=env)
    subprocess.run([str(d/'test.exe')],check=True)
print('PASS actual layout/projection helpers: both aspects, all 15 player masks, 1-4 players, split exit projection reset')
