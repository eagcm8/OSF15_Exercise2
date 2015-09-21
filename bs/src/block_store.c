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
/* 
 * PURPOSE:
 * INPUTS: 
 * RETURN:
 *
 **/
block_store_t *block_store_create() {
    block_store_t *bs = malloc(sizeof(block_store_t));
    if (bs) {
        bs->fbm = bitmap_create(BLOCK_COUNT);
        if (bs->fbm) {
            bs->dbm = bitmap_create(BLOCK_COUNT);
            if (bs->dbm) {
                // Eh, calloc, why not (technically a security risk if we don't)
                bs->data_blocks = calloc(BLOCK_SIZE, BLOCK_COUNT - FBM_SIZE);
                if (bs->data_blocks) {
                    for (size_t idx = 0; idx < FBM_SIZE; ++idx) {
                        bitmap_set(bs->fbm, idx);
                        bitmap_set(bs->dbm, idx);
                    }
                    block_store_errno = BS_OK;
                    return bs;
                }
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
/* 
 * PURPOSE: destroys block store
 * INPUTS: bs (block store)
 * RETURN: nothing
 **/
void block_store_destroy(block_store_t *const bs) {
    if (bs) {
        bitmap_destroy(bs->fbm);
        bs->fbm = NULL;
        bitmap_destroy(bs->dbm);
        bs->dbm = NULL;
        free(bs->data_blocks);
        bs->data_blocks = NULL;
        free(bs);
        block_store_errno = BS_OK;
    }
    block_store_errno = BS_PARAM;
}

// TODO: Comment
/* 
 * PURPOSE: allocates block store
 * INPUTS: bs (block store)
 * RETURN: size_t 0 if success
 **/
size_t block_store_allocate(block_store_t *const bs) {
    if (bs && bs->fbm) {
        size_t free_block = bitmap_ffz(bs->fbm);
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
    /* 
 * PURPOSE: formats/ clears blocks
 * INPUTS: bs (block store), size_t block_id
 * RETURN: size_t 0 if success
 **/
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
    /* 
 * PURPOSE: reads block store
 * INPUTS: bs (block store), size_t block_id, uint8_t buffer pointer, size_t nbytes, size_t offset
 * RETURN: size_t 0 if success
 **/
size_t block_store_read(const block_store_t *const bs, const size_t block_id, const uint8_t *buffer, const size_t nbytes, const size_t offset) {
    if (bs && bs->fbm && bs->data_blocks && BLOCKID_VALID(block_id)
            && buffer && nbytes && (nbytes + offset <= BLOCK_SIZE)) {
        // Not going to forbid reading of not-in-use blocks (but we'll log it via errno)
        memcpy((void *)buffer, bs->data_blocks + offset, nbytes);
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
    /* 
 * PURPOSE: writes block store
 * INPUTS: bs (block store), size_t block_id, uint8_t buffer pointer, size_t nbytes, size_t offset
 * RETURN: size_t 0 if success
 **/
size_t block_store_write(block_store_t *const bs, const size_t block_id, const uint8_t *buffer, const size_t nbytes, const size_t offset) {
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

//make new blockstore object and return
    /* 
 * PURPOSE: imports block store
 * INPUTS: char, filename
 * RETURN: block_store_t
 **/
block_store_t *block_store_import(const char *const filename) {
    block_store_t *bs = block_store_create(); //create blockstore bs
    if(bs){
        bitmap_destroy(bs -> fbm); //destroy fbm in bs
        bitmap_format(bs -> dbm, 0); //format dbm in bs
        struct stat file_info; //
        size_t urf = 0;
        if(stat(filename, &file_info) != -1){
            if(file_info.st_size == BLOCK_SIZE * BLOCK_COUNT){
                int fd;
                fd = open(filename, O_RDONLY);
                if(fd != -1){
                    size_t ct = FBM_SIZE * BLOCK_SIZE;
                    urf = utility_read_file(fd, bs -> data_blocks, ct);
                    if (urf)
                    {
                        bs -> fbm = bitmap_import(BLOCK_COUNT, bs -> data_blocks);
                        if (bs -> fbm)
                        {
                            ct = (BLOCK_COUNT - FBM_SIZE) * BLOCK_SIZE;
                            urf = utility_read_file(fd, bs -> data_blocks, ct);
                            if (urf)
                            {
                                block_store_errno = BS_OK;
                                close(fd);
                            }return 0;
                        }
                    }
                }
            }
        }
    }
    


// struct stat {
//                dev_t     st_dev;         /* ID of device containing file */
//                ino_t     st_ino;         /* inode number */
//                mode_t    st_mode;        /* protection */
//                nlink_t   st_nlink;       /* number of hard links */
//                uid_t     st_uid;         /* user ID of owner */
//                gid_t     st_gid;         /* group ID of owner */
//                dev_t     st_rdev;        /* device ID (if special file) */
//                off_t     st_size;        /* total size, in bytes */
//                blksize_t st_blksize;     /* blocksize for filesystem I/O */
//                blkcnt_t  st_blocks;      /* number of 512B blocks allocated */


    block_store_errno = BS_FATAL;
    return NULL;
}

// TODO: Comment
    /* 
 * PURPOSE: exports block store
 * INPUTS: bs (block store), block_store_t, char, filename
 * RETURN: size_t 0 if success
 **/
size_t block_store_export(const block_store_t *const bs, const char *const filename) {
    // Thankfully, this is less of a mess than import...
    // we're going to ignore dbm, we'll treat export like it's making a new copy of the drive
    if (filename && bs && bs->fbm && bs->data_blocks) {
        const int fd = open(filename, O_WRONLY | O_TRUNC);
        if (fd != -1) {
            if (utility_write_file(fd, bitmap_export(bs->fbm), FBM_SIZE * BLOCK_SIZE) == 0) {
                if (utility_write_file(fd, bs->data_blocks, BLOCK_SIZE * (BLOCK_COUNT - FBM_SIZE)) == 0) {
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
    /* 
 * PURPOSE: returns block store error codes
 * INPUTS: block_store_status bs_err
 * RETURN: character array
 **/
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


    /* 
 * PURPOSE: reads in file
 * INPUTS: fd (file descriptor), buffer pointer, count
 * RETURN: size_t 0 if success
 **/
size_t utility_read_file(const int fd, uint8_t *buffer, const size_t count) {
    size_t amount_read = 0;
    do
    {
        ssize_t readfile = read(fd, buffer+amount_read, count-amount_read);
        if (readfile == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }return 0;
        } amount_read += readfile;
    } while (amount_read < count);
    
    return 0;
}

    /* 
 * PURPOSE: writes in file
 * INPUTS: fd (file descriptor), buffer pointer, count
 * RETURN: size_t 0 if success
 **/
size_t utility_write_file(const int fd, const uint8_t *buffer, const size_t count) {
    size_t amount_read = 0;
    do
    {
        ssize_t writefile = write(fd, buffer+amount_read, count-amount_read);
        if (writefile == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }return 0;
        } amount_read += writefile;
    } while (amount_read < count);
    
    return 0;
}