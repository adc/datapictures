#
#http://korgano.eecs.ohiou.edu/pipermail/python/2009-February/000043.html
#
import SimpleHTTPServer
from SimpleHTTPServer import SimpleHTTPRequestHandler
from BaseHTTPServer import HTTPServer
from PIL import Image
from math import sqrt
from math import floor
import urlparse
from random import randint
import subprocess
import json

def randomImage():
    W = H = 128    
    j = Image.new("RGB", (W, H));
    pixels = j.load();
    for a in range(1, W):
     colors = (randint(0,256),randint(0,256),randint(0,256))
     for b in range(1, H):
      pixels[a,b] = colors
    j.save("test.bmp");
    f = open("test.bmp",'rb');
    data = f.read()
    f.close()
    return data

def genPalette(count):
    colors = []
    for i in xrange(256):
        colors += [(i, 128, 128)]
    return colors

def DataToImage(rawdata):
    
    palette = genPalette(count=256)
    
    #XYZ, rawdata should be a squarenumber
    N = int(floor(len(rawdata)**0.5))
    W = H = N
    #make each byte SxS pixels
    S = 4
    j = Image.new("RGB", (W*S, H*S));
    pixels = j.load();
    for a in xrange(0, W):
     for b in xrange(0, H):
      byteValue = ord(rawdata[a*W + b])
      colors = palette[byteValue]
      for x in range(0, S):
          for y in range(0, S):
              pixels[a*S+x,b*S+y] = colors
    j.save("test.bmp");
    f = open("test.bmp",'rb');
    data = f.read()
    f.close()
    return data

def readMemory(pid, address, length):
    proc = subprocess.Popen(['./memslice','-p',pid,'-r',address,'-l',length], stdout=subprocess.PIPE)
    data = proc.stdout.read()
    if len(data) == 0:
        data = "\x00"*int(length)
    return data
        
def makeImage(args):
    if 'pid' in args and 'length' in args and 'address' in args:
        rawdata = readMemory(args['pid'], args['address'], args['length'])
        return DataToImage(rawdata)
    else:
        """failed"""
        return randomImage()

def getRegions(args):
    if 'pid' in args:
        proc = subprocess.Popen(['./memslice','-p',args['pid']], stdout=subprocess.PIPE)
        data = proc.stdout.read().splitlines()
        regions = []
        for entry in data:
            parts = entry.split()
            perms = parts[3]
            address = parts[5]
            size = parts[7]
            #not a submap and readable
            if parts[0] != '1' and perms != '---':
                regions += [(perms, address, size)]
        return regions
    else:
        return []

def listProcesses(args):
    proc = subprocess.Popen(['ps','-A'], stdout=subprocess.PIPE)
    data = proc.stdout.read().splitlines()
    procs = []
    for entry in data:
        parts = entry.split()
        parts[3] = ' '.join(parts[3:])
        pid, cmd = parts[0], parts[3]
        if pid == 'PID': continue
        procs += [(int(pid), cmd)]
    #sort the processes by pid
    procs.sort()
    return procs

def convertArgs(argDict):
    n = {}
    for key in argDict:
        n[key] = argDict[key][0]
    return n

class MyHandler(SimpleHTTPRequestHandler):
    def do_GET(self):
        try:
            parsedParams = urlparse.urlparse(self.path)
            parseQuery = urlparse.parse_qs(parsedParams.query)
            args = convertArgs(parseQuery)
            if parsedParams.path.endswith('.fun'):
                # get memory contents and convert them to an image
                data = makeImage(args)

                self.send_response(200)
                self.send_header('Content-type','image/bmp')
                self.end_headers()
                self.wfile.write(data)
                return
            elif parsedParams.path.endswith('.list'):
                #retrieve region info for a pid
                regionInfo = getRegions(args)
                self.send_response(200)
                self.send_header('Content-type','text/javascript')
                self.end_headers()
                json.dump(regionInfo, self.wfile)                
                return
            elif parsedParams.path.endswith(".listproc"):
                #get the list of processes on the system
                procListing = listProcesses(args)
                self.send_response(200)
                self.send_header('Content-type','text/javascript')
                self.end_headers()
                json.dump(procListing, self.wfile)
                return
            SimpleHTTPRequestHandler.do_GET(self)
            
        except IOError:
            pass            
def main():
    try:
        webServ = HTTPServer( ('localhost',8000), MyHandler )
        print( 'Started HTTP Server' )
        webServ.serve_forever()
    except KeyboardInterrupt:
        print("Keyboard interupt recieved, terminating webserv");

main()
