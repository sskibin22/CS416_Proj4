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

#include "block.h"
#include "rufs.h"

#define SUPER_IDX 0
#define IBM_IDX 1
#define DBM_IDX 2
#define IREG_IDX 3
#define ROOT_INO 0

// Declare your in-memory data structures here
char diskfile_path[PATH_MAX];
struct superblock* s_block_mem;


/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {
	bitmap_t inode_bm  = (bitmap_t)malloc((s_block_mem->max_inum/8)*sizeof(char));
	memset(inode_bm, 0, sizeof((s_block_mem->max_inum/8)*sizeof(char)));
	char* block_buffer = (char*)malloc(BLOCK_SIZE);
	memset(block_buffer, 0, BLOCK_SIZE);
	// Step 1: Read inode bitmap from disk
	bio_read(s_block_mem->i_bitmap_blk, block_buffer);
	memcpy(inode_bm, block_buffer, (s_block_mem->max_inum/8)*sizeof(char));

	// Step 2: Traverse inode bitmap to find an available slot
	for(int i = 0; i < s_block_mem->max_inum; i++){
		if(get_bitmap(inode_bm, i) == 0){
			// Step 3: Update inode bitmap and write to disk 
			set_bitmap(inode_bm, i);
			memcpy(block_buffer, inode_bm, (s_block_mem->max_inum/8)*sizeof(char));
			bio_write(s_block_mem->i_bitmap_blk, block_buffer);
			free(block_buffer);
			return i;
		}
	}

	free(block_buffer);
	free(inode_bm);
	return -1;
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {
	bitmap_t data_bm  = (bitmap_t)malloc((s_block_mem->max_dnum/8)*sizeof(char));
	memset(data_bm, 0, sizeof((s_block_mem->max_dnum/8)*sizeof(char)));
	char* block_buffer = (char*)malloc(BLOCK_SIZE);
	memset(block_buffer, 0, BLOCK_SIZE);
	// Step 1: Read data block bitmap from disk
	bio_read(s_block_mem->d_bitmap_blk, block_buffer);
	memcpy(data_bm, block_buffer, (s_block_mem->max_dnum/8)*sizeof(char));

	// Step 2: Traverse data block bitmap to find an available slot
	for(int i = 0; i < s_block_mem->max_dblks; i++){ //s_block_mem->max_dnum
		if(get_bitmap(data_bm, i) == 0){
			// Step 3: Update data block bitmap and write to disk 
			set_bitmap(data_bm, i);
			memcpy(block_buffer, data_bm, (s_block_mem->max_dnum/8)*sizeof(char));
			bio_write(s_block_mem->d_bitmap_blk, block_buffer);
			free(block_buffer);
			s_block_mem->d_start_blk++;
			return s_block_mem->d_start_blk + i;
		}
	}

	free(block_buffer);
	free(data_bm);
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
  	uint16_t idx = ino % s_block_mem->inodes_per_blk;

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
	uint16_t idx = ino % s_block_mem->inodes_per_blk;

	// Step 3: Write inode to disk 
	bio_read(blk, block_buffer);
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
	for(int i = 0; i < NUM_DPTRS; i++){
		if(curr_inode->direct_ptr[i] != 0){
			bio_read(curr_inode->direct_ptr[i], block_buffer);
			for(int j = 0; j < s_block_mem->dirents_per_blk; j++){
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
	free(curr_inode);
	free(curr_dirent);
	free(block_buffer);
	return -1;
}

int dir_add(struct inode* dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {
	// Step 1: Read dir_inode's data block and check each directory entry of dir_inode
	// Step 2: Check if fname (directory name) is already used in other entries
	// Step 3: Add directory entry in dir_inode's data block and write to disk
	// Allocate a new data block for this directory if it does not exist
	// Update directory inode
	// Write directory entry
	struct dirent* curr_dirent = (struct dirent*)malloc(sizeof(struct dirent));
	memset(curr_dirent, 0, sizeof(struct dirent));
	if(dir_find(dir_inode->ino, fname, name_len, curr_dirent) == -1){
		char* block_buffer = (char*)malloc(BLOCK_SIZE);
		memset(block_buffer, 0, BLOCK_SIZE);
		for(int i = 0; i < NUM_DPTRS; i++){
			if(dir_inode->direct_ptr[i] != 0){
				bio_read(dir_inode->direct_ptr[i], block_buffer);
				for(int j = 0; j < s_block_mem->dirents_per_blk; j++){
					memcpy((void*)curr_dirent, (void*)block_buffer + (j*sizeof(struct dirent)), sizeof(struct dirent));
					if(curr_dirent->valid == 0){
						curr_dirent->ino = f_ino;
						curr_dirent->len = name_len;
						strcpy(curr_dirent->name, fname);
						curr_dirent->valid = 1;
						/*UPDATE dir_inode*/
						time(&dir_inode->vstat.st_atime);
						time(&dir_inode->vstat.st_mtime);

						/*WRITE new dirent to disk*/
						memcpy((void*)block_buffer + (j*sizeof(struct dirent)), (void*)curr_dirent, sizeof(struct dirent));
						bio_write(dir_inode->direct_ptr[i], block_buffer);
					
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
				
				dir_inode->direct_ptr[i] = get_avail_blkno();
				if(dir_inode->direct_ptr[i] == -1){
					free(curr_dirent);
					free(block_buffer);
					return -1;
				}
				/*UPDATE dir_inode*/
				dir_inode->size += BLOCK_SIZE;
				dir_inode->vstat.st_size += BLOCK_SIZE;
				time(&dir_inode->vstat.st_atime);
				time(&dir_inode->vstat.st_mtime);
				
				/*Build new block buffer*/
				memcpy((void*)block_buffer, (void*)curr_dirent, sizeof(struct dirent));
				for(int x = 1; x < s_block_mem->dirents_per_blk; x++){
					struct dirent* new_dirent = (struct dirent*)calloc(1, sizeof(struct dirent));
					new_dirent->valid = 0;
					memcpy((void*)block_buffer + (x*sizeof(struct dirent)), (void*)new_dirent, sizeof(struct dirent));
					free(new_dirent);
				}
				/*WRITE new dirent to disk*/
				bio_write(dir_inode->direct_ptr[i], block_buffer);
				free(curr_dirent);
				free(block_buffer);
				return 0;
			}
		}
		free(block_buffer);
	}
	free(curr_dirent);
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
	if(strcmp(path, "/") == 0){
        readi(ino, inode);
        return 0;
    }
	uint16_t curr_ino = ino;
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
            break;
        }
		struct dirent* curr_dirent = (struct dirent*)calloc(1, sizeof(struct dirent));
        if(dir_find(curr_ino, name, name_len, curr_dirent) == -1){
			free(name);
			free(curr_dirent);
			return -1;
		}
		curr_ino = curr_dirent->ino;
        path_ptr += name_len;
        free(name);
		free(curr_dirent);
    }
	readi(curr_ino, inode);
    return 0;
}


/* 
 * Make file system
 */
int rufs_mkfs() {
	// Call dev_init() to initialize (Create) Diskfile
	printf("initializing disk\n");
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
	s_block_mem->inodes_per_blk = BLOCK_SIZE / sizeof(struct inode);
	s_block_mem->dirents_per_blk = BLOCK_SIZE / sizeof(struct dirent);
	s_block_mem->max_dblks = (DISK_SIZE/BLOCK_SIZE) - (((sizeof(struct inode)*MAX_INUM)/BLOCK_SIZE)+3);
	s_block_mem->max_file_size = NUM_DPTRS * BLOCK_SIZE;
	s_block_mem->total_blocks_alloc = s_block_mem->d_start_blk;

	// initialize block buffer
	char* block_buffer = (char*)malloc(BLOCK_SIZE);
	memset(block_buffer, 0, BLOCK_SIZE);

	memcpy(block_buffer, s_block_mem, sizeof(struct superblock));
	bio_write(SUPER_IDX, block_buffer);
	memset(block_buffer, 0, BLOCK_SIZE);
	// initialize inode bitmap
	bitmap_t inode_bm = (bitmap_t)malloc((MAX_INUM/8)*sizeof(char));
	if(inode_bm == NULL){
		printf("inode_bm fail\n");
	}
	memset(inode_bm, 0, (MAX_INUM/8)*sizeof(char));
	// initialize data block bitmap
	bitmap_t data_bm = (bitmap_t)malloc((MAX_DNUM/8)*sizeof(char));
	memset(data_bm, 0, (MAX_DNUM/8)*sizeof(char));

	// update bitmap information for root directory
	set_bitmap(inode_bm, ROOT_INO);
	set_bitmap(data_bm, 0);

	// update inode for root directory
	struct inode* root_inode = (struct inode*)calloc(1, sizeof(struct inode));
	root_inode->ino = ROOT_INO;
	root_inode->valid = 1;
	root_inode->type = S_IFDIR | 0755;
	root_inode->link = 1;

	/*Init vstat fields*/
	root_inode->vstat.st_uid = getuid();
	root_inode->vstat.st_gid = getgid();
	root_inode->vstat.st_mode = S_IFDIR | 0755;
	root_inode->vstat.st_nlink = 1;
	
	dir_add(root_inode, root_inode->ino, ".", 1);

	writei(ROOT_INO, root_inode);

	memcpy(block_buffer, inode_bm, (MAX_INUM/8)*sizeof(char));
	bio_write(s_block_mem->i_bitmap_blk, block_buffer);
	memset(block_buffer, 0, BLOCK_SIZE);
	
	memcpy(block_buffer, data_bm, (MAX_DNUM/8)*sizeof(char));
	bio_write(s_block_mem->d_bitmap_blk, block_buffer);
	memset(block_buffer, 0, BLOCK_SIZE);

	free(root_inode);
	free(inode_bm);
	free(data_bm);
	free(block_buffer);
	return 0;
}


/* 
 * FUSE file operations
 */
static void *rufs_init(struct fuse_conn_info *conn) {

	// Step 1b: If disk file is found, just initialize in-memory data structures
  	// and read superblock from disk
	if (access(diskfile_path, F_OK) == 0) {
		printf("disk already initialized\n");
		char* block_buffer = (char*)malloc(BLOCK_SIZE);
		memset(block_buffer, 0, BLOCK_SIZE);
		dev_open(diskfile_path);

		s_block_mem = (struct superblock*)malloc(sizeof(struct superblock));
		bio_read(SUPER_IDX, block_buffer);
		memcpy(s_block_mem, block_buffer, sizeof(struct superblock));

		free(block_buffer);
	}
	// Step 1a: If disk file is not found, call mkfs
	else {
		rufs_mkfs();
	}
  
	return NULL;
}

static void rufs_destroy(void *userdata) {

	// Step 1: De-allocate in-memory data structures
	free(s_block_mem);
	// Step 2: Close diskfile
	dev_close();
}

static int rufs_getattr(const char *path, struct stat *stbuf) {
	memset(stbuf, 0, sizeof(struct stat));
	printf("allocated blocks: %i\n", s_block_mem->d_start_blk);
	// Step 1: call get_node_by_path() to get inode from path
	struct inode* curr_inode = (struct inode*)calloc(1, sizeof(struct inode));
	if(get_node_by_path(path, ROOT_INO, curr_inode) == -1){

		free(curr_inode);
		return -ENOENT;
	}
	
	// Step 2: fill attribute of file into stbuf from inode
	stbuf->st_uid = curr_inode->vstat.st_uid;
	stbuf->st_gid = curr_inode->vstat.st_gid;
	stbuf->st_mode = curr_inode->vstat.st_mode;
	stbuf->st_size = curr_inode->vstat.st_size;
	stbuf->st_nlink = curr_inode->vstat.st_nlink;
	time(&curr_inode->vstat.st_atime);
	time(&curr_inode->vstat.st_mtime);
	stbuf->st_atime = curr_inode->vstat.st_atime;
	stbuf->st_mtime = curr_inode->vstat.st_mtime;

	writei(curr_inode->ino, curr_inode);

	free(curr_inode);
	return 0;
}

static int rufs_opendir(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path
	struct inode* curr_inode = (struct inode*)calloc(1, sizeof(struct inode));
	if(get_node_by_path(path, ROOT_INO, curr_inode) == -1){
		// Step 2: If not find, return -1
		free(curr_inode);
		return -ENOENT;
	}
	fi->fh = (uint64_t)curr_inode;
	free(curr_inode);
    return 0;
}

static int rufs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path
	struct inode* curr_inode = (struct inode*)calloc(1, sizeof(struct inode));
	if(get_node_by_path(path, ROOT_INO, curr_inode) == -1){
		free(curr_inode);
		return -ENOENT;
	}
	// Step 2: Read directory entries from its data blocks, and copy them to filler
	char* block_buffer = (char*)malloc(BLOCK_SIZE);
	memset(block_buffer, 0, BLOCK_SIZE);
	struct dirent* curr_dirent = (struct dirent*)malloc(sizeof(struct dirent));
	memset(curr_dirent, 0, sizeof(struct dirent));

	for(int i = 0; i < NUM_DPTRS; i++){
		if(curr_inode->direct_ptr[i] != 0){
			bio_read(curr_inode->direct_ptr[i], block_buffer);
			for(int j = 0; j < s_block_mem->dirents_per_blk; j++){
				memcpy((void*)curr_dirent, (void*)block_buffer + (j*sizeof(struct dirent)), sizeof(struct dirent));
				if(curr_dirent->valid == 1){
					filler(buffer, curr_dirent->name, NULL, 0);
            	}
			}
			memset(block_buffer, 0, BLOCK_SIZE);
		}
	}

	free(curr_dirent);
	free(block_buffer);
	free(curr_inode);

	return 0;
}

static int rufs_mkdir(const char *path, mode_t mode) {
	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
	int path_len = strlen(path);
	char* path_cpy = (char*)calloc(path_len, sizeof(char));
	strcpy(path_cpy, path);
	char* base = basename(path_cpy);
	char* dir = dirname(path_cpy);
	if(strcmp(dir, "/") == 0){
		base = (char*)path;
		base++;
	}
	size_t base_len = strlen(base);
	
	// Step 2: Call get_node_by_path() to get inode of parent directory
	struct inode* curr_inode = (struct inode*)calloc(1, sizeof(struct inode));
	if(get_node_by_path(dir, ROOT_INO, curr_inode) == -1){
		free(curr_inode);
		return -ENOENT;
	}

	struct dirent* curr_dirent = (struct dirent*)calloc(1, sizeof(struct dirent));
	if(dir_find(curr_inode->ino, base, base_len, curr_dirent) == 0){
		free(curr_dirent);
		free(curr_inode);
		return EEXIST;
	}
	free(curr_dirent);

	// Step 3: Call get_avail_ino() to get an available inode number
	int avail_ino = get_avail_ino();
	if(avail_ino == -1){
		free(curr_inode);
		return ENOSPC;
	}

	// Step 4: Call dir_add() to add directory entry of target directory to parent directory
	int retval = dir_add(curr_inode, avail_ino, base, base_len);
	if(retval == -1){
		free(curr_inode);
		return ENOSPC;
	}

	curr_inode->link++;
	curr_inode->vstat.st_nlink++;
	writei(curr_inode->ino, curr_inode);

	// Step 5: Update inode for target directory
	struct inode* new_inode = (struct inode*)calloc(1, sizeof(struct inode));
	new_inode->ino = avail_ino;
	new_inode->type = S_IFDIR | mode;
	new_inode->link = 2;
	new_inode->valid = 1;
	new_inode->vstat.st_uid = getuid();
	new_inode->vstat.st_gid = getgid();
	new_inode->vstat.st_mode = S_IFDIR | mode;
	new_inode->vstat.st_nlink = 2;

	//Add self and parent dirents to target directory
	if(dir_add(new_inode, new_inode->ino, ".", 1) == -1){
		free(new_inode);
		free(curr_inode);
		return ENOSPC;
	}

	dir_add(new_inode, curr_inode->ino, "..", 2);

	// Step 6: Call writei() to write inode to disk
	writei(avail_ino, new_inode);
	
	free(new_inode);
	free(curr_inode);
	free(path_cpy);

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
	int path_len = strlen(path);
	char* path_cpy = (char*)calloc(path_len, sizeof(char));
	strcpy(path_cpy, path);
	char* base = basename(path_cpy);
	char* dir = dirname(path_cpy);
	if(strcmp(dir, "/") == 0){
		base = (char*)path;
		base++;
	}

	size_t base_len = strlen(base);

	// Step 2: Call get_node_by_path() to get inode of parent directory
	struct inode* curr_inode = (struct inode*)calloc(1, sizeof(struct inode));
	if(get_node_by_path(dir, ROOT_INO, curr_inode) == -1){
		free(curr_inode);
		return -ENOENT;
	}

	struct dirent* curr_dirent = (struct dirent*)calloc(1, sizeof(struct dirent));
	if(dir_find(curr_inode->ino, base, base_len, curr_dirent) == 0){
		free(curr_dirent);
		free(curr_inode);
		return EEXIST;
	}
	free(curr_dirent);

	// Step 3: Call get_avail_ino() to get an available inode number
	int avail_ino = get_avail_ino();
	if(avail_ino == -1){
		free(curr_inode);
		return ENOSPC;
	}

	// Step 4: Call dir_add() to add directory entry of target file to parent directory
	int retval = dir_add(curr_inode, avail_ino, base, base_len);
	if(retval == -1){
		free(curr_inode);
		return ENOSPC;
	}

	writei(curr_inode->ino, curr_inode);
	// Step 5: Update inode for target file
	struct inode* new_inode = (struct inode*)calloc(1, sizeof(struct inode));
	new_inode->ino = avail_ino;
	new_inode->type = S_IFREG | mode;
	new_inode->link = 1;
	new_inode->valid = 1;
	new_inode->vstat.st_uid = getuid();
	new_inode->vstat.st_gid = getgid();
	new_inode->vstat.st_mode = S_IFREG | mode;
	new_inode->vstat.st_nlink = 1;
	fi->fh = (uint64_t)new_inode;
	// Step 6: Call writei() to write inode to disk
	writei(avail_ino, new_inode);
	
	free(new_inode);
	free(curr_inode);
	free(path_cpy);

	return 0;
}

static int rufs_open(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path
	struct inode* curr_inode = (struct inode*)calloc(1, sizeof(struct inode));
	if(get_node_by_path(path, ROOT_INO, curr_inode) == -1){
		free(curr_inode);
		return -ENOENT;
	}
	// Step 2: If not find, return -1
	fi->fh = (uint64_t)curr_inode;
	free(curr_inode);
	return 0;
}
/*TODO: FIZ rufs_read(), figure out test_cases*/
static int rufs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	// Step 1: You could call get_node_by_path() to get inode from path
	struct inode* curr_inode = (struct inode*)calloc(1, sizeof(struct inode));
	if(get_node_by_path(path, ROOT_INO, curr_inode) == -1){
		free(curr_inode);
		return -ENOENT;
	}
	// Step 2: Based on size and offset, read its data blocks from disk
	if(offset > s_block_mem->max_file_size || size > s_block_mem->max_file_size || (offset + size) > s_block_mem->max_file_size){
		free(curr_inode);
		return 	EFBIG;
	}
	uint32_t tot_blks = (offset + size) / BLOCK_SIZE;
	uint32_t start_blk = offset / BLOCK_SIZE;
	uint32_t start_offset = offset % BLOCK_SIZE;
	// Step 3: copy the correct amount of data from offset to buffer
	char* block_buffer = (char*)malloc(BLOCK_SIZE);
	memset(block_buffer, 0, BLOCK_SIZE);
	
	if(tot_blks < 1){
		bio_read(curr_inode->direct_ptr[0], block_buffer);
		memcpy((void*)buffer, (void*)block_buffer + start_offset, size);

		fi->fh = (uint64_t)curr_inode;

		free(block_buffer);
		free(curr_inode);
		return size;
	}

	int curr_bytes = size;
	size_t write_amount = BLOCK_SIZE - start_offset;
	for(int i = start_blk; i < tot_blks; i++){
		if(i == start_blk){
			bio_read(curr_inode->direct_ptr[i], block_buffer);
			curr_bytes -= write_amount;
			memcpy((void*)buffer, (void*)block_buffer + start_offset, write_amount);
			memset(block_buffer, 0, BLOCK_SIZE);
		}
		if(curr_bytes > 0 && i > start_blk){
			bio_read(curr_inode->direct_ptr[i], block_buffer);
			if(curr_bytes > BLOCK_SIZE){
				curr_bytes -= BLOCK_SIZE;
				memcpy((void*)buffer + write_amount, (void*)block_buffer, BLOCK_SIZE);
				write_amount += BLOCK_SIZE;
			}
			else{
				memcpy((void*)buffer + write_amount, (void*)block_buffer, curr_bytes);
				curr_bytes -= curr_bytes;
			}
			memset(block_buffer, 0, BLOCK_SIZE);
		}
	}
	fi->fh = (uint64_t)curr_inode;
	// Note: this function should return the amount of bytes you copied to buffer
	free(block_buffer);
	free(curr_inode);
	return size;
}

static int rufs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	// Step 1: You could call get_node_by_path() to get inode from path
	struct inode* curr_inode = (struct inode*)calloc(1, sizeof(struct inode));
	if(get_node_by_path(path, ROOT_INO, curr_inode) == -1){
		free(curr_inode);
		return -ENOENT;
	}
	// Step 2: Based on size and offset, read its data blocks from disk
	if(offset > s_block_mem->max_file_size || size > s_block_mem->max_file_size || (offset + size) > s_block_mem->max_file_size){
		free(curr_inode);
		return 	EFBIG;
	}
	uint32_t tot_blks = (offset + size) / BLOCK_SIZE;
	uint32_t start_blk = offset / BLOCK_SIZE;
	uint32_t start_offset = offset % BLOCK_SIZE;

	// Step 3: Write the correct amount of data from offset to disk
	char* block_buffer = (char*)malloc(BLOCK_SIZE);
	memset(block_buffer, 0, BLOCK_SIZE);
	
	if(tot_blks < 1){
		if(curr_inode->direct_ptr[0] == 0){
			curr_inode->direct_ptr[0] = get_avail_blkno();
			if(curr_inode->direct_ptr[0] == -1){
				free(curr_inode);
				return ENOSPC;
			}
			curr_inode->size += BLOCK_SIZE;
			curr_inode->vstat.st_size += BLOCK_SIZE;
		}
		bio_read(curr_inode->direct_ptr[0], block_buffer);
		memcpy((void*)block_buffer + start_offset, (void*)buffer, size);
		bio_write(curr_inode->direct_ptr[0], block_buffer);
		memset(block_buffer, 0, BLOCK_SIZE);

		time(&curr_inode->vstat.st_atime);
		time(&curr_inode->vstat.st_mtime);
		writei(curr_inode->ino, curr_inode);

		fi->fh = (uint64_t)curr_inode;

		free(block_buffer);
		free(curr_inode);
		return size;
	}

	int curr_bytes = size;
	size_t write_amount = BLOCK_SIZE - start_offset;
	for(int i = 0; i < tot_blks; i++){
		if(curr_inode->direct_ptr[i] == 0){
			curr_inode->direct_ptr[i] = get_avail_blkno();
			if(curr_inode->direct_ptr[i] == -1){
				free(curr_inode);
				return ENOSPC;
			}
			curr_inode->size += BLOCK_SIZE;
			curr_inode->vstat.st_size += BLOCK_SIZE;
		
		}
		if(i == start_blk){
			bio_read(curr_inode->direct_ptr[i], block_buffer);
			curr_bytes -= write_amount;
			memcpy((void*)block_buffer + start_offset, (void*)buffer,  write_amount);
			bio_write(curr_inode->direct_ptr[i], block_buffer);
			memset(block_buffer, 0, BLOCK_SIZE);
		}
		if(curr_bytes > 0 && i > start_blk){
			bio_read(curr_inode->direct_ptr[i], block_buffer);
			if(curr_bytes > BLOCK_SIZE){
				curr_bytes -= BLOCK_SIZE;
				memcpy((void*)block_buffer, (void*)buffer + write_amount,  BLOCK_SIZE);
				write_amount += BLOCK_SIZE;
			}
			else{
				memcpy((void*)block_buffer, (void*)buffer + write_amount,  curr_bytes);
				curr_bytes -= curr_bytes;
			}
			bio_write(curr_inode->direct_ptr[i], block_buffer);
			memset(block_buffer, 0, BLOCK_SIZE);
		}
	}

	// Step 4: Update the inode info and write it to disk
	time(&curr_inode->vstat.st_atime);
	time(&curr_inode->vstat.st_mtime);
	writei(curr_inode->ino, curr_inode);
	// Note: this function should return the amount of bytes you write to disk
	fi->fh = (uint64_t)curr_inode;

	free(block_buffer);
	free(curr_inode);
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

