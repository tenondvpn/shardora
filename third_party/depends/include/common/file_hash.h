/*
* file_hash.h
* @version 1.0
*
*/

#ifndef __FILE_HASH_H__
#define __FILE_HASH_H__

#if defined (__cplusplus)
extern "C" {
#endif

typedef struct _file_hash_data {
    unsigned char data[32];
} file_hash_data;

/** Compute the gived file's hash.
 *
 *  Returns: 1: success
 *           0: failed
 *  Args:
 *  Out:    hash:       file's hash
 *  In:     file_path:  linux absolute path
 */
int get_file_hash(const char* file_path, file_hash_data* hash);

/** file signature.
 *
 *  Returns: 1: success
 *           0: failed
 *  Args:
 *  In:     file_path:  linux absolute path
 *          prikey:  user private key
 */
int file_signature(const char* file_path, const char* prikey);

/** verify file signature.
 *
 *  Returns: 1: success
 *           0: failed
 *  Args:
 *  In:     file_path:  linux absolute path
 */
int verify_file_signature(const char* file_path);

#if defined (__cplusplus)
}
#endif

#endif
