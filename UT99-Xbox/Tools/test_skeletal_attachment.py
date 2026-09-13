"""Compile the actual attachment builder and check it against skinned axes."""
import pathlib
import re
import subprocess
import tempfile
import argparse


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler', default='C:/Program Files/LLVM/bin/clang++.exe')
    args = parser.parse_args()
    source = (pathlib.Path(__file__).resolve().parents[2] / 'Engine/Src/USkeletalMesh.cpp').read_text()
    function = re.search(r'static FCoords SkelBuildClassicWeaponCoords\b.*?\n}\n', source, re.S).group()
    math_source = (pathlib.Path(__file__).resolve().parents[2] / 'Core/Inc/UnMath.h').read_text()
    multiply = re.search(r'inline FCoords& FCoords::operator\*=\( const FCoords& TransformCoords \).*?\n}', math_source, re.S).group()
    multiply += '\n' + re.search(r'inline FCoords FCoords::operator\*\( const FCoords &TransformCoords \) const.*?\n}', math_source, re.S).group()
    support = r'''
#include <cmath>
#include <cstdio>
#include <cstdlib>
struct FCoords;
struct FVector {
 double X,Y,Z;
 FVector(double x=0,double y=0,double z=0):X(x),Y(y),Z(z){}
 FVector operator+(FVector b)const{return FVector(X+b.X,Y+b.Y,Z+b.Z);}
 FVector operator-(FVector b)const{return FVector(X-b.X,Y-b.Y,Z-b.Z);}
 FVector operator*(double b)const{return FVector(X*b,Y*b,Z*b);}
 FVector operator^(FVector b)const{return FVector(Y*b.Z-Z*b.Y,Z*b.X-X*b.Z,X*b.Y-Y*b.X);}
 double dot(FVector b)const{return X*b.X+Y*b.Y+Z*b.Z;}
 FVector SafeNormal()const{return *this*(1/std::sqrt(dot(*this)));}
 FVector TransformVectorBy(const FCoords&)const;
 FVector TransformPointBy(const FCoords&)const;
};
struct FCoords {
 FVector Origin,XAxis,YAxis,ZAxis;
 FCoords& operator*=(const FCoords&);
 FCoords operator*(const FCoords&)const;
};
FVector FVector::TransformVectorBy(const FCoords& c)const{return FVector(dot(c.XAxis),dot(c.YAxis),dot(c.ZAxis));}
FVector FVector::TransformPointBy(const FCoords& c)const{return (*this-c.Origin).TransformVectorBy(c);}
FVector SkelPivotTransformVector(const FCoords& c,const FVector& v){return v.TransformVectorBy(c);}
struct {FCoords UnitCoords;} GMath;
void equal(FVector a,FVector b){if((a-b).dot(a-b)>1e-12){std::puts("attachment disagrees with skinned local axis");std::exit(1);}}
'''
    checks = r'''
int main(){
 GMath.UnitCoords={FVector(0,0,0),FVector(1,0,0),FVector(0,1,0),FVector(0,0,1)};
 for(int i=0;i<360;i++){
  double a=i*3.141592653589793/180, c=cos(a),s=sin(a);
  FCoords bone={FVector(3,8,12),FVector(c,-s,0),FVector(s,c,0),FVector(0,0,1)};
  FCoords view={FVector(7,2,-9),FVector(0,-.3,0),FVector(.3,0,0),FVector(0,0,.3)};
  FVector origin(0,0,124);
  FCoords got=SkelBuildClassicWeaponCoords(bone,origin,view);
  // Known rotated local X is (cos a,sin a,0), independently of storage layout.
  FVector expectedX=FVector(c,s,0).TransformVectorBy(view).SafeNormal();
  FVector expectedY=FVector(-s,c,0).TransformVectorBy(view).SafeNormal();
  FVector pivot=(bone.Origin-origin).TransformPointBy(view);
  // Exercise the real FCoords inversion and the final vertex transform.
  equal(FVector(0,0,0).TransformPointBy(got),pivot);
  equal(FVector(1,0,0).TransformPointBy(got),pivot+expectedX);
  equal(FVector(0,1,0).TransformPointBy(got),pivot+(expectedX^expectedY)*-1);
  equal(FVector(0,0,1).TransformPointBy(got),pivot+(expectedX^(expectedX^expectedY)));
 }
 std::puts("360 attachment rotations agree with skinned local axes");
}
'''
    with tempfile.TemporaryDirectory(prefix='ut99_attach_') as folder:
        cpp = pathlib.Path(folder) / 'attachment.cpp'
        exe = pathlib.Path(folder) / 'attachment.exe'
        cpp.write_text(support + multiply + function + checks)
        subprocess.run([args.compiler, str(cpp), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
