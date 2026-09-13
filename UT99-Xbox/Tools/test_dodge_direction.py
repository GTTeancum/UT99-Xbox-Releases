"""Compile production dodge mapping and compare it with stock double-tap semantics."""
from pathlib import Path
import subprocess,os,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'UT99-Xbox/XboxDrv/src/XboxViewport.cpp').read_text()
# Find declarations without depending on whitespace before the parameter list.
def function(name):
 import re
 m=re.search(r'static\s+\w+\s+'+name+r'\s*\(',s);a=m.start();b=s.index('{',a);e=b+1;n=1
 while n:n+=(s[e]=='{')-(s[e]=='}');e+=1
 return s[a:e]
head='''#include <algorithm>
#include <cstdlib>
#include <cassert>
#include <cstdio>
using INT=int;using FLOAT=float;using SHORT=short;using BYTE=unsigned char;
template<class T>T Clamp(T v,T a,T b){return std::max(a,std::min(v,b));}
template<class T>T Abs(T v){return std::abs(v);}
enum{XSL_Default,XSL_Southpaw,XSL_Legacy,XSL_LegacySouthpaw};
enum{DODGE_None,DODGE_Left,DODGE_Right,DODGE_Forward,DODGE_Back};
struct XINPUT_GAMEPAD{short sThumbLX=0,sThumbLY=0,sThumbRX=0,sThumbRY=0;};
'''
main='''int main(){int count=0;
 for(int layout=0;layout<4;layout++) for(int x: {-32768,-14000,-13999,0,13999,14000,32767}) for(int y: {-32768,-14000,-13999,0,13999,14000,32767}){
 XINPUT_GAMEPAD p;
 (layout==1||layout==2?p.sThumbRX:p.sThumbLX)=x;
 (layout==1||layout==3?p.sThumbRY:p.sThumbLY)=y;
 int actual=XboxDodgeDirectionFromStick(layout,p);
 int expected=0;
 if(abs(x)>=14000||abs(y)>=14000)expected=abs(x)>abs(y)?(x>0?DODGE_Left:DODGE_Right):(y>0?DODGE_Forward:DODGE_Back);
 assert(actual==expected);count++;
 }
 printf("PASS: %d directions across all four stick layouts, including deadzone and diagonals\\n",count);
}'''
# These are the stock paths the button mapping must agree with.
pawn=(root/'Engine/Classes/PlayerPawn.uc').read_text()
assert 'bWasLeft = (aStrafe > 0)' in pawn and 'bWasRight = (aStrafe < 0)' in pawn
with tempfile.TemporaryDirectory() as d:
 d=Path(d);(d/'test.cpp').write_text(head+'\n'.join(function(n) for n in ['XboxStickLayoutClamp','XboxStickLayoutAxes','XboxDodgeDirectionFromStick'])+main)
 compiler=Path('C:/msys64/mingw64/bin/g++.exe');env=os.environ.copy();env['PATH']=str(compiler.parent)+';'+env['PATH']
 subprocess.run([str(compiler),'-std=c++11','-static',str(d/'test.cpp'),'-o',str(d/'test.exe')],check=True,env=env)
 subprocess.run([str(d/'test.exe')],check=True,env=env)
