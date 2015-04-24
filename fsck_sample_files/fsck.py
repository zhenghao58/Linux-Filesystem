import time
import sys
__author__ = 'apple'

'''
Hao Zheng
N15613232
hz837@nyu.edu
'''



FILEDATAPREFIX = 'FS/fusedata.'


def _readdatafromfile(filenumber):
    filename = FILEDATAPREFIX + str(filenumber)
    f = open(filename, 'r')
    data = f.read()
    f.close()
    return data

def _readlistfromfile(filenumber):
    filename = FILEDATAPREFIX + str(filenumber)
    f = open(filename, 'r')
    data = f.read()
    f.close()
    list = data.split(', ')

    if list[-1][-1]=='}':
        list[-1]=list[-1][:-1]
    list[0]=list[0].replace('{', '')

    if _isdir(filenumber):
        matching = [s for s in list if "filename_to_inode_dict" in s][0]
        index=list.index(matching)
        list[index:]=[', '.join(list[index:])]
    return list

def _getfilesize(inodenumber):
    inode = _getmetadata(inodenumber)
    if inode['indirect'] == 0:
        data = _readdatafromfile(inode['location'])
        return len(data)
    else:
        indexblockdata = _readdatafromfile(inode['location'])
        arr = indexblockdata.replace(" ", '').split(',')
        data = ''
        for i in arr:
            data += _readdatafromfile(i)
        return len(data)

def _writezero(inodenumber):
    f = open(FILEDATAPREFIX+str(inodenumber), 'w')
    f.write('0'*4096)
    f.close()


def _getinodefromnumber(inodenumber):
    datalist=_readlistfromfile(inodenumber)
    inode={}
    for item in datalist:
        if 'location' in item and 'indirect' in item:
            for i in item.split(' '):
                if 'location' in i:
                    location = i.split(':')[-1]
                elif 'indirect' in i:
                    indirect = i.split(':')[-1]
            inode['location'] = int(location)
            inode['indirect'] = int(indirect)
        else:
            data = item.split(':')[-1]
            name = item.split(':')[0]
            data = int(data)
            inode[name] = data
    return inode

def _writeinodeintofile(inode, inodenumber):
    data = '{'+', '.join("%s:%d" % (key, val) for (key, val) in inode.iteritems())+'}'
    f=open(FILEDATAPREFIX+str(inodenumber), 'w')
    f.write(data)
    f.close()

def _writedirintofile(dir, inodenumber):
    filetoinode = dir['filetoinode']
    del dir['filetoinode']
    filetoinode = 'filename_to_inode_dict: {'+str(filetoinode)[1:-1].replace('\'', '')+'}'
    data = ', '.join("%s:%d" % (key, val) for (key, val) in dir.iteritems())
    data = '{'+', '.join([data, filetoinode])+'}'
    f = open(FILEDATAPREFIX+str(inodenumber), 'w')
    f.write(data)
    f.close()

def _getdirfromnumber(inodenumber):
    datalist=_readlistfromfile(inodenumber)
    dir={}
    filetoinode=datalist[-1]
    filetoinode=filetoinode[filetoinode.index('{')+1:filetoinode.index('}')]
    filetoinodelist=filetoinode.split(', ')
    for item in datalist[:-1]:
        data = item.split(':')[-1]
        name = item.split(':')[0]
        data = int(data)
        dir[name] = data
    dir['filetoinode']=filetoinodelist
    return dir

def _isdir(filenumber):
    data = _readdatafromfile(filenumber)
    return data.find('filename_to') != -1

def _isfilenode(filenumber):
    data = _readdatafromfile(filenumber)
    return data.find('indirect') != -1

def _getblocks():
    free=[]
    for i in range(1, 26):
        data = _readdatafromfile(i)
        data = data.replace(' ', '').split(',')
        data = map(int, data)
        free.extend(data)

    usedlist=[]
    for i in range(26, 10000):
        if i not in free:
            usedlist.append(i)
    return {'used': usedlist, 'free': free}

blockdict = _getblocks()

def _getmetadata(inodenumber):
    if _isdir(inodenumber):
        datalist=_getdirfromnumber(inodenumber)
    elif _isfilenode(inodenumber) or inodenumber == 0:
        datalist=_getinodefromnumber(inodenumber)
    else:
        return 0
    return datalist

def _writemetadata(inode, inodenumber):
    if _isdir(inodenumber):
        _writedirintofile(inode, inodenumber)
    elif _isfilenode(inodenumber) or inodenumber == 0:
        _writeinodeintofile(inode, inodenumber)
    else:
        print "You've had a wrong call to writemetadata"

def _writefreelist(freelist, inodenumber, i):
    n = (i/400+1)*400-1
    m = i/400*400
    for j in range(0, 400):
        if m in freelist and n in freelist:
            break
        if m not in freelist:
            m+=1
        if n not in freelist:
            n-=1
    newdata=freelist[freelist.index(m):freelist.index(n)+1]
    f = open(FILEDATAPREFIX+str(inodenumber), 'w')
    f.write(str(newdata).lstrip('[').rstrip(']'))
    f.close()

def _getparentnumber(inodenumber):
    if inodenumber == 26:
        return 26
    usdelist = blockdict['used']
    for j in usdelist:
        if _isdir(j):
            dir = _getdirfromnumber(j)
            if any(s for s in dir['filetoinode'] if str(inodenumber) in s and 'd:' in s):
                return j
    return 0

def checkdevice():
    superblock = _getmetadata(0)
    deviceid = superblock['devId']
    if deviceid != 20:
        print "Device id is wrong!!"
        return False
    else:
        print "Device id is correct!"
        return True

def checktime():
    superblock = _getmetadata(0)
    if superblock['creationTime'] > time.time():
        print 'Creation time is wrong'
        superblock['creationTime'] = time.time()
        _writeinodeintofile(superblock, 0)

    usedlist = blockdict['used']
    for i in usedlist:
        found = False
        try:
            data=_getmetadata(i)
            if data != 0:
                for x in ['mtime', 'atime', 'ctime']:
                    if data[x] > time.time():
                        print "Time is wrong in block %d; %s " % (i, x)
                        data[x] = int(time.time())
                        found = True
                if found:
                    _writemetadata(data, i)
        except:
            pass

def checkfreeblocks():
    global blockdict
    freelist = blockdict['free']
    usedlist = blockdict['used']
    found = False
    for i in freelist[:]:
        try:
            data = _readdatafromfile(i)
            if not data == '0'*4096:
                found = True
                print "block %d should not be on the free block list as it stored something!!" % i
                freelist.remove(i)
                usedlist.append(i)
                usedlist.sort()
                _writefreelist(freelist, i/400+1, i)
        except:
            pass

    for i in usedlist[:]:
        try:
            data = _readdatafromfile(i)
            if data == '0'*4096:
                found = True
                freelist.append(i)
                freelist.sort()
                usedlist.remove(i)
                _writefreelist(freelist, i/400+1, i)
                print "block %d should be on the free block list as it contains nothing!" % i
        except:
            found = True
            freelist.append(i)
            freelist.sort()
            usedlist.remove(i)
            _writefreelist(freelist, i/400+1, i)
            print "block %d should be on the free block list as it doesn't exist!" % i
            pass

    if not found:
        print "Free block list is correct!!"

def checkdir():
    global blockdict
    usdelist = blockdict['used']
    (found, foundwronglink, foundwrongblock) = [False]*3
    for i in usdelist:
        if _isdir(i):
            data = _getdirfromnumber(i)
            filetoinode = data['filetoinode']
            if 'd:.:'+str(i) not in filetoinode:
                (found, foundwrongblock) = [True]*2
                print 'd:.: in block %d has wrong block number' % i
                matching = [s for s in filetoinode if "d:.:" in s]
                if matching:
                    record = matching[0]
                    index = filetoinode.index(record)
                    filetoinode[index] = 'd:.:'+str(i)
                else:
                    filetoinode.append('d:.:'+str(i))


            matching = [s for s in filetoinode if "d:..:" in s]
            if matching:
                parentrecord = matching[0]
                parentnumber = parentrecord.split(":..:")[-1]
                correctparent = _getparentnumber(i)
                if int(parentnumber) != correctparent:
                    (found, foundwrongblock) = [True]*2
                    print 'parent block number of block %d is wrong' % i
                    index = filetoinode.index(parentrecord)
                    filetoinode[index] = 'd:..:' + str(correctparent)
            else:
                (found, foundwrongblock) = [True]*2
                filetoinode.append('d:..:'+str(correctparent))
                print 'parent block number of block %d is wrong' % i


            linkcount = data['linkcount']
            if linkcount!=len(filetoinode):
                print 'Linkcount of block %d is wrong!' % i
                (found, foundwronglink) = [True]*2
                data['linkcount'] = len(filetoinode)

            if found:
                _writemetadata(data, i)

    if not foundwronglink:
        print "All link counts are correct!!"
    if not foundwrongblock:
        print 'All directories inode contain correct block number information!!'

def checkindirectandsize():
    global blockdict
    usdelist = blockdict['used']
    freelist = blockdict['free']
    found = False
    for i in usdelist:
        if _isfilenode(i):
            needwrite = False
            inode = _getmetadata(i)
            indirect = inode['indirect']
            location = inode['location']
            indexblockdata = _readdatafromfile(location)
            arr = indexblockdata.replace(" ", '').split(',')
            size = inode['size']
            if indirect == 1:
                if any(not s.isdigit() for s in arr):
                    print "The data contained in a location pointer %s is not an array!!" % location
                    found = True
                elif len(arr) == 1:
                    found = True
                    print 'The indirect of block %d should be 0 as it just contains one file block' % i
                    inode['indirect'] = 0
                    inode['location'] = int(arr[0])
                    _writezero(location)
                    usdelist.remove(location)
                    freelist.append(location)
                    freelist.sort()
                    _writemetadata(inode, i)
                    _writefreelist(freelist, location/400+1, location)
                if size >= 4096*len(arr) or size <= 4096 *(len(arr)-1):
                    print 'The size of block %d is wrong!' % i
                    inode['size'] = _getfilesize(i)
                    needwrite = True
                    found = True
            elif indirect == 0:
                if all(s.isdigit() for s in arr) and len(arr)>1:
                    print "There is something wrong block %d's indirect number because the location block contains just an array!" % i
                    found = True
                    inode['indirect'] = 1
                    needwrite = True

                if size>=4096 or size <=0:
                    print "The size of block %d is wrong! It cannot be larger than 4096 and must be postive!"
                    inode['size'] = _getfilesize(i)
                    found = True
                    needwrite = True
            else:
                print "Indirect number in block %d could only be 1 or 0!!" % i
                found = True

            if needwrite:
                _writemetadata(inode, i)

    if not found:
        print "Indirect and the size of all files are correct!"


def main():
    if not checkdevice():
        print 'Program will exit!'
        sys.exit()
    print 'Begin check free blocks!'
    checkfreeblocks()
    print 'Check freeblocks finished'
    checkdir()
    print 'Check directory finished'
    checktime()
    print 'Check time finished'
    checkindirectandsize()
    print 'All finished'

if __name__ == "__main__":
    main()



