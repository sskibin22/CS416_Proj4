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

#define DISK_SIZE	32*1024*1024
#define BLOCK_SIZE 4096

struct superblock {
	uint32_t	magic_num;
};

int diskfile = -1;
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

typedef unsigned char* bitmap_t;

void set_bitmap(bitmap_t b, int i) {
    b[i / 8] |= 1 << (i & 7);
}

void unset_bitmap(bitmap_t b, int i) {
    b[i / 8] &= ~(1 << (i & 7));
}

uint8_t get_bitmap(bitmap_t b, int i) {
    return b[i / 8] & (1 << (i & 7)) ? 1 : 0;
}

char diskfile_path[PATH_MAX] = "/tmp/foo/bar/choo";
char str[11] = "Hello";
char str2[6] = "world";
char str3[23];
char str4[23] = "The whole world is big";

void print_str(const char* str, const char end) {
    int i = 0;
    while(str[i] != end){
        printf("%c", str[i]);
        i++;
    }
    printf("\n");
}

bitmap_t bm;
struct superblock* s_block_mem;
char* block;
int main(int argc, char** argv){ 
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
    // struct superblock s_block1;
    // s_block1.magic_num = 5;
    // struct superblock s_block2;
    // s_block2.magic_num = 10;
    // struct superblock s_block3;
    // s_block3.magic_num = 15;
    // struct superblock s_block4;
    // s_block4.magic_num = 20;
    // printf("copying data from superblocks to block buffer...\n");
    // memcpy((void*)block, &s_block1, sizeof(struct superblock));
    // memcpy((void*)block + (1*sizeof(struct superblock)), &s_block2, sizeof(struct superblock));
    // memcpy((void*)block + (2*sizeof(struct superblock)), &s_block3, sizeof(struct superblock));
    // memcpy((void*)block + (3*sizeof(struct superblock)), &s_block4, sizeof(struct superblock));

    printf("writing block buffer to disk...\n");
    bio_write(1, block);
    printf("reading superblock from disk...\n");
    bio_read(1, block);
    // s_block_mem = (struct superblock*)block;
    memcpy((void*)s_block_mem, block, sizeof(struct superblock));
    printf("value of magic_num in s_block1: %i\n", s_block_mem->magic_num);
    // s_block_mem = (struct superblock*)block + 1;
    memcpy((void*)s_block_mem, (void*)block + (1*sizeof(struct superblock)), sizeof(struct superblock));
    printf("value of magic_num in s_block2: %i\n", s_block_mem->magic_num);
    // s_block_mem = (struct superblock*)block + 2;
    memcpy((void*)s_block_mem, (void*)block + (2*sizeof(struct superblock)), sizeof(struct superblock));
    printf("value of magic_num in s_block3: %i\n", s_block_mem->magic_num);
    // s_block_mem = (struct superblock*)block + 3;
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
    

    // print_str(diskfile_path, '\0');
    // printf("len: %ld\n", strlen(diskfile_path));
    // printf("%s\n", str);

    // strcat(str, str2);
    // printf("%s\n", str);
    // printf("%s\n", str2);

    // strcpy(str3, str4);
    // if(strcmp(str3, str4) == 0){
    //     printf("same\n");
    // }
    // else{
    //     printf("not same\n");
    // }

    // char* instr = strstr(str3, str2);
    // printf("here\n");
    // print_str(instr, ' ');

    // char* inchar = strchr(str3, 'b');
    // print_str(inchar, '\0');

    return 0;
}