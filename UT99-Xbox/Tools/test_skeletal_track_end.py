"""Exercise the actual skeletal sampler's loop and one-shot endpoints."""
import pathlib
import re
import subprocess
import tempfile

source = (pathlib.Path(__file__).resolve().parents[2] / 'Engine/Src/USkeletalMesh.cpp').read_text()
functions = '\n'.join(re.search(r'static (?:FLOAT|void) ' + name + r'\b.*?\n}', source, re.S).group()
                      for name in ('SkelWrapFrame', 'SkelGetUniformKeyParams', 'SkelGetKeyParams'))
support = r'''
#include <cmath>
#include <vector>
#include <cassert>
#include <cstdio>
typedef float FLOAT; typedef int INT; typedef int UBOOL;
template<class T>T Min(T a,T b){return a<b?a:b;}
template<class T>T Max(T a,T b){return a>b?a:b;}
template<class T>T Clamp(T v,T a,T b){return Min(Max(v,a),b);}
float Abs(float a){return std::fabs(a);}
int appFloor(float a){return (int)std::floor(a);}
template<class T>struct Array:std::vector<T>{using std::vector<T>::operator=; int Num()const{return this->size();}T operator()(int i)const{return (*this)[i];}};
struct FAnimationTrack{Array<float> KeyTime,KeyQuat,KeyPos;};
'''
checks = r'''
int main(){
 FAnimationTrack t; t.KeyTime={0,.2f,.6f}; t.KeyPos={0,1,2}; t.KeyQuat={0,1,2};
 int a,b; float alpha;
 for(float time=.6f;time<=1.2f;time+=.05f){
  SkelGetKeyParams(t,time,1,0,a,b,alpha);
  assert(a==2 && b==2 && alpha==0);
 }
 SkelGetKeyParams(t,.8f,1,1,a,b,alpha);
 assert(a==2 && b==0 && std::fabs(alpha-.5f)<.0001f);
 SkelGetKeyParams(t,.4f,1,0,a,b,alpha);
 assert(a==1 && b==2 && std::fabs(alpha-.5f)<.0001f);
 t.KeyTime.clear();
 SkelGetKeyParams(t,.9f,1,0,a,b,alpha);
 assert(a==2 && b==2 && alpha==0);
 SkelGetKeyParams(t,.9f,1,1,a,b,alpha);
 assert(a==2 && b==0 && std::fabs(alpha-.7f)<.0001f);
 std::puts("Timed and uniform tracks hold one-shot endpoints; loops interpolate across wrap");
}
'''
with tempfile.TemporaryDirectory(prefix='ut99_track_end_') as folder:
    cpp = pathlib.Path(folder) / 'track.cpp'
    exe = pathlib.Path(folder) / 'track.exe'
    cpp.write_text(support + functions + checks)
    subprocess.run(['C:/Program Files/LLVM/bin/clang++.exe', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
