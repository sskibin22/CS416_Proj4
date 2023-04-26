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
		for(int i = 0; i < NUM_DPTRS; i++){
			if(dir_inode.direct_ptr[i] != 0){
				bio_read(dir_inode.direct_ptr[i], block_buffer);
				for(int j = 0; j < s_block_mem->dirents_per_blk; j++){
					memcpy((void*)curr_dirent, (void*)block_buffer + (j*sizeof(struct dirent)), sizeof(struct dirent));
					if(curr_dirent->valid == 0){
						curr_dirent->ino = f_ino;
						curr_dirent->len = name_len;
						strcpy(curr_dirent->name, fname);
						curr_dirent->valid = 1;
						/*UPDATE dir_inode*/
							/*vstat, link*/
						time(&dir_inode.vstat.st_atime);
						time(&dir_inode.vstat.st_mtime);

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
				dir_inode.vstat.st_size += BLOCK_SIZE;
				time(&dir_inode.vstat.st_atime);
				time(&dir_inode.vstat.st_mtime);
				/*UPDATE dir_inode*/
				/*link, vstat*/

				/*Build new block buffer*/
				memcpy((void*)block_buffer, (void*)curr_dirent, sizeof(struct dirent));
				for(int x = 1; x < s_block_mem->dirents_per_blk; x++){
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
		free(block_buffer);
	}
	free(curr_dirent);
	perror("Directoy Memory Full");
	return -1;
}
/*OPTIONAL: SKIP*/
int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and checks each directory entry of dir_inode
	
	// Step 2: Check if fname exist

	// Step 3: If exist, then remove it from dir_inode's data block and write to disk

	return 0;
}
/*Helper recursive function for get_node_by_path()*/
int get_node_by_path_rec(char* path, char* token, uint16_t ino, struct inode *inode) {
	printf("in get_node_by_path_rec()\n");
    if((token = strtok_r(path, "/", &path))){
		struct dirent* curr_dirent = (struct dirent*)calloc(1, sizeof(struct dirent));
        size_t length = strlen(token);
		/*Find if token name exists in current directory*/
		if(dir_find(ino, token, length, curr_dirent) == -1){
			free(curr_dirent);
			return -1;
		}
		/*If not end of path*/
        if(strcmp(path, "\0") != 0){
            if(get_node_by_path_rec(path, token, curr_dirent->ino, inode) == -1){
				free(curr_dirent);
				return -1;
			}
			else{
				free(curr_dirent);
				return 0;
			}
        }
		/*Read inode from disk, copy to passed inode arg*/
		readi(curr_dirent->ino, inode);
        free(curr_dirent);
        return 0;
    }
	else{
		return -1;
	}
}
/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
	
	// Step 1: Resolve the path name, walk through path, and finally, find its inode.
	// Note: You could either implement it in a iterative way or recursive way
	printf("in get_node_by_path()\n");
	if(strcmp(path, "/") == 0){
		readi(ino, inode);
		return 0;
	}
	else{
		char* tok = NULL;
		char path_cpy[PATH_MAX];
		strcpy(path_cpy, path);
		if(get_node_by_path_rec(path_cpy, tok, ino, inode) == 0){
			return 0;
		}
		else{
			return -1;
		}
	}
}


/* 
 * Make file system
 */
int rufs_mkfs() {
	// Call dev_init() to initialize (Create) Diskfile
	printf("initializing disk\n");
	dev_init(diskfile_path);
	dev_open(diskfile_path);
	bitmap_t inode_bm = (bitmap_t)malloc((MAX_INUM/8)*sizeof(char));
	memset(inode_bm, 0, (MAX_INUM/8)*sizeof(char));
	bitmap_t data_bm = (bitmap_t)malloc((MAX_DNUM/8)*sizeof(char));
	memset(data_bm, 0, (MAX_DNUM/8)*sizeof(char));
	char* block_buffer = (char*)malloc(BLOCK_SIZE);
	memset(block_buffer, 0, BLOCK_SIZE);
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
	set_bitmap(inode_bm, ROOT_INO);
	set_bitmap(data_bm, 0);

	// update inode for root directory
	struct inode* root_inode = (struct inode*)calloc(1, sizeof(struct inode));
	root_inode->ino = ROOT_INO;
	root_inode->valid = 1;
	root_inode->size = BLOCK_SIZE;
	root_inode->type = S_IFDIR | 0755;
	root_inode->link = 1;
	root_inode->direct_ptr[0] = s_block_mem->d_start_blk;
	/*Init vstat fields*/
	root_inode->vstat.st_uid = getuid();
	root_inode->vstat.st_gid = getgid();
	root_inode->vstat.st_size = BLOCK_SIZE;
	root_inode->vstat.st_mode = S_IFDIR | 0755;
	root_inode->vstat.st_nlink = 1;
	time(&root_inode->vstat.st_atime);
	time(&root_inode->vstat.st_mtime);

	writei(ROOT_INO, root_inode);

	struct dirent* root_dirent = (struct dirent*)malloc(sizeof(struct dirent));
	root_dirent->ino = ROOT_INO;
	root_dirent->len = 1;
	strcpy(root_dirent->name, ".");
	root_dirent->valid = 1;

	memcpy(block_buffer, root_dirent, sizeof(struct dirent));
	bio_write(s_block_mem->d_start_blk, block_buffer);
	memset(block_buffer, 0, BLOCK_SIZE);

	memcpy(block_buffer, inode_bm, (MAX_INUM/8)*sizeof(char));
	bio_write(s_block_mem->i_bitmap_blk, block_buffer);
	memset(block_buffer, 0, BLOCK_SIZE);
	
	memcpy(block_buffer, data_bm, (MAX_DNUM/8)*sizeof(char));
	bio_write(s_block_mem->d_bitmap_blk, block_buffer);
	memset(block_buffer, 0, BLOCK_SIZE);

	free(block_buffer);
	free(inode_bm);
	free(data_bm);

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
	printf( "[getattr] Called\n" );
	printf( "\tAttributes of %s requested\n", path );

	// Step 1: call get_node_by_path() to get inode from path
	struct inode* curr_inode = (struct inode*)calloc(1, sizeof(struct inode));
	if(get_node_by_path(path, ROOT_INO, curr_inode) == -1){
		free(curr_inode);
		// perror("path does not exist");
		return ENOENT;
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

	// stbuf->st_mode = S_IFDIR | 0755;
	// stbuf->st_nlink = 2;
	// time(&stbuf->st_mtime);

	free(curr_inode);
	return 0;
}

static int rufs_opendir(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path
	struct inode* curr_inode = (struct inode*)calloc(1, sizeof(struct inode));
	if(get_node_by_path(path, ROOT_INO, curr_inode) == -1){
		// Step 2: If not find, return -1
		free(curr_inode);
		perror("path does not exist");
		return -1;
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
		perror("path does not exist");
		return -1;
	}
	// Step 2: Read directory entries from its data blocks, and copy them to filler
	//filler( buffer, ".", NULL, 0 );
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

