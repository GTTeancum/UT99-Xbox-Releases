"""Compare bounded DrawString with the original function on a terminated copy."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

HEAD = r"""
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdio>
#include <string>
#include <vector>
using INT=int; using DWORD=unsigned; using UBOOL=int; using TCHAR=char; using TCHARU=unsigned char;
#define MAXINT INT_MAX
#define guardSlow(x)
#define unguardSlow
#define Min std::min
struct FPlane {};
static std::vector<int> calls;
struct UTexture;
struct FTextureInfo {UTexture* Texture;};
struct UTexture {int id; void Lock(FTextureInfo& i,double,int,void*){i.Texture=this;calls.push_back(-100-id);} void Unlock(FTextureInfo&){calls.push_back(-200-id);}};
template<class T> struct Array:std::vector<T> {int Num()const{return this->size();} T& operator()(int i){return this->at(i);}};
struct FFontCharacter {int USize,VSize,StartU,StartV;};
struct FFontPage {UTexture* Texture;Array<FFontCharacter> Characters;};
struct UFont {int CharactersPerPage;Array<FFontPage> Pages;};
struct View {double CurrentTime;void* RenDev;};
struct UCanvas {View* Viewport;float OrgX,OrgY,SpaceX,ClipX,ClipY;};
static void DrawChar(DWORD f,UCanvas*,FTextureInfo& info,int x,int y,int w,int h,int u,int v,int us,int vs,FPlane) {
 int values[]={int(f),info.Texture->id,x,y,w,h,u,v,us,vs};calls.insert(calls.end(),values,values+10);
}
"""
MAIN = r"""
int main(){
 UTexture textures[8];UFont font; font.CharactersPerPage=32;
 for(int p=0;p<8;p++){textures[p].id=p;FFontPage page;page.Texture=p==3?nullptr:&textures[p];
  for(int c=0;c<(p==7?20:32);c++)page.Characters.push_back({c%11,c%17,c*2,p*3});font.Pages.push_back(page);}
 View view{1.0,nullptr};unsigned seed=57291;auto next=[&](){seed=seed*1664525u+1013904223u;return seed;};
 for(int trial=0;trial<100000;trial++){
  std::string text;int len=next()%64;for(int i=0;i<len;i++)text.push_back(char(next()%256));
  if(trial%3==0)text="A&&B&C&"+text;
  int limit=next()%(text.size()+1);std::string prefix=text.substr(0,limit);
  UCanvas canvas{&view,float(int(next()%20)-10),float(int(next()%20)-10),float(next()%7)*0.5f,float(next()%160),float(next()%80)};
  int x=int(next()%120)-60,y=int(next()%80)-40,clip=next()%2,hot=(trial/2)%2;
  calls.clear();int before=LegacyDrawString(17,&canvas,&font,x,y,prefix.c_str(),{},clip,hot);auto want=calls;
  calls.clear();int after=DrawString(17,&canvas,&font,x,y,text.c_str(),{},clip,hot,limit);
  assert(before==after&&want==calls);
 }
 printf("PASS 100000 bounded text cases: width, glyph calls, texture locks and unlocks match\n");
}
"""

def extract(source):
    start=source.index('static inline INT DrawString\n')
    end=source.index('\n}',start)+2
    # Adapt VC6 loop-variable scope for the host compiler.
    return source[start:end].replace("for( i=0; i<5; i++ )", "for( INT i=0; i<5; i++ )")

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',required=True)
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[2]
    original=subprocess.check_output(['git','show','8fcbc4f:Engine/Src/UnCanvas.cpp'],cwd=root).decode('utf-8')
    current=(root/'Engine/Src/UnCanvas.cpp').read_text(encoding='utf-8')
    compiler=Path(args.compiler).resolve();env=dict(os.environ);env['PATH']=str(compiler.parent)+os.pathsep+env.get('PATH','')
    with tempfile.TemporaryDirectory(prefix='ut99_text_slice_') as temp:
        cpp=Path(temp)/'test.cpp';exe=Path(temp)/'test.exe'
        cpp.write_text(HEAD+extract(original).replace('DrawString','LegacyDrawString')+'\n'+extract(current)+MAIN,encoding='utf-8')
        subprocess.run([str(compiler),'-std=c++11','-O2','-static',str(cpp),'-o',str(exe)],env=env,check=True)
        subprocess.run([str(exe)],env=env,check=True)

if __name__=='__main__':main()
