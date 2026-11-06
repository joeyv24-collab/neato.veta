import struct,zlib
def load(path):
    d=open(path,'rb').read()
    ver=struct.unpack('<I',d[23:27])[0];big=ver>=7500
    def rp(o):
        t=chr(d[o]);o+=1
        if t in 'YCILFD':
            f={'Y':'<h','C':'<?','I':'<i','L':'<q','F':'<f','D':'<d'}[t];s=struct.calcsize(f)
            return struct.unpack(f,d[o:o+s])[0],o+s
        if t in 'fdlib':
            n,enc,cl=struct.unpack('<III',d[o:o+12]);o+=12
            raw=d[o:o+cl];o+=cl
            if enc:raw=zlib.decompress(raw)
            f={'f':'f','d':'d','l':'q','i':'i','b':'?'}[t]
            return list(struct.unpack('<%d%s'%(n,f),raw)),o
        n=struct.unpack('<I',d[o:o+4])[0];o+=4
        v=d[o:o+n];o+=n
        return (v.decode('utf8','replace') if t=='S' else v),o
    def rn(o):
        if big:end,np_,pl=struct.unpack('<QQQ',d[o:o+24]);o+=24
        else:end,np_,pl=struct.unpack('<III',d[o:o+12]);o+=12
        nl=d[o];o+=1
        if end==0:return None,o
        name=d[o:o+nl].decode();o+=nl
        props=[]
        for _ in range(np_):
            v,o=rp(o);props.append(v)
        kids=[]
        while o<end:
            k,o=rn(o)
            if k is None:break
            kids.append(k)
        return (name,props,kids),end
    o=27;root=[]
    while o<len(d)-200:
        n,o=rn(o)
        if n is None:break
        root.append(n)
    return root
def child(n,name):
    for k in n[2]:
        if k[0]==name:return k
def props70(n):
    p=child(n,'Properties70');out={}
    if p:
        for q in p[2]:out[q[1][0]]=q[1][4:]
    return out
