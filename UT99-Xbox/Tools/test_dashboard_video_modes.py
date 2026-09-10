"""Compile the production mode-selection block against enumerated-mode fixtures."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'UT99-Xbox/XboxRender/src/XboxRender.cpp').read_text(encoding='utf-8')
start = source.index('    UBOOL Can480p = 0;')
end = source.index('    GXboxLog.Write("XVIDEO avpack=', start)
selection = source[start:end]
fixture = r'''
#include <cassert>
#include <vector>
using UINT=unsigned; using UBOOL=int;
const unsigned D3DADAPTER_DEFAULT=0, D3DPRESENTFLAG_PROGRESSIVE=0x40,
 D3DPRESENTFLAG_INTERLACED=0x20, D3DPRESENTFLAG_WIDESCREEN=0x10;
#define SUCCEEDED(x) ((x)>=0)
struct D3DDISPLAYMODE { unsigned Width,Height,RefreshRate,Flags,Format; };
struct Logger { template<class... T> void Write(const char*,T...){} } GXboxLog;
struct Adapter {
 std::vector<D3DDISPLAYMODE> modes; bool fail=false;
 unsigned GetAdapterModeCount(unsigned){return unsigned(modes.size());}
 int EnumAdapterModes(unsigned,unsigned i,D3DDISPLAYMODE* m){*m=modes[i];return fail?-1:0;}
};
unsigned choose(Adapter* Direct3D,bool Widescreen){struct{unsigned Flags=0;}PP;
SELECTION
return PP.Flags;}
int main(){Adapter a;
 for(int wide=0;wide<2;wide++){
  unsigned w=wide?0x10:0;
  a.modes.clear(); assert(choose(&a,wide)==(0x20|w));
  a.modes={{1280,720,60,0x40,0x12},{1920,1080,60,0x20,0x12},
           {640,480,50,0x40,0x12},{640,480,60,0x20,0x12}};
  assert(choose(&a,wide)==(0x20|w));
  // Scanout uses a linear format, not the swizzled backbuffer enum.
  a.modes.push_back({640,480,60,0x40,0x12});
  assert(choose(&a,wide)==(0x40|w));
  a.fail=true; assert(choose(&a,wide)==(0x20|w)); a.fail=false;
 }
}
'''.replace('SELECTION', selection)
with tempfile.TemporaryDirectory() as temp:
    path = Path(temp)
    (path / 'test.cpp').write_text(fixture, encoding='utf-8')
    compiler = Path('C:/msys64/mingw64/bin/g++.exe')
    env = os.environ.copy()
    env['PATH'] = str(compiler.parent) + ';' + env['PATH']
    subprocess.run([str(compiler), '-std=c++11', '-static', str(path/'test.cpp'),
                    '-o', str(path/'test.exe')], check=True, env=env)
    subprocess.run([str(path/'test.exe')], check=True)
print('PASS production mode selector: both aspects, 480i fallback, 480p preference, HD/50Hz rejection, enumeration failure')
