"""Exercise production input scheduling and axis scaling at varied tick rates."""
from pathlib import Path
import os, re, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'UT99-Xbox/XboxDrv/src/XboxViewport.cpp').read_text()
def extract(name):
    a=s.index('void UXboxViewport::'+name+'('); b=s.index('{',a); n=1; e=b+1
    while n:
        n+=(s[e]=='{')-(s[e]=='}'); e+=1
    return s[a:e]
expression=re.search(r'FLOAT Sensitivity = (.*);',s[s.index('void UXboxViewport::ProcessControllerInput'):])[1]
head=r'''
#include <cassert>
#include <cmath>
using FLOAT=float;
struct UXboxViewport;
void XboxSensitivityProofAfterMove(UXboxViewport*,float){}
struct Base {void ReadInput(float);};
struct ClientType {float ControllerSensitivity=1;};
struct UXboxViewport:Base {
 using Super=Base;
 float ControllerInputSeconds=0, axis=0; double yaw=0;
 bool bControllerPolled=false; int polls=0, gameplay=0;
 ClientType client, *Client=&client;
 void ReadInput(float);void PollControllerForFrame();
 void PollController(){bControllerPolled=true;polls++;
 if(ControllerInputSeconds<=0)return;
 gameplay++;
 float Sensitivity=EXPRESSION;
 axis+=.01f*100.f*Sensitivity*5.9f;
 }
};
void Base::ReadInput(float dt){auto*v=static_cast<UXboxViewport*>(this);
 if(dt>=0){v->PollController();v->axis*=20.f/dt;}else v->axis=0;}
'''.replace('EXPRESSION',expression)
main=r'''
int main(){
 for(int players: {1,2,4})for(int fps: {15,20,30,40,60,120}){
   UXboxViewport v[4];
   for(int f=0;f<fps*10;f++)for(int p=0;p<players;p++){
     auto& a=v[p];float dt=1.f/fps;
     a.ReadInput(dt);a.yaw+=32.f*dt*.24f*a.axis;
     a.ReadInput(-1);a.PollControllerForFrame();
   }
   for(int p=0;p<players;p++){
     assert(v[p].gameplay==fps*10);assert(v[p].polls==fps*10);
     double expected=32.*.24*.2*100*5.9*60*10;
     assert(std::fabs(v[p].yaw/expected-1)<.00001);
     int before=v[p].gameplay;
     for(int i=0;i<20;i++)v[p].PollControllerForFrame();
     assert(v[p].gameplay==before);assert(v[p].axis==0);
     v[p].ReadInput(1.f/fps);assert(v[p].gameplay==before+1);
   }
 }
}
'''
with tempfile.TemporaryDirectory() as d:
    d=Path(d); (d/'t.cpp').write_text('#include <initializer_list>\n'+head+extract('ReadInput')+extract('PollControllerForFrame')+main)
    compiler=Path('C:/msys64/mingw64/bin/g++.exe');env=os.environ.copy();env['PATH']=str(compiler.parent)+';'+env['PATH']
    subprocess.run([str(compiler),'-std=c++11','-static',str(d/'t.cpp'),'-o',str(d/'t.exe')],check=True,env=env)
    subprocess.run([str(d/'t.exe')],check=True)
assert 'VP->PollControllerForFrame();' in (root/'UT99-Xbox/XboxDrv/src/XboxClient.cpp').read_text()
print('PASS production scheduling/scaling: 1/2/4 players, 15-120 FPS, paused menu polls and resume')
