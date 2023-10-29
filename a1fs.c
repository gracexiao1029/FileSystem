/*
 * This code is provided solely for the personal and private use of students
 * taking the CSC369H course at the University of Toronto. Copying for purposes
 * other than this use is expressly prohibited. All forms of distribution of
 * this code, including but not limited to public repositories on GitHub,
 * GitLab, Bitbucket, or any other online platform, whether as given or with
 * any changes, are expressly prohibited.
 *
 * Authors: Alexey Khrabrov, Karen Reid
 *
 * All of the files in this directory and all subdirectories are:
 * Copyright (c) 2019, 2021 Karen Reid
 */

/**
 * CSC369 Assignment 1 - a1fs driver implementation.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <libgen.h>

// Using 2.9.x FUSE API
#define FUSE_USE_VERSION 29
#include <fuse.h>

#include "a1fs.h"
#include "fs_ctx.h"
#include "options.h"
#include "map.h"

//NOTE: All path arguments are absolute paths within the a1fs file system and
// start with a '/' that corresponds to the a1fs root directory.
//
// For example, if a1fs is mounted at "~/my_csc369_repo/a1b/mnt/", the path to a
// file at "~/my_csc369_repo/a1b/mnt/dir/file" (as seen by the OS) will be
// passed to FUSE callbacks as "/dir/file".
//
// Paths to directories (except for the root directory - "/") do not end in a
// trailing '/'. For example, "~/my_csc369_repo/a1b/mnt/dir/" will be passed to
// FUSE callbacks as "/dir".


/**
 * Initialize the file system.
 *
 * Called when the file system is mounted. NOTE: we are not using the FUSE
 * init() callback since it doesn't support returning errors. This function must
 * be called explicitly before fuse_main().
 *
 * @param fs    file system context to initialize.
 * @param opts  command line options.
 * @return      true on success; false on failure.
 */
static bool a1fs_init(fs_ctx *fs, a1fs_opts *opts)
{
	// Nothing to initialize if only printing help
	if (opts->help) {
		return true;
	}

	size_t size;
	void *image = map_file(opts->img_path, A1FS_BLOCK_SIZE, &size);
	if (!image) {
		return false;
	}

	return fs_ctx_init(fs, image, size);
}

/**
 * Cleanup the file system.
 *
 * Called when the file system is unmounted. Must cleanup all the resources
 * created in a1fs_init().
 */
static void a1fs_destroy(void *ctx)
{
	fs_ctx *fs = (fs_ctx*)ctx;
	if (fs->image) {
		munmap(fs->image, fs->size);
		fs_ctx_destroy(fs);
	}
}

/** Get file system context. */
static fs_ctx *get_fs(void)
{
	return (fs_ctx*)fuse_get_context()->private_data;
}


/**
 * Get file system statistics.
 *
 * Implements the statvfs() system call. See "man 2 statvfs" for details.
 * The f_bfree and f_bavail fields should be set to the same value.
 * The f_ffree and f_favail fields should be set to the same value.
 * The following fields can be ignored: f_fsid, f_flag.
 * All remaining fields are required.
 *
 * Errors: none
 *
 * @param path  path to any file in the file system. Can be ignored.
 * @param st    pointer to the struct statvfs that receives the result.
 * @return      0 on success; -errno on error.
 */
static int a1fs_statfs(const char *path, struct statvfs *st)
{
	(void)path;// unused
	fs_ctx *fs = get_fs();

	memset(st, 0, sizeof(*st));
	//Block size of the file system
	st->f_bsize   = A1FS_BLOCK_SIZE;
	//Fragment size, same value as block size
	st->f_frsize  = A1FS_BLOCK_SIZE;
	//TODO: fill in the rest of required fields based on the information stored
	// in the superblock
	(void)fs;
	//Maximum length of the file name
	st->f_namemax = A1FS_NAME_MAX;
	//Number of free blocks
	st->f_bfree = fs->sb->s_free_blocks_count;
	//Num of free blocks for unprivilaged users
	st->f_bavail = fs->sb->s_free_blocks_count;
	//Size of fs in f_frsize units
	st->f_blocks = fs->sb->size / A1FS_BLOCK_SIZE;
	//Number of inodes
	st->f_files = fs->sb->s_inodes_count;
	//Number of free inodes
	st->f_ffree = fs->sb->s_free_inodes_count;
	//Number of free inodes for unprivilaged users
	st->f_favail = fs->sb->s_free_inodes_count;
	return 0;
}

// helper functions

/**
 * Look up the directory entry for the given directory. 
 * 
 * If the dentry is found successfully, return the dentry.
 * Otherwise return component does not exist error.
 */
struct a1fs_dentry *lookup_dentry(struct a1fs_inode *dir, char *dir_name, int *inode_num, fs_ctx *fs) {
	struct a1fs_extent *extent;
	extent = (struct a1fs_extent*)(fs->image + A1FS_BLOCK_SIZE * (fs->sb->s_first_data_block + dir->indirect_block));

	// go into each blocks in the extents and find the drectery entry with dir_name
	unsigned int size_dir = dir->count_extent;
	for (unsigned int i = 0; i < size_dir; i++) {
		unsigned int size_ext = extent[i].start + extent[i].count;
		for (unsigned int j = extent[i].start; j < size_ext; j++) {
			struct a1fs_dentry *dentry;
			dentry = (struct a1fs_dentry*)(fs->image + A1FS_BLOCK_SIZE * (fs->sb->s_first_data_block + j));
			unsigned int dentry_num = 0;
			if (i == size_dir - 1 && j == size_ext - 1) {
				while (dentry_num < dir->size % A1FS_BLOCK_SIZE / sizeof(a1fs_dentry)) {
					// if the block's dentry name is equal to dir_name, we found the target dentry
					if (strcmp(dentry[dentry_num].name, dir_name) == 0) {
						// record the current inode number
						*inode_num = dentry->ino;
						return &dentry[dentry_num];
					}
					dentry_num++;
				}  
			} else {
				while (dentry_num < A1FS_BLOCK_SIZE / sizeof(a1fs_dentry)) {
					// if the block's dentry name is equal to dir_name, we found the target dentry
					if (strcmp(dentry[dentry_num].name, dir_name) == 0) {
						// record the current inode number
						*inode_num = dentry->ino;
						return &dentry[dentry_num];
					}
					dentry_num++;
				}  
			}
		}
	}
	// no such directory entry is found, return NULL
	return NULL;
}

/**
 * Look up the inode for given path. If the inode is found successfully, 
 * make the pointer to the inode correctly.
 * 
 * Return 1 if path exists, error otherwise.
 */
int lookup_inode(const char *path, fs_ctx *fs, a1fs_inode **inode) {
	// define a copy of path
	char path_cpy[A1FS_PATH_MAX];
	strcpy(path_cpy, path);
	path_cpy[A1FS_PATH_MAX-1] = '\0';

	// search from the root directory
	struct a1fs_inode *inode_root;
	inode_root = (struct a1fs_inode*)(fs->image + A1FS_BLOCK_SIZE * fs->sb->inode_table);
	int inode_num = 0;

	// split path into single components
	char *dir_name;
	const char s[2] = "/";
	dir_name = strtok(path_cpy, s);
	while (dir_name != NULL) {
		// lookup the inode_num stored in the directory entry that has name
		// to be the first token found in dir_name
		struct a1fs_inode *curr_inode;
		curr_inode = &(inode_root[inode_num]);
		if((curr_inode->mode & S_IFDIR) != S_IFDIR) {
			return -ENOTDIR;
		}
		struct a1fs_dentry *dentry;
		dentry = lookup_dentry(curr_inode, dir_name, &inode_num, fs);
		if (dentry == NULL) {
			return -ENOENT;
		}
		dir_name = strtok(NULL, s);
	}
	*inode = &inode_root[inode_num];
	
	return 0; 
}

/**
 * Return the round up value of a division.
 */
int ceiling(int dividend, int divider) {
	if (dividend % divider == 0) {
		return dividend / divider;
	}
	return dividend / divider + 1;
}

/**
 * Get file or directory attributes.
 *
 * Implements the lstat() system call. See "man 2 lstat" for details.
 * The following fields can be ignored: st_dev, st_ino, st_uid, st_gid, st_rdev,
 *                                      st_blksize, st_atim, st_ctim.
 * All remaining fields are required.
 *
 * NOTE: the st_blocks field is measured in 512-byte units (disk sectors);
 *       it should include any metadata blocks that are allocated to the inode.
 *
 * NOTE: the st_mode field must be set correctly for files and directories.
 *
 * Errors:
 *   ENAMETOOLONG  the path or one of its components is too long.
 *   ENOENT        a component of the path does not exist.
 *   ENOTDIR       a component of the path prefix is not a directory.
 *
 * @param path  path to a file or directory.
 * @param st    pointer to the struct stat that receives the result.
 * @return      0 on success; -errno on error;
 */
static int a1fs_getattr(const char *path, struct stat *st)
{
	if (strlen(path) >= A1FS_PATH_MAX) {
		return -ENAMETOOLONG;
	}
	fs_ctx *fs = get_fs();

	memset(st, 0, sizeof(*st));

	//NOTE: all the fields set below are required and must be set according
	// to the information stored in the corresponding inode

	// TODO: lookup the inode for given path and, if it exists, fill in the
	// required fields based on the information stored in the inode
	a1fs_inode *inode;
	
	int value = lookup_inode(path, fs, &inode);
	if (value != 0) { 
		if (value == -ENOTDIR) { 
			return -ENOTDIR; // path prefix is not a directory
		} 
		if (value == -ENOENT) {
			return -ENOENT; // path does not exist
		}
	}

	st->st_mode = inode->mode;
	st->st_nlink = inode->links;
	st->st_size = inode->size;
	st->st_blocks = ceiling(inode->size, A1FS_BLOCK_SIZE) * A1FS_BLOCK_SIZE / 512;
	st->st_mtim = inode->mtime;
	return 0;
}

/**
 * Read a directory.
 *
 * Implements the readdir() system call. Should call filler(buf, name, NULL, 0)
 * for each directory entry. See fuse.h in libfuse source code for details.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a directory.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a filler() call failed).
 *
 * @param path    path to the directory.
 * @param buf     buffer that receives the result.
 * @param filler  function that needs to be called for each directory entry.
 *                Pass 0 as offset (4th argument). 3rd argument can be NULL.
 * @param offset  unused.
 * @param fi      unused.
 * @return        0 on success; -errno on error.
 */
static int a1fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi)
{
	(void)offset;// unused
	(void)fi;// unused
	fs_ctx *fs = get_fs();

	//TODO: lookup the directory inode for given path and iterate through its
	// directory entries
	(void)fs;
	a1fs_inode *dir;
	//fill in the data of the inode from the path into dir
	lookup_inode(path, fs, &dir);
	a1fs_dentry *dentries;
	//total number of directory entries
	int count_dentries = dir->size / sizeof(a1fs_dentry);
	dentries = malloc(dir->size);
	if (dentries == NULL) {
		return -ENOMEM;
	}

	void *temp = (void *)dentries;
	//find the location that stores the extents
	a1fs_extent *extents = fs->image + (fs->sb->s_first_data_block + dir->indirect_block) * A1FS_BLOCK_SIZE;

	//loop through the extents and copy data into dentries
	for(unsigned int i = 0; i < dir->count_extent; i++){
		//iterate the blocks in current extent
		for(unsigned int j = extents[i].start; j < extents[i].start + extents[i].count; j++){
			//the datablock at j
			const void *block = fs->image + (fs->sb->s_first_data_block + j) * A1FS_BLOCK_SIZE;
			
			//if it is the last data block
			bool is_last = (i == (dir->count_extent - 1)) && (j == (extents[i].start + extents[i].count - 1));
			if(is_last){
				//copy whats left
				memcpy(temp, block, dir->size % A1FS_BLOCK_SIZE);
			} 
			else{
				//copy whole block
				memcpy(temp, block, A1FS_BLOCK_SIZE);
			}
			temp += A1FS_BLOCK_SIZE;
		}
	}

	if(filler(buf, "." , NULL, 0) + filler(buf, "..", NULL, 0) != 0) {
		return -ENOMEM;
	}

	for(int i = 0; i < count_dentries; i++){
		if(filler(buf, dentries[i].name, NULL, 0) != 0) {
			return -ENOMEM;
		}
	}
	free(dentries);

	return 0;
}

/**
 * switch bit bit_number from 0 to 1 in inode bitmap
**/
void set_flip_ino_bitmap(a1fs_ino_t ino_number, fs_ctx *fs){
	unsigned char *ino_bitmap = fs->image + (fs->sb->inode_bitmap) * A1FS_BLOCK_SIZE;
	int byte_number = ino_number / 8;
	// find the bit number of the inode in the byte it belongs to
	int bit_number = ino_number % 8;
	// 00000000 and change the bit_number's bit to 1
	unsigned char flip_one = (1 << (7 - bit_number));
	// merge flip_one with the previous
	ino_bitmap[byte_number] = ino_bitmap[byte_number] | flip_one;
	fs->sb->s_free_inodes_count -= 1;
}

/**
 * switch bit bit_number from 0 to 1 in data block bitmap
**/
void set_flip_block_bitmap(a1fs_blk_t block_number, fs_ctx *fs){
	unsigned char *block_bitmap = fs->image + (fs->sb->dblock_bitmap) * A1FS_BLOCK_SIZE;
	int byte_number = block_number / 8;
	// find the bit number of the inode in the byte it belongs to
	int bit_number = block_number % 8;
	// 00000000 and change the bit_number's bit to 1
	unsigned char flip_one = (1 << (7 - bit_number));
	// merge flip_one with the previous
	block_bitmap[byte_number] = block_bitmap[byte_number] | flip_one;
	fs->sb->s_free_blocks_count -= 1;
}

/**
 * find the first 0 in the inode bitmap and set the corresponding inode.
 * return 0 on success, -1 on error
 * 
 * @param fs 			file system context
 * @param ino_num 		the index or inode number of the avaliable inode we found
 * @return 				int 0 on success, -1 on error
 */
int set_inode(int *ino_num, fs_ctx *fs){
    unsigned char *inode_bitmap = fs->image + (fs->sb->inode_bitmap) * A1FS_BLOCK_SIZE;
	//total number of bits in the inode bitmap
	int inode_bits = fs->sb->s_inodes_count;
	//total number of bytes in the inode bitmap
	int inode_bytes = inode_bits/8;
	int i = 0;
	int iterate_bit = 0;
	int iterated_bits = 0;
	bool found = false;
	while (i <= inode_bytes && !found) {
		if ((i == inode_bytes) && (inode_bits%8 != 0)){
			iterate_bit = inode_bits%8;
		} else {
			iterate_bit = 8;
		}
		for (int j = 0; j < iterate_bit; j++){
			//inode_bitmap[i] has eight bits each representing an inode
			if (!(inode_bitmap[i] & (1 << (7 - j)))){
				*ino_num = iterated_bits + j;
				found = true;
				break;
			}
		}
		iterated_bits += iterate_bit;
	}
	if (found == false){
		return -ENOSPC;
	}
	set_flip_ino_bitmap(*ino_num, fs);
	return 0;
}

/**
 * find the best avaliable extent depending on length
 * 
 * find the first extent that has extent.count equal to length,
 * if none exist, find the longest extent possible
 * 
 * @param dblock_bitmap    points to the start of the datablock bitmap
 * @param length    	length of the extent we want to find
 * @param extent    	the struct extent
 * @param fs         	file system context
 * @return          	true on success, false on error
 */
bool iterate_data_bitmap(unsigned char *dblock_bitmap, unsigned int length, a1fs_extent *extent, fs_ctx *fs){
	int total_blocks = (fs->sb->data_block_count);
	int block_bytes = total_blocks / 8;
	int iterate_bit = 0;
	unsigned int start = 0;
	unsigned int count = 0;
	int i = 0;
	int iterated_bits = 0;
	extent->count = 0;
	while (i <= block_bytes){
		// number of bits of the current byte that needs to be counted
		if ((i == block_bytes) && (total_blocks%8 != 0)){
			iterate_bit = total_blocks%8;
		} else {
			iterate_bit = 8;
		}
		// iterate through each bit
		for (int j = 0; j < iterate_bit; j++){
			if (dblock_bitmap[i] & (1 << (7 - j))){
				if(count > extent->count){
					extent->start = start;
					extent->count = count;
				} 
				count = 0;
			}else{
				count++;
				if(count == 1){
					start = iterated_bits + j;
				}
				if(count == length){
					extent->start = start;
					extent->count = count;
					return true;
				}
			}
		}
		// increase the counter for number of bits iterated
		iterated_bits += iterate_bit;
		i+=1;
	}
	// no space to allocate
	if(extent->count == 0) {
		return false;
	}
	return true;
}

/**
 * Set blocks to the inode
 * 
 * @param inode      pointer to inode that needs to allocate block
 * @param num_blocks  number of blocks that needs to be allocated to that inode
 * @param fs         file system context
 * @return           return 0 on success, -ENOSPC if not enough space available
**/
int set_block(a1fs_inode *inode, int num_blocks, fs_ctx *fs){
	// check space
	if(fs->sb->s_free_blocks_count == 0) {
		return -ENOSPC;
	} else if(num_blocks > (int)fs->sb->s_free_blocks_count) {
		return -ENOSPC;
	} else if (inode->count_extent == A1FS_BLOCK_SIZE / sizeof(a1fs_extent)) {
		return -ENOSPC;
	}
	// find the address of the start of the data bitmap
	unsigned char *data_bitmap = fs->image + fs->sb->dblock_bitmap * A1FS_BLOCK_SIZE;
	a1fs_extent extent;
	// if the inode does not have an extent allocated, initialize one.
	if((inode->indirect_block) == -1){
		// find place to allocate, check if no space to allocate.
		if (!iterate_data_bitmap(data_bitmap, 1, &extent, fs)){
			return ENOSPC;
		}
		set_flip_block_bitmap(extent.start, fs);
		inode->indirect_block = extent.start;
	}
	a1fs_extent *extents = fs->image + (fs->sb->s_first_data_block + inode->indirect_block) * A1FS_BLOCK_SIZE;
	while(num_blocks > 0){
		// find place to allocate, check if no space to allocate.
		if (!iterate_data_bitmap(data_bitmap, num_blocks, &extent, fs)){
			return ENOSPC;
		}
		for(unsigned int i = extent.start; i < extent.start + extent.count; i++){
			set_flip_block_bitmap(i, fs);
			a1fs_blk_t *dblock = fs->image + (fs->sb->s_first_data_block + i) * A1FS_BLOCK_SIZE;
			memset(dblock, 0, A1FS_BLOCK_SIZE);
		}
		extents[inode->count_extent] = extent;
		inode->count_extent++;
		num_blocks -= extent.count;
	}

	return 0;
}

/** 
 * Add the directory entry to the given directory. 
 * 
 * If the dentry is added successfully, return 0.
 * Otherwise return not enough free space error.
 */
int add_dentry(struct a1fs_inode *dir_parent, char *parent_name, struct a1fs_inode *dir, fs_ctx *fs) {
	// set block if the directory has no space for new directory entry
	int enough_space = dir_parent->size % A1FS_BLOCK_SIZE;
	if (enough_space == 0) {
		int value = set_block(dir_parent, 1, fs);
		if (value != 0) {
			return -ENOSPC;
		}
	}

	struct a1fs_extent *extent;
	extent = (struct a1fs_extent*)(fs->image + A1FS_BLOCK_SIZE * (fs->sb->s_first_data_block + dir_parent->indirect_block));
	int i = dir_parent->count_extent;
	int j = extent[i-1].start + extent[i-1].count - 1;
	
	// add the directory entry to the last block
	struct a1fs_dentry *dentry;
	dentry = (struct a1fs_dentry*)(fs->image + A1FS_BLOCK_SIZE * (fs->sb->s_first_data_block + j) + enough_space);

	dentry->ino = dir->inode_num;
	char name[A1FS_NAME_MAX];
	strcpy(name, parent_name);
	strcpy(dentry->name, name);

	if ((dir_parent->mode & S_IFDIR) == S_IFDIR) {
		dir_parent->links++;
	}
	dir_parent->size += sizeof(a1fs_dentry);
	return 0;
}

/**
 * Create a directory.
 *
 * Implements the mkdir() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the directory to create.
 * @param mode  file mode bits.
 * @return      0 on success; -errno on error.
 */
static int a1fs_mkdir(const char *path, mode_t mode)
{
	mode = mode | S_IFDIR;
	fs_ctx *fs = get_fs();

	int ino_num;
	if ((set_inode(&ino_num, fs)) != 0){
		return -ENOSPC;
	}
	a1fs_inode *inode_table = fs->image + (fs->sb->inode_table * A1FS_BLOCK_SIZE);
	a1fs_inode *dir = &inode_table[ino_num];
	dir->inode_num = ino_num;
	dir->links = 2;	
	dir->size = 0;
	dir->mode = mode;
	clock_gettime(CLOCK_REALTIME, &(dir->mtime));
	dir->indirect_block = -1;
	dir->count_extent = 0;

	char dir_name[A1FS_NAME_MAX];
	char path_parent[A1FS_PATH_MAX];
	char path_cpy[A1FS_PATH_MAX];
	strcpy(path_cpy, path);

	//find parent directory path
	for (int i = strlen(path_cpy) - 1; i >= 0; i--) {
		if (path_cpy[i] == '/') {
			strncpy(path_parent, &path_cpy[0], i);
			path_parent[i] = '\0';
			break;
		}
	}

	//find the new file name
	for (int i = strlen(path_cpy) - 1; i >= 0; i--) {
		if (path_cpy[i] == '/') {
			strncpy(dir_name, &path_cpy[i+1], strlen(path_cpy)-(i+1));
			dir_name[strlen(path_cpy)-(i+1)] = '\0';
			break;
		}
	}

	//find the inode that stores the parent directory
	a1fs_inode *dir_parent;
	lookup_inode((const char *)(path_parent), fs, &dir_parent);

	//add dentry with name, inode number of the new directory into the parent directory
	add_dentry(dir_parent, dir_name, dir, fs);
	return 0;
}

/**
 * switch bit bit_number from 1 to 0 in data inode bitmap
**/
void unset_flip_inode_bitmap(a1fs_blk_t inode_number, fs_ctx *fs){
	unsigned char *inode_bitmap = fs->image + (fs->sb->inode_bitmap) * A1FS_BLOCK_SIZE;
	int byte_number = inode_number / 8;
	// find the bit number of the inode in the byte it belongs to
	int bit_number = inode_number % 8;
	// change the bit_number's bit to 0
	unsigned char flip_zero = ~(1 << (7 - bit_number));
	// merge flip_one with the previous
	inode_bitmap[byte_number] = inode_bitmap[byte_number] & flip_zero;
	fs->sb->s_free_inodes_count += 1;
}

/**
 * switch bit bit_number from 1 to 0 in data block bitmap
**/
void unset_flip_block_bitmap(a1fs_blk_t block_number, fs_ctx *fs){
	unsigned char *block_bitmap = fs->image + (fs->sb->dblock_bitmap) * A1FS_BLOCK_SIZE;
	int byte_number = block_number / 8;
	// find the bit number of the inode in the byte it belongs to
	int bit_number = block_number % 8;
	// change the bit_number's bit to 0
	unsigned char flip_zero = ~(1 << (7 - bit_number));
	// merge flip_one with the previous
	block_bitmap[byte_number] = block_bitmap[byte_number] & flip_zero;
	fs->sb->s_free_blocks_count += 1;
}

/**
 * Unset blocks from the inode
 * 
 * @param inode      pointer to inode to allocate space for
 * @param num_blocks  number of blocks that needs to be allocated to that inode
 * @param fs         file system context
 * @return           return 0 on success, otherwise return -1
**/
int unset_block(a1fs_inode *inode, unsigned int num_blocks, fs_ctx *fs){
	struct a1fs_extent *extent;
	extent = (struct a1fs_extent*)(fs->image + A1FS_BLOCK_SIZE * (fs->sb->s_first_data_block + inode->indirect_block));

	// index of last block
	int i = inode->count_extent;
	int j = extent[i-1].start + extent[i-1].count - 1;

	while(num_blocks > 0) {
		// if number of blocks we need to remove is equal to the length of the current extent
		if (num_blocks == extent[i-1].count) {
			for (unsigned int k = extent[i-1].start; k < extent[i-1].start + extent[i-1].count; k++) {
				unset_flip_block_bitmap(k, fs);
			}
			inode->count_extent--;
			break;
		}
		// if number of blocks we need to remove is smaller than the length of the current extent
		if (num_blocks < extent[i-1].count){
			for (unsigned int k = 0; k < num_blocks; k++) {
				a1fs_blk_t block_num = j - k;
				unset_flip_block_bitmap(block_num, fs);
			}
			extent[i-1].count -= num_blocks;
			break;
		}
		// if number of blocks we need to remove is larger than the length of the current extent
		for (unsigned int k = extent[i-1].start; k < extent[i-1].start + extent[i-1].count; k++) {
			unset_flip_block_bitmap(k, fs);
		}
		inode->count_extent--;
		num_blocks -= extent[i-1].count;
		i = inode->count_extent;
		j = extent[i-1].start + extent[i-1].count - 1;
	}

	return 0;
}

/** 
 * Remove the directory entry from the given directory. 
 * 
 * If the dentry is removed successfully, return 0.
 * Otherwise return -1.
 */
int rm_dentry(struct a1fs_inode *dir_parent, char *dir_name, struct a1fs_dentry *dir, fs_ctx *fs) {
	(void)dir_name;
	int enough_space = dir_parent->size % A1FS_BLOCK_SIZE;

	struct a1fs_extent *extent;
	extent = (struct a1fs_extent*)(fs->image + A1FS_BLOCK_SIZE * (fs->sb->s_first_data_block + dir_parent->indirect_block));
	int i = dir_parent->count_extent;
	// last block that belongs to the inode
	int j = extent[i-1].start + extent[i-1].count - 1;
	
	// remove the directory entry from the last block
	int lastb = sizeof(a1fs_dentry);
	struct a1fs_dentry *last_dentry;
	last_dentry = (struct a1fs_dentry*)(fs->image + A1FS_BLOCK_SIZE * (fs->sb->s_first_data_block + j) + enough_space);
	last_dentry -= lastb;

	memcpy(dir, last_dentry, sizeof(a1fs_dentry));

	dir_parent->size -= sizeof(a1fs_dentry);

	if (dir_parent->size % A1FS_BLOCK_SIZE == 0) {
		int value = unset_block(dir_parent, 1, fs);
		if (value != 0) {
			return -1;
		}
	}

	return 0;
}

/**
 * Remove a directory.
 *
 * Implements the rmdir() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a non-root directory.
 *
 * Errors:
 *   ENOTEMPTY  the directory is not empty.
 *
 * @param path  path to the directory to remove.
 * @return      0 on success; -errno on error.
 */
static int a1fs_rmdir(const char *path)
{
	assert(strcmp(path, "/") != 0);
	fs_ctx *fs = get_fs();
	(void)path;
	(void)fs;
	char path_cpy[A1FS_PATH_MAX];
	strcpy(path_cpy, path);
	
	// give an inode pointer to the parent directory
	char parent_path[A1FS_PATH_MAX];
	for (int i = strlen(path_cpy) - 1; i >= 0; i--) {
		if (path_cpy[i] == '/') {
			strncpy(parent_path, &path_cpy[0], i);
			parent_path[i] = '\0';
			break;
		}
	}
	struct a1fs_inode *parent_dir;
	lookup_inode(parent_path, fs, &parent_dir);

	char dir_name[A1FS_NAME_MAX];
	for (int i = strlen(path_cpy) - 1; i >= 0; i--) {
		if (path_cpy[i] == '/') {
			strncpy(dir_name, &path_cpy[i+1], strlen(path_cpy)-(i+1));
			dir_name[strlen(path_cpy)-(i+1)] = '\0';
			break;
		}
	}

	// Go into the path and loop up the directory entry and extents
	struct a1fs_dentry *dentry;
	int inode_num = parent_dir->inode_num;
	dentry = lookup_dentry(parent_dir, dir_name, &inode_num, fs);
	struct a1fs_inode *inode;
	inode = fs->image + A1FS_BLOCK_SIZE * fs->sb->inode_table + sizeof(a1fs_inode) * dentry->ino;

	// return error if the directory is not empty
	if (inode->size > 0) {
		return -ENOTEMPTY;
	}
	
	// remove the current directory by removing its dentry and block
	// remove dentry with name, inode number of the directory in the parent directory
	int value = rm_dentry(parent_dir, dir_name, dentry, fs);
	if (value != 0) {
		return -1;
	}

	// unset all blocks through the inode extent
	struct a1fs_extent *extent = (struct a1fs_extent*)(fs->image + A1FS_BLOCK_SIZE * (fs->sb->s_first_data_block + inode->indirect_block));
	for(unsigned int i = 0; i < inode->count_extent; i++) {
		for(unsigned int j = extent[i].start; j < extent[i].start + extent[i].count; j++) {
			unset_flip_inode_bitmap(j, fs);
		}
	}
	unset_flip_inode_bitmap(inode->inode_num, fs);

	return 0;
}

/**
 * Create a file.
 *
 * Implements the open()/creat() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the file to create.
 * @param mode  file mode bits.
 * @param fi    unused.
 * @return      0 on success; -errno on error.
 */
static int a1fs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	(void)fi;// unused
	assert(S_ISREG(mode));
	fs_ctx *fs = get_fs();

	//TODO: create a file at given path with given mode
	int ino_num;
	// if no more space to allocate inode, return ENOSPC
	if ((set_inode(&ino_num, fs)) != 0){
		return -ENOSPC;
	}
	// initialize inode
	struct a1fs_inode *inode;
    inode = fs->image + A1FS_BLOCK_SIZE * fs->sb->inode_table + sizeof(a1fs_inode) * ino_num;
	inode->mode = mode;
	inode->links = 1;
	inode->size = 0;
	clock_gettime(CLOCK_REALTIME, &(inode->mtime));
	inode->inode_num = ino_num;
	inode->indirect_block = -1;
	inode->count_extent = 0;

	char dir_name[A1FS_NAME_MAX];
	char path_parent[A1FS_PATH_MAX];
	char path_cpy[A1FS_PATH_MAX];
	strcpy(path_cpy, path);

	//find parent directory path
	for (int i = strlen(path_cpy) - 1; i >= 0; i--) {
		if (path_cpy[i] == '/') {
			strncpy(path_parent, &path_cpy[0], i);
			path_parent[i] = '\0';
			break;
		}
	}

	//find the new file name
	for (int i = strlen(path_cpy) - 1; i >= 0; i--) {
		if (path_cpy[i] == '/') {
			strncpy(dir_name, &path_cpy[i+1], strlen(path_cpy)-(i+1));
			dir_name[strlen(path_cpy)-(i+1)] = '\0';
			break;
		}
	}

	// find parent directory inode using parent path
	a1fs_inode *parent_ino;
	lookup_inode((const char *)(path_parent), fs, &parent_ino);
	add_dentry(parent_ino, dir_name, inode, fs);
	return 0;
}

/**
 * Remove a file.
 *
 * Implements the unlink() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors: none
 *
 * @param path  path to the file to remove.
 * @return      0 on success; -errno on error.
 */
static int a1fs_unlink(const char *path)
{
	fs_ctx *fs = get_fs();

	(void)path;
	(void)fs;
	char path_cpy[A1FS_PATH_MAX];
	strcpy(path_cpy, path);
	
	// give an inode pointer to the parent directory
	char parent_path[A1FS_PATH_MAX];
	for (int i = strlen(path_cpy) - 1; i >= 0; i--) {
		if (path_cpy[i] == '/') {
			strncpy(parent_path, &path_cpy[0], i);
			parent_path[i] = '\0';
			break;
		}
	}
	struct a1fs_inode *parent_dir;
	lookup_inode(parent_path, fs, &parent_dir);

	char dir_name[A1FS_NAME_MAX];
	for (int i = strlen(path_cpy) - 1; i >= 0; i--) {
		if (path_cpy[i] == '/') {
			strncpy(dir_name, &path_cpy[i+1], strlen(path_cpy)-(i+1));
			dir_name[strlen(path_cpy)-(i+1)] = '\0';
			break;
		}
	}

	// Go into the path and look up the directory entry and extents
	struct a1fs_dentry *dentry;
	int inode_num = parent_dir->inode_num;
	dentry = lookup_dentry(parent_dir, dir_name, &inode_num, fs);
	struct a1fs_inode *inode;
	inode = fs->image + A1FS_BLOCK_SIZE * fs->sb->inode_table + sizeof(a1fs_inode) * dentry->ino;

	// unset all blocks through the inode extent
	struct a1fs_extent *extent = (struct a1fs_extent*)(fs->image + A1FS_BLOCK_SIZE * (fs->sb->s_first_data_block + inode->indirect_block));
	for(unsigned int i = 0; i < inode->count_extent; i++) {
		for(unsigned int j = extent[i].start; j < extent[i].start + extent[i].count; j++) {
			unset_flip_inode_bitmap(j, fs);
		}
	}
	unset_flip_inode_bitmap(inode->inode_num, fs);

	// remove dentry with name, inode number of the directory in the parent directory
	int value = rm_dentry(parent_dir, dir_name, dentry, fs);
	if (value != 0) {
		return -1;
	}

	return 0;
}


/**
 * Change the modification time of a file or directory.
 *
 * Implements the utimensat() system call. See "man 2 utimensat" for details.
 *
 * NOTE: You only need to implement the setting of modification time (mtime).
 *       Timestamp modifications are not recursive. 
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists.
 *
 * Errors: none
 *
 * @param path   path to the file or directory.
 * @param times  timestamps array. See "man 2 utimensat" for details.
 * @return       0 on success; -errno on failure.
 */
static int a1fs_utimens(const char *path, const struct timespec times[2])
{
	fs_ctx *fs = get_fs();

	// update the modification timestamp (mtime) in the inode for given
	// path with either the time passed as argument or the current time,
	// according to the utimensat man page
	struct a1fs_inode *inode;
	lookup_inode(path, fs, &inode);

	// if the tv_nsec field of one of the timespec structures has the special value
	// UTIME_NOW, then the corresponding file timestamp is set to the current time.
	if (times[1].tv_nsec == UTIME_NOW) { // last modification time
		clock_gettime(CLOCK_REALTIME, &(inode->mtime));
	} else {
		inode->mtime = times[1];
	}
	return 0;
}

int extend_file(a1fs_inode *inode, int num_bytes, fs_ctx *fs){
	// find the rermaining bytes that does not belong to the inode in the last block of the inode.
	int remaining;
	remaining = inode->size % A1FS_BLOCK_SIZE;
	if (remaining != 0){
		remaining = A1FS_BLOCK_SIZE - remaining;
	}

	if(inode->count_extent > 0){
		int enough_space = inode->size % A1FS_BLOCK_SIZE;
		struct a1fs_extent *extent;
		extent = (struct a1fs_extent*)(fs->image + A1FS_BLOCK_SIZE * (fs->sb->s_first_data_block + inode->indirect_block));
		int i = inode->count_extent;
		int j = extent[i-1].start + extent[i-1].count - 1;
		
		// get first byte that does not belong to the inode and add <leftover_space> 0s to the end
		struct a1fs_dentry *dentry;
		dentry = (struct a1fs_dentry*)(fs->image + A1FS_BLOCK_SIZE * (fs->sb->s_first_data_block + j) + enough_space);
		memset(dentry, 0, remaining);
	}
	
	if(remaining >= num_bytes) {	// no need to allocate new blocks
		inode->size += num_bytes;
	}
	else {	// need to allocate new blocks
		int rest = num_bytes - remaining;
		// get the number of blocks we need to allocate for that inode
		int num_blocks = ceiling(rest, A1FS_BLOCK_SIZE);
		if(set_block(inode, num_blocks, fs) != 0) {
			return -ENOSPC;
		}
		inode->size += num_bytes;
	}
	return 0;
}

/**
 * Change the size of a file.
 *
 * Implements the truncate() system call. Supports both extending and shrinking.
 * If the file is extended, the new uninitialized range at the end must be
 * filled with zeros.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the file to set the size.
 * @param size  new file size in bytes.
 * @return      0 on success; -errno on error.
 */
static int a1fs_truncate(const char *path, off_t size)
{
	fs_ctx *fs = get_fs();

	//set new file size, possibly "zeroing out" the uninitialized range
	(void)path;
	(void)size;
	(void)fs;
	a1fs_inode *inode;
	lookup_inode(path, fs, &inode);
	if((uint64_t)size > inode->size){
		if(extend_file(inode, size, fs)!= 0) {
			return -ENOSPC;
		}
	}
	if((uint64_t)size < inode->size){
		// find the number of blocks we need to deallocate from inode
		int over_bytes = inode->size - size;
		inode->size = size;
		int block_fill_bytes = inode->size % A1FS_BLOCK_SIZE;
		int num_blocks_deallocate = (over_bytes - block_fill_bytes) / A1FS_BLOCK_SIZE;
		if(num_blocks_deallocate > 0){
			unset_block(inode, num_blocks_deallocate, fs);
		}
	}
	return 0;
}

/** 
 * Look up the pointer to the file that we want read/write data
 * 
 * Return the pointer to the file by the given inode
 */
void *lookup_file(a1fs_inode *inode, int num_bytes, fs_ctx *fs){
	struct a1fs_extent *extent;
	extent = (struct a1fs_extent*)(fs->image + A1FS_BLOCK_SIZE * (fs->sb->s_first_data_block + inode->indirect_block));
	int num_block;
	int remainder = num_bytes % A1FS_BLOCK_SIZE;
	if (num_bytes == 0) {
		num_block = 1;
	} else {
		if (remainder == 0) {
			num_block = num_bytes / A1FS_BLOCK_SIZE;
		} else {
			num_block = num_bytes / A1FS_BLOCK_SIZE + 1;
		}
	}
	
	int total = 0;
	void *lastb;
	for(unsigned int i = 0; i < inode->count_extent; i++){
		total += extent[i].count;
		if(total >= num_block) {
			int count = total - num_block;
			lastb = fs->image + A1FS_BLOCK_SIZE * (fs->sb->s_first_data_block + extent[i].start + count);
			break;
		} else {
			lastb = fs->image + A1FS_BLOCK_SIZE * (fs->sb->s_first_data_block - 1);
		}
	}
	
	return lastb + remainder;
}

/**
 * Read data from a file.
 *
 * Implements the pread() system call. Must return exactly the number of bytes
 * requested except on EOF (end of file). Reads from file ranges that have not
 * been written to must return ranges filled with zeros. You can assume that the
 * byte range from offset to offset + size is contained within a single block.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 * 
 * Errors: none
 *
 * @param path    path to the file to read from.
 * @param buf     pointer to the buffer that receives the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to read from.
 * @param fi      unused.
 * @return        number of bytes read on success; 0 if offset is beyond EOF;
 *                -errno on error.
 */
static int a1fs_read(const char *path, char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi)
{
	(void)fi;// unused
	fs_ctx *fs = get_fs();

	//read data from the file at given offset into the buffer
	(void)path;
	(void)buf;
	(void)size;
	(void)offset;
	(void)fs;
	// find the inode from the given path
	struct a1fs_inode *inode;
	lookup_inode(path, fs, &inode);

	int enough_space = inode->size % A1FS_BLOCK_SIZE;

	struct a1fs_extent *extent;
	extent = (struct a1fs_extent*)(fs->image + A1FS_BLOCK_SIZE * (fs->sb->s_first_data_block + inode->indirect_block));
	int i = inode->count_extent;
	// last block that belongs to the inode
	int j = extent[i-1].start + extent[i-1].count - 1;
	void *lastb = fs->image + A1FS_BLOCK_SIZE * (fs->sb->s_first_data_block + j) + enough_space;

	// find the remaining bytes that does not belong to the inode in the last block of the inode.
	int byte_num;
	int remaining = lastb - lookup_file(inode, offset, fs);
	// get the number of bytes that is requested
	if (size - remaining > 0){
		byte_num = remaining;
	} else {
		byte_num = size;
	}
	// make buffer receive information from the file data
	memcpy(buf, lookup_file(inode, offset, fs), byte_num);
	return byte_num;
}

/**
 * Write data to a file.
 *
 * Implements the pwrite() system call. Must return exactly the number of bytes
 * requested except on error. If the offset is beyond EOF (end of file), the
 * file must be extended. If the write creates a "hole" of uninitialized data,
 * the new uninitialized range must filled with zeros. You can assume that the
 * byte range from offset to offset + size is contained within a single block.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *   ENOSPC  too many extents (a1fs only needs to support 512 extents per file)
 *
 * @param path    path to the file to write to.
 * @param buf     pointer to the buffer containing the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to write to.
 * @param fi      unused.
 * @return        number of bytes written on success; -errno on error.
 */
static int a1fs_write(const char *path, const char *buf, size_t size,
                      off_t offset, struct fuse_file_info *fi)
{
	(void)fi;// unused
	fs_ctx *fs = get_fs();

	//write data from the buffer into the file at given offset, possibly
	// "zeroing out" the uninitialized range
	a1fs_inode *inode;
	lookup_inode(path, fs, &inode);
	if(size == 0) {
		return 0;
	}
	if(inode->indirect_block == -1){
		// set an empty extent if the inode does not have an extent
		if(set_block(inode, 0, fs) != 0) {
			return -ENOSPC;
		}
	}
	int size_of_inode = inode->size;
	if(offset > size_of_inode){
		int total_bytes = offset - inode->size;
		// add bytes
		if(extend_file(inode, total_bytes, fs)!= 0) {
			return -ENOSPC;
		}
		size_of_inode = inode->size;
	}
	int rest = (offset + size) - size_of_inode;
	if(rest > 0){
		if(extend_file(inode, size, fs)!= 0) {
			return -ENOSPC;
		}
	}
	void *byte = lookup_file(inode, offset, fs);
	memcpy(byte, buf, size);
	return size;
}


static struct fuse_operations a1fs_ops = {
	.destroy  = a1fs_destroy,
	.statfs   = a1fs_statfs,
	.getattr  = a1fs_getattr,
	.readdir  = a1fs_readdir,
	.mkdir    = a1fs_mkdir,
	.rmdir    = a1fs_rmdir,
	.create   = a1fs_create,
	.unlink   = a1fs_unlink,
	.utimens  = a1fs_utimens,
	.truncate = a1fs_truncate,
	.read     = a1fs_read,
	.write    = a1fs_write,
};

int main(int argc, char *argv[])
{
	a1fs_opts opts = {0};// defaults are all 0
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	if (!a1fs_opt_parse(&args, &opts)) {
		return 1;
	}

	fs_ctx fs = {0};
	if (!a1fs_init(&fs, &opts)) {
		fprintf(stderr, "Failed to mount the file system\n");
		return 1;
	}

	return fuse_main(args.argc, args.argv, &a1fs_ops, &fs);
}
