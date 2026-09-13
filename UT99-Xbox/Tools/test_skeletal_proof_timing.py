"""Check the actual proof clock against UE1 PlayAnim's rate normalization."""
import pathlib
import re
import subprocess
import tempfile

source = (pathlib.Path(__file__).resolve().parents[2] / 'Engine/Src/UnGame.cpp').read_text()
function = re.search(r'static FLOAT XboxSkeletalProofFrame\b.*?\n}', source, re.S).group()
support = '''
#include <cmath>
#include <cstdio>
#include <cstdlib>
typedef float FLOAT; typedef int INT; typedef int UBOOL;
template<class T>T Max(T a,T b){return a>b?a:b;}
template<class T>T Min(T a,T b){return a<b?a:b;}
float appFloor(float a){return std::floor(a);}
void near(float a,float b){if(std::fabs(a-b)>0.0001f){std::printf("timing mismatch %.6f != %.6f\\n",a,b);std::exit(1);}}
'''
checks = '''
int main(){
 // UT RunLg: 10 frames at 17 fps. At a quarter second it is 42.5% through.
 near(XboxSkeletalProofFrame(.25f,10,17,1),.425f);
 // Retargeted 31-frame Halo motion must have exactly the same cycle duration.
 near(XboxSkeletalProofFrame(.25f,31,52.7f,1),.425f);
 for(int hz=20;hz<=120;hz+=10){
  float elapsed=0;
  for(int i=0;i<hz*3;i++)elapsed+=1.0f/hz;
  near(XboxSkeletalProofFrame(elapsed,10,17,1),.1f);
 }
 near(XboxSkeletalProofFrame(10,1,30,1),0);
 near(XboxSkeletalProofFrame(10,10,17,0),.9f);
 near(XboxSkeletalProofFrame(-1,10,17,1),0);
 std::puts("Native-rate playback and frame-rate independence passed (20-120 Hz)");
}
'''
with tempfile.TemporaryDirectory(prefix='ut99_anim_clock_') as folder:
    cpp = pathlib.Path(folder) / 'clock.cpp'
    exe = pathlib.Path(folder) / 'clock.exe'
    cpp.write_text(support + function + checks)
    subprocess.run(['C:/Program Files/LLVM/bin/clang++.exe', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
