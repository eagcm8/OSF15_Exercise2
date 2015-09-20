#include "../include/block_store.h"

// Overriding these will probably break it since I'm not testing it that much
// it probably won't go crazy so long as the sizes are reasonable and powers of two
// So just don't touch it unless you want to debug it
#define BLOCK_COUNT 65536
#define BLOCK_SIZE 1024
#define FBM_SIZE ((BLOCK_COUNT >> 3) / BLOCK_SIZE)
#if (((FBM_SIZE * BLOCK_SIZE) << 3) != BLOCK_COUNT)
    #error "BLOCK MATH DIDN'T CHECK OUT"
#endif

// Handy macro, does what it says on the tin
#define BLOCKID_VALID(id) ((id > (FBM_SIZE - 1)) && (id < BLOCK_COUNT))

// TODO: Implement, comment, param check
// It needs to read count bytes from fd into buffer
// Probably a good idea to handle EINTR
// Maybe do a block-based read? It's more efficient, but it's more complex
// Return number of bytes read if it all worked (should always be count)
//  and 0 on error (can only really be a major disaster file error)
// There should be POSIX stuff for this somewhere
size_t utility_read_file(const int fd, uint8_t *buffer, const size_t count);

// TODO: implement, comment, param check
// Exactly the same as read, but we're writing count from buffer to fd
size_t utility_write_file(const int fd, const uint8_t *buffer, const size_t count);



// The magical glue that holds it all together
struct block_store {
    bitmap_t *dbm;
    // Why not. It'll track if we have unsaved changes.
    // It'll be handly in V2
    bitmap_t *fbm;
    uint8_t *data_blocks;
    // add an fd for V2 for better disk stuff
};

// TODO: Comment
//PURPOSE: Create a new block_store and return it's pointer
//INPUT: none
//RETURN: pointer to new bitmap
block_store_t *block_store_create() {
    block_store_t *bs = malloc(sizeof(block_store_t));
	//check if memory was correctly allocated
    if (bs) {
	//create new bitmap, assign to bs->fbm
        bs->fbm = bitmap_create(BLOCK_COUNT);
        if (bs->fbm) {
            bs->dbm = bitmap_create(BLOCK_COUNT);
            if (bs->dbm) {
                // Eh, calloc, why not (technically a security risk if we don't)
                bs->data_blocks = calloc(BLOCK_SIZE, BLOCK_COUNT - FBM_SIZE);
                if (bs->data_blocks) {
                    for (size_t idx = 0; idx < FBM_SIZE; ++idx) {
			//use functions for everything, even setting bitmap values
			//set all bits to 1
                        bitmap_set(bs->fbm, idx);
                        bitmap_set(bs->dbm, idx);
                    }
                    block_store_errno = BS_OK;
			//OK
                    return bs;
                }
		//memory cleanup, method failed
                bitmap_destroy(bs->dbm);
            }
            bitmap_destroy(bs->fbm);
        }
        free(bs);
    }
    block_store_errno = BS_MEMORY;
    return NULL;
}

// TODO: Comment
//PURPOSE: Destroy an existing block store instance
//INPUT: pointer to existing block store. It const because all destroying will be performed on the contents of the pointer, and the pointer itself will not and should not be modified
//RETURN: void
void block_store_destroy(block_store_t *const bs) {
	//is input null?
    if (bs) {
	//clear out everything
        bitmap_destroy(bs->fbm);
        bs->fbm = NULL;
        bitmap_destroy(bs->dbm);
        bs->dbm = NULL;
        free(bs->data_blocks);
        bs->data_blocks = NULL;
        free(bs);
        block_store_errno = BS_OK;
        return;
    }
    block_store_errno = BS_PARAM;
}

// TODO: Comment
//PURPOSE: Finds a free block and uses it for a new block store allocation
//INPUT:   pointer to block store to be allocated
//RETURN:  size_t containing the location  
size_t block_store_allocate(block_store_t *const bs) {
    if (bs && bs->fbm) {
        size_t free_block = bitmap_ffz(bs->fbm); //ffz = find first zero. Returns location of bit
        if (free_block != SIZE_MAX) {
            bitmap_set(bs->fbm, free_block);
            // not going to mark dbm because there's no change (yet)
            return free_block;
        }
        block_store_errno = BS_FULL;
        return 0;
    }
    block_store_errno = BS_PARAM;
    return 0;
}

/*
    // V2
    size_t block_store_request(block_store_t *const bs, size_t block_id) {
    if (bs && bs->fbm && BLOCKID_VALID(block_id)) {
        if (!bitmap_test(bs->fbm, block_id)) {
            bitmap_set(bs->fbm, block_id);
            block_store_errno = BS_OK;
            return block_id;
        } else {
            block_store_errno = BS_IN_USE;
            return 0;
        }
    }
    block_store_errno = BS_PARAM;
    return 0;
    }
*/

// TODO: Comment
//PURPOSE: frees a block in a block storage
//INPUT: block storage device
//the specific block to free from the block storage
//RETURN: 0 if null
//the block_id if sucess
size_t block_store_release(block_store_t *const bs, const size_t block_id) {
    if (bs && bs->fbm && BLOCKID_VALID(block_id)) {
        // we could clear the dirty bit, since the info is no longer in use but...
        // We'll keep it. Could be useful. Doesn't really hurt anything.
        // Keeps it more true to a standard block device.
        // You could also use this function to format the specified block for security reasons
        bitmap_reset(bs->fbm, block_id);
        block_store_errno = BS_OK;
        return block_id;
    }
    block_store_errno = BS_PARAM;
    return 0;
}

// TODO: Comment
//PURPOSE: read a number of bytes from a block storage
//INPUT: const ptr to block storage
//the block_id of block to read
//void ptr to buffer to copy to
//number of bytes to copy
//offset of first byte
//RETURN: 0 if there was an error
//the number of bytes to copy if no error

size_t block_store_read(const block_store_t *const bs, const size_t block_id, void *buffer, const size_t nbytes, const size_t offset) {
    if (bs && bs->fbm && bs->data_blocks && BLOCKID_VALID(block_id)
            && buffer && nbytes && (nbytes + offset <= BLOCK_SIZE)) {
        // Not going to forbid reading of not-in-use blocks (but we'll log it via errno)
        size_t total_offset = offset + (BLOCK_SIZE * (block_id - FBM_SIZE));
        memcpy(buffer, (void *)(bs->data_blocks + total_offset), nbytes);
        block_store_errno = bitmap_test(bs->fbm, block_id) ? BS_OK : BS_FBM_REQUEST_MISMATCH;
        return nbytes;
    }
    // technically we return BS_PARAM even if the internal structure of the BS object is busted
    // Which, in reality, would be more of a BS_INTERNAL or a BS_FATAL... but it'll add another branch to everything
    // And technically the bs is a parameter...
    block_store_errno = BS_PARAM;
    return 0;
}

// TODO: Implement, comment, param check
// Gotta take read in nbytes from the buffer and write it to the offset of the block
// Pretty easy, actually
// Gotta remember to mess with the DBM!
// Let's allow writing to blocks not marked as in use as well, but log it like with read

//PURPOSE: read bytes from a buffer and write it to a block
//INPUT: ptr to block store to copy to
//destination block id
//buffer to copy from
//number of bytes to copy
//offset of first byte
//RETURN: size_t of numner of bytes copied if success
//0 if failed
size_t block_store_write(block_store_t *const bs, const size_t block_id, const void *buffer, const size_t nbytes, const size_t offset) {

	/*
	psuedocode that isn't used in my actuall function
	just used this to think things through
	size_t index     		= block_id + offset
	size_t lastIndex 		= index + nbytes * 8
	size_t bytes_written 		= 0;
	
	//copy byte by byte, or bit by bit?
	//this is 'supposed' to copy byte by byte
	while(index < lastIndex)
		lastIndex += 8
		memcpy(buffer -> bs.fdm)
		memcpy(bs.fdm -> bs.dbm)
		bytes_written += 1
	
	return bytes_written
	*/


	//paramater check
	if (bs && bs->fbm && bs->data_blocks && BLOCKID_VALID(block_id)
		    && buffer && nbytes && (nbytes + offset <= BLOCK_SIZE)) {
		size_t total_offset = offset + (BLOCK_SIZE * (block_id - FBM_SIZE));

		//this function is very similar to block_store_read, but in here, the first two parameters of memcpy are flipped
		memcpy((void *)(bs->data_blocks + total_offset), buffer, nbytes);
		block_store_errno = bitmap_test(bs->fbm, block_id) ? BS_OK : BS_FBM_REQUEST_MISMATCH;
		return nbytes;
	    }
	    block_store_errno = BS_PARAM;
	    return 0;
    block_store_errno = BS_FATAL;
    return 0;
}

// TODO: Implement, comment, param check
// Gotta make a new BS object and read it from the file
// Need to remember to get the file format right, where's the FBM??
// Since it's just loaded from file, the DBM is easy
// Should probably make sure that the file is actually the right size
// There should be POSIX stuff for everything file-related
// Probably going to have a lot of resource management, better be careful
// Lots of different errors can happen

//PURPOSE: reads a block store from a file
//INPUT: c-string of filename
//RETURN: block store of newly imported block store
block_store_t *block_store_import(const char *const filename) {
	block_store_t *bs 	= block_store_create();
		if(!bs) {
			return NULL;
		}
	
	
	    if (filename && strlen(filename) > 0 && strlen(filename) < 100) {
		const int fd = open(filename, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP);

		if (fd != -1) {
		    
			block_store_errno = BS_OK;
			close(fd);
			return bs;

		}
		block_store_errno = BS_FILE_ACCESS;
		return NULL;
	    }
    block_store_errno = BS_FATAL;
    return NULL;
}

// TODO: Comment

//PURPOSE: write a block store to an output file
//INPUT: block store to copy from
//c-string of the filename
//RETURN: 0 if failed
//total number of blocks exported to the file
size_t block_store_export(const block_store_t *const bs, const char *const filename) {
    // Thankfully, this is less of a mess than import...
    // we're going to ignore dbm, we'll treat export like it's making a new copy of the drive
    if (filename && bs && bs->fbm && bs->data_blocks) {
        const int fd = open(filename, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP);
        if (fd != -1) {
            if (utility_write_file(fd, bitmap_export(bs->fbm), FBM_SIZE * BLOCK_SIZE) == (FBM_SIZE * BLOCK_SIZE)) {
                if (utility_write_file(fd, bs->data_blocks, BLOCK_SIZE * (BLOCK_COUNT - FBM_SIZE)) == (BLOCK_SIZE * (BLOCK_COUNT - FBM_SIZE))) {
                    block_store_errno = BS_OK;
                    close(fd);
                    return BLOCK_SIZE * BLOCK_COUNT;
                }
            }
            block_store_errno = BS_FILE_IO;
            close(fd);
            return 0;
        }
        block_store_errno = BS_FILE_ACCESS;
        return 0;
    }
    block_store_errno = BS_PARAM;
    return 0;
}

// TODO: Comment

//PURPOSE: Convert a block_store_status error message into a human readable string
//INPUT: the block_store_status to be converted
//RETURN: string of human readable message
const char *block_store_strerror(block_store_status bs_err) {
    switch (bs_err) {
        case BS_OK:
            return "Ok";
        case BS_PARAM:
            return "Parameter error";
        case BS_INTERNAL:
            return "Generic internal error";
        case BS_FULL:
            return "Device full";
        case BS_IN_USE:
            return "Block in use";
        case BS_NOT_IN_USE:
            return "Block not in use";
        case BS_FILE_ACCESS:
            return "Could not access file";
        case BS_FATAL:
            return "Generic fatal error";
        case BS_FILE_IO:
            return "Error during disk I/O";
        case BS_MEMORY:
            return "Memory allocation failure";
        case BS_WARN:
            return "Generic warning";
        case BS_FBM_REQUEST_MISMATCH:
            return "Read/write request to a block not marked in use";
        default:
            return "???";
    }
}


// V2 idea:
//  add an fd field to the struct (and have export(change name?) fill it out if it doesn't exist)
//  and use that for sync. When we sync, we only write the dirtied blocks
//  and once the FULL sync is complete, we format the dbm
//  So, if at any time, the connection to the file dies, we have the dbm saved so we can try again
//   but it's probably totally broken if the sync failed for whatever reason
//   I guess a new export will fix that?


// TODO: Implement, comment, param check
// It needs to read count bytes from fd into buffer
// Probably a good idea to handle EINTR
// Maybe do a block-based read? It's more efficient, but it's more complex
// Return number of bytes read if it all worked (should always be count)
//  and 0 on error (can only really be a major disaster file error)
// There should be POSIX stuff for this somewhere

//PURPOSE: utility function to make it easier to read a file
//INPUT: a file descriptor fd
//buffer copying to
//count copying the number of bytes
//RETURN: size_t containing the size of the bytes read
size_t utility_read_file(const int fd, uint8_t *buffer, const size_t count) {

	if(!(buffer)) {
		return 0;
	}
	if(count == 0) {
		return 0;
	}
	
	size_t read_return = read(fd, buffer, count);
	if(read_return == 0){
		//printf("Error");
		return 0;
	}
	return read_return;

	
    return 0;
}


//PURPOSE: utility function to make it easier to write a file
//INPUT: a file descriptor fd
//buffer copying from
//count copying the number of bytes
//RETURN: size_t containing the size of the bytes written
size_t utility_write_file(const int fd, const uint8_t *buffer, const size_t count) {
    if(!(buffer)) {
		return 0;
	}
	if(count == 0) {
		return 0;
	}
	
	size_t write_return = write(fd, buffer, count);
	if(write_return == 0){
		//printf("Error");
		return 0;
	}
	return write_return;

	
    return 0;
}


