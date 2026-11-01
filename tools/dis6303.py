#!/usr/bin/env python3 -I
"""Tiny HD6303 disassembler for the TX81Z ROM. usage: dis6303.py rom bank start end"""
import sys
inh={0x01:'nop',0x04:'lsrd',0x05:'asld',0x06:'tap',0x07:'tpa',0x08:'inx',0x09:'dex',0x0a:'clv',0x0b:'sev',0x0c:'clc',0x0d:'sec',0x0e:'cli',0x0f:'sei',0x10:'sba',0x11:'cba',0x16:'tab',0x17:'tba',0x18:'xgdx',0x19:'daa',0x1a:'slp',0x1b:'aba',
0x30:'tsx',0x31:'ins',0x32:'pula',0x33:'pulb',0x34:'des',0x35:'txs',0x36:'psha',0x37:'pshb',0x38:'pulx',0x39:'rts',0x3a:'abx',0x3b:'rti',0x3c:'pshx',0x3d:'mul',0x3e:'wai',0x3f:'swi'}
br=['bra','brn','bhi','bls','bcc','bcs','bne','beq','bvc','bvs','bpl','bmi','bge','blt','bgt','ble']
un={0:'neg',3:'com',4:'lsr',6:'ror',7:'asr',8:'asl',9:'rol',10:'dec',12:'inc',13:'tst',15:'clr'}
a8=['sub','cmp','sbc',None,'and','bit','ld','st','eor','adc','or','add']
def dis(d,a,base=0x8000):
    op=d[a-base]; hi=op>>4; lo=op&15; n=1
    def b(i): return d[a-base+i]
    if op<0x20:
        return (inh.get(op,'???'),1,'')
    if hi==2: t=(a+2+(b(1)-256 if b(1)>127 else b(1)))&0xffff; return (br[lo],2,'$%04x'%t)
    if hi==3: return (inh.get(op,'???'),1,'')
    if hi in(4,5): return (un.get(lo,'???')+'ab'[hi-4],1,'')
    if hi in(6,7):
        if lo in(1,2,5,11):
            nm={1:'aim',2:'oim',5:'eim',11:'tim'}[lo]
            return (nm,3,'#$%02x,%s'%(b(1),('$%02x,x'%b(2)) if hi==6 else '$%02x'%b(2)))
        if lo==14: return ('jmp',2 if hi==6 else 3,('$%02x,x'%b(1)) if hi==6 else '$%02x%02x'%(b(1),b(2)))
        if lo not in un: return ('???',1,'')
        return (un[lo],2 if hi==6 else 3,('$%02x,x'%b(1)) if hi==6 else '$%04x'%((b(1)<<8)|b(2)))
    mode=hi&3; B=hi>=12
    if lo==13 and not B and mode==0: t=(a+2+(b(1)-256 if b(1)>127 else b(1)))&0xffff; return ('bsr',2,'$%04x'%t)
    sz16 = lo in(3,12,14) or (lo in(13,15) and B) or (lo in(14,15) and not B) or (lo==12 and not B)
    if lo in(3,): nm='addd' if B else 'subd'
    elif lo==12: nm='ldd' if B else 'cpx'
    elif lo==13: nm='std' if B else 'jsr'
    elif lo==14: nm='ldx' if B else 'lds'
    elif lo==15: nm='stx' if B else 'sts'
    else: nm=a8[lo]+('b' if B else 'a')
    if mode==0: 
        if sz16: return (nm,3,'#$%02x%02x'%(b(1),b(2)))
        return (nm,2,'#$%02x'%b(1))
    if mode==1: return (nm,2,'$%02x'%b(1))
    if mode==2: return (nm,2,'$%02x,x'%b(1))
    return (nm,3,'$%02x%02x'%(b(1),b(2)))
if __name__=='__main__':
    rom=open(sys.argv[1],'rb').read(); bank=int(sys.argv[2]); s=int(sys.argv[3],16); e=int(sys.argv[4],16)
    d=rom[bank*0x8000:(bank+1)*0x8000]; a=s
    while a<e:
        m,n,o=dis(d,a)
        print('%04x: %-12s %-5s %s'%(a,d[a-0x8000:a-0x8000+n].hex(' '),m,o)); a+=n
