We implemented a file system that can be interacted with FUSE

It contains the basic callback functions:
    - a1fs_destroy
	- a1fs_statfs
	- a1fs_getattr
	- a1fs_readdir
	- a1fs_mkdir
	- a1fs_rmdir
	- a1fs_create
	- a1fs_unlink
	- a1fs_utimens
    - a1fs_truncate
	- a1fs_read
	- a1fs_write

The uses of the above functions are described in runit.sh

There is an aspect of the code that does not work well:
    - Our file system cannot create both file and directory at the same time
        our file system cannot have create both files and directories at the same time for most of the time.
		If we create a directory after creating a file, it outputs INPUT/OUTPUT error.
		if we create a file after creating a directory, it seems fine, but if we want to read, write, truncate the file, it outputs 
		"is a directory". We are unable to fix the bug before the due date, however, all of the commands works as expected as long as
		we only create files or we only create directories in our file system. 
		As I mentioned before, this happens most of the time, but not all of the time. In one scenrio we are able to create both directories
		and files, this is when we create a few directories in the root directory in the beginning, and then create files inside one of the directories.
		In this case we were able to as many files as we want and we won't get errors if we truncate, read, and write to the file. However, we
		are only able to create files but not directories after we created one file.