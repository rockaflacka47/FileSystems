/*
	FUSE: Filesystem in Userspace
	Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

	This program can be distributed under the terms of the GNU GPL.
	See the file COPYING.
*/

#define DISK        ".disk"         
#define BLOCK_SIZE  512             
#define DISK_SIZE   5242880   
#define	FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>



//we'll use 8.3 filenames
#define	MAX_FILENAME 8
#define	MAX_EXTENSION 3
#define MAX_LENGTH (MAX_FILENAME * 2 + MAX_EXTENSION + 1)
#define DISK_SIZE   5242880  
#define EMPTY 0
static int *blocksFilled;
static int init = 0;


#define MAX_FILES_IN_DIR (BLOCK_SIZE - sizeof(int)) / ((MAX_FILENAME + 1) + (MAX_EXTENSION + 1) + sizeof(size_t) + sizeof(long))
#define MAX_DIRS_IN_ROOT (BLOCK_SIZE - sizeof(int)) / ((MAX_FILENAME + 1) + sizeof(long))

//The attribute packed means to not align these things
struct cs1550_directory_entry
{
	int nFiles;	//How many files are in this directory.
				//Needs to be less than MAX_FILES_IN_DIR

	struct cs1550_file_directory
	{
		char fname[MAX_FILENAME + 1];	//fileName (plus space for nul)
		char fext[MAX_EXTENSION + 1];	//extension (plus space for nul)
		size_t fsize;					//file size
		long nStartBlock;				//where the first block is on disk
	} __attribute__((packed)) files[MAX_FILES_IN_DIR];	//There is an array of these

	//This is some space to get this to be exactly the size of the disk block.
	//Don't use it for anything.  
	char padding[BLOCK_SIZE - MAX_FILES_IN_DIR * sizeof(struct cs1550_file_directory) - sizeof(int)];
} ;

typedef struct cs1550_root_directory cs1550_root_directory;

//#define MAX_DIRS_IN_ROOT (BLOCK_SIZE - sizeof(int)) / ((MAX_FILENAME + 1) + sizeof(long))

struct cs1550_root_directory
{
	int nDirectories;	//How many subdirectories are in the root
						//Needs to be less than MAX_DIRS_IN_ROOT
	struct cs1550_directory
	{
		char dname[MAX_FILENAME + 1];	//directory name (plus space for nul)
		long nStartBlock;				//where the directory block is on disk
	} __attribute__((packed)) directories[MAX_DIRS_IN_ROOT];	//There is an array of these

	//This is some space to get this to be exactly the size of the disk block.
	//Don't use it for anything.  
	char padding[BLOCK_SIZE - MAX_DIRS_IN_ROOT * sizeof(struct cs1550_directory) - sizeof(int)];
} ;


typedef struct cs1550_directory_entry cs1550_directory_entry;


#define	MAX_DATA_IN_BLOCK (BLOCK_SIZE - sizeof(long))

struct cs1550_disk_block
{
	//The next disk block, if needed. This is the next pointer in the linked 
	//allocation list
	long nNextBlock;

	//And all the rest of the space in the block can be used for actual data
	//storage.
	char data[MAX_DATA_IN_BLOCK];
};

typedef struct cs1550_disk_block cs1550_disk_block;
typedef struct cs1550_file_directory cs1550_file_directory;


static long findDirectory(char *dirName) {
    long index = -1;                        

    
    FILE *disk = fopen(DISK, "rb");         

   
    if (disk == NULL) {
        // ERROR

    } else {
        cs1550_root_directory root;                                
        fread(&root, sizeof(cs1550_root_directory), 1, disk);      

        
        int i;
        for (i=0; i < root.nDirectories; i++) {                     
            if (strcmp(root.directories[i].dname, dirName) == 0) {
                index = root.directories[i].nStartBlock;            
                break;
            }
        }

        fclose(disk);                                               
    }

    return index;
}



static cs1550_directory_entry *getDirectory(long index) {
    cs1550_directory_entry *dirName;                                
    dirName = (cs1550_directory_entry*)calloc(1, sizeof(cs1550_directory_entry));

   
    FILE *disk = fopen(DISK, "rb");                            

    
    if (disk == NULL) {
        // ERROR

    } else {
        fseek(disk, index * BLOCK_SIZE, SEEK_SET);              
        fread(dirName, sizeof(cs1550_directory_entry), 1, disk);    
        fclose(disk);                                           
    }

    return dirName;
}



static int findFile(cs1550_directory_entry *dirName, char *fileName, char *extName) {
    int index = -1;

    int i;
    cs1550_file_directory file_dir;
    for (i=0; i < dirName->nFiles; i++) {                   
        file_dir = dirName->files[i];
        if (strcmp(file_dir.fname, fileName) == 0) {
            if (strcmp(file_dir.fext, extName) == 0) {
                index = i;                             
                break;
            }
        }
    }

    return index;
}



static cs1550_file_directory *getFile(cs1550_directory_entry *dirName, char *fileName, char *extName) {
    cs1550_file_directory *disk_file = NULL;           


    int i;
    cs1550_file_directory file_dir;
    for (i=0; i < dirName->nFiles; i++) {                   
        file_dir = dirName->files[i];
        if (strcmp(file_dir.fname, fileName) == 0) {
            if (strcmp(file_dir.fext, extName) == 0) {
                disk_file = &file_dir;                 
                break;
            }
        }
    }

    return disk_file;
}

/*
 * Called whenever the system wants to know the file attributes, including
 * simply whether the file exists or not. 
 *
 * man -s 2 stat will show the fields of a stat structure
 */
static int cs1550_getattr(const char *path, struct stat *stbuf)
{
	int res = -ENOENT;	//FILE NOT FOUND

	memset(stbuf, 0, sizeof(struct stat));
   
	
	if (strcmp(path, "/") == 0) {	// Root dirName
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		res = 0;
	} 
	else {
		char directory[MAX_FILENAME];
		char fileName[MAX_FILENAME];
		char extension[MAX_EXTENSION];
		int filled = sscanf(path, "/%[^/]/%[^.].%s", directory, fileName, extension);
		if(filled <= 0){ //error
			
		}
		else{	
			long startDir = findDirectory(directory);
			if(startDir < 0){
				//error
			}
			//directory not a file
			else if(filled==1){
				stbuf->st_mode = S_IFDIR | 0755;
				stbuf->st_nlink = 2;
				res = 0;
			}
			else{
				//file
				cs1550_directory_entry *dirName;      
				dirName = getDirectory(startDir);   
				if (filled == 2) { extension[0] = '\0';} 
				//printf("%d\n", startDir);
				//printf("%s\n", directory);
				//printf("%s\n", path);
				cs1550_file_directory *file;
				file = getFile(dirName, fileName, extension);
				//printf("%s \n %s \n %s\n", dirName, fileName, extension);
				if (file == NULL) {
					// FILE NOT FOUND
					printf("IN THIS IF\n");

				} 
				else {
					//file found
					stbuf->st_mode = S_IFREG | 0666;    
					stbuf->st_nlink = 1;                
					stbuf->st_size = file->fsize;      
					
					res = 0;                         
				}
			}	
		}		
	}
	return res;
}

/* 
 * Called whenever the contents of a directory are desired. Could be from an 'ls'
 * or could even be when a user hits TAB to do autocompletion
 */
static int cs1550_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{
	//Since we're building with -Wall (all warnings reported) we need
	//to "use" every parameter, so let's just cast them to void to
	//satisfy the compiler
	(void) offset;
	(void) fi;
	int res = 0;
	char dirName[MAX_FILENAME];
	char fileName[MAX_FILENAME];
	int num;
	
	int filled = sscanf(path, "/%[^/]/%[^.]", dirName, fileName);
	
	
	if (strcmp(path, "/") < 0 && filled == EOF){
		return -ENOENT; 		//FILE NOT FOUND
	}
	else{
		FILE *disk = fopen(DISK, "rb");
		if(disk == NULL){

		}
		else{
			filler(buf, ".", NULL, 0);                                             
            filler(buf, "..", NULL, 0);                                    
			cs1550_root_directory root;                                             
            fread(&root, sizeof(cs1550_root_directory), 1, disk);                   

            if (filled <= 0) {

				// list contents of root directory
				for (num = 0; num < root.nDirectories; num++) {
					filler(buf, root.directories[num].dname, NULL, 0);             
				}
				
			} 
			else {
				long offset = 0;
				//get the directory and list the files
                    for (num = 0; num < root.nDirectories; num++) {
                        if (strcmp(root.directories[num].dname, dirName) == 0) {
                            offset = root.directories[num].nStartBlock * BLOCK_SIZE;    
                        }
                    }
				 cs1550_directory_entry dirEntry;
                    fseek(disk, offset, SEEK_SET);                                      
                    fread(&dirEntry, sizeof(cs1550_directory_entry), 1, disk);        

                   
                    char fileName[MAX_FILENAME];
                    for (num=0; num < dirEntry.nFiles; num++) {
                        
                        strcpy(fileName, dirEntry.files[num].fname);
                        if (strlen(dirEntry.files[num].fext) > 0) {
                            strcat(fileName, ".");
                            strcat(fileName, dirEntry.files[num].fext);
                        }
                        filler(buf, fileName, NULL, 0);                                
                    }
                }

                fclose(disk);                                                           
            }
		}
		return res;
    }


//reads the array from the disk into the program
static void fillArray(){
	FILE *disk = fopen(DISK, "rb");   
	blocksFilled = calloc(DISK_SIZE/BLOCK_SIZE, 1);
	if(disk == NULL){

	}
	else{
		int loc = BLOCK_SIZE;
		fseek(disk, loc, SEEK_END);
		fread(blocksFilled, DISK_SIZE/BLOCK_SIZE, 1, disk);
		blocksFilled[0] = 1;
		int i;
		for(i = 1; i <= 20; i++){
			blocksFilled[i] = 1;
		}
		
	}
	fclose(disk);
	
}
static int findFreeBlock(){
	int i = 0;
	//FILE *disk = fopen(".disk", "rb");
	for(i = 0; i < DISK_SIZE/BLOCK_SIZE; i+= 1){
		//printf("%d\n", blocksFilled[i]);
		if(blocksFilled[i] == 0){
			//fclose(disk);
			return i;
		}
	}
	return -1;
}
static void writeArray(){
	FILE *disk = fopen(DISK, "r+b");   
	//blocksFilled = calloc(DISK_SIZE/BLOCK_SIZE, 1);
	if(disk == NULL){

	}
	else{
		int loc = BLOCK_SIZE;
		fseek(disk, loc, SEEK_END);
		fwrite(blocksFilled, sizeof(blocksFilled), 1, disk);
		/*blocksFilled[0] = 1;
		int i;
		for(i = 1; i <= 20; i++){
			blocksFilled[i] = 1;
		}*/
		
	}
	fclose(disk);
}
/* 
 * Creates a directory. We can ignore mode since we're not dealing with
 * permissions, as long as getattr returns appropriate ones for us.
 */
static int cs1550_mkdir(const char *path, mode_t mode)
{
	(void) path;
	(void) mode;
	int res = 0;
    long free_block;
    char dirName[MAX_FILENAME];
    char file[MAX_FILENAME];
    FILE *disk;
	if(init == 0) fillArray();		//if array is unitialized initialize it
	init = 1;
   
    int filled = sscanf(path, "/%[^/]/%[^.]", dirName, file);


    if (filled != 1) {
        res = -EPERM;                                        // ERROR: can ONLY create dirName within '/' root

    } else if (strlen(dirName) > MAX_FILENAME) {
        res = -ENAMETOOLONG;                                 // ERROR: directory name too long

    } else if ((free_block = findFreeBlock()) == -1) {
        res = -ENOSPC;                                       // ERROR: no space left on disk

    } else {
        /* TRY TO CREATE THE DIRECTORY */

        
        disk = fopen(DISK, "r+b");                              

        if (disk == NULL) {
            res = -ENOENT;                                   // ERROR: file not opened successfully

        } else {
            // get the root within the disk file
            cs1550_root_directory root;                                         
            fread(&root, sizeof(cs1550_root_directory), 1, disk);               
            // make sure the root can hold another directory listing
            if (root.nDirectories >= MAX_DIRS_IN_ROOT) {
                res = -ENOSPC;                                               // ERROR: not enough space

            } else {
                // create directory inside the free block
                cs1550_directory_entry *new_dir;                                
                new_dir = (struct cs1550_directory_entry*)calloc(1, sizeof(struct cs1550_directory_entry));
                new_dir->nFiles = 0;                                           

                fseek(disk, free_block * BLOCK_SIZE, SEEK_SET);                 
                fwrite(new_dir, sizeof(cs1550_directory_entry), 1, disk);       

                // create root dirName struct
                struct cs1550_directory *new_dirEntry;                         
                new_dirEntry = (struct cs1550_directory*)calloc(1, sizeof(struct cs1550_directory));

                strcpy(new_dirEntry->dname, dirName);                        

                new_dirEntry->nStartBlock = free_block;                        

                root.directories[root.nDirectories] = *new_dirEntry;           
                root.nDirectories++;

                
                fseek(disk, 0, SEEK_SET);     
                fwrite(&root, sizeof(struct cs1550_root_directory), 1, disk);   

                // free up space
                free(new_dir);
                free(new_dirEntry);
            }
		}
		
    }
    
    if (res == 0) {
		fclose(disk);    
		blocksFilled[free_block] = 0;
		writeArray();
    }

	return res;
}



/* 
 * Removes a directory.
 */
static int cs1550_rmdir(const char *path)
{
	(void) path;
    return 0;
}

/* 
 * Does the actual creation of a file. Mode and dev can be ignored.
 *
 */
static int cs1550_mknod(const char *path, mode_t mode, dev_t dev)
{
    (void) mode;
    (void) dev;
    int res = 0;                             
    char dirName[MAX_LENGTH];                       
    char fileName[MAX_LENGTH];                  
	char extName[MAX_LENGTH];     
	if(init = 0) fillArray();
	init = 1;                  
    
    if (strcmp(path, "/") == 0) {
        res = -EPERM;                        // ERROR: file cannot be created in root dirName

    } else if (strlen(path) >= MAX_LENGTH) {
        res = -ENAMETOOLONG;                 // ERROR: path name too long

    } else {
        
        int scan_result = sscanf(path, "/%[^/]/%[^.].%s", dirName, fileName, extName);

        if (scan_result == 2) { extName[0] = '\0';}     // make extension NULL if blank
        
        if (scan_result == EOF || strlen(dirName) > MAX_FILENAME || strlen(fileName) > MAX_FILENAME || strlen(extName) > MAX_EXTENSION){
            res = -ENAMETOOLONG;                 // ERROR: path or file invalid

        } else if (scan_result <= 1) {
            res = -EPERM;                        // ERROR: cannot create file in root

        } else {
            long dirBlock = findDirectory(dirName);               

            cs1550_directory_entry *dirEntry;
            dirEntry = getDirectory(dirBlock);               
			// get the list of files for this directory
			cs1550_file_directory *file = getFile(dirEntry, fileName, extName);

			if (file == NULL) {
				
				long free_block;
				if ((free_block = findFreeBlock()) == -1) {
					res = -ENOSPC;                           // ERROR: no space left on disk

				} else {
					cs1550_file_directory *newFile;            
					newFile = (cs1550_file_directory*)calloc(1, sizeof(cs1550_file_directory));

					strcpy(newFile->fname, fileName);          
					strcpy(newFile->fext, extName);                
					newFile->nStartBlock = findFreeBlock();  
					newFile->fsize = 0;                       
					dirEntry->files[dirEntry->nFiles] = *newFile;    
					dirEntry->nFiles++;                                

					// write out the directory to disk
					FILE *disk = fopen(DISK, "r+b");                                    
					fseek(disk, dirBlock * BLOCK_SIZE, SEEK_SET);                      
					fwrite(dirEntry, sizeof(struct cs1550_directory_entry), 1, disk);  
					fclose(disk);
					blocksFilled[newFile->nStartBlock] = 1;
				}
			} else {
				res = -EEXIST;                                       // ERROR: file already exists
			}
		}
	}

	if(res = 0) writeArray();
    return res;
}


/*
 * Deletes a file
 */
static int cs1550_unlink(const char *path)
{
    (void) path;
    return 0;
}

//returns next block in the format of the struct
static cs1550_disk_block *getNextBlock(long index) {
    cs1550_disk_block *currBlock;
    currBlock = (cs1550_disk_block*)calloc(1, sizeof(cs1550_disk_block));
    FILE *disk = fopen(DISK, "rb");                                 
    if (disk == NULL) {
        // ERROR
    } else {
        fseek(disk, index * BLOCK_SIZE, SEEK_SET);              
        fread(currBlock, sizeof(cs1550_disk_block), 1, disk);  
        index = currBlock->nNextBlock;                         
        fclose(disk);                                               
    }
    return currBlock;
}
/* 
 * Read size bytes from file into buf starting from offset
 */
static int cs1550_read(const char *path, char *buf, size_t size, off_t offset,
   struct fuse_file_info *fi)
{
    (void) fi;
    char dirName[MAX_LENGTH];           
    char fileName[MAX_LENGTH];      
    char extName[MAX_LENGTH];           

    int scan_result = sscanf(path, "/%[^/]/%[^.].%s", dirName, fileName, extName);
    if (scan_result < 2) {
        return -EISDIR;                                                             // ERROR: trying to read out a directory

    } else {
        long dirBlock = findDirectory(dirName);                                       
        cs1550_directory_entry *dirEntry = getDirectory(dirBlock);               
        cs1550_file_directory *fileEntry = getFile(dirEntry, fileName, extName);     
        long blockLocation = fileEntry->nStartBlock;                                   

        // determine how many blocks will be needed
        long numBlocksNeeded = (1 + (((size - 1) / MAX_DATA_IN_BLOCK)));          
        long firstBlock = offset / MAX_DATA_IN_BLOCK;                              
        long firstOffset = offset % MAX_DATA_IN_BLOCK;                             
        size_t bytesRead = 0;                                                      
         
        cs1550_disk_block *diskBlock = getNextBlock(blockLocation);     
        long currBlock, dataOffset=0;
        size_t bytesLeft;                                                          
        for (currBlock = 0; currBlock < numBlocksNeeded; currBlock++) {
            bytesLeft = size - bytesRead;                                         
            dataOffset = (currBlock == 0) ? firstOffset : 0;  

			strncpy((buf + bytesRead), (diskBlock->data + dataOffset), ((bytesLeft < MAX_DATA_IN_BLOCK) ? bytesLeft : MAX_DATA_IN_BLOCK));
            bytesRead += ((bytesLeft < MAX_DATA_IN_BLOCK) ? bytesLeft : MAX_DATA_IN_BLOCK);
 
            if (currBlock != (numBlocksNeeded - 1)) {      
				blockLocation = diskBlock->nNextBlock;                                                     
            } else {
				//dont need to do anythin here
            }

            if (currBlock != (numBlocksNeeded - 1)) {
                diskBlock = getNextBlock(diskBlock->nNextBlock);                
            }
        }
    }

    return size;
}

//write a block to disk
static void writeBlock(cs1550_disk_block *block, long index) {
    FILE *disk = fopen(DISK, "r+b");                        

    if (disk == NULL) {
        // ERROR

    } else {
        fseek(disk, index * BLOCK_SIZE, SEEK_SET);          
        fwrite(block, sizeof(cs1550_disk_block), 1, disk);  
		fclose(disk);                                       
		blocksFilled[index] = 1;
    }
}
/* 
 * Write size bytes from buf into file starting from offset
 *
 */
static int cs1550_write(const char *path, const char *buf, size_t size, 
   off_t offset, struct fuse_file_info *fi)
{
    (void) fi;

    char dirName[MAX_LENGTH];           
    char fileName[MAX_LENGTH];      
	char extName[MAX_LENGTH];      
	if(init = 0) fillArray();
	init = 1;     

    sscanf(path, "/%[^/]/%[^.].%s", dirName, fileName, extName);

    long dirBlock = findDirectory(dirName);                                           
    cs1550_directory_entry *dirEntry = getDirectory(dirBlock);                   
    int file_index = findFile(dirEntry, fileName, extName);                           
    cs1550_file_directory *fileEntry = getFile(dirEntry, fileName, extName);         
    long blockLocation = fileEntry->nStartBlock;                                       
    fileEntry->fsize = size;
    dirEntry->files[file_index] = *fileEntry;

    FILE *disk = fopen(DISK, "r+b");                                                
    fseek(disk, dirBlock * BLOCK_SIZE, SEEK_SET);                                  
    fwrite(dirEntry, sizeof(struct cs1550_directory_entry), 1, disk);              
    fclose(disk);

    if ((size == 0) || (offset > size)) {
        return -EFBIG;                                                              // ERROR: offset is beyond file size
    } else {

        // determine how many blocks will be needed
        long numBlocksNeeded = (1 + (((size - 1) / MAX_DATA_IN_BLOCK)));          
        long firstBlock = offset / MAX_DATA_IN_BLOCK;                              
        long firstOffset = offset % MAX_DATA_IN_BLOCK;                             
        size_t bytesDone = 0;                                                     

        cs1550_disk_block *diskBlock = getNextBlock(blockLocation);     
        long currBlock, dataOffset=0;
        size_t bytesLeft;                                                          
        for (currBlock = 0; currBlock < numBlocksNeeded; currBlock++) {
            bytesLeft = size - bytesDone;                                        
            dataOffset = ((currBlock == 0) ? firstOffset : 0);                    

			strncpy((diskBlock->data + dataOffset), (buf + bytesDone), ((bytesLeft < MAX_DATA_IN_BLOCK) ? bytesLeft : MAX_DATA_IN_BLOCK));
            bytesDone += ((bytesLeft < MAX_DATA_IN_BLOCK) ? bytesLeft : MAX_DATA_IN_BLOCK);
			int temp = blockLocation;
			
            if (currBlock != (numBlocksNeeded - 1)) {                          
                blockLocation = findFreeBlock();                                      
                if (blockLocation < 0) { return -ENOSPC; }                              // ERROR: no space left
                diskBlock->nNextBlock = blockLocation;                                 
            } else {
                diskBlock->nNextBlock = 0;                                           
            }

            writeBlock(diskBlock, temp); 

            if (currBlock != (numBlocksNeeded - 1)) {
                diskBlock = getNextBlock(blockLocation);                
            }
		}
		writeArray();
    }
	
    return size;
}



/******************************************************************************
 *
 *  DO NOT MODIFY ANYTHING BELOW THIS LINE
 *
 *****************************************************************************/

/*
 * truncate is called when a new file is created (with a 0 size) or when an
 * existing file is made shorter. We're not handling deleting files or 
 * truncating existing ones, so all we need to do here is to initialize
 * the appropriate directory entry.
 *
 */
static int cs1550_truncate(const char *path, off_t size)
{
	(void) path;
	(void) size;

    return 0;
}


/* 
 * Called when we open a file
 *
 */
static int cs1550_open(const char *path, struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;
    /*
        //if we can't find the desired file, return an error
        return -ENOENT;
    */

    //It's not really necessary for this project to anything in open

    /* We're not going to worry about permissions for this project, but 
	   if we were and we don't have them to the file we should return an error

        return -EACCES;
    */

    return 0; //success!
}

/*
 * Called when close is called on a file descriptor, but because it might
 * have been dup'ed, this isn't a guarantee we won't ever need the file 
 * again. For us, return success simply to avoid the unimplemented error
 * in the debug log.
 */
static int cs1550_flush (const char *path , struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;

	return 0; //success!
}


//register our new functions as the implementations of the syscalls
static struct fuse_operations hello_oper = {
    .getattr	= cs1550_getattr,
    .readdir	= cs1550_readdir,
    .mkdir	= cs1550_mkdir,
	.rmdir = cs1550_rmdir,
    .read	= cs1550_read,
    .write	= cs1550_write,
	.mknod	= cs1550_mknod,
	.unlink = cs1550_unlink,
	.truncate = cs1550_truncate,
	.flush = cs1550_flush,
	.open	= cs1550_open,
};

//Don't change this.
int main(int argc, char *argv[])
{
	return fuse_main(argc, argv, &hello_oper, NULL);
}
