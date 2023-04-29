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
#define BLOCK_SIZE 1024
#define MAX_INUM 1024
#define MAX_DNUM 16384
#define NUM_DPTRS 16
#define IREG_IDX 3

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
    uint32_t    inodes_per_blk;
	uint32_t    dirents_per_blk;
	uint32_t    max_dblks;
};

struct inode {
	uint16_t	ino;				/* inode number */
	uint16_t	valid;				/* validity of the inode */
	uint32_t	size;				/* size of the file */
	uint32_t	type;				/* type of the file */
	uint32_t	link;				/* link count */
	int			direct_ptr[NUM_DPTRS];		/* direct pointer to data block */
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

int fill_dirent(struct dirent dir, const char* fname, size_t flen, uint16_t inum){
    dir.ino = inum;
    dir.valid = 1;
    strcpy(dir.name, fname);
    dir.len = flen;
    return 0;
}
void print_dirent(struct dirent dir){
    printf("valid: %i | name: %s | len: %d | ino: %i\n", dir.valid, dir.name, dir.len, dir.ino);
}
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
char str6[100];

void print_str(const char* str, const char end) {
    int i = 0;
    while(str[i] != end){
        printf("%c", str[i]);
        i++;
    }
    printf("\n");
}
char str7[100] = "/tmp/foo/bar/meh/some.txt";

int seperate_path(char* path, char* token) {
    if((token = strtok_r(path, "/", &path))){
        printf("token: %s\n", token);
        printf("path: %s\n", path);
        printf("\n");
        if(strcmp(path, "\0") == 0){
            printf("last\n");
            return 0;
        }
        seperate_path(path, token);
        return 0;
    }
    else{
        return 0;
    }
}
int sep_path_helper(const char* path){
    char* tok = NULL;
    char str[PATH_MAX];
    strcpy(str, path);
    seperate_path(str, tok);
    return 0;
}

int get_node_by_path(const char *path) {
    if(strcmp(path, "/") == 0){
        printf("root\n");
        return 0;
    }
    const char* path_ptr = path;
    while(strcmp(path_ptr, "\0") != 0){
        if(path_ptr[0] == '/'){
            path_ptr++;
        }
        int name_len = strcspn(path_ptr, "/");
        char* name = (char*)calloc(name_len, sizeof(char));
        strncpy(name, path_ptr, name_len);
        if(strcmp(name, "\0") == 0){
            free(name);
            return 0;
        }
        printf("%s\n", name);
        path_ptr += name_len;
        free(name);
    }
    return 0;
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
            printf("NUM INODE BLOCKS: %ld\n", (sizeof(struct inode)*MAX_INUM)/BLOCK_SIZE);
            printf("DIRENTS PER BLOCK: %ld\n", BLOCK_SIZE / sizeof(struct dirent));
            printf("MAX DATA BLOCKS(on disk): %ld\n", (DISK_SIZE/BLOCK_SIZE) - (((sizeof(struct inode)*MAX_INUM)/BLOCK_SIZE)+3));
            printf("MAX NUM INODES: %d\n", MAX_INUM);
            printf("MAX NUM DATA BLOCKS: %d\n", MAX_DNUM);
            printf("MAX FILE SIZE: %d\n", NUM_DPTRS * BLOCK_SIZE);
            printf("DATA REGION START BLOCK: %ld\n", ((sizeof(struct inode)*MAX_INUM)/BLOCK_SIZE) + (IREG_IDX + 1));
            printf("superblock: %ld bytes\n", sizeof(struct superblock));
            printf("inode: %ld bytes\n", sizeof(struct inode));
            printf("dirent: %ld bytes\n", sizeof(struct dirent));
            printf("============\n");


            printf("%d\n", S_IFDIR | 0755 );
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

            uint16_t x = 0;
            uint32_t y = 16;
            uint16_t num = x % y;
            printf("%d\n", num);

            char str_name[100] = "file123";
            size_t len = strlen(str_name);
            // struct dirent* direct_en = (struct dirent*)calloc(1, sizeof(struct dirent));
            // struct dirent direct_en;
            struct dirent direct_en;
            fill_dirent(direct_en, str_name, len, 10);
            print_dirent(direct_en);
            // free(direct_en);
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

            strcpy(str6, ".");
            printf("%s\n", str6);
            printf("len str6: %ld\n", strlen(str6));
            printf("\n");
            sep_path_helper(str7);
            printf("%s\n", str7);
            printf("\n");

            const char str8[PATH_MAX] = "/foo/bar";
            
            // char path_cpy[PATH_MAX];
            char* path_cpy = (char*)calloc(strlen(str8), sizeof(char));
            strcpy(path_cpy, str8);
            printf("path_cpy: %s\n", path_cpy);
            char* base = basename(path_cpy);
            char* dir = dirname(path_cpy);
            
            // char* new_base;
            printf("dirname: %s\n", dir);
            printf("basename: %s\n", base);
            if(strcmp(dir, "/") == 0){
                printf("in root\n");
                base = str8;
                base++;
                printf("base: %s\n", base);
                size_t len = strlen(base);
                printf("len: %ld\n", len);
            }
            else{
                printf("not same\n");
            }
            char* base1 = basename(str8);
            char* dir1 = dirname(str8);
            
            printf("dirname: %s\n", dir1);
            printf("basename: %s\n", base1);
            printf("og path: %s\n", str8);
            free(path_cpy);
            printf("\n");
            const char path[PATH_MAX] = "/tmp/foo/bar/baz/a.txt";
            const char path2[PATH_MAX] = "/";
            const char path3[PATH_MAX] = "/tmp";
            const char path4[PATH_MAX] = "/tmp/";
            const char path5[PATH_MAX] = "tmp";

            printf("testing: %s\n", path);
            get_node_by_path(path);
            printf("path: %s\n", path);
            printf("-----------------\n");
            printf("testing: %s\n", path2);
            get_node_by_path(path2);
            printf("path: %s\n", path2);
            printf("-----------------\n");
            printf("testing: %s\n", path3);
            get_node_by_path(path3);
            printf("path: %s\n", path3);
            printf("-----------------\n");
            printf("testing: %s\n", path4);
            get_node_by_path(path4);
            printf("path: %s\n", path4);
            printf("-----------------\n");
            printf("testing: %s\n", path5);
            get_node_by_path(path5);
            printf("path: %s\n", path5);
            printf("-----------------\n");
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