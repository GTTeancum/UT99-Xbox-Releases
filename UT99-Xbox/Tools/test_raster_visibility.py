"""Compile the actual legacy and query-only raster functions and compare results.

Requires a C++11 host compiler. This does not replace Xbox runtime qualification.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

HEAD = r'''
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>
using INT=int;
#define guard(x)
#define unguard
#define checkSlow(x) assert(x)
#define NAME_Warning 0
#define TEXT(x) x
#define debugf(...) assert(false)
#define Max std::max
#define Min std::min
struct FSpan {int Start,End; FSpan* Next;};
struct FRasterSpan {int X[2];};
static std::vector<FSpan*> allocated;
#define UPDATE_PREVLINK_ALLOC(A,B) {NewSpan=new FSpan{A,B,nullptr}; allocated.push_back(NewSpan); *PrevLink=NewSpan; PrevLink=&NewSpan->Next; ValidLines++;}
template<class T> T* New(int&,int,int) {T* p=new T; allocated.push_back(p);return p;}
static int pool;
struct FSpanBuffer {
 int StartY,EndY,ValidLines; FSpan** Index; int* Mem;
 FSpanBuffer(int a,int b,int c,FSpan** d):StartY(a),EndY(b),ValidLines(c),Index(d),Mem(&pool) {}
 int CopyFromRaster(FSpanBuffer&,int,int,FRasterSpan*);
 int CopyFromRasterUpdate(FSpanBuffer&,int,int,FRasterSpan*);
 static int UpdateRasterScreen(FSpanBuffer&,int,int,FRasterSpan*);
 static int RasterVisible(const FSpanBuffer&,int,int,const FRasterSpan*);
};
'''
MAIN = r'''int main() {
 long cases=0;
 for(int mask=0;mask<256;mask++) for(int lo=-2;lo<=10;lo++) for(int hi=-2;hi<=10;hi++)
 for(int ystart=-1;ystart<=1;ystart++) for(int yend=ystart;yend<=2;yend++) {
  FSpan spans[8]; int n=0;
  for(int x=0;x<8;) {if(!(mask&(1<<x))) {x++;continue;} int a=x;while(x<8&&(mask&(1<<x)))x++; spans[n++]={a,x,nullptr};}
  for(int i=0;i<n-1;i++)spans[i].Next=&spans[i+1];
  FSpan* screenIndex[1]={n?spans:nullptr}; FSpan* outputIndex[3]={};
  FSpanBuffer screen{0,1,n,screenIndex}, output{ystart,yend,0,outputIndex};
  FRasterSpan raster[3]={{{lo,hi}},{{lo,hi}},{{lo,hi}}};
  int want=output.CopyFromRaster(screen,ystart,yend,raster);
  int got=FSpanBuffer::RasterVisible(screen,ystart,yend,raster);
  assert(want==got); assert(screen.ValidLines==n);assert(screenIndex[0]==(n?spans:nullptr));
  for(int i=0;i<n;i++)assert(spans[i].Next==(i+1<n?&spans[i+1]:nullptr));
  FSpan original[8], candidate[8];
  for(int i=0;i<n;i++) {original[i]=spans[i];candidate[i]=spans[i];
   original[i].Next=i+1<n?&original[i+1]:nullptr;
   candidate[i].Next=i+1<n?&candidate[i+1]:nullptr;}
  FSpan* oldIndex[1]={n?original:nullptr}; FSpan* newIndex[1]={n?candidate:nullptr};
  FSpanBuffer oldScreen(0,1,n,oldIndex),newScreen(0,1,n,newIndex);
  want=output.CopyFromRasterUpdate(oldScreen,ystart,yend,raster);
  got=FSpanBuffer::UpdateRasterScreen(newScreen,ystart,yend,raster);
  assert(want==got);assert(oldScreen.ValidLines==newScreen.ValidLines);
  FSpan* left=oldIndex[0];FSpan* right=newIndex[0];
  while(left&&right) {assert(left->Start==right->Start&&left->End==right->End);left=left->Next;right=right->Next;}
  assert(!left&&!right);
  for(auto p:allocated)delete p;allocated.clear();cases++;
 }
 unsigned seed=123456789;
 auto next=[&]() {seed=seed*1664525u+1013904223u;return seed;};
 for(int trial=0;trial<100000;trial++) {
  FSpan leftNodes[16][32],rightNodes[16][32]; FSpan* leftIndex[16];FSpan* rightIndex[16];
  int total=0;
  for(int y=0;y<16;y++) {
   unsigned mask=next();int n=0;
   for(int x=0;x<32;) {if(!(mask&(1u<<x))) {x++;continue;} int lo=x;while(x<32&&(mask&(1u<<x)))x++;
    leftNodes[y][n]=rightNodes[y][n]={lo,x,nullptr};n++;}
   for(int i=0;i<n-1;i++){leftNodes[y][i].Next=&leftNodes[y][i+1];rightNodes[y][i].Next=&rightNodes[y][i+1];}
   leftIndex[y]=n?leftNodes[y]:nullptr;rightIndex[y]=n?rightNodes[y]:nullptr;total+=n;
  }
  int first=int(next()%22)-3,last=first+int(next()%(20-first));
  FRasterSpan raster[24]; FSpan* outIndex[24]={};
  for(int y=first;y<last;y++)raster[y-first]={{int(next()%40)-4,int(next()%40)-4}};
  FSpanBuffer left(0,16,total,leftIndex),right(0,16,total,rightIndex),output(first,last,0,outIndex);
  assert(output.CopyFromRaster(left,first,last,raster)==FSpanBuffer::RasterVisible(right,first,last,raster));
  assert(output.CopyFromRasterUpdate(left,first,last,raster)==FSpanBuffer::UpdateRasterScreen(right,first,last,raster));
  assert(left.ValidLines==right.ValidLines);
  for(int y=0;y<16;y++) {FSpan* a=leftIndex[y];FSpan* b=rightIndex[y];
   while(a&&b){assert(a->Start==b->Start&&a->End==b->End);a=a->Next;b=b->Next;}assert(!a&&!b);}
  for(auto p:allocated)delete p;allocated.clear();
 }
 printf("PASS %ld exhaustive and 100000 multi-line cases; visibility and screen updates matched\n",cases);
}
'''

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = (root / 'Render/Src/UnSpan.cpp').read_text(encoding='utf-8')
    def method(name):
        start = source.index('INT FSpanBuffer::' + name + '(')
        end = source.index('\n}', start) + 2
        return source[start:end]
    compiler = Path(args.compiler).resolve()
    env = dict(os.environ)
    env['PATH'] = str(compiler.parent) + os.pathsep + env.get('PATH', '')
    with tempfile.TemporaryDirectory(prefix='ut99_raster_') as directory:
        cpp = Path(directory) / 'equivalence.cpp'
        exe = Path(directory) / 'equivalence.exe'
        cpp.write_text(HEAD + method('CopyFromRaster') + '\n' +
                       method('RasterVisible') + '\n' + method('CopyFromRasterUpdate') + '\n' +
                       method('UpdateRasterScreen') + '\n' + MAIN, encoding='utf-8')
        subprocess.run([str(compiler), '-std=c++11', '-O2', '-static',
                        str(cpp), '-o', str(exe)], env=env, check=True)
        subprocess.run([str(exe)], env=env, check=True)

if __name__ == '__main__':
    main()
