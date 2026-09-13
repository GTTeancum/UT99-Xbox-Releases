"""Exercise production split teardown with real, dummy and borrowed pawns."""
from pathlib import Path
import os, subprocess, tempfile

root = Path(__file__).resolve().parents[2]
s = (root/'UT99-Xbox/XboxDrv/src/XboxViewport.cpp').read_text(encoding='utf-8')
a = s.index('static void XboxSplitResetRuntime(')
b = s.index('\nstatic UBOOL XboxSmokeMarkerExists', a)
fixture = r'''
#include <cassert>
#include <cstdint>
#include <vector>
#include <algorithm>
using INT=int; using DWORD=uintptr_t;
#define XBOX_SCREEN_WIDTH 640
#define XBOX_SCREEN_HEIGHT 480
struct Actor;
struct Level {int destroyed=0; void DestroyActor(Actor*,int){destroyed++;}};
struct Actor {void* Player=nullptr; Level* level; Level* GetLevel(){return level;}};
struct VP;
struct List {std::vector<VP*> v; int Num(){return int(v.size());} VP* operator()(int i){return v[i];}};
struct UXboxClient {List Viewports;};
struct VP {bool bXboxSplitDummy=false; struct Actor* Actor=nullptr; void* RenDev=nullptr;
 int ViewX=80,ViewY=0,SizeX=240,ViewWidth=240,SizeY=240,ViewHeight=240;
 UXboxClient* client; bool destroyed=false;
 void ConditionalDestroy(){assert(!RenDev); destroyed=true;auto& v=client->Viewports.v;v.erase(std::find(v.begin(),v.end(),this));}
};
using UXboxViewport=VP;
template<class T>T* Cast(VP* p){return p;}
struct Logger {template<class... T>void Write(const char*,T...) {}} GXboxLog;
int GXboxSplitPending=1,GXboxSplitActive=1,GXboxSplitUseReadySlots=1,
 GXboxSplitActiveMask=15,GXboxSplitActivePlayerCount=4,GXboxSplitRenderViewport=3,
 GXboxSplitRenderViewportCount=4,GXboxSystemLinkChildJoinSentMask=15,GXboxSystemLinkChildBoundMask=15;
float GXboxSplitDummyDeathTime[4]; int GXboxSplitBorrowedActor[4]={0,0,0,1};
PRODUCTION
int main(){UXboxClient c;Level level;Actor actors[4];VP views[4];int renderer;
 for(int i=0;i<4;i++){actors[i].level=&level;actors[i].Player=&views[i];views[i].Actor=&actors[i];views[i].RenDev=&renderer;
 views[i].client=&c;c.Viewports.v.push_back(&views[i]);}
 views[2].bXboxSplitDummy=true;
 XboxSplitResetRuntime(&c,"test");
 assert(c.Viewports.Num()==1 && c.Viewports(0)==&views[0]);
 assert(views[0].RenDev==&renderer && !views[0].destroyed);
 assert(level.destroyed==2); // real and dummy owned pawns, never borrowed
 assert(!actors[1].Player && !actors[2].Player && actors[3].Player==&views[3]);
 for(int i=1;i<4;i++)assert(views[i].destroyed && !views[i].Actor);
 assert(views[0].ViewX==0 && views[0].ViewY==0 && views[0].ViewWidth==640 && views[0].ViewHeight==480);
 assert(!GXboxSplitActive && GXboxSplitActivePlayerCount==1);
 XboxSplitResetRuntime(&c,"repeat");assert(c.Viewports.Num()==1 && level.destroyed==2);
}
'''.replace('PRODUCTION',s[a:b])
with tempfile.TemporaryDirectory() as temp:
    p=Path(temp);(p/'test.cpp').write_text(fixture,encoding='utf-8')
    compiler=Path('C:/msys64/mingw64/bin/g++.exe');env=os.environ.copy()
    env['PATH']=str(compiler.parent)+';'+env['PATH']
    subprocess.run([str(compiler),'-std=c++11','-static',str(p/'test.cpp'),'-o',str(p/'test.exe')],check=True,env=env)
    subprocess.run([str(p/'test.exe')],check=True)
print('PASS production teardown: real/dummy removal, borrowed actor preserved, shared renderer preserved, repeat reset')
