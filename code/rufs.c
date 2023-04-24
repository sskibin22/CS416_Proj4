/*
 *  Copyright (C) 2023 CS416 Rutgers CS
 *	Tiny File System
 *	File:	rufs.c
 *
 */

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <limits.h>
#include <math.h>

#include "block.h"
#include "rufs.h"

#define SUPER_IDX 0
#define IBM_IDX 1
#define DBM_IDX 2
#define IREG_IDX 3


char diskfile_path[PATH_MAX];
int inodes_per_block = 0;
int level2_dirptrs = 0;
int max_inode_blocks = 0;
int max_dirents_per_block = 0;
struct superblock* s_block_mem;
// char* block_buff;
bitmap_t inode_bm;
bitmap_t data_bm;
// struct inode* curr_inode;
// struct dirent* curr_dirent;

// Declare your in-memory data structures here

/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {
	char* block_buffer = (char*)malloc(BLOCK_SIZE);
	memset(block_buffer, 0, BLOCK_SIZE);
	// Step 1: Read inode bitmap from disk
	bio_read(s_block_mem->i_bitmap_blk, block_buffer);
	memcpy(inode_bm, block_buffer, (MAX_INUM/8)*sizeof(char));

	// Step 2: Traverse inode bitmap to find an available slot
	for(int i = 0; i < MAX_INUM; i++){
		if(get_bitmap(inode_bm, i) == 0){
			// Step 3: Update inode bitmap and write to disk 
			set_bitmap(inode_bm, i);
			memcpy(block_buffer, inode_bm, (MAX_INUM/8)*sizeof(char));
			bio_write(s_block_mem->i_bitmap_blk, block_buffer);
			free(block_buffer);
			return i;
		}
	}

	free(block_buffer);
	return -1;
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {
	char* block_buffer = (char*)malloc(BLOCK_SIZE);
	memset(block_buffer, 0, BLOCK_SIZE);
	// Step 1: Read data block bitmap from disk
	bio_read(s_block_mem->d_bitmap_blk, block_buffer);
	memcpy(data_bm, block_buffer, (MAX_DNUM/8)*sizeof(char));

	// Step 2: Traverse data block bitmap to find an available slot
	for(int i = 0; i < MAX_DNUM; i++){
		if(get_bitmap(data_bm, i) == 0){
			// Step 3: Update data block bitmap and write to disk 
			set_bitmap(data_bm, i);
			memcpy(block_buffer, data_bm, (MAX_DNUM/8)*sizeof(char));
			bio_write(s_block_mem->d_bitmap_blk, block_buffer);
			free(block_buffer);
			return i;
		}
	}

	free(block_buffer);
	return -1;
}

/* 
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {
	char* block_buffer = (char*)malloc(BLOCK_SIZE);
	memset(block_buffer, 0, BLOCK_SIZE);
  // Step 1: Get the inode's on-disk block number
  	uint16_t blk = ((ino * sizeof(struct inode)) / BLOCK_SIZE) + s_block_mem->i_start_blk; 

  // Step 2: Get offset of the inode in the inode on-disk block
  	uint16_t idx = ino % inodes_per_block;

  // Step 3: Read the block from disk and then copy into inode structure
	bio_read(blk, block_buffer);
	memcpy((void*)inode, (void*)block_buffer + (idx*sizeof(struct inode)), sizeof(struct inode));

	free(block_buffer);
	return 0;
}

int writei(uint16_t ino, struct inode *inode) {
	char* block_buffer = (char*)malloc(BLOCK_SIZE);
	memset(block_buffer, 0, BLOCK_SIZE);
	// Step 1: Get the block number where this inode resides on disk
	uint16_t blk = ((ino * sizeof(struct inode)) / BLOCK_SIZE) + s_block_mem->i_start_blk; 
	
	// Step 2: Get the offset in the block where this inode resides on disk
	uint16_t idx = ino % inodes_per_block;

	// Step 3: Write inode to disk 
	memcpy((void*)block_buffer + (idx*sizeof(struct inode)), (void*)inode, sizeof(struct inode));
	bio_write(blk, block_buffer);

	free(block_buffer);
	return 0;
}


/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {
	char* block_buffer = (char*)malloc(BLOCK_SIZE);
	memset(block_buffer, 0, BLOCK_SIZE);
	struct inode* curr_inode = (struct inode*)malloc(sizeof(struct inode));
	memset(curr_inode, 0, sizeof(struct inode));
	struct dirent* curr_dirent = (struct dirent*)malloc(sizeof(struct dirent));
	memset(curr_dirent, 0, sizeof(struct dirent));
  // Step 1: Call readi() to get the inode using ino (inode number of current directory)
  	readi(ino, curr_inode);

  // Step 2: Get data block of current directory from inode
  // Step 3: Read directory's data block and check each directory entry.
  //If the name matches, then copy directory entry to dirent structure
	int blks = ceil(curr_inode->size / BLOCK_SIZE);
	for(int i = 0; i < NUM_DPTRS; i++){
		if(curr_inode->direct_ptr[i] != 0){
			bio_read(curr_inode->direct_ptr[i], block_buffer);
			for(int j = 0; j < max_dirents_per_block; j++){
				memcpy((void*)curr_dirent, (void*)block_buffer + (j*sizeof(struct dirent)), sizeof(struct dirent));
				if(curr_dirent->valid == 1 && strcmp(fname, curr_dirent->name) == 0){
                	memcpy(dirent, curr_dirent, sizeof(struct dirent));
					memset(block_buffer, 0, BLOCK_SIZE);
					free(curr_inode);
					free(curr_dirent);
					free(block_buffer);
					return 0;
            	}
			}
			memset(block_buffer, 0, BLOCK_SIZE);
		}
	}
	if(blks > NUM_DPTRS){
		int* indir_ptrs_block_buff = (int*)malloc(BLOCK_SIZE);
		memset(indir_ptrs_block_buff, 0, BLOCK_SIZE);
		for(int i = 0; i < NUM_IDPTRS; i++){
			if(curr_inode->indirect_ptr[i] != 0){
				bio_read(curr_inode->indirect_ptr[i], indir_ptrs_block_buff);
				for(int j = 0; j < level2_dirptrs; j++){
					if(indir_ptrs_block_buff[j] != 0){
						bio_read(indir_ptrs_block_buff[j], block_buffer);
						for(int k = 0; k < max_dirents_per_block; k++){
							memcpy((void*)curr_dirent, (void*)block_buffer + (k*sizeof(struct dirent)), sizeof(struct dirent));
							if(curr_dirent->valid == 1 && strcmp(fname, curr_dirent->name) == 0){
								memcpy(dirent, curr_dirent, sizeof(struct dirent));
								memset(block_buffer, 0, BLOCK_SIZE);
								free(curr_inode);
								free(curr_dirent);
								free(block_buffer);
								free(indir_ptrs_block_buff);
								return 0;
							}
						}
						memset(block_buffer, 0, BLOCK_SIZE);
					}
				}
				memset(indir_ptrs_block_buff, 0, BLOCK_SIZE);
			}
		}
		free(indir_ptrs_block_buff);
	}
	free(curr_inode);
	free(curr_dirent);
	free(block_buffer);
	return -1;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {
	// Step 1: Read dir_inode's data block and check each directory entry of dir_inode
	// Step 2: Check if fname (directory name) is already used in other entries
	// Step 3: Add directory entry in dir_inode's data block and write to disk
	// Allocate a new data block for this directory if it does not exist
	// Update directory inode
	// Write directory entry
	struct dirent* curr_dirent = (struct dirent*)malloc(sizeof(struct dirent));
	memset(curr_dirent, 0, sizeof(struct dirent));
	if(dir_find(dir_inode.ino, fname, name_len, curr_dirent) == -1){
		char* block_buffer = (char*)malloc(BLOCK_SIZE);
		memset(block_buffer, 0, BLOCK_SIZE);
		// int blks = ceil(dir_inode.size / BLOCK_SIZE);
		for(int i = 0; i < NUM_DPTRS; i++){
			if(dir_inode.direct_ptr[i] != 0){
				bio_read(dir_inode.direct_ptr[i], block_buffer);
				for(int j = 0; j < max_dirents_per_block; j++){
					memcpy((void*)curr_dirent, (void*)block_buffer + (j*sizeof(struct dirent)), sizeof(struct dirent));
					if(curr_dirent->valid == 0){
						curr_dirent->ino = f_ino;
						curr_dirent->len = name_len;
						strcpy(curr_dirent->name, fname);
						curr_dirent->valid = 1;
						/*UPDATE dir_inode*/
							/*vstat, link*/

						/*WRITE new dirent to disk*/
						memcpy((void*)block_buffer + (j*sizeof(struct dirent)), (void*)curr_dirent, sizeof(struct dirent));
						bio_write(dir_inode.direct_ptr[i], block_buffer);
					
						free(curr_dirent);
						free(block_buffer);
						return 0;
					}
				}
				memset(block_buffer, 0, BLOCK_SIZE);
			}
			else{
				curr_dirent->ino = f_ino;
				curr_dirent->len = name_len;
				strcpy(curr_dirent->name, fname);
				curr_dirent->valid = 1;
				
				dir_inode.direct_ptr[i] = get_avail_blkno();
				if(dir_inode.direct_ptr[i] == -1){
					perror("no free data blocks");
					free(curr_dirent);
					free(block_buffer);
					return -1;
				}
				dir_inode.size += BLOCK_SIZE;
				/*UPDATE dir_inode*/
				/*link, vstat*/

				/*Build new block buffer*/
				memcpy((void*)block_buffer, (void*)curr_dirent, sizeof(struct dirent));
				for(int x = 1; x < max_dirents_per_block; x++){
					struct dirent* new_dirent = (struct dirent*)calloc(1, sizeof(struct dirent));
					memcpy((void*)block_buffer + (x*sizeof(struct dirent)), (void*)new_dirent, sizeof(struct dirent));
					free(new_dirent);
				}
				/*WRITE new dirent to disk*/
				bio_write(dir_inode.direct_ptr[i], block_buffer);

				free(curr_dirent);
				free(block_buffer);
				return 0;
			}
		}
		/*No free space for a new dirent in direct_ptrs of dir_inode*/
		/*Begin to search for free space for new dirent in indirect_ptrs of dir_inode*/
		int* indir_ptrs_block_buff = (int*)malloc(BLOCK_SIZE);
		memset(indir_ptrs_block_buff, 0, BLOCK_SIZE);
		for(int i = 0; i < NUM_IDPTRS; i++){
			if(dir_inode.indirect_ptr[i] != 0){
				bio_read(dir_inode.indirect_ptr[i], indir_ptrs_block_buff);
				for(int j = 0; j < level2_dirptrs; j++){
					if(indir_ptrs_block_buff[j] != 0){
						bio_read(indir_ptrs_block_buff[j], block_buffer);
						for(int k = 0; k < max_dirents_per_block; k++){
							memcpy((void*)curr_dirent, (void*)block_buffer + (k*sizeof(struct dirent)), sizeof(struct dirent));
							if(curr_dirent->valid == 0){
								curr_dirent->ino = f_ino;
								curr_dirent->len = name_len;
								strcpy(curr_dirent->name, fname);
								curr_dirent->valid = 1;
								/*UPDATE dir_inode*/
									/*vstat, link*/

								/*WRITE new dirent to disk*/
								memcpy((void*)block_buffer + (k*sizeof(struct dirent)), (void*)curr_dirent, sizeof(struct dirent));
								bio_write(indir_ptrs_block_buff[j], block_buffer);
							
								free(curr_dirent);
								free(block_buffer);
								free(indir_ptrs_block_buff);
								return 0;
							}
						}
						memset(block_buffer, 0, BLOCK_SIZE);
					}
					else{
						curr_dirent->ino = f_ino;
						curr_dirent->len = name_len;
						strcpy(curr_dirent->name, fname);
						curr_dirent->valid = 1;
						/*UPDATE dir_inode*/
						/*vstat, link*/

						indir_ptrs_block_buff[j] = get_avail_blkno();
						if(indir_ptrs_block_buff[j] == -1){
							perror("no free data blocks");
							free(curr_dirent);
							free(block_buffer);
							free(indir_ptrs_block_buff);
							return -1;
						}
						dir_inode.size += BLOCK_SIZE;
						/*Build new block buffer*/
						memcpy((void*)block_buffer, (void*)curr_dirent, sizeof(struct dirent));
						for(int x = 1; x < max_dirents_per_block; x++){
							struct dirent* new_dirent = (struct dirent*)calloc(1, sizeof(struct dirent));
							memcpy((void*)block_buffer + (x*sizeof(struct dirent)), (void*)new_dirent, sizeof(struct dirent));
							free(new_dirent);
						}
						/*WRITE new dirent to disk*/
						bio_write(indir_ptrs_block_buff[j], block_buffer);

						free(curr_dirent);
						free(block_buffer);
						free(indir_ptrs_block_buff);
						return 0;
					}
				}
				memset(indir_ptrs_block_buff, 0, BLOCK_SIZE);
			}
			else{
				curr_dirent->ino = f_ino;
				curr_dirent->len = name_len;
				strcpy(curr_dirent->name, fname);
				curr_dirent->valid = 1;
				/*UPDATE dir_inode*/
					/*vstat, link*/

				/*Allocate a new data block for new indirect ptr*/
				dir_inode.indirect_ptr[i] = get_avail_blkno();
				if(dir_inode.indirect_ptr[i] == -1){
					perror("no free data blocks");
					free(curr_dirent);
					free(block_buffer);
					free(indir_ptrs_block_buff);
					return -1;
				}
				dir_inode.size += BLOCK_SIZE;
				/*Allocate new data block for new indirect ptr entry*/
				indir_ptrs_block_buff[0] = get_avail_blkno();
				if(dir_inode.indirect_ptr[i] == -1){
					perror("no free data blocks");
					free(curr_dirent);
					free(block_buffer);
					free(indir_ptrs_block_buff);
					return -1;
				}
				dir_inode.size += BLOCK_SIZE;
				/*Write new indirect ptr block to disk*/
				bio_write(dir_inode.indirect_ptr[i], indir_ptrs_block_buff);

				/*Build new block buffer*/
				memset(block_buffer, 0, BLOCK_SIZE);
				memcpy((void*)block_buffer, (void*)curr_dirent, sizeof(struct dirent));
				for(int x = 1; x < max_dirents_per_block; x++){
					struct dirent* new_dirent = (struct dirent*)calloc(1, sizeof(struct dirent));
					memcpy((void*)block_buffer + (x*sizeof(struct dirent)), (void*)new_dirent, sizeof(struct dirent));
					free(new_dirent);
				}
				/*WRITE new dirent to disk*/
				bio_write(indir_ptrs_block_buff[0], block_buffer);

				free(curr_dirent);
				free(block_buffer);
				free(indir_ptrs_block_buff);
				return 0;
			}
		}
		free(indir_ptrs_block_buff);
		free(block_buffer);
	}
	free(curr_dirent);
	perror("Disk Memory Full");
	return -1;
}
/*OPTIONAL: SKIP*/
int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and checks each directory entry of dir_inode
	
	// Step 2: Check if fname exist

	// Step 3: If exist, then remove it from dir_inode's data block and write to disk

	return 0;
}

/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
	
	// Step 1: Resolve the path name, walk through path, and finally, find its inode.
	// Note: You could either implement it in a iterative way or recursive way

	return 0;
}

/* 
 * Make file system
 */
int rufs_mkfs() {
	char* block_buffer = (char*)malloc(BLOCK_SIZE);
	memset(block_buffer, 0, BLOCK_SIZE);
	// Call dev_init() to initialize (Create) Diskfile
	dev_init(diskfile_path);
	dev_open(diskfile_path);

	// write superblock information
	s_block_mem = (struct superblock*)malloc(sizeof(struct superblock));
	s_block_mem->magic_num = MAGIC_NUM;
	s_block_mem->max_inum = MAX_INUM;
	s_block_mem->max_dnum = MAX_DNUM;
	s_block_mem->i_bitmap_blk = IBM_IDX;
	s_block_mem->d_bitmap_blk = DBM_IDX;
	s_block_mem->i_start_blk = IREG_IDX;
	s_block_mem->d_start_blk = ((sizeof(struct inode)*MAX_INUM)/BLOCK_SIZE) + (IREG_IDX + 1);

	memcpy(block_buffer, s_block_mem, sizeof(struct superblock));
	bio_write(SUPER_IDX, block_buffer);
	memset(block_buffer, 0, BLOCK_SIZE);
	// initialize inode bitmap
	inode_bm = (bitmap_t)malloc((MAX_INUM/8)*sizeof(char));
	memset(inode_bm, 0, (MAX_INUM/8)*sizeof(char));
	// initialize data block bitmap
	data_bm = (bitmap_t)malloc((MAX_DNUM/8)*sizeof(char));
	memset(data_bm, 0, (MAX_DNUM/8)*sizeof(char));
	
	// update bitmap information for root directory


	// update inode for root directory

	memcpy(block_buffer, inode_bm, (MAX_INUM/8)*sizeof(char));
	bio_write(s_block_mem->i_bitmap_blk, block_buffer);
	memset(block_buffer, 0, BLOCK_SIZE);
	
	memcpy(block_buffer, data_bm, (MAX_DNUM/8)*sizeof(char));
	bio_write(s_block_mem->d_bitmap_blk, block_buffer);
	memset(block_buffer, 0, BLOCK_SIZE);
	free(block_buffer);
	return 0;
}


/* 
 * FUSE file operations
 */
static void *rufs_init(struct fuse_conn_info *conn) {

	// Step 1b: If disk file is found, just initialize in-memory data structures
  	// and read superblock from disk
	// block_buff = (char*)malloc(BLOCK_SIZE);
	// memset(block_buff, 0, BLOCK_SIZE);
	if (access(diskfile_path, F_OK) == 0) {
		char* block_buffer = (char*)malloc(BLOCK_SIZE);
		memset(block_buffer, 0, BLOCK_SIZE);
		dev_open(diskfile_path);

		s_block_mem = (struct superblock*)malloc(sizeof(struct superblock));
		bio_read(SUPER_IDX, block_buffer);
		memcpy(s_block_mem, block_buffer, sizeof(struct superblock));

		inode_bm = (bitmap_t)malloc((MAX_INUM/8)*sizeof(char));
		bio_read(IBM_IDX, block_buffer);
		memcpy(inode_bm, block_buffer, (MAX_INUM/8)*sizeof(char));


		data_bm = (bitmap_t)malloc((MAX_DNUM/8)*sizeof(char));
		bio_read(SUPER_IDX, block_buffer);
		memcpy(data_bm, block_buffer, (MAX_DNUM/8)*sizeof(char));
		free(block_buffer);
	}
	// Step 1a: If disk file is not found, call mkfs
	else {
		rufs_mkfs();
	}

	// curr_inode = (struct inode*)malloc(sizeof(struct inode));
	// memset(curr_inode, 0, sizeof(struct inode));
	// curr_dirent = (struct dirent*)malloc(sizeof(struct dirent));
	// memset(curr_dirent, 0, sizeof(struct dirent));
	inodes_per_block = BLOCK_SIZE / sizeof(struct inode);
	level2_dirptrs = BLOCK_SIZE / sizeof(int);
	max_inode_blocks = (NUM_DPTRS + NUM_IDPTRS) + (NUM_IDPTRS*level2_dirptrs);
	max_dirents_per_block = BLOCK_SIZE / sizeof(struct dirent);
  
	return NULL;
}

static void rufs_destroy(void *userdata) {

	// Step 1: De-allocate in-memory data structures
	free(inode_bm);
	free(data_bm);
	free(s_block_mem);
	// free(curr_inode);
	// free(curr_dirent);
	// free(block_buff);
	// Step 2: Close diskfile
	dev_close();
}

static int rufs_getattr(const char *path, struct stat *stbuf) {

	// Step 1: call get_node_by_path() to get inode from path

	// Step 2: fill attribute of file into stbuf from inode

		// stbuf->st_mode   = S_IFDIR | 0755;
		stbuf->st_nlink  = 2;
		time(&stbuf->st_mtime);

	return 0;
}

static int rufs_opendir(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	// Step 2: If not find, return -1

    return 0;
}

static int rufs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	// Step 2: Read directory entries from its data blocks, and copy them to filler

	return 0;
}


static int rufs_mkdir(const char *path, mode_t mode) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name

	// Step 2: Call get_node_by_path() to get inode of parent directory

	// Step 3: Call get_avail_ino() to get an available inode number

	// Step 4: Call dir_add() to add directory entry of target directory to parent directory

	// Step 5: Update inode for target directory

	// Step 6: Call writei() to write inode to disk
	

	return 0;
}
/*OPTIONAL: SKIP*/
static int rufs_rmdir(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name

	// Step 2: Call get_node_by_path() to get inode of target directory

	// Step 3: Clear data block bitmap of target directory

	// Step 4: Clear inode bitmap and its data block

	// Step 5: Call get_node_by_path() to get inode of parent directory

	// Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory

	return 0;
}

static int rufs_releasedir(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name

	// Step 2: Call get_node_by_path() to get inode of parent directory

	// Step 3: Call get_avail_ino() to get an available inode number

	// Step 4: Call dir_add() to add directory entry of target file to parent directory

	// Step 5: Update inode for target file

	// Step 6: Call writei() to write inode to disk

	return 0;
}

static int rufs_open(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	// Step 2: If not find, return -1

	return 0;
}

static int rufs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {

	// Step 1: You could call get_node_by_path() to get inode from path

	// Step 2: Based on size and offset, read its data blocks from disk

	// Step 3: copy the correct amount of data from offset to buffer

	// Note: this function should return the amount of bytes you copied to buffer
	return 0;
}

static int rufs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	// Step 1: You could call get_node_by_path() to get inode from path

	// Step 2: Based on size and offset, read its data blocks from disk

	// Step 3: Write the correct amount of data from offset to disk

	// Step 4: Update the inode info and write it to disk

	// Note: this function should return the amount of bytes you write to disk
	return size;
}

/*OPTIONAL: SKIP*/
static int rufs_unlink(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name

	// Step 2: Call get_node_by_path() to get inode of target file

	// Step 3: Clear data block bitmap of target file

	// Step 4: Clear inode bitmap and its data block

	// Step 5: Call get_node_by_path() to get inode of parent directory

	// Step 6: Call dir_remove() to remove directory entry of target file in its parent directory

	return 0;
}

static int rufs_truncate(const char *path, off_t size) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_release(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int rufs_flush(const char * path, struct fuse_file_info * fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_utimens(const char *path, const struct timespec tv[2]) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}


static struct fuse_operations rufs_ope = {
	.init		= rufs_init,
	.destroy	= rufs_destroy,

	.getattr	= rufs_getattr,
	.readdir	= rufs_readdir,
	.opendir	= rufs_opendir,
	.releasedir	= rufs_releasedir,
	.mkdir		= rufs_mkdir,
	.rmdir		= rufs_rmdir,

	.create		= rufs_create,
	.open		= rufs_open,
	.read 		= rufs_read,
	.write		= rufs_write,
	.unlink		= rufs_unlink,

	.truncate   = rufs_truncate,
	.flush      = rufs_flush,
	.utimens    = rufs_utimens,
	.release	= rufs_release
};


int main(int argc, char *argv[]) {
	int fuse_stat;

	getcwd(diskfile_path, PATH_MAX);
	// printf("%s\n", diskfile_path);
	strcat(diskfile_path, "/DISKFILE");
	// printf("%s\n", diskfile_path);
	fuse_stat = fuse_main(argc, argv, &rufs_ope, NULL);

	return fuse_stat;
}

