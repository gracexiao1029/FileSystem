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
 * CSC369 Assignment 1 - a1fs formatting tool.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <math.h>
#include <time.h>

#include "a1fs.h"
#include "map.h"


/** Command line options. */
typedef struct mkfs_opts {
	/** File system image file path. */
	const char *img_path;
	/** Number of inodes. */
	size_t n_inodes;

	/** Print help and exit. */
	bool help;
	/** Overwrite existing file system. */
	bool force;
	/** Zero out image contents. */
	bool zero;

} mkfs_opts;

static const char *help_str = "\
Usage: %s options image\n\
\n\
Format the image file into a1fs file system. The file must exist and\n\
its size must be a multiple of a1fs block size - %zu bytes.\n\
\n\
Options:\n\
    -i num  number of inodes; required argument\n\
    -h      print help and exit\n\
    -f      force format - overwrite existing a1fs file system\n\
    -z      zero out image contents\n\
";

static void print_help(FILE *f, const char *progname)
{
	fprintf(f, help_str, progname, A1FS_BLOCK_SIZE);
}


static bool parse_args(int argc, char *argv[], mkfs_opts *opts)
{
	char o;
	while ((o = getopt(argc, argv, "i:hfvz")) != -1) {
		switch (o) {
			case 'i': opts->n_inodes = strtoul(optarg, NULL, 10); break;

			case 'h': opts->help  = true; return true;// skip other arguments
			case 'f': opts->force = true; break;
			case 'z': opts->zero  = true; break;

			case '?': return false;
			default : assert(false);
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "Missing image path\n");
		return false;
	}
	opts->img_path = argv[optind];

	if (!opts->n_inodes) {
		fprintf(stderr, "Missing or invalid number of inodes\n");
		return false;
	}
	return true;
}


/** Determine if the image has already been formatted into a1fs. */
static bool a1fs_is_present(void *image)
{
	//TODO: check if the image already contains a valid a1fs superblock
	const struct a1fs_superblock *sb = (const struct a1fs_superblock *)(image);
	if (sb->magic != A1FS_MAGIC){
		return false;
	}
	return true;
}


/**
 * Format the image into a1fs.
 *
 * NOTE: Must update mtime of the root directory.
 *
 * @param image  pointer to the start of the image.
 * @param size   image size in bytes.
 * @param opts   command line options.
 * @return       true on success;
 *               false on error, e.g. options are invalid for given image size.
 */
static bool mkfs(void *image, size_t size, mkfs_opts *opts)
{
	//NOTE: the mode of the root directory inode should be set to S_IFDIR | 0777
	//remember to do false
	(void)image;
	(void)size;
	(void)opts;
	//total number of blocks
	unsigned int total_block = size/A1FS_BLOCK_SIZE;
	//total number of inodes
	unsigned int total_inodes = opts->n_inodes;
	//number of inodes per block
	unsigned int inodes_per_block = A1FS_BLOCK_SIZE/sizeof(a1fs_inode);
	//bits per block
	unsigned int bits_per_block = A1FS_BLOCK_SIZE*8;

	//number of blocks needed for inode bitmap
	unsigned int num_ino_bitmap = (total_inodes)/(bits_per_block) + (((total_inodes) % (bits_per_block)) != 0);
	//number of blocks needed for inode tables
	unsigned int num_ino_table = (total_inodes)/(inodes_per_block) + (((total_inodes) % inodes_per_block) != 0);
	//number of blocks left after allocating the inode bitmap, super block, and inode bitmap.
	unsigned int num_block_left = total_block - 1 - num_ino_bitmap - num_ino_table;
	//number of blocks needed for data block bitmap
	int num_dblock_bitmap = num_block_left / (1 + bits_per_block);
	//get ceiling
	num_dblock_bitmap += num_block_left % (1 + bits_per_block) > 1;
	//number of blocks needed for data block
	unsigned int num_dblock = num_block_left - num_dblock_bitmap;
	//number of free inodes count, used 1 for root directory
	unsigned int free_inodes_count = (total_inodes) - 1;
	unsigned int free_blocks_count = total_block - (1 + num_ino_bitmap + num_dblock_bitmap + num_ino_table);

	struct a1fs_superblock *sb = (struct a1fs_superblock*)(image);
	sb->magic = A1FS_MAGIC;
	sb->size = size;
	sb->dblock_bitmap = 1;
	sb->inode_bitmap = 1 + num_dblock_bitmap;
	sb->inode_table = 1 + num_dblock_bitmap + num_ino_bitmap;
	sb->s_first_data_block = 1 + num_dblock_bitmap + num_ino_bitmap + num_ino_table;
	sb->s_block_size = A1FS_BLOCK_SIZE;
	sb->s_inodes_count = total_inodes;
	sb->data_block_count = num_dblock;
	sb->s_free_blocks_count = free_blocks_count;
	sb->s_free_inodes_count = free_inodes_count;

	// initialize root directory
	unsigned char *dblock_bitmap_arr = image + sb->dblock_bitmap * A1FS_BLOCK_SIZE;
	unsigned char *inode_bitmap_arr = image + sb->inode_bitmap * A1FS_BLOCK_SIZE;
	memset(dblock_bitmap_arr, 0, num_dblock_bitmap * A1FS_BLOCK_SIZE);
	memset(inode_bitmap_arr, 0, num_ino_bitmap * A1FS_BLOCK_SIZE);

	// initialize the first index of inode bitmap array to be 1000 0000
	inode_bitmap_arr[0] = 1 << 7;

	struct a1fs_inode *inode_root;
	inode_root = (struct a1fs_inode*)(image + A1FS_BLOCK_SIZE * sb->inode_table);
	inode_root->mode = S_IFDIR | 0777;
	inode_root->links = 2;
	inode_root->size = 0;
	clock_gettime(CLOCK_REALTIME, &(inode_root->mtime));
	inode_root->inode_num = 0;
	inode_root->count_extent = 0;
	// set the pointer to block to -1 if it is invalid (empty)
	inode_root->indirect_block = -1;
	return true;
}


int main(int argc, char *argv[])
{
	mkfs_opts opts = {0};// defaults are all 0
	if (!parse_args(argc, argv, &opts)) {
		// Invalid arguments, print help to stderr
		print_help(stderr, argv[0]);
		return 1;
	}
	if (opts.help) {
		// Help requested, print it to stdout
		print_help(stdout, argv[0]);
		return 0;
	}

	// Map image file into memory
	size_t size;
	void *image = map_file(opts.img_path, A1FS_BLOCK_SIZE, &size);
	if (!image) {
		return 1;
	}

	// Check if overwriting existing file system
	int ret = 1;
	if (!opts.force && a1fs_is_present(image)) {
		fprintf(stderr, "Image already contains a1fs; use -f to overwrite\n");
		goto end;
	}

	if (opts.zero) {
		memset(image, 0, size);
	}
	if (!mkfs(image, size, &opts)) {
		fprintf(stderr, "Failed to format the image\n");
		goto end;
	}

	ret = 0;
end:
	munmap(image, size);
	return ret;
}
