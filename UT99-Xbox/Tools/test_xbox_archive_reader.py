"""Compile the actual Xbox archive reader against counted in-memory Win32 I/O.

Exercises nested export seeks, precache hints, buffer/direct-read transitions,
EOF and randomized seek/read traffic against an independent byte oracle.
Requires a host C++ compiler; never sends input to the desktop or emulator.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

PREAMBLE = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>
#include <random>
typedef int INT; typedef uint32_t DWORD; typedef unsigned char BYTE;
typedef bool UBOOL;
struct File { std::vector<BYTE> bytes; INT pos=0, reads=0, seeks=0; };
typedef File* HANDLE;
#define TEXT(x) x
#define guardSlow(x)
#define unguardSlow
#define check(x) assert(x)
#define checkSlow(x) assert(x)
#define ARRAY_COUNT(x) (sizeof(x)/sizeof((x)[0]))
#define MAXINT 2147483647
#define FILE_BEGIN 0
#define Min(a,b) std::min((a),(b))
#define appMemcpy std::memcpy
const char* appGetSystemErrorMessage() { return "mock"; }
struct FOutputDevice { template<class... T> void Logf(const char*, T...) {} };
struct FArchive { bool ArIsLoading=false, ArIsPersistent=false, ArIsError=false; };
bool CloseHandle(HANDLE) { return true; }
DWORD SetFilePointer(HANDLE f, INT pos, INT, INT) { f->seeks++; f->pos=pos; return pos; }
bool ReadFile(HANDLE f, void* out, INT size, DWORD* count, void*) {
    assert(size>=0); f->reads++;
    *count=std::min(size, int(f->bytes.size())-f->pos);
    std::memcpy(out,f->bytes.data()+f->pos,*count); f->pos+=*count; return true;
}
'''

TEST = r'''
int main() {
    File f; f.bytes.resize(262144);
    for (size_t i=0;i<f.bytes.size();++i) f.bytes[i]=BYTE((i*37)^(i>>8));
    FOutputDevice error;
    FArchiveFileReader r(&f,&error,int(f.bytes.size()));
    auto read=[&](int pos,int size) {
        std::vector<BYTE> out(size);
        r.Serialize(out.data(),size);
        assert(!r.ArIsError && r.Tell()==pos+size);
        assert(std::equal(out.begin(),out.end(),f.bytes.begin()+pos));
    };
    read(0,1);
    int reads=f.reads, seeks=f.seeks;
    r.Seek(32); r.Precache(100); read(32,100);
    r.Seek(1); r.Precache(20); read(1,20);
    assert(f.reads==reads && f.seeks==seeks);
    r.Seek(8190); read(8190,20000); // cached tail, direct I/O, then refill
    r.Seek(20000); r.Precache(200); read(20000,9000);
    std::mt19937 random(0x58424f58);
    for(int i=0;i<10000;++i) {
        int pos=random()%f.bytes.size();
        int size=std::min(int(random()%20000),int(f.bytes.size())-pos);
        r.Seek(pos);
        if(i%3==0) r.Precache(1+random()%10000);
        read(pos,size);
    }
    r.Seek(int(f.bytes.size())); BYTE byte=0; r.Serialize(&byte,1);
    assert(r.ArIsError);
    std::printf("PASS: cached export seeks, direct reads, 10000 random operations, EOF\n");
}
'''

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='clang++')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    text = (root / 'UT99-Xbox/XboxLaunch/inc/FFileManagerXbox.h').read_text()
    reader = text[text.index('class FArchiveFileReader'):text.index('/*---', text.index('class FArchiveFileReader'))]
    with tempfile.TemporaryDirectory(prefix='ut99_archive_') as folder:
        source = Path(folder) / 'archive_test.cpp'
        binary = Path(folder) / 'archive_test.exe'
        source.write_text(PREAMBLE + reader + TEST)
        subprocess.run([args.compiler, '-std=c++17', '-O2', str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)

if __name__ == '__main__':
    main()
