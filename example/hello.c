/*
  FUSE: Filesystem in Userspace
  Hao Zheng <hz837@nyu.edu>

  This program may produce several warnings, just ignore them.

  compile: $ gcc hello.c `pkg-config fuse --cflags --libs` -o hello

  run: $ sudo ./hello ./tmp -d

  To enter the file system, you must be in root.
*/

#define ROOTDIRECTORYINODE 26

#define MAXBLOCKNUMBER 10000

#define MAXBLOCKSIZE 4096

#define MAXFILESIZE 1638400

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#include <stdlib.h>

static const char FILEDATAPREFIX[] = "/fusedata/fusedata.";
static const char ROOTNAME[] = "/fusedata/fusedata.26";
static const char SUPERBLOCKNAME[] = "/fusedata/fusedata.0";
int freeblocks[10000-27];
int freelist[25][400];
int nodemap[4000];
char pathmap[4000][50];
int typemap[4000];//directory is 0; file inode is 1;

struct inode{
	int size;
	int uid;
	int gid;
	int mode;
	int atime;
	int ctime;
	int mtime;	
	int linkcount;
	char filetoinode[200][20];
};

struct filenode{
	int size;
	int uid;
	int gid;
	unsigned int mode;
	int atime;
	int ctime;
	int mtime;	
	int linkcount;
	int indirect;
	int location;
};


int getinode(char *path){
	int i;
	for (i = 0; i < 4000; ++i)
	{
		if(strcmp(path, pathmap[i])==0){
			return nodemap[i];
		}
	}
	return -1;
}

int gettype(char *path){
	int i;
	for (i = 0; i < 4000; ++i)
	{
		if(strcmp(path, pathmap[i])==0){
			return typemap[i];
		}
	}
	return -1;
}

int substr(char *buf, char *sub, int start, int len){
	memcpy(sub, &buf[start], len);
	sub[len] = '\0';
	return 0;
}

int writeintofile(char *filename, int data[], int datasize){
	FILE *fp;
	fp=fopen(filename, "w+");
	fseek(fp, 0, SEEK_SET);
	fwrite(data, sizeof(int), datasize, fp);
	fclose(fp);
	return 0;
}

int* readoutfile(char *filename, int datasize){
	FILE *fp;
	fp=fopen(filename, "r");
	fseek(fp, 0, SEEK_SET);
	static int a[400];
	memset(a, 0, sizeof(int)*400);
	fread(a, sizeof(int), datasize, fp);
	fclose(fp);
	return a;
}

int readdata(int nodenumber, char *data){
	FILE *fp;
	char filename[20];
	sprintf(filename, "%s%d", FILEDATAPREFIX, nodenumber);
	fp=fopen(filename, "r");
	fread(data, 1, MAXBLOCKSIZE, fp);
	fclose(fp);
} 

struct inode readstruct(int nodenumber){
    char filename[20];
	sprintf(filename, "%s%d", FILEDATAPREFIX, nodenumber);
    struct inode node;
    FILE *file= fopen(filename, "rb");
	fread(&node, sizeof(struct inode), 1, file);
	fclose(file);
	return node;
}

struct filenode readfilestruct(int nodenumber){
    char filename[20];
	sprintf(filename, "%s%d", FILEDATAPREFIX, nodenumber);
    struct filenode node;
    FILE *file= fopen(filename, "rb");
	fread(&node, sizeof(struct filenode), 1, file);
	fclose(file);
	return node;
}

int findlastblock(int indexblock){
	char filename[20];
	sprintf(filename, "%s%d", FILEDATAPREFIX, indexblock);
	int *p;
	p=readoutfile(filename, 400);
	int i;
	for(i=0;i<400;i++){
		if(*(p+i)==0) break;
	}
	i--;
	return *(p+i);
}

int writestruct(struct inode node, int nodenumber){
	if(nodenumber<=25) return -1;
	struct inode *structpointer;
	structpointer = &node;
	char filename[20];
	sprintf(filename, "%s%d", FILEDATAPREFIX,nodenumber);
	FILE *file= fopen(filename, "wb");
	fwrite(structpointer, sizeof(struct inode), 1, file);
    fclose(file);
    return 0;
}

int writesfiletruct(struct filenode node, int nodenumber){
	if(nodenumber<=25) return -1;
	struct filenode *structpointer;
	structpointer = &node;
	char filename[20];
	sprintf(filename, "%s%d", FILEDATAPREFIX,nodenumber);
	FILE *file= fopen(filename, "wb");
	fwrite(structpointer, sizeof(struct filenode), 1, file);
    fclose(file);
    return 0;
}

int appenddata(int fileblocknumber, char *buf, int size){
	char filename[20];
	sprintf(filename, "%s%d", FILEDATAPREFIX, fileblocknumber);
	FILE *fp;
	fp=fopen(filename, "a");
	fseek(fp, 0, SEEK_SET);
	fwrite(buf, sizeof(char), size, fp);
	fclose(fp);
}

int getnamefromrecord(char *record, char name[]){
	char* ptr=strstr(record, ":");
	char colon=':';
	int counter=0;
	ptr++;
	while(*ptr!=colon)
	{
		name[counter]=*ptr;
		ptr++;
		counter++;	
	}
	name[counter]='\0';
	return 0;
}

int getnextfreeblockandupdate(){
	int node=1;
	int newinode=-1;
	while(node<26){
		int* freeblock;
		char filename[20];
		sprintf(filename, "%s%d", FILEDATAPREFIX, node);
		int listsize;
		if(node==1) listsize=373;
		else listsize=400;
		freeblock=readoutfile(filename, listsize);
		int i;
		for(i=0;i<listsize;i++){
			if(*(freeblock+i)>1){
				newinode=*(freeblock+i);
				printf("next freeblock: %d\n", freeblock[i]);
				freeblock[i]=-1;
				node=25;
				break;
			}
		}
		writeintofile(filename, freeblock, listsize);

		node++;
	}
	printf("%s\n", "Freeblocklist updated!");
	return newinode;
}

int releasefreeblock(int block){
	if(block<400){
		int* freeblock;
		char filename[20];
		sprintf(filename, "%s%d", FILEDATAPREFIX, 1);
		freeblock=readoutfile(filename, 373);
		freeblock[block-27]=block;
		writeintofile(filename, freeblock, 373);
		return 0;
	}
	return -1;
}

int eraseblock(int nodenumber){
	char filename[20];
	sprintf(filename, "%s%d", FILEDATAPREFIX, nodenumber);
	FILE *fp;
	fp=fopen(filename, "w");
	fclose(fp);
	return 0;
}

int updatemap(char* path, int newinode, int type){
	int i;
	for (i = 0; i < 4000; i++)
	{
		if(nodemap[i]==0) break;
	}
	strcpy(pathmap[i], path);
	nodemap[i]=newinode;
	typemap[i]= type;
}

int removemap(char *path){
	int i;
	for (i = 0; i < 4000; ++i)
	{
		if(strcmp(path, pathmap[i])==0){
			strcpy(pathmap[i], "");
			nodemap[i]=0;
			typemap[i]=0;
		}
	}
	return 0;
}

int* getfileinfofrompath(char* path){
	int nodenumber=getinode(path);
	struct filenode node=readfilestruct(nodenumber);
	static int res[2];
	int location=node.location;
	int indirect=node.indirect;
	res[0]=location;
	res[1]=indirect;
	return res;
}

int getparentpath(char* path){
	char* ptr;
	ptr=strrchr(path,'/');
	if(ptr) *ptr='\0';
	if (strlen(path)==0) strcpy(path, "/");
	return 0;
}

int rmrecordfromparent(char *path, int type){
	char* name=strrchr(path, '/');
	name++;
	char parentpath[20];
	strcpy(parentpath, path);
	getparentpath(parentpath);

	int parentinode=getinode(parentpath);
	struct inode parentnode=readstruct(parentinode);
	printf("%s\n", parentnode.filetoinode[2]);
	int j;
	for(j=0;j<200;j++){
		char* record=parentnode.filetoinode[j];
		char recordname[20];
		int namelen=strlen(name);
		if(type==0&&record[0]=='f') continue;
		if(type==1&&record[0]=='d') continue;  
		substr(record, recordname, 2, namelen);
		printf("record: %s; filetoinode: %s\n", recordname, record);
		if(strcmp(recordname,name)==0) break;
	}
	strcpy(parentnode.filetoinode[j], "");
	writestruct(parentnode, parentinode);
	return 0;
}

void* fs_init(){
	int i;
	for(i=0;i<MAXBLOCKNUMBER;i++){
		char filename[20];
		sprintf(filename, "%s%d", FILEDATAPREFIX,i);
		remove(filename);
	}

	for(i=0;i<100;i++){
		char filename[20];
		sprintf(filename, "%s%d", FILEDATAPREFIX,i);
		FILE *fp;
		fp=fopen(filename, "w+");
		int k;
		char x[MAXBLOCKSIZE];
		for (k = 0; k < MAXBLOCKSIZE; k++){
			x[k] = '0';
		}
		fwrite(x, 1, MAXBLOCKSIZE, fp);
		fclose(fp);
	}

	//initialize 25 freeblock lists
	for (i = 0; i < 373; i++){
		freelist[0][i]=i+27;
	}
	for(i=2;i<26;i++){
		int k;
		for(k=400+400*(i-2);k<400+400*(i-1);k++){
			freelist[i-1][(k-400)%400]=k;
		}
	}
	
	//write the lists into 25 files
	for (i = 1; i <26 ; i++){
		char filename[20];
		sprintf(filename, "%s%d", FILEDATAPREFIX,i);
		if(i==1) writeintofile(filename, freelist[i-1], 373);
		else writeintofile(filename, freelist[i-1], sizeof(freelist[i-1])/sizeof(int));
	}

	int superblockdata[7];
	superblockdata[0]= 13743;
	superblockdata[1]= 50;
	superblockdata[2]= 20;
	superblockdata[3]= 1;
	superblockdata[4]= 25;
	superblockdata[5]= ROOTDIRECTORYINODE;
	superblockdata[6]= MAXBLOCKNUMBER;
	writeintofile("/fusedata/fusedata.0", superblockdata, sizeof(superblockdata)/sizeof(int));
	
	int *p;
	p=readoutfile("/fusedata/fusedata.0", 7);
	printf("%s\n", "superblock:");
	for(i=0;i<7;i++){
		printf("%d\n", *(p+i));
	}

	struct inode rootnode={0, 1, 1, 16877, 13223, 13223,13223,2};
	strcpy(rootnode.filetoinode[0], "d:.:26");
	strcpy(rootnode.filetoinode[1], "d:..:26");
	struct inode *structpointer;
	structpointer = &rootnode;
	FILE *file= fopen(ROOTNAME, "wb");
	fwrite(structpointer, sizeof(struct inode), 1, file);
    fclose(file);

	printf("%s\n", "initialize finished!");
	strcpy(pathmap[0], "/");
	nodemap[0]=26;
	typemap[0]=0;

	int index=getinode("/");
	printf("Root node: %d\n", index);
}

int getattr(const char *path, struct stat *stbuf){
	int res = 0;
	memset(stbuf, 0, sizeof(struct stat));
	int type=-1;
	int nodenumber = getinode(path);
	type=gettype(path);
	printf("%d\n", type);
	if(nodenumber==-1){
		res=-ENOENT;
	} 
	else{
		if(type==0){
			struct inode node=readstruct(nodenumber);
			stbuf->st_mode = node.mode;
			stbuf->st_nlink = node.linkcount;
			stbuf->st_size = node.size;
		}
		else if(type==1){
			struct filenode node=readfilestruct(nodenumber);
			stbuf->st_mode = node.mode;
			stbuf->st_nlink = node.linkcount;
			stbuf->st_size = node.size;
			printf("%d\n", node.mode);
		}
	}
	return res;
}


int fs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi){
	char* name=strrchr(path, '/');
	printf("%d\n", size);
	name++;
	int nodenumber=getinode(path);
	struct filenode node= readfilestruct(nodenumber);
	int presize=node.size;
	node.size+=size;
	int firstblocknumber=node.location;
	if(node.size<=MAXBLOCKSIZE){
		writesfiletruct(node, nodenumber);
		appenddata(firstblocknumber, buf, size);
	}else{
		if(node.indirect==0){
			node.indirect=1;
			int indexblock=getnextfreeblockandupdate();
			printf("index block is:%d\n", indexblock);
			int index[400];
			int totalblock=node.size/MAXBLOCKSIZE;
			totalblock++;
			int i;
			index[0]=firstblocknumber;
			index[1]=getnextfreeblockandupdate();
			eraseblock(index[1]);
			printf("seconde block is %d\n", index[1]);
			char filename[20];
			sprintf(filename, "%s%d", FILEDATAPREFIX, indexblock);
			writeintofile(filename, index, 400);
			node.location=indexblock;
			writesfiletruct(node, nodenumber);

			char sub[MAXBLOCKSIZE];
			int restlen=presize%MAXBLOCKSIZE;
			if(restlen==0) restlen=4096;
			substr(buf, sub, 0, MAXBLOCKSIZE-restlen);
			appenddata(firstblocknumber, sub, MAXBLOCKSIZE-restlen);
			substr(buf, sub, MAXBLOCKSIZE-restlen, size-(MAXBLOCKSIZE-restlen));
			appenddata(index[1], sub, size-(MAXBLOCKSIZE-restlen));

		}else{
			writesfiletruct(node, nodenumber);
			int restlen=presize%MAXBLOCKSIZE;
			printf("rest length is %d\n", restlen);
			if(restlen==0) restlen=4096;
			if(MAXBLOCKSIZE-restlen>=size){
				int lastblock=findlastblock(firstblocknumber);
				appenddata(lastblock, buf, size);
			}else{
				char sub[MAXBLOCKSIZE];
				substr(buf, sub, 0, MAXBLOCKSIZE-restlen);
				int lastblock=findlastblock(firstblocknumber);
				printf("last block:%d\n",  lastblock);
				appenddata(lastblock, sub, MAXBLOCKSIZE-restlen);
				int next=getnextfreeblockandupdate();
				eraseblock(next);
				char filename[20];
				sprintf(filename, "%s%d", FILEDATAPREFIX, firstblocknumber);
				int *p;
				p=readoutfile(filename, 400);
				int i;
				for(i=0;i<400;i++){
					if(p[i]==0) break;
				}
				p[i]=next;
				printf("The %dth block is block %d\n", i+1, p[i]);
				writeintofile(filename, p, 400);
				char sub2[MAXBLOCKSIZE];
				substr(buf, sub2, MAXBLOCKSIZE-restlen, size-(MAXBLOCKSIZE-restlen));
				appenddata(next, sub2, size-(MAXBLOCKSIZE-restlen));
			}
		}
	}	
	return size;
}

int fs_open(const char *path, struct fuse_file_info *fi){
	int res=0;
	int nodenumber=getinode(path);
	if(nodenumber==-1) res= -ENOENT;
	printf("file inode number :%d\n", nodenumber);
	return res;
}

int fs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi){
	int* info=getfileinfofrompath(path);
	int location=info[0];
	int indirect=info[1];
	if(indirect==0){
		readdata(location, buf);
		return size;
	}else{
		char filename[20];
		sprintf(filename, "%s%d", FILEDATAPREFIX, location);
		int *p;
		p=readoutfile(filename, 400);
		int i;
		//char buffer[102400];
		for(i=0;i<400;i++){
			if(p[i]==0) break;
			if(i==0) {
				char smallbuffer[MAXBLOCKSIZE];
				readdata(p[i], buf);
				//strcpy(buffer, smallbuffer);
			}
			else{
				char smallbuffer[MAXBLOCKSIZE];
				readdata(p[i], smallbuffer);
				strcat(buf, smallbuffer);
			}
		}
	}
	//strcpy(buf, buffer);
}

int fs_link(const char *from, const char *to){
	printf("%s\n", "hihihi");
	char toparent[20];
	strcpy(toparent, to);
	getparentpath(toparent);
	char *toname;
	toname=strrchr(to, '/');
	toname++;
	int frominode=getinode(from);
	updatemap(to, frominode, 1);

	struct filenode fromnode=readfilestruct(frominode);
	fromnode.linkcount++;
	writesfiletruct(fromnode, frominode);

	int toparentinode=getinode(toparent);
	struct inode parentnode=readstruct(toparentinode);
	int i;
	for (i = 0; i < 200; i++){
		if(strlen(parentnode.filetoinode[i])==0) break;
	}
	char record[20];
	sprintf(record, "f:%s:%d", toname, frominode);
	strcpy(parentnode.filetoinode[i], record);
	writestruct(parentnode, toparentinode);
	return 0;
}

int fs_unlink(const char *path){
	int inode=getinode(path);
	removemap(path);
	struct filenode node=readfilestruct(inode);
	if(node.linkcount==1){
		printf("%s\n", "I am about to be deleted!!");
		if(node.indirect==1){
			char filename[20];
			sprintf(filename, "%s%d", FILEDATAPREFIX, node.location);
			char *p;
			p=readoutfile(filename, 400);
			while(*p>0){
				eraseblock(*p);
				releasefreeblock(*p);
				p++;
			}
		}
		eraseblock(node.location);
		releasefreeblock(node.location);
		eraseblock(inode);
		releasefreeblock(inode);
		rmrecordfromparent(path, 1);
	}else{
		char* name=strrchr(path, '/');
		name++;
		node.linkcount--;
		writesfiletruct(node,inode);
		rmrecordfromparent(path, 1);
	}
	
	return 0;
}

int mkdir(const char *path, mode_t mode){
	int res=0;
	if(getinode(path)!=-1) res=-EEXIST;
	else{
		int newinode=getnextfreeblockandupdate();
		updatemap(path, newinode,0);
		char* name=strrchr(path, '/');
		name++;
		struct inode dirnode={1033, 1000, 1000, 16877, 13223, 13223, 13223, 2};
		char record[20];

		//get new directory record
		char newinodestr[10];
		sprintf(newinodestr, "%d", newinode);
		strcpy(record, "d:.:");
		strcat(record, newinodestr);
		strcpy(dirnode.filetoinode[0], record);

		//get parent directory record
		strcpy(record, "d:..:");
		char parent[10];
		strcpy(parent,path);
		getparentpath(parent);
		int parentnodenumber=getinode(parent);
		char parentnodenumberstr[10];
		sprintf(parentnodenumberstr, "%d", parentnodenumber);
		strcat(record, parentnodenumberstr);
		strcpy(dirnode.filetoinode[1], record);

		//write into new directory block
		writestruct(dirnode, newinode);

		//update current directory block
		struct inode parentnode=readstruct(parentnodenumber);
		int i;
		for (i = 0; i < 200; i++){
			if(strlen(parentnode.filetoinode[i])==0) break;
		}
		sprintf(record, "d:%s:%d", name, newinode);
		strcpy(parentnode.filetoinode[i], record);
		parentnode.linkcount++;
		writestruct(parentnode, parentnodenumber);

	}
	return res;

}



int fs_rename(const char *path, const char *to){
	int inode=getinode(path);
	if(inode==-1) return -ENOENT;
	
	int t=getinode(to);
	if(t>0) return -EEXIST;
	int res=0;
	char parent[20];
	strcpy(parent, path);
	getparentpath(parent);

	char toparent[20];
	strcpy(toparent, to);
	getparentpath(toparent);

	if(strcmp(toparent, parent)!=0) return -ENOENT;

	char *oldname;
	oldname=strrchr(path, '/');
	oldname++;

	char *newname;
	newname=strrchr(to, '/');
	newname++;
	

	int i;
	for(i=0;i<4000;i++){
		if(nodemap[i]==inode) break;
	}
	strcpy(pathmap[i],to);
	int type=typemap[i];
	char pre;
	if(type==0) pre='d';
	if(type==1) pre='f';
	int parentnodenumber=getinode(parent);
	struct inode parentnode=readstruct(parentnodenumber);
	for (i = 0; i < 100; i++)
	{
		char* record=parentnode.filetoinode[i];
		if (strlen(record)!=0)
		{	
			char name[20];
			getnamefromrecord(record, name);

			if(strcmp(name, oldname)==0){
				sprintf(record, "%c:%s:%d", pre, newname, inode);
				strcpy(parentnode.filetoinode[i], record);
			}

		}
	}
	writestruct(parentnode, parentnodenumber);
	return res;
}



int fs_create(const char *path, mode_t mode, struct fuse_file_info *fi){
	int newinode=getnextfreeblockandupdate();
	int newfileblock=getnextfreeblockandupdate();
	updatemap(path, newinode,1);
	char* name=strrchr(path, '/');
	name++;
	struct filenode node={0, 1, 1, 33261, 1323, 1323, 1323, 1,0};
	node.location=newfileblock;
	writesfiletruct(node, newinode);
	eraseblock(newfileblock);
	
	char parent[10];
	strcpy(parent,path);
	getparentpath(parent);
	int parentnodenumber=getinode(parent);
	struct inode parentnode=readstruct(parentnodenumber);
	int i;
	for (i = 0; i < 200; i++){
		if(strlen(parentnode.filetoinode[i])==0) break;
	}
	char record[20];
	sprintf(record, "f:%s:%d", name, newinode);
	strcpy(parentnode.filetoinode[i], record);
	writestruct(parentnode, parentnodenumber);
	return 0;
}

int fs_utimens(const char *path, const struct timespec tv[2]){
	return 0;
}

int fs_chmod(const char *path, mode_t mode){
	return 0;
}

int fs_chown(const char *path, uid_t uid, gid_t gid){
	return 0;
}

int fs_truncate(const char *path, off_t offset, struct fuse_file_info *fi){
	int* location=getfileinfofrompath(path);
	eraseblock(location[0]);
	return 0;
}


int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi){

	(void) offset;
	(void) fi;
	int res = 0;
	int i;
	int nodenumber=getinode(path);
	int validfiles=0;
	if(nodenumber==-1) res= -ENOENT;
	else{
		struct inode node=readstruct(nodenumber);

		for (i = 0; i < 100; i++)
		{
			char* record=node.filetoinode[i];
			if (strlen(record)!=0)
			{	
				validfiles++;
				char name[20];
				getnamefromrecord(record, name);
				printf("%d ; %s ; %s\n", validfiles,record, name);
				filler(buf, name, NULL, 0);
			}
		}
	}
	printf("there are %d valid records\n",validfiles);
	return res;
}

void fs_destroy(){
	int i;
	for(i=0;i<MAXBLOCKNUMBER;i++){
		char filename[20];
		sprintf(filename, "%s%d", FILEDATAPREFIX,i);
		remove(filename);
	}
}

int fs_rmdir(const char* path){
	int inode=getinode(path);
	eraseblock(inode);
	releasefreeblock(inode);
	rmrecordfromparent(path, 0);
	removemap(path);
	return 0;
}

int fs_release(const char* path, struct fuse_file_info *fi){
	return 0;
}

int fs_statfs(const char *path, struct statvfs *stbuf){
	int inode=getinode(path);
	if(inode==-1) return -ENOENT;
	int *p;
	p=readoutfile(SUPERBLOCKNAME, 7);
	stbuf->f_bsize=MAXBLOCKSIZE;
	stbuf->f_frsize=0;
	stbuf->f_blocks=0;
	stbuf->f_bfree=0;
	stbuf->f_files=9975;
	stbuf->f_ffree=9000;
	stbuf->f_favail=9000;
	stbuf->f_fsid=603;
	stbuf->f_flag=0;
	stbuf->f_namemax=0;
	return 0;
}

static struct fuse_operations hello_oper = {
	.init       = fs_init,
	.getattr	= getattr,
	.readdir	= fs_readdir,
	.open		= fs_open,
	.read		= fs_read,
	.mkdir	    = mkdir,
	.create     = fs_create,
	.chown      = fs_chown,
	.chmod      = fs_chmod,
	.utimens    = fs_utimens,
	.write      = fs_write,
	.truncate   = fs_truncate,
	.rename     = fs_rename,
	.link       = fs_link,
	.unlink     = fs_unlink,
	.destroy    = fs_destroy,
	.rmdir      = fs_rmdir,
	.release    = fs_release,
	.statfs     = fs_statfs,
};

int main(int argc, char *argv[])
{

	return fuse_main(argc, argv, &hello_oper, NULL);
}