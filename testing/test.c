#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <limits.h>
#include <linux/limits.h>

#define DISK_SIZE  32*1024*1024
#define BLOCK_SIZE 4096
#define MAX_INUM 1024
#define MAX_DNUM 16384

/*******************************************************************************************************/
/*Data Structures*/
/*******************************************************************************************************/
struct superblock {
	uint32_t	magic_num;			/* magic number */
	uint16_t	max_inum;			/* maximum inode number */
	uint16_t	max_dnum;			/* maximum data block number */
	uint32_t	i_bitmap_blk;		/* start block of inode bitmap */
	uint32_t	d_bitmap_blk;		/* start block of data block bitmap */
	uint32_t	i_start_blk;		/* start block of inode region */
	uint32_t	d_start_blk;		/* start block of data block region */
};

struct inode {
	uint16_t	ino;				/* inode number */
	uint16_t	valid;				/* validity of the inode */
	uint32_t	size;				/* size of the file */
	uint32_t	type;				/* type of the file */
	uint32_t	link;				/* link count */
	int			direct_ptr[16];		/* direct pointer to data block */
	int			indirect_ptr[8];	/* indirect pointer to data block */
	struct stat	vstat;				/* inode stat */
};

struct dirent {
	uint16_t ino;					/* inode number of the directory entry */
	uint16_t valid;					/* validity of the directory entry */
	char name[208];					/* name of the directory entry */
	uint16_t len;					/* length of name */
};

typedef unsigned char* bitmap_t;
/*******************************************************************************************************/
/*Global Variables*/
/*******************************************************************************************************/
bitmap_t bm;
struct superblock* s_block_mem;
char* block;
int diskfile = -1;
/*******************************************************************************************************/
/*File System low-level Functions*/
/*******************************************************************************************************/
//Creates a file which is your new emulated disk
void dev_init(const char* diskfile_path) {
    if (diskfile >= 0) {
		return;
    }
    
    diskfile = open(diskfile_path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (diskfile < 0) {
		perror("disk_open failed");
		exit(EXIT_FAILURE);
    }
	
    ftruncate(diskfile, DISK_SIZE);
}
int dev_open(const char* diskfile_path) {
    if (diskfile >= 0) {
		return 0;
    }
    
    diskfile = open(diskfile_path, O_RDWR, S_IRUSR | S_IWUSR);
    if (diskfile < 0) {
		perror("disk_open failed");
		return -1;
    }
	return 0;
}

void dev_close() {
    if (diskfile >= 0) {
		close(diskfile);
    }
}

//Read a block from the disk
int bio_read(const int block_num, void *buf) {
    int retstat = 0;
    retstat = pread(diskfile, buf, BLOCK_SIZE, block_num*BLOCK_SIZE);
    if (retstat <= 0) {
		memset (buf, 0, BLOCK_SIZE);
		if (retstat < 0)
			perror("block_read failed");
    }

    return retstat;
}

//Write a block to the disk
int bio_write(const int block_num, const void *buf) {
    int retstat = 0;
    retstat = pwrite(diskfile, buf, BLOCK_SIZE, block_num*BLOCK_SIZE);
    if (retstat < 0) {
		    perror("block_write failed");
    }
    return retstat;
}

/*******************************************************************************************************/
/*Bitmaps*/
/*******************************************************************************************************/
void set_bitmap(bitmap_t b, int i) {
    b[i / 8] |= 1 << (i & 7);
}

void unset_bitmap(bitmap_t b, int i) {
    b[i / 8] &= ~(1 << (i & 7));
}

uint8_t get_bitmap(bitmap_t b, int i) {
    return b[i / 8] & (1 << (i & 7)) ? 1 : 0;
}

/*******************************************************************************************************/
/*String Ops*/
/*******************************************************************************************************/
char diskfile_path[PATH_MAX] = "/tmp/foo/bar/choo";
char str[11] = "Hello";
char str2[6] = "world";
char str3[23];
char str4[23] = "The whole world is big";
char str5[23];

void print_str(const char* str, const char end) {
    int i = 0;
    while(str[i] != end){
        printf("%c", str[i]);
        i++;
    }
    printf("\n");
}

/*******************************************************************************************************/
/*===============================================MAIN===================================================*/
/*******************************************************************************************************/
int main(int argc, char** argv){ 
    if(argc == 2){ 
        int arg = atoi(argv[1]);   
    /*******************************************************************************************************/
    /*Size Info*/
    /*******************************************************************************************************/
        if(arg == 0){
            printf("SIZING INFO:\n");
            printf("============\n");
            printf("DISK SIZE: %d bytes\n", DISK_SIZE);
            printf("BLOCK SIZE: %d bytes\n", BLOCK_SIZE);
            printf("TOTAL BLOCKS IN DISK: %d\n", DISK_SIZE / BLOCK_SIZE);
            printf("INODES PER BLOCK: %ld\n", BLOCK_SIZE / sizeof(struct inode));
            printf("NUM INODE BLOCKS: %ld\n", MAX_INUM / (BLOCK_SIZE / sizeof(struct inode)));
            printf("DIRENTS PER BLOCK: %ld\n", BLOCK_SIZE / sizeof(struct dirent));
            printf("NUM DIRENT BLOCKS: %ld\n", MAX_DNUM / (BLOCK_SIZE / sizeof(struct dirent)));
            printf("MAX NUM INODES: %d\n", MAX_INUM);
            printf("MAX NUM DATA ENTRIES: %d\n", MAX_DNUM);
            printf("superblock: %ld bytes\n", sizeof(struct superblock));
            printf("inode: %ld bytes\n", sizeof(struct inode));
            printf("dirent: %ld bytes\n", sizeof(struct dirent));

            struct dirent* nd = (struct dirent*)calloc(1, sizeof(struct dirent));
            nd->name[0] = 'n';
            printf("ino: %u\n", nd->ino);
            printf("len: %u\n", nd->len);
            printf("valid: %u\n", nd->valid);
            printf("name: %s\n", nd->name);
            free(nd);

            int* darr = (int*)malloc(4*sizeof(int));
            memset(darr, 0, 4*sizeof(int));
            darr[0] = 10;
            for(int i = 0; i < 4; i++){
                printf("%i ", darr[i]);
            }
            printf("\n");
            free(darr);
        }
    /*******************************************************************************************************/
    /*Testing open/read/write/close disk mem with data structures and bitmaps*/
    /*******************************************************************************************************/
        else if(arg == 1){
            block = (char*)malloc(BLOCK_SIZE);
            memset(block, 0, BLOCK_SIZE);
            dev_init("foo/disk");
            dev_open("foo/disk");
            printf("init superblock...\n");
            s_block_mem = (struct superblock*)malloc(sizeof(struct superblock));
            printf("copying data from superblocks to block buffer...\n");
            s_block_mem->magic_num = 5;
            memcpy((void*)block, (void*)s_block_mem, sizeof(struct superblock));
            s_block_mem->magic_num = 10;
            memcpy((void*)block + (1*sizeof(struct superblock)), (void*)s_block_mem, sizeof(struct superblock));
            s_block_mem->magic_num = 15;
            memcpy((void*)block + (2*sizeof(struct superblock)), (void*)s_block_mem, sizeof(struct superblock));
            s_block_mem->magic_num = 20;
            memcpy((void*)block + (3*sizeof(struct superblock)), (void*)s_block_mem, sizeof(struct superblock));

            printf("writing block buffer to disk...\n");
            bio_write(1, block);
            printf("reading superblock from disk...\n");
            bio_read(1, block);

            memcpy((void*)s_block_mem, block, sizeof(struct superblock));
            printf("value of magic_num in s_block1: %i\n", s_block_mem->magic_num);

            memcpy((void*)s_block_mem, (void*)block + (1*sizeof(struct superblock)), sizeof(struct superblock));
            printf("value of magic_num in s_block2: %i\n", s_block_mem->magic_num);

            memcpy((void*)s_block_mem, (void*)block + (2*sizeof(struct superblock)), sizeof(struct superblock));
            printf("value of magic_num in s_block3: %i\n", s_block_mem->magic_num);

            memcpy((void*)s_block_mem, (void*)block + (3*sizeof(struct superblock)), sizeof(struct superblock));
            printf("value of magic_num in s_block4: %i\n", s_block_mem->magic_num);

            bm = (bitmap_t)malloc(8*sizeof(char));
            memset(bm, 0, 8*sizeof(char));
            for(int i = 0; i < 8; i++){
                printf("%d :: ", i);
                for(int j = 0; j < 8; j++){
                    printf("%d | ", get_bitmap(bm, (i*8)+j));
                }
                printf("\n");
            }
            printf("\n");
            printf("writing bitmap to disk...\n");
            memset(block, 0, BLOCK_SIZE);
            memcpy(block, bm, 8*sizeof(char));
            bio_write(0, block);
            printf("reading bitmap from disk...\n");
            bio_read(0, block);
            memcpy(bm, block, 8*sizeof(char));
            for(int i = 0; i < 8; i++){
                printf("%d :: ", i);
                for(int j = 0; j < 8; j++){
                    printf("%d | ", get_bitmap(bm, (i*8)+j));
                }
                printf("\n");
            }
            printf("\n");
            printf("set bit 16...\n");
            set_bitmap(bm, 16);
            for(int i = 0; i < 8; i++){
                printf("%d :: ", i);
                for(int j = 0; j < 8; j++){
                    printf("%d | ", get_bitmap(bm, (i*8)+j));
                }
                printf("\n");
            }
            printf("\n");
            printf("writing bitmap to disk...\n");
            memcpy(block, bm, 8*sizeof(char));
            bio_write(0, block);

            printf("unset bit 16...\n");
            unset_bitmap(bm, 16);
            for(int i = 0; i < 8; i++){
                printf("%d :: ", i);
                for(int j = 0; j < 8; j++){
                    printf("%d | ", get_bitmap(bm, (i*8)+j));
                }
                printf("\n");
            }
            printf("\n");

            printf("reading bitmap from disk...\n");
            bio_read(0, block);
            memcpy(bm, block, 8*sizeof(char));

            for(int i = 0; i < 8; i++){
                printf("%d :: ", i);
                for(int j = 0; j < 8; j++){
                    printf("%d | ", get_bitmap(bm, (i*8)+j));
                }
                printf("\n");
            }
            printf("\n");
            
            free(bm);
            free(block);
            free(s_block_mem);
            dev_close();
        }
    /*******************************************************************************************************/
    /*Testing string ops*/
    /*******************************************************************************************************/
        else if(arg == 2){
            print_str(diskfile_path, '\0');
            printf("len: %ld\n", strlen(diskfile_path));
            printf("%s\n", str);

            strcat(str, str2);
            printf("%s\n", str);
            printf("%s\n", str2);

            strcpy(str3, str4);
            if(strcmp(str3, str4) == 0){
                printf("same\n");
            }
            else{
                printf("not same\n");
            }

            char* instr = strstr(str3, str2);
            printf("here\n");
            print_str(instr, ' ');

            char* inchar = strchr(str3, 'b');
            print_str(inchar, '\0');

            strcpy(str5, inchar);
            printf("%s\n", str5);
        }
        else{
            printf("Bad Usage: Must pass in one of the following integers...\n");
            printf("0 :: Data Structure Size Information\n");
            printf("1 :: File I/O Ops\n");
            printf("2 :: String Ops\n");
            exit(1);
        }
    }
    else{
        printf("Bad Usage: Must pass in one of the following integers...\n");
        printf("0 :: Data Structure Size Information\n");
        printf("1 :: File I/O Ops\n");
        printf("2 :: String Ops\n");
        exit(1); 
    }
    
    return 0;
}