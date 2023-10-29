# our file system cannot have create both files and directories at the same time for most of the time.
# If we create a directory after creating a file, it outputs INPUT/OUTPUT error.
# if we create a file after creating a directory, it seems fine, but if we want to read, write, truncate the file, it outputs 
# "is a directory". We are unable to fix the bug before the due date, however, all of the commands works as expected as long as
# we only create files or we only create directories in our file system. 
# As I mentioned before, this happens most of the time, but not all of the time. In one scenrio we are able to create both directories
# and files, this is when we create a few directories in the root directory in the beginning, and then create files inside one of the directories.
# In this case we were able to as many files as we want and we won't get errors if we truncate, read, and write to the file. However, we
# are only able to create files but not directories after we created one file.


# unmount and create image img
fusermount -u /tmp/yuxin15
make all
truncate -s 65536 img

# format the image img
./mkfs.a1fs -i 4 img -f

# mount image img
./a1fs img -d /tmp/yuxin15

# display contents of the root directory
ls -la /tmp/yuxin15

# create an empty directory dir2 under root directory
mkdir /tmp/yuxin15/dir2
ls -la /tmp/yuxin15

# create an empty directory dir3 under dir2
mkdir /tmp/yuxin15/dir2/dir3
ls -la /tmp/yuxin15
ls -la /tmp/yuxin15/dir2

# create another empty directory dir4 under root directory
mkdir /tmp/yuxin15/dir4
ls -la /tmp/yuxin15

# remove directory dir4
rmdir /tmp/yuxin15/dir4
ls -la /tmp/yuxin15

# unmount the file system
fusermount -u /tmp/yuxin15

# mount image img and show contents of the root directory
./a1fs img -d /tmp/yuxin15
ls -la /tmp/yuxin15

#### start a new file system to test creating files

# unmount and create image img
fusermount -u /tmp/yuxin15
make all
truncate -s 65536 img

# format the image img
./mkfs.a1fs -i 4 img -f

# mount image img
./a1fs img -d /tmp/yuxin15

# display contents of the root directory
ls -la /tmp/yuxin15

# create a new file named file1.txt under dir2
touch /tmp/yuxin15/file1.txt
ls -la /tmp/yuxin15

# write to the file file1.txt and read the content
echo "this is some input" >> /tmp/yuxin15/file1.txt
cat /tmp/yuxin15/file1.txt
ls -la /tmp/yuxin15

# truncate the file size to 5
truncate /tmp/yuxin15/file1.txt --size 5
ls -la /tmp/yuxin15

# truncate the file size to 10
truncate /tmp/yuxin15/file1.txt --size 10
ls -la /tmp/yuxin15

# remove the file
unlink /tmp/yuxin15/file1.txt
ls -la /tmp/yuxin15