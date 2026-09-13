"""Check native safe-zone bounds against full-screen and split-screen geometry."""
import argparse,json,re
from pathlib import Path

def collect(path,players,wide):
    text=path.read_text(errors='replace')
    pattern=r'XSAFEUI phase=(\d+) slot=(\d+) size=(\d+) offset=(-?\d+),(-?\d+) world=(\d+),(\d+),(\d+),(\d+) ui=(\d+),(\d+),(\d+),(\d+) aim=([\d.-]+),([\d.-]+)'
    rows=[]
    for match in dict.fromkeys(re.findall(pattern,text)):
        phase,slot,size,dx,dy,*rect=map(int,match[:13]);aim=list(map(float,match[13:]))
        if phase not in range(5) or slot not in range(1,players+1):raise ValueError('Unexpected phase/slot')
        if (size,dx,dy)!=[(100,0,0),(85,0,0),(92,18,-12),(100,0,0),(85,0,0)][phase]:raise ValueError('Wrong safe-zone settings')
        w=480 if wide and players>1 else 640;left=(640-w)//2;p=slot-1
        world=[left+(p%2*w//2 if players>2 else 0),p//2*240 if players>2 else p*240 if players==2 else 0,w//2 if players>2 else w,240 if players>1 else 480]
        ix=(640*(100-size)+100)//200;iy=(480*(100-size)+100)//200
        sx=ix+max(-ix,min(dx,ix));sy=iy+max(-iy,min(dy,iy));sw=640-2*ix;sh=480-2*iy
        x=sx+world[0]*sw//640;y=sy+world[1]*sh//480
        world=[x,y,sx+(world[0]+world[2])*sw//640-x,sy+(world[1]+world[3])*sh//480-y]
        ui=world.copy()
        if rect[:4]!=world:raise ValueError('Wrong whole-picture bounds: '+str(match))
        if rect[4:]!=ui:raise ValueError('World and UI must share bounds: '+str(match))
        if aim!=[0,0]:raise ValueError('Whole-picture scaling requires no crosshair correction')
        rows.append(dict(phase=phase,slot=slot,size=size,offset=[dx,dy],world=world,ui=ui,aimCorrection=aim))
    if {(r['phase'],r['slot']) for r in rows}!={(p,s) for p in range(5) for s in range(1,players+1)}:raise ValueError('Missing phase/slot bounds')
    from PIL import Image
    border_checks=[]
    for phase in range(5):
        shot=path.parent/'screenshots'/('safe_zone_%d.png'%(phase+1))
        if not shot.exists():raise ValueError('Missing native capture: '+str(shot))
        im=Image.open(shot).convert('RGB')
        if im.size not in [(640,480),(853,480)]:raise ValueError('Unexpected native capture size')
        bounds=[r['world'] for r in rows if r['phase']==phase]
        x=min(r[0] for r in bounds);y=min(r[1] for r in bounds)
        right=max(r[0]+r[2] for r in bounds);bottom=max(r[1]+r[3] for r in bounds)
        # Xemu's native capture applies the selected host aspect (853x480
        # for 16:9). Exclude only the fractional boundary pixel from the border.
        from math import floor,ceil
        scale=im.width/640
        x=floor(x*scale);right=ceil(right*scale)
        boxes=[(0,0,im.width,y),(0,bottom,im.width,480),(0,y,x,bottom),(right,y,im.width,bottom)]
        if players==3:
            tile=next(r['world'] for r in rows if r['phase']==phase and r['slot']==2)
            boxes.append((ceil(tile[0]*scale),y+(bottom-y)//2,right,bottom))
        for box in boxes:
            if box[2]<=box[0] or box[3]<=box[1]:continue
            if any(maximum for minimum,maximum in im.crop(box).getextrema()):
                raise ValueError('World/UI leaked into black border: '+str(shot)+' '+str(box))
        border_checks.append(dict(phase=phase,screenshot=str(shot),blackBorder=True))
    return dict(players=players,wide=wide,source=str(path),checks=rows,nativeBorderChecks=border_checks)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('log',type=Path);p.add_argument('--players',type=int,required=True);p.add_argument('--wide',action='store_true');p.add_argument('--output',type=Path)
    a=p.parse_args();d=collect(a.log,a.players,a.wide)
    if a.output:a.output.write_text(json.dumps(d,indent=2)+'\n')
    print('PASS',a.players,'players; wide=',a.wide,';',len(d['checks']),'bounds checks')
