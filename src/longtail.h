#pragma once

#include <stdint.h>
#include <stddef.h>

#if defined(LONGTAIL_EXPORT_SYMBOLS)
    #if defined(__GNUC__) || defined(__clang__)
        #define LONGTAIL_EXPORT __attribute__ ((visibility ("default")))
    #else
        #if defined(_WIN32)
            #define LONGTAIL_EXPORT __declspec(dllexport)
        #endif
    #endif
#else
    #define LONGTAIL_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t TLongtail_Hash;
struct Longtail_BlockIndex;
struct Longtail_Paths;
struct Longtail_FileInfos;
struct Longtail_VersionIndex;
struct Longtail_StoredBlock;
struct Longtail_ContentIndex;
struct Longtail_VersionDiff;

////////////// Longtail_API

struct Longtail_API;

/*! @brief Prototype for the Dispose API function.
 */
typedef void (*Longtail_DisposeFunc)(struct Longtail_API* api);

/*! @brief Base API interface.
 *
 * Each API includes a the basic Longtail_API as the first member of its API structure allowing for a generic Dispose pattern
 *
 */
struct Longtail_API
{
    Longtail_DisposeFunc Dispose;
};

/*! @brief Dispose an API.
 *
 * Calls the Dispose function of the API base interface. An interface struct adds an the struct Longtail_API member as its first
 * member making it possible to have a generic pattern for disposing APIs.
 *
 * @param[in] api              A pointer to a Longtail_API structure
 * @return                     void
 */
LONGTAIL_EXPORT void Longtail_DisposeAPI(struct Longtail_API* api);

/*! @brief Convenience function to abstract the API dispose
*/
#define SAFE_DISPOSE_API(api) if (api) { Longtail_DisposeAPI(&(api)->m_API);}

////////////// Longtail_CancelAPI
struct Longtail_CancelAPI;

/*! @brief Longtail_CancelAPI cancel token.
 */
typedef struct Longtail_CancelAPI_CancelToken* Longtail_CancelAPI_HCancelToken;

typedef int (*Longtail_CancelAPI_CreateTokenFunc)(struct Longtail_CancelAPI* cancel_api, Longtail_CancelAPI_HCancelToken* out_token);
typedef int (*Longtail_CancelAPI_CancelFunc)(struct Longtail_CancelAPI* cancel_api, Longtail_CancelAPI_HCancelToken token);
typedef int (*Longtail_CancelAPI_IsCancelledFunc)(struct Longtail_CancelAPI* cancel_api, Longtail_CancelAPI_HCancelToken token);
typedef int (*Longtail_CancelAPI_DisposeTokenFunc)(struct Longtail_CancelAPI* cancel_api, Longtail_CancelAPI_HCancelToken token);

/*! @brief struct Longtail_CancelAPI.
 *
 * API for handling asyncronous cancelling of operations
 *
 * The controller of an operation would create a cancel API and a cancel token. Both the API and and the token
 * is passed to the operation which would check if the cancel token is cancelled at convenient times and abort
 * if a cancel is detected.
 *
 * The controller uses the cancel token to cancel the operation and then waits for the operation to return.
 *
 * @code
 * void DoAsyncWork(struct Longtail_CancelAPI* api, Longtail_CancelAPI_HCancelToken token) {
 *  while(!api->IsCancelled(api, token)) {
 *      // Do work
 *  }
 * }
 *
 * struct Longtail_CancelAPI* api = Longtail_MakeCancelAPI(malloc(sizeof(struct Longtail_CancelAPI)), dispose, create_token, cancel, is_cancelled, dispose_token);
 * Longtail_CancelAPI_HCancelToken token = api->CreateToken(api);
 * DoAsyncWork(api, token);
 * if (UserPressedCancel()) {
 *  api->Cancel(api, token);
 * }
 * WaitForAsyncWork();
 * api->DisposeToken(api, token);
 * SAFE_DISPOSE_API(api);
 * @endcode
 */
struct Longtail_CancelAPI
{
    struct Longtail_API m_API;
    Longtail_CancelAPI_CreateTokenFunc CreateToken;
    Longtail_CancelAPI_CancelFunc Cancel;
    Longtail_CancelAPI_IsCancelledFunc IsCancelled;
    Longtail_CancelAPI_DisposeTokenFunc DisposeToken;
};

LONGTAIL_EXPORT uint64_t Longtail_GetCancelAPISize();

/*! @brief Create a Longtail_CancelAPI cancel API.
 *
 * @param[in] mem                   pointer to memory at least sizeof(struct Longtail_CancelAPI) bytes
 * @param[in] dispose_func          implementation of Longtail_DisposeFunc
 * @param[in] create_token_func     implementation of Longtail_CancelAPI_CreateTokenFunc
 * @param[in] cancel_func           implementation of Longtail_CancelAPI_CancelFunc
 * @param[in] is_cancelled          implementation of Longtail_CancelAPI_IsCancelledFunc
 * @param[in] dispose_token_func    implementation of Longtail_CancelAPI_DisposeTokenFunc
 * @return                          initialize API structure (same address as @p mem)
 */
LONGTAIL_EXPORT struct Longtail_CancelAPI* Longtail_MakeCancelAPI(
    void* mem,
    Longtail_DisposeFunc dispose_func,
    Longtail_CancelAPI_CreateTokenFunc create_token_func,
    Longtail_CancelAPI_CancelFunc cancel_func,
    Longtail_CancelAPI_IsCancelledFunc is_cancelled,
    Longtail_CancelAPI_DisposeTokenFunc dispose_token_func);

/*! @brief Create a cancel token
 *
 * The token is valid until disposed with Longtail_CancelAPI_DisposeToken.
 *
 * @param[in] cancel_api    pointer to an initialized struct Longtail_CancelAPI
 * @param[out] out_token    pointer to a uninitialized Longtail_CancelAPI_HCancelToken which will be initialized on success
 * @return                  errno style error - zero on success
 */
LONGTAIL_EXPORT int Longtail_CancelAPI_CreateToken(struct Longtail_CancelAPI* cancel_api, Longtail_CancelAPI_HCancelToken* out_token);

/*! @brief Set a cancel token to cancelled state
 *
 * The token is set to cancelled and calling Longtail_CancelAPI_IsCancelled with the token will return ECANCELED
 * It is allowed to call Longtail_CancelAPI_Cancel multiple times on the same token.
 *
 * @param[in] cancel_api    pointer to an initialized struct Longtail_CancelAPI
 * @param[in] token         pointer to a initialized Longtail_CancelAPI_HCancelToken
 * @return                  errno style error - zero on success
 */
LONGTAIL_EXPORT int Longtail_CancelAPI_Cancel(struct Longtail_CancelAPI* cancel_api, Longtail_CancelAPI_HCancelToken token);

/*! @brief Dispose a cancel token
 *
 * Calling Dispose on the token invalidates it and renders it unusable.
 *
 * @param[in] cancel_api    pointer to an initialized struct Longtail_CancelAPI
 * @param[in] token         pointer to a initialized Longtail_CancelAPI_HCancelToken
 * @return                  errno style error - zero on success
 */
LONGTAIL_EXPORT int Longtail_CancelAPI_DisposeToken(struct Longtail_CancelAPI* cancel_api, Longtail_CancelAPI_HCancelToken token);

/*! @brief Check if token is cancelled
 *
 * @param[in] cancel_api    pointer to an initialized struct Longtail_CancelAPI
 * @param[in] token         pointer to a initialized Longtail_CancelAPI_HCancelToken
 * @return                  errno style error - zero = not cancelled, ECANCELED if token is cancelled
 */
LONGTAIL_EXPORT int Longtail_CancelAPI_IsCancelled(struct Longtail_CancelAPI* cancel_api, Longtail_CancelAPI_HCancelToken token);

////////////// Longtail_PathFilterAPI

struct Longtail_PathFilterAPI;

typedef int (*Longtail_PathFilter_IncludeFunc)(
    struct Longtail_PathFilterAPI* path_filter_api,
    const char* root_path,
    const char* asset_path,
    const char* asset_name,
    int is_dir,
    uint64_t size,
    uint16_t permissions);

struct Longtail_PathFilterAPI
{
    struct Longtail_API m_API;
    Longtail_PathFilter_IncludeFunc Include;
};

LONGTAIL_EXPORT uint64_t Longtail_GetPathFilterAPISize();

LONGTAIL_EXPORT struct Longtail_PathFilterAPI* Longtail_MakePathFilterAPI(
    void* mem,
    Longtail_DisposeFunc dispose_func,
    Longtail_PathFilter_IncludeFunc include_filter_func);

LONGTAIL_EXPORT int Longtail_PathFilter_Include(struct Longtail_PathFilterAPI* path_filter_api, const char* root_path, const char* asset_path, const char* asset_name, int is_dir, uint64_t size, uint16_t permissions);

////////////// Longtail_HashAPI

struct Longtail_HashAPI;

typedef struct Longtail_HashAPI_Context* Longtail_HashAPI_HContext;
typedef uint32_t (*Longtail_Hash_GetIdentifierFunc)(struct Longtail_HashAPI* hash_api);
typedef int (*Longtail_Hash_BeginContextFunc)(struct Longtail_HashAPI* hash_api, Longtail_HashAPI_HContext* out_context);
typedef void (*Longtail_Hash_HashFunc)(struct Longtail_HashAPI* hash_api, Longtail_HashAPI_HContext context, uint32_t length, const void* data);
typedef uint64_t (*Longtail_Hash_EndContextFunc)(struct Longtail_HashAPI* hash_api, Longtail_HashAPI_HContext context);
typedef int (*Longtail_Hash_HashBufferFunc)(struct Longtail_HashAPI* hash_api, uint32_t length, const void* data, uint64_t* out_hash);

struct Longtail_HashAPI
{
    struct Longtail_API m_API;
    Longtail_Hash_GetIdentifierFunc GetIdentifier;
    Longtail_Hash_BeginContextFunc BeginContext;
    Longtail_Hash_HashFunc Hash;
    Longtail_Hash_EndContextFunc EndContext;
    Longtail_Hash_HashBufferFunc HashBuffer;
};

LONGTAIL_EXPORT uint64_t Longtail_GetHashAPISize();

LONGTAIL_EXPORT struct Longtail_HashAPI* Longtail_MakeHashAPI(
    void* mem,
    Longtail_DisposeFunc dispose_func,
    Longtail_Hash_GetIdentifierFunc get_identifier_func,
    Longtail_Hash_BeginContextFunc begin_context_func,
    Longtail_Hash_HashFunc hash_func,
    Longtail_Hash_EndContextFunc end_context_func,
    Longtail_Hash_HashBufferFunc hash_buffer_func);

LONGTAIL_EXPORT uint32_t Longtail_Hash_GetIdentifier(struct Longtail_HashAPI* hash_api);
LONGTAIL_EXPORT int Longtail_Hash_BeginContext(struct Longtail_HashAPI* hash_api, Longtail_HashAPI_HContext* out_context);
LONGTAIL_EXPORT void Longtail_Hash_Hash(struct Longtail_HashAPI* hash_api, Longtail_HashAPI_HContext context, uint32_t length, const void* data);
LONGTAIL_EXPORT uint64_t Longtail_Hash_EndContext(struct Longtail_HashAPI* hash_api, Longtail_HashAPI_HContext context);
LONGTAIL_EXPORT int Longtail_Hash_HashBuffer(struct Longtail_HashAPI* hash_api, uint32_t length, const void* data, uint64_t* out_hash);

////////////// Longtail_HashRegistryAPI

struct Longtail_HashRegistryAPI;

typedef int (*Longtail_HashRegistry_GetHashAPIFunc)(struct Longtail_HashRegistryAPI* hash_registry, uint32_t hash_type, struct Longtail_HashAPI** out_hash_api);

struct Longtail_HashRegistryAPI
{
    struct Longtail_API m_API;
    Longtail_HashRegistry_GetHashAPIFunc GetHashAPI;
};

LONGTAIL_EXPORT uint64_t Longtail_GetHashRegistrySize();

LONGTAIL_EXPORT struct Longtail_HashRegistryAPI* Longtail_MakeHashRegistryAPI(
    void* mem,
    Longtail_DisposeFunc dispose_func,
    Longtail_HashRegistry_GetHashAPIFunc get_hash_api_func);

LONGTAIL_EXPORT int Longtail_GetHashRegistry_GetHashAPI(struct Longtail_HashRegistryAPI* hash_registry, uint32_t hash_type, struct Longtail_HashAPI** out_compression_api);


////////////// Longtail_CompressionAPI

struct Longtail_CompressionAPI;

typedef size_t (*Longtail_CompressionAPI_GetMaxCompressedSizeFunc)(struct Longtail_CompressionAPI* compression_api, uint32_t settings_id, size_t size);
typedef int (*Longtail_CompressionAPI_CompressFunc)(struct Longtail_CompressionAPI* compression_api, uint32_t settings_id, const char* uncompressed, char* compressed, size_t uncompressed_size, size_t max_compressed_size, size_t* out_compressed_size);
typedef int (*Longtail_CompressionAPI_DecompressFunc)(struct Longtail_CompressionAPI* compression_api, const char* compressed, char* uncompressed, size_t compressed_size, size_t max_uncompressed_size, size_t* out_uncompressed_size);

struct Longtail_CompressionAPI
{
    struct Longtail_API m_API;
    Longtail_CompressionAPI_GetMaxCompressedSizeFunc GetMaxCompressedSize;
    Longtail_CompressionAPI_CompressFunc Compress;
    Longtail_CompressionAPI_DecompressFunc Decompress;
};

LONGTAIL_EXPORT uint64_t Longtail_GetCompressionAPISize();

LONGTAIL_EXPORT struct Longtail_CompressionAPI* Longtail_MakeCompressionAPI(
    void* mem,
    Longtail_DisposeFunc dispose_func,
    Longtail_CompressionAPI_GetMaxCompressedSizeFunc get_max_compressed_size_func,
    Longtail_CompressionAPI_CompressFunc compress_func,
    Longtail_CompressionAPI_DecompressFunc decompress_func);


////////////// Longtail_CompressionRegistryAPI

struct Longtail_CompressionRegistryAPI;

typedef int (*Longtail_CompressionRegistry_GetCompressionAPIFunc)(struct Longtail_CompressionRegistryAPI* compression_registry, uint32_t compression_type, struct Longtail_CompressionAPI** out_compression_api, uint32_t* out_settings_id);

struct Longtail_CompressionRegistryAPI
{
    struct Longtail_API m_API;
    Longtail_CompressionRegistry_GetCompressionAPIFunc GetCompressionAPI;
};

LONGTAIL_EXPORT uint64_t Longtail_GetCompressionRegistryAPISize();

LONGTAIL_EXPORT struct Longtail_CompressionRegistryAPI* Longtail_MakeCompressionRegistryAPI(
    void* mem,
    Longtail_DisposeFunc dispose_func,
    Longtail_CompressionRegistry_GetCompressionAPIFunc get_compression_api_func);

LONGTAIL_EXPORT int Longtail_GetCompressionRegistry_GetCompressionAPI(struct Longtail_CompressionRegistryAPI* compression_registry, uint32_t compression_type, struct Longtail_CompressionAPI** out_compression_api, uint32_t* out_settings_id);

////////////// Longtail_StorageAPI

typedef struct Longtail_StorageAPI_OpenFile* Longtail_StorageAPI_HOpenFile;
typedef struct Longtail_StorageAPI_Iterator* Longtail_StorageAPI_HIterator;
typedef struct Longtail_StorageAPI_LockFile* Longtail_StorageAPI_HLockFile;

enum
{
    Longtail_StorageAPI_OtherExecuteAccess  = 0001,
    Longtail_StorageAPI_OtherWriteAccess    = 0002,
    Longtail_StorageAPI_OtherReadAccess     = 0004,

    Longtail_StorageAPI_GroupExecuteAccess  = 0010,
    Longtail_StorageAPI_GroupWriteAccess    = 0020,
    Longtail_StorageAPI_GroupReadAccess     = 0040,

    Longtail_StorageAPI_UserExecuteAccess   = 0100,
    Longtail_StorageAPI_UserWriteAccess     = 0200,
    Longtail_StorageAPI_UserReadAccess      = 0400
};

struct Longtail_StorageAPI_EntryProperties
{
    const char* m_Name;
    uint64_t m_Size;
    uint16_t m_Permissions;
    int m_IsDir;
};

struct Longtail_StorageAPI;

typedef int (*Longtail_Storage_OpenReadFileFunc)(struct Longtail_StorageAPI* storage_api, const char* path, Longtail_StorageAPI_HOpenFile* out_open_file);
typedef int (*Longtail_Storage_GetSizeFunc)(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HOpenFile f, uint64_t* out_size);
typedef int (*Longtail_Storage_ReadFunc)(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HOpenFile f, uint64_t offset, uint64_t length, void* output);
typedef int (*Longtail_Storage_OpenWriteFileFunc)(struct Longtail_StorageAPI* storage_api, const char* path, uint64_t initial_size, Longtail_StorageAPI_HOpenFile* out_open_file);
typedef int (*Longtail_Storage_WriteFunc)(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HOpenFile f, uint64_t offset, uint64_t length, const void* input);
typedef int (*Longtail_Storage_SetSizeFunc)(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HOpenFile f, uint64_t length);
typedef int (*Longtail_Storage_SetPermissionsFunc)(struct Longtail_StorageAPI* storage_api, const char* path, uint16_t permissions);
typedef int (*Longtail_Storage_GetPermissionsFunc)(struct Longtail_StorageAPI* storage_api, const char* path, uint16_t* out_permissions);
typedef void (*Longtail_Storage_CloseFileFunc)(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HOpenFile f);
typedef int (*Longtail_Storage_CreateDirFunc)(struct Longtail_StorageAPI* storage_api, const char* path);
typedef int (*Longtail_Storage_RenameFileFunc)(struct Longtail_StorageAPI* storage_api, const char* source_path, const char* target_path);
typedef char* (*Longtail_Storage_ConcatPathFunc)(struct Longtail_StorageAPI* storage_api, const char* root_path, const char* sub_path);
typedef int (*Longtail_Storage_IsDirFunc)(struct Longtail_StorageAPI* storage_api, const char* path);
typedef int (*Longtail_Storage_IsFileFunc)(struct Longtail_StorageAPI* storage_api, const char* path);
typedef int (*Longtail_Storage_RemoveDirFunc)(struct Longtail_StorageAPI* storage_api, const char* path);
typedef int (*Longtail_Storage_RemoveFileFunc)(struct Longtail_StorageAPI* storage_api, const char* path);
typedef int (*Longtail_Storage_StartFindFunc)(struct Longtail_StorageAPI* storage_api, const char* path, Longtail_StorageAPI_HIterator* out_iterator);
typedef int (*Longtail_Storage_FindNextFunc)(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HIterator iterator);
typedef void (*Longtail_Storage_CloseFindFunc)(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HIterator iterator);
typedef int (*Longtail_Storage_GetEntryPropertiesFunc)(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HIterator iterator, struct Longtail_StorageAPI_EntryProperties* out_properties);
typedef int (*Longtail_Storage_LockFileFunc)(struct Longtail_StorageAPI* storage_api, const char* path, Longtail_StorageAPI_HLockFile* out_lock_file);
typedef int (*Longtail_Storage_UnlockFileFunc)(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HLockFile file_lock);

struct Longtail_StorageAPI
{
    struct Longtail_API m_API;
    Longtail_Storage_OpenReadFileFunc OpenReadFile;
    Longtail_Storage_GetSizeFunc GetSize;
    Longtail_Storage_ReadFunc Read;
    Longtail_Storage_OpenWriteFileFunc OpenWriteFile;
    Longtail_Storage_WriteFunc Write;
    Longtail_Storage_SetSizeFunc SetSize;
    Longtail_Storage_SetPermissionsFunc SetPermissions;
    Longtail_Storage_GetPermissionsFunc GetPermissions;
    Longtail_Storage_CloseFileFunc CloseFile;
    Longtail_Storage_CreateDirFunc CreateDir;
    Longtail_Storage_RenameFileFunc RenameFile;
    Longtail_Storage_ConcatPathFunc ConcatPath;
    Longtail_Storage_IsDirFunc IsDir;
    Longtail_Storage_IsFileFunc IsFile;
    Longtail_Storage_RemoveDirFunc RemoveDir;
    Longtail_Storage_RemoveFileFunc RemoveFile;
    Longtail_Storage_StartFindFunc StartFind;
    Longtail_Storage_FindNextFunc FindNext;
    Longtail_Storage_CloseFindFunc CloseFind;
    Longtail_Storage_GetEntryPropertiesFunc GetEntryProperties;
    Longtail_Storage_LockFileFunc LockFile;
    Longtail_Storage_UnlockFileFunc UnlockFile;
};

LONGTAIL_EXPORT uint64_t Longtail_GetStorageAPISize();

LONGTAIL_EXPORT struct Longtail_StorageAPI* Longtail_MakeStorageAPI(
    void* mem,
    Longtail_DisposeFunc dispose_func,
    Longtail_Storage_OpenReadFileFunc open_read_file_func,
    Longtail_Storage_GetSizeFunc get_size_func,
    Longtail_Storage_ReadFunc read_func,
    Longtail_Storage_OpenWriteFileFunc open_write_file_func,
    Longtail_Storage_WriteFunc write_func,
    Longtail_Storage_SetSizeFunc set_size_func,
    Longtail_Storage_SetPermissionsFunc set_permissions_func,
    Longtail_Storage_GetPermissionsFunc get_permissions_func,
    Longtail_Storage_CloseFileFunc close_file_func,
    Longtail_Storage_CreateDirFunc create_dir_func,
    Longtail_Storage_RenameFileFunc rename_file_func,
    Longtail_Storage_ConcatPathFunc concat_path_func,
    Longtail_Storage_IsDirFunc is_dir_func,
    Longtail_Storage_IsFileFunc is_file_func,
    Longtail_Storage_RemoveDirFunc remove_dir_func,
    Longtail_Storage_RemoveFileFunc remove_file_func,
    Longtail_Storage_StartFindFunc start_find_func,
    Longtail_Storage_FindNextFunc find_next_func,
    Longtail_Storage_CloseFindFunc close_find_func,
    Longtail_Storage_GetEntryPropertiesFunc get_entry_properties_func,
    Longtail_Storage_LockFileFunc lock_file_func,
    Longtail_Storage_UnlockFileFunc unlock_file_func);

LONGTAIL_EXPORT int Longtail_Storage_OpenReadFile(struct Longtail_StorageAPI* storage_api, const char* path, Longtail_StorageAPI_HOpenFile* out_open_file);
LONGTAIL_EXPORT int Longtail_Storage_GetSize(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HOpenFile f, uint64_t* out_size);
LONGTAIL_EXPORT int Longtail_Storage_Read(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HOpenFile f, uint64_t offset, uint64_t length, void* output);
LONGTAIL_EXPORT int Longtail_Storage_OpenWriteFile(struct Longtail_StorageAPI* storage_api, const char* path, uint64_t initial_size, Longtail_StorageAPI_HOpenFile* out_open_file);
LONGTAIL_EXPORT int Longtail_Storage_Write(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HOpenFile f, uint64_t offset, uint64_t length, const void* input);
LONGTAIL_EXPORT int Longtail_Storage_SetSize(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HOpenFile f, uint64_t length);
LONGTAIL_EXPORT int Longtail_Storage_SetPermissions(struct Longtail_StorageAPI* storage_api, const char* path, uint16_t permissions);
LONGTAIL_EXPORT int Longtail_Storage_GetPermissions(struct Longtail_StorageAPI* storage_api, const char* path, uint16_t* out_permissions);
LONGTAIL_EXPORT void Longtail_Storage_CloseFile(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HOpenFile f);
LONGTAIL_EXPORT int Longtail_Storage_CreateDir(struct Longtail_StorageAPI* storage_api, const char* path);
LONGTAIL_EXPORT int Longtail_Storage_RenameFile(struct Longtail_StorageAPI* storage_api, const char* source_path, const char* target_path);
LONGTAIL_EXPORT char* Longtail_Storage_ConcatPath(struct Longtail_StorageAPI* storage_api, const char* root_path, const char* sub_path);
LONGTAIL_EXPORT int Longtail_Storage_IsDir(struct Longtail_StorageAPI* storage_api, const char* path);
LONGTAIL_EXPORT int Longtail_Storage_IsFile(struct Longtail_StorageAPI* storage_api, const char* path);
LONGTAIL_EXPORT int Longtail_Storage_RemoveDir(struct Longtail_StorageAPI* storage_api, const char* path);
LONGTAIL_EXPORT int Longtail_Storage_RemoveFile(struct Longtail_StorageAPI* storage_api, const char* path);
LONGTAIL_EXPORT int Longtail_Storage_StartFind(struct Longtail_StorageAPI* storage_api, const char* path, Longtail_StorageAPI_HIterator* out_iterator);
LONGTAIL_EXPORT int Longtail_Storage_FindNext(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HIterator iterator);
LONGTAIL_EXPORT void Longtail_Storage_CloseFind(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HIterator iterator);
LONGTAIL_EXPORT int Longtail_Storage_GetEntryProperties(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HIterator iterator, struct Longtail_StorageAPI_EntryProperties* out_properties);
LONGTAIL_EXPORT int Longtail_Storage_LockFile(struct Longtail_StorageAPI* storage_api, const char* path, Longtail_StorageAPI_HLockFile* out_lock_file);
LONGTAIL_EXPORT int Longtail_Storage_UnlockFile(struct Longtail_StorageAPI* storage_api, Longtail_StorageAPI_HLockFile lock_file);

////////////// Longtail_ProgressAPI

struct Longtail_ProgressAPI;

typedef void (*Longtail_Progress_OnProgressFunc)(struct Longtail_ProgressAPI* progressAPI, uint32_t total_count, uint32_t done_count);

struct Longtail_ProgressAPI
{
    struct Longtail_API m_API;
    Longtail_Progress_OnProgressFunc OnProgress;
};

LONGTAIL_EXPORT uint64_t Longtail_GetProgressAPISize();

LONGTAIL_EXPORT struct Longtail_ProgressAPI* Longtail_MakeProgressAPI(
    void* mem,
    Longtail_DisposeFunc dispose_func,
    Longtail_Progress_OnProgressFunc on_progress_func);

LONGTAIL_EXPORT void Longtail_Progress_OnProgress(struct Longtail_ProgressAPI* progressAPI, uint32_t total_count, uint32_t done_count);

////////////// Longtail_JobAPI

struct Longtail_JobAPI;
typedef void* Longtail_JobAPI_Jobs;
typedef int (*Longtail_JobAPI_JobFunc)(void* context, uint32_t job_id, int is_cancelled);
typedef void* Longtail_JobAPI_Group;

typedef uint32_t (*Longtail_Job_GetWorkerCountFunc)(struct Longtail_JobAPI* job_api);
typedef int (*Longtail_Job_ReserveJobsFunc)(struct Longtail_JobAPI* job_api, uint32_t job_count, Longtail_JobAPI_Group* out_job_group);
typedef int (*Longtail_Job_CreateJobsFunc)(struct Longtail_JobAPI* job_api, Longtail_JobAPI_Group job_group, uint32_t job_count, Longtail_JobAPI_JobFunc job_funcs[], void* job_contexts[], Longtail_JobAPI_Jobs* out_jobs);
typedef int (*Longtail_Job_AddDependeciesFunc)(struct Longtail_JobAPI* job_api, uint32_t job_count, Longtail_JobAPI_Jobs jobs, uint32_t dependency_job_count, Longtail_JobAPI_Jobs dependency_jobs);
typedef int (*Longtail_Job_ReadyJobsFunc)(struct Longtail_JobAPI* job_api, uint32_t job_count, Longtail_JobAPI_Jobs jobs);
typedef int (*Longtail_Job_WaitForAllJobsFunc)(struct Longtail_JobAPI* job_api, Longtail_JobAPI_Group job_group, struct Longtail_ProgressAPI* progressAPI, struct Longtail_CancelAPI* optional_cancel_api, Longtail_CancelAPI_HCancelToken optional_cancel_token);
typedef int (*Longtail_Job_ResumeJobFunc)(struct Longtail_JobAPI* job_api, uint32_t job_id);

struct Longtail_JobAPI
{
    struct Longtail_API m_API;
    Longtail_Job_GetWorkerCountFunc GetWorkerCount;
    Longtail_Job_ReserveJobsFunc ReserveJobs;
    Longtail_Job_CreateJobsFunc CreateJobs;
    Longtail_Job_AddDependeciesFunc AddDependecies;
    Longtail_Job_ReadyJobsFunc ReadyJobs;
    Longtail_Job_WaitForAllJobsFunc WaitForAllJobs;
    Longtail_Job_ResumeJobFunc ResumeJob;
};

LONGTAIL_EXPORT uint64_t Longtail_GetJobAPISize();

struct Longtail_JobAPI* Longtail_MakeJobAPI(
    void* mem,
    Longtail_DisposeFunc dispose_func,
    Longtail_Job_GetWorkerCountFunc get_worker_count_func,
    Longtail_Job_ReserveJobsFunc reserve_jobs_func,
    Longtail_Job_CreateJobsFunc create_jobs_func,
    Longtail_Job_AddDependeciesFunc add_dependecies_func,
    Longtail_Job_ReadyJobsFunc ready_jobs_func,
    Longtail_Job_WaitForAllJobsFunc wait_for_all_jobs_func,
    Longtail_Job_ResumeJobFunc resume_job_func);

LONGTAIL_EXPORT uint32_t Longtail_Job_GetWorkerCount(struct Longtail_JobAPI* job_api);
LONGTAIL_EXPORT int Longtail_Job_ReserveJobs(struct Longtail_JobAPI* job_api, uint32_t job_count, Longtail_JobAPI_Group* out_job_group);
LONGTAIL_EXPORT int Longtail_Job_CreateJobs(struct Longtail_JobAPI* job_api, Longtail_JobAPI_Group job_group, uint32_t job_count, Longtail_JobAPI_JobFunc job_funcs[], void* job_contexts[], Longtail_JobAPI_Jobs* out_jobs);
LONGTAIL_EXPORT int Longtail_Job_AddDependecies(struct Longtail_JobAPI* job_api, uint32_t job_count, Longtail_JobAPI_Jobs jobs, uint32_t dependency_job_count, Longtail_JobAPI_Jobs dependency_jobs);
LONGTAIL_EXPORT int Longtail_Job_ReadyJobs(struct Longtail_JobAPI* job_api, uint32_t job_count, Longtail_JobAPI_Jobs jobs);
LONGTAIL_EXPORT int Longtail_Job_WaitForAllJobs(struct Longtail_JobAPI* job_api, Longtail_JobAPI_Group job_group, struct Longtail_ProgressAPI* progressAPI, struct Longtail_CancelAPI* optional_cancel_api, Longtail_CancelAPI_HCancelToken optional_cancel_token);
LONGTAIL_EXPORT int Longtail_Job_ResumeJob(struct Longtail_JobAPI* job_api, uint32_t job_id);

////////////// Longtail_ChunkerAPI

struct Longtail_ChunkerAPI;

typedef struct Longtail_ChunkerAPI_Chunker* Longtail_ChunkerAPI_HChunker;

typedef int (*Longtail_Chunker_Feeder)(void* context, Longtail_ChunkerAPI_HChunker chunker, uint32_t requested_size, char* buffer, uint32_t* out_size);

struct Longtail_Chunker_ChunkRange
{
    const uint8_t* buf;
    uint64_t offset;
    uint32_t len;
};

typedef int (*Longtail_Chunker_GetMinChunkSizeFunc)(struct Longtail_ChunkerAPI* chunker_api, uint32_t* out_min_chunk_size);
typedef int (*Longtail_Chunker_CreateChunkerFunc)(struct Longtail_ChunkerAPI* chunker_api, uint32_t min_chunk_size, uint32_t avg_chunk_size, uint32_t max_chunk_size, Longtail_ChunkerAPI_HChunker* out_chunker);
typedef int (*Longtail_Chunker_NextChunkFunc)(struct Longtail_ChunkerAPI* chunker_api, Longtail_ChunkerAPI_HChunker chunker, Longtail_Chunker_Feeder feeder, void* feeder_context, struct Longtail_Chunker_ChunkRange* out_chunk_range);
typedef int (*Longtail_Chunker_DisposeChunkerFunc)(struct Longtail_ChunkerAPI* chunker_api, Longtail_ChunkerAPI_HChunker chunker);

struct Longtail_ChunkerAPI
{
    struct Longtail_API m_API;
    Longtail_Chunker_GetMinChunkSizeFunc GetMinChunkSize;
    Longtail_Chunker_CreateChunkerFunc CreateChunker;
    Longtail_Chunker_NextChunkFunc NextChunk;
    Longtail_Chunker_DisposeChunkerFunc DisposeChunker;
};

LONGTAIL_EXPORT uint64_t Longtail_GetChunkerAPISize();

LONGTAIL_EXPORT struct Longtail_ChunkerAPI* Longtail_MakeChunkerAPI(
    void* mem,
    Longtail_DisposeFunc dispose_func,
    Longtail_Chunker_GetMinChunkSizeFunc get_min_chunk_size_func,
    Longtail_Chunker_CreateChunkerFunc create_chunker_func,
    Longtail_Chunker_NextChunkFunc next_chunk_func,
    Longtail_Chunker_DisposeChunkerFunc dispose_chunker_func);

LONGTAIL_EXPORT int Longtail_Chunker_GetMinChunkSize(struct Longtail_ChunkerAPI* chunker_api, uint32_t* out_min_chunk_size);
LONGTAIL_EXPORT int Longtail_Chunker_CreateChunker(struct Longtail_ChunkerAPI* chunker_api, uint32_t min_chunk_size, uint32_t avg_chunk_size, uint32_t max_chunk_size, Longtail_ChunkerAPI_HChunker* out_chunker);
LONGTAIL_EXPORT int Longtail_Chunker_NextChunk(struct Longtail_ChunkerAPI* chunker_api, Longtail_ChunkerAPI_HChunker chunker, Longtail_Chunker_Feeder feeder, void* feeder_context, struct Longtail_Chunker_ChunkRange* out_chunk_range);
LONGTAIL_EXPORT int Longtail_Chunker_DisposeChunker(struct Longtail_ChunkerAPI* chunker_api, Longtail_ChunkerAPI_HChunker chunker);

////////////// Longtail_AsyncPutStoredBlockAPI

struct Longtail_AsyncPutStoredBlockAPI;

typedef void (*Longtail_AsyncPutStoredBlock_OnCompleteFunc)(struct Longtail_AsyncPutStoredBlockAPI* async_complete_api, int err);

struct Longtail_AsyncPutStoredBlockAPI
{
    struct Longtail_API m_API;
    Longtail_AsyncPutStoredBlock_OnCompleteFunc OnComplete;
};

LONGTAIL_EXPORT uint64_t Longtail_GetAsyncPutStoredBlockAPISize();

LONGTAIL_EXPORT struct Longtail_AsyncPutStoredBlockAPI* Longtail_MakeAsyncPutStoredBlockAPI(
    void* mem,
    Longtail_DisposeFunc dispose_func,
    Longtail_AsyncPutStoredBlock_OnCompleteFunc on_complete_func);

LONGTAIL_EXPORT void Longtail_AsyncPutStoredBlock_OnComplete(struct Longtail_AsyncPutStoredBlockAPI* async_complete_api, int err);

////////////// Longtail_AsyncGetStoredBlockAPI

struct Longtail_AsyncGetStoredBlockAPI;

typedef void (*Longtail_AsyncGetStoredBlock_OnCompleteFunc)(struct Longtail_AsyncGetStoredBlockAPI* async_complete_api, struct Longtail_StoredBlock* stored_block, int err);

struct Longtail_AsyncGetStoredBlockAPI
{
    struct Longtail_API m_API;
    Longtail_AsyncGetStoredBlock_OnCompleteFunc OnComplete;
};

LONGTAIL_EXPORT uint64_t Longtail_GetAsyncGetStoredBlockAPISize();

LONGTAIL_EXPORT struct Longtail_AsyncGetStoredBlockAPI* Longtail_MakeAsyncGetStoredBlockAPI(
    void* mem,
    Longtail_DisposeFunc dispose_func,
    Longtail_AsyncGetStoredBlock_OnCompleteFunc on_complete_func);

LONGTAIL_EXPORT void Longtail_AsyncGetStoredBlock_OnComplete(struct Longtail_AsyncGetStoredBlockAPI* async_complete_api, struct Longtail_StoredBlock* stored_block, int err);

////////////// Longtail_AsyncRetargetContentAPI

struct Longtail_AsyncRetargetContentAPI;

typedef void (*Longtail_AsyncRetargetContent_OnCompleteFunc)(struct Longtail_AsyncRetargetContentAPI* async_complete_api, struct Longtail_ContentIndex* content_index, int err);

struct Longtail_AsyncRetargetContentAPI
{
    struct Longtail_API m_API;
    Longtail_AsyncRetargetContent_OnCompleteFunc OnComplete;
};

LONGTAIL_EXPORT uint64_t Longtail_GetAsyncRetargetContentAPISize();

LONGTAIL_EXPORT struct Longtail_AsyncRetargetContentAPI* Longtail_MakeAsyncRetargetContentAPI(
    void* mem,
    Longtail_DisposeFunc dispose_func,
    Longtail_AsyncRetargetContent_OnCompleteFunc on_complete_func);

LONGTAIL_EXPORT void Longtail_AsyncRetargetContent_OnComplete(struct Longtail_AsyncRetargetContentAPI* async_complete_api, struct Longtail_ContentIndex* content_index, int err);

////////////// Longtail_AsyncFlushAPI

struct Longtail_AsyncFlushAPI;

typedef void (*Longtail_AsyncFlush_OnCompleteFunc)(struct Longtail_AsyncFlushAPI* async_complete_api, int err);

struct Longtail_AsyncFlushAPI
{
    struct Longtail_API m_API;
    Longtail_AsyncFlush_OnCompleteFunc OnComplete;
};

LONGTAIL_EXPORT uint64_t Longtail_GetAsyncFlushAPISize();

LONGTAIL_EXPORT struct Longtail_AsyncFlushAPI* Longtail_MakeAsyncFlushAPI(
    void* mem,
    Longtail_DisposeFunc dispose_func,
    Longtail_AsyncFlush_OnCompleteFunc on_complete_func);

LONGTAIL_EXPORT void Longtail_AsyncFlush_OnComplete(struct Longtail_AsyncFlushAPI* async_complete_api, int err);

////////////// Longtail_BlockStoreAPI

struct Longtail_BlockStoreAPI;

enum
{
    Longtail_BlockStoreAPI_StatU64_GetStoredBlock_Count,
    Longtail_BlockStoreAPI_StatU64_GetStoredBlock_RetryCount,
    Longtail_BlockStoreAPI_StatU64_GetStoredBlock_FailCount,
    Longtail_BlockStoreAPI_StatU64_GetStoredBlock_Chunk_Count,
    Longtail_BlockStoreAPI_StatU64_GetStoredBlock_Byte_Count,

    Longtail_BlockStoreAPI_StatU64_PutStoredBlock_Count,
    Longtail_BlockStoreAPI_StatU64_PutStoredBlock_RetryCount,
    Longtail_BlockStoreAPI_StatU64_PutStoredBlock_FailCount,
    Longtail_BlockStoreAPI_StatU64_PutStoredBlock_Chunk_Count,
    Longtail_BlockStoreAPI_StatU64_PutStoredBlock_Byte_Count,

    Longtail_BlockStoreAPI_StatU64_RetargetContent_Count,
    Longtail_BlockStoreAPI_StatU64_RetargetContent_RetryCount,
    Longtail_BlockStoreAPI_StatU64_RetargetContent_FailCount,

    Longtail_BlockStoreAPI_StatU64_PreflightGet_Count,
    Longtail_BlockStoreAPI_StatU64_PreflightGet_RetryCount,
    Longtail_BlockStoreAPI_StatU64_PreflightGet_FailCount,

    Longtail_BlockStoreAPI_StatU64_Flush_Count,
    Longtail_BlockStoreAPI_StatU64_Flush_FailCount,

    Longtail_BlockStoreAPI_StatU64_GetStats_Count,
        Longtail_BlockStoreAPI_StatU64_Count
};

struct Longtail_BlockStore_Stats
{
    uint64_t m_StatU64[Longtail_BlockStoreAPI_StatU64_Count];
};

typedef int (*Longtail_BlockStore_PutStoredBlockFunc)(struct Longtail_BlockStoreAPI* block_store_api, struct Longtail_StoredBlock* stored_block, struct Longtail_AsyncPutStoredBlockAPI* async_complete_api);
typedef int (*Longtail_BlockStore_PreflightGetFunc)(struct Longtail_BlockStoreAPI* block_store_api, const struct Longtail_ContentIndex* content_index);
typedef int (*Longtail_BlockStore_GetStoredBlockFunc)(struct Longtail_BlockStoreAPI* block_store_api, uint64_t block_hash, struct Longtail_AsyncGetStoredBlockAPI* async_complete_api);
typedef int (*Longtail_BlockStore_RetargetContentFunc)(struct Longtail_BlockStoreAPI* block_store_api, const struct Longtail_ContentIndex* content_index, struct Longtail_AsyncRetargetContentAPI* async_complete_api);
typedef int (*Longtail_BlockStore_GetStatsFunc)(struct Longtail_BlockStoreAPI* block_store_api, struct Longtail_BlockStore_Stats* out_stats);
typedef int (*Longtail_BlockStore_FlushFunc)(struct Longtail_BlockStoreAPI* block_store_api, struct Longtail_AsyncFlushAPI* async_complete_api);

struct Longtail_BlockStoreAPI
{
    struct Longtail_API m_API;
    Longtail_BlockStore_PutStoredBlockFunc PutStoredBlock;
    Longtail_BlockStore_PreflightGetFunc PreflightGet;
    Longtail_BlockStore_GetStoredBlockFunc GetStoredBlock;
    Longtail_BlockStore_RetargetContentFunc RetargetContent;
    Longtail_BlockStore_GetStatsFunc GetStats;
    Longtail_BlockStore_FlushFunc Flush;
};


LONGTAIL_EXPORT uint64_t Longtail_GetBlockStoreAPISize();

LONGTAIL_EXPORT struct Longtail_BlockStoreAPI* Longtail_MakeBlockStoreAPI(
    void* mem,
    Longtail_DisposeFunc dispose_func,
    Longtail_BlockStore_PutStoredBlockFunc put_stored_block_func,
    Longtail_BlockStore_PreflightGetFunc preflight_get_func,
    Longtail_BlockStore_GetStoredBlockFunc get_stored_block_func,
    Longtail_BlockStore_RetargetContentFunc retarget_content_func,
    Longtail_BlockStore_GetStatsFunc get_stats_func,
    Longtail_BlockStore_FlushFunc flush_func);

LONGTAIL_EXPORT int Longtail_BlockStore_PutStoredBlock(struct Longtail_BlockStoreAPI* block_store_api, struct Longtail_StoredBlock* stored_block, struct Longtail_AsyncPutStoredBlockAPI* async_complete_api);
LONGTAIL_EXPORT int Longtail_BlockStore_PreflightGet(struct Longtail_BlockStoreAPI* block_store_api, const struct Longtail_ContentIndex* content_index);
LONGTAIL_EXPORT int Longtail_BlockStore_GetStoredBlock(struct Longtail_BlockStoreAPI* block_store_api, uint64_t block_hash, struct Longtail_AsyncGetStoredBlockAPI* async_complete_api);
LONGTAIL_EXPORT int Longtail_BlockStore_RetargetContent(struct Longtail_BlockStoreAPI* block_store_api, const struct Longtail_ContentIndex* content_index, struct Longtail_AsyncRetargetContentAPI* async_complete_api);
LONGTAIL_EXPORT int Longtail_BlockStore_GetStats(struct Longtail_BlockStoreAPI* block_store_api, struct Longtail_BlockStore_Stats* out_stats);
LONGTAIL_EXPORT int Longtail_BlockStore_Flush(struct Longtail_BlockStoreAPI* block_store_api, struct Longtail_AsyncFlushAPI* async_complete_api);

typedef void (*Longtail_Assert)(const char* expression, const char* file, int line);
LONGTAIL_EXPORT void Longtail_SetAssert(Longtail_Assert assert_func);
typedef void (*Longtail_Log)(void* context, int level, const char* str);
LONGTAIL_EXPORT void Longtail_SetLog(Longtail_Log log_func, void* context);
LONGTAIL_EXPORT void Longtail_SetLogLevel(int level);

#define LONGTAIL_LOG_LEVEL_DEBUG    0
#define LONGTAIL_LOG_LEVEL_INFO     1
#define LONGTAIL_LOG_LEVEL_WARNING  2
#define LONGTAIL_LOG_LEVEL_ERROR    3
#define LONGTAIL_LOG_LEVEL_OFF      4

#ifndef LONGTAIL_LOG
    void Longtail_CallLogger(int level, const char* fmt, ...);
    #define LONGTAIL_LOG(level, fmt, ...) \
        Longtail_CallLogger(level, fmt, __VA_ARGS__);
#endif

#if defined(LONGTAIL_ASSERTS)
    extern Longtail_Assert Longtail_Assert_private;
#    define LONGTAIL_FATAL_ASSERT(x, bail) \
        if (!(x)) \
        { \
            LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "%s(%d): Assert failed in %s(): failed on condition: `%s`\n", __FILE__, __LINE__, __FUNCTION__, #x); \
            if (Longtail_Assert_private) \
            { \
                Longtail_Assert_private(#x, __FILE__, __LINE__); \
            } \
            bail; \
        }
#   define LONGTAIL_VALIDATE_INPUT(x, bail) \
    if (!(x)) \
    { \
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "%s(%d): Input validation for `%s()`: failed on condition `%s`\n", __FILE__, __LINE__, __FUNCTION__, #x); \
        if (Longtail_Assert_private) \
        { \
            Longtail_Assert_private(#x, __FILE__, __LINE__); \
        } \
        bail; \
    }
#else // defined(LONGTAIL_ASSERTS)
#   define LONGTAIL_FATAL_ASSERT(x, y)
#   define LONGTAIL_VALIDATE_INPUT(x, bail) \
    if (!(x)) \
    { \
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "%s(%d): Input validation for `%s()`: failed on condition `%s`\n", __FILE__, __LINE__, __FUNCTION__, #x); \
        bail; \
    }
#endif // defined(LONGTAIL_ASSERTS)


typedef void* (*Longtail_Alloc_Func)(size_t s);
typedef void (*Longtail_Free_Func)(void* p);
LONGTAIL_EXPORT void Longtail_SetAllocAndFree(Longtail_Alloc_Func alloc, Longtail_Free_Func free);

LONGTAIL_EXPORT void* Longtail_Alloc(size_t s);
LONGTAIL_EXPORT void Longtail_Free(void* p);

/*! @brief Ensures the full parent path exists.
 *
 * Creates any parent directories for @p path if they do not exist.
 *
 * @param[in] storage_api           An implementation of struct Longtail_StorageAPI interface.
 * @param[in] path                  A normalized path
 * @return                          Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int EnsureParentPathExists(struct Longtail_StorageAPI* storage_api, const char* path);

/*! @brief Duplicates a string.
 *
 * Creates a copy of a string using the Longtail_Alloc() function.
 *
 * @param[in] str           String to duplicate
 * @return                  Pointer to newly allocated string, zero if out of memory or invalid input parameter
 */
LONGTAIL_EXPORT char* Longtail_Strdup(const char* str);

/*! @brief Get all files and directories in a path recursivley.
 *
 * Gets all the files and directories and allocates a struct Longtail_FileInfos structure with the details.
 * Free the struct Longtail_FileInfos using Longtail_Free()
 *
 * @param[in] storage_api           An implementation of struct Longtail_StorageAPI interface.
 * @param[in] path_filter_api       An implementation of struct Longtail_PathFilter interface or null if no filtering is required
 * @param[in] optional_cancel_api   An implementation of struct Longtail_CancelAPI interface or null if no cancelling is required
 * @param[in] optional_cancel_token A cancel token or null if @p optional_cancel_api is null
 * @param[in] root_path             Root path to search for files and directories - may not be null
 * @param[out] out_file_infos       Pointer to a struct Longtail_FileInfos* pointer which will be set on success
 * @return                          Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_GetFilesRecursively(
    struct Longtail_StorageAPI* storage_api,
    struct Longtail_PathFilterAPI* path_filter_api,
    struct Longtail_CancelAPI* optional_cancel_api,
    Longtail_CancelAPI_HCancelToken optional_cancel_token,
    const char* root_path,
    struct Longtail_FileInfos** out_file_infos);

/*! @brief Create a version index for a struct Longtail_FileInfos.
 *
 * All files are chunked and hashes to create a struct VersionIndex, allocated using Longtail_Alloc()
 * Free the version index with Longtail_Free()
 *
 * @param[in] storage_api           An implementation of struct Longtail_StorageAPI interface.
 * @param[in] hash_api              An implementation of struct Longtail_HashAPI interface.
 * @param[in] chunker_api           An implementation of struct Longtail_ChunkerAPI interface.
 * @param[in] job_api               An implementation of struct Longtail_JobAPI interface
 * @param[in] progress_api          An implementation of struct Longtail_JobAPI interface or null if no progress indication is required
 * @param[in] optional_cancel_api   An implementation of struct Longtail_CancelAPI interface or null if no cancelling is required
 * @param[in] optional_cancel_token A cancel token or null if @p optional_cancel_api is null
 * @param[in] root_path             Root path for files in @p file_infos
 * @param[in] optional_asset_tags   An array with a tag for each entry in @p file_infos, usually a compression tag, set to zero if no tags are wanted
 * @param[in] target_chunk_size     The target size of chunks, with minimum size set to @target_chunk_size / 8 and maximum size set to @p target_chunk_size * 2
 * @param[out] out_version_index    Pointer to a struct Longtail_VersionIndex* pointer which will be set on success
 * @return                          Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_CreateVersionIndex(
    struct Longtail_StorageAPI* storage_api,
    struct Longtail_HashAPI* hash_api,
    struct Longtail_ChunkerAPI* chunker_api,
    struct Longtail_JobAPI* job_api,
    struct Longtail_ProgressAPI* progress_api,
    struct Longtail_CancelAPI* optional_cancel_api,
    Longtail_CancelAPI_HCancelToken optional_cancel_token,
    const char* root_path,
    const struct Longtail_FileInfos* file_infos,
    const uint32_t* optional_asset_tags,
    uint32_t target_chunk_size,
    struct Longtail_VersionIndex** out_version_index);

/*! @brief Writes a struct Longtail_VersionIndex to a byte buffer.
 *
 * Serializes a struct Longtail_VersionIndex to a buffer which is allocated using Longtail_Alloc()
 *
 * @param[in] version_index         Pointer to an initialized struct Longtail_VersionIndex
 * @param[out] out_buffer           Pointer to a buffer pointer intitialized on success
 * @param[out] out_size             Pointer to a size variable intitialized on success
 * @return                          Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_WriteVersionIndexToBuffer(
    const struct Longtail_VersionIndex* version_index,
    void** out_buffer,
    size_t* out_size);

/*! @brief Reads a struct Longtail_VersionIndex from a byte buffer.
 *
 * Deserializes a struct Longtail_VersionIndex from a buffer, the struct Longtail_VersionIndex is allocated using Longtail_Alloc()
 *
 * @param[in] buffer                Buffer containing the serialized struct Longtail_VersionIndex
 * @param[in] size                  Size of the buffer
 * @param[out] out_version_index    Pointer to an struct Longtail_VersionIndex pointer
 * @return                          Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_ReadVersionIndexFromBuffer(
    const void* buffer,
    size_t size,
    struct Longtail_VersionIndex** out_version_index);

/*! @brief Writes a struct Longtail_VersionIndex.
 *
 * Serializes a struct Longtail_VersionIndex to a file in a struct Longtail_StorageAPI at the specified path.
 * The parent folder of the file path must exist.
 *
 * @param[in] storage_api           An initialized struct Longtail_StorageAPI
 * @param[in] version_index         Pointer to an initialized struct Longtail_VersionIndex
 * @param[in] path                  A path in the storage api to store the version index to
 * @return                          Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_WriteVersionIndex(
    struct Longtail_StorageAPI* storage_api,
    struct Longtail_VersionIndex* version_index,
    const char* path);

/*! @brief Reads a struct Longtail_VersionIndex.
 *
 * Deserializes a struct Longtail_VersionIndex from a file in a struct Longtail_StorageAPI at the specified path.
 * The file must exist.
 *
 * @param[in] storage_api           An initialized struct Longtail_StorageAPI
 * @param[in] path                  A path in the storage api to read the version index from
 * @param[out] out_version_index    Pointer to an struct Longtail_VersionIndex pointer
 * @return                          Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_ReadVersionIndex(
    struct Longtail_StorageAPI* storage_api,
    const char* path,
    struct Longtail_VersionIndex** out_version_index);

/*! @brief Get size of content index data.
 *
 * Content index data size is the size raw size of the content index excluding the struct Longtail_ContentIndex
 * header structure.
 *
 * @param[in] block_count           Number of blocks
 * @param[in] chunk_count           Number of chunks
 * @return                          Number of bytes required to store the content index data
 */
LONGTAIL_EXPORT size_t Longtail_GetContentIndexDataSize(
    uint64_t block_count,
    uint64_t chunk_count);

/*! @brief Get size of content index.
 *
 * Content index data size is the size size of the content index including the struct Longtail_ContentIndex
 * header structure.
 *
 * @param[in] block_count           Number of blocks
 * @param[in] chunk_count           Number of chunks
 * @return                          Number of bytes required to store the content index data
 */
LONGTAIL_EXPORT size_t Longtail_GetContentIndexSize(
    uint64_t block_count,
    uint64_t chunk_count);

/*! @brief Initialize content index from raw data.
 *
 * Initialize a struct Longtail_ContentIndex from raw data.
 *
 * @param[in] content_index Pointer to an uninitialized struct Longtail_VersionIndex
 * @param[in] data          Pointer to a raw content index data
 * @param[in] data_size     Size of raw content index data
 * @return                  Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_InitContentIndexFromData(
    struct Longtail_ContentIndex* content_index,
    void* data,
    uint64_t data_size);

/*! @brief Initialize content index.
 *
 * Initialize a struct Longtail_ContentIndex.
 *
 * @param[in] content_index         Pointer to an uninitialized struct Longtail_VersionIndex
 * @param[in] data                  Pointer to a uninitialized contend index data
 * @param[in] data_size             Size of uninitialized contend index data
 * @param[in] hash_api              Hash API identifier
 * @param[in] max_block_size        Max block size
 * @param[in] max_chunks_per_block  Max chunks per block
 * @param[in] block_count           Block count
 * @param[in] chunk_count           Chunk count
 * @return                          Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_InitContentIndex(
    struct Longtail_ContentIndex* content_index,
    void* data,
    uint64_t data_size,
    uint32_t hash_api,
    uint32_t max_block_size,
    uint32_t max_chunks_per_block,
    uint64_t block_count,
    uint64_t chunk_count);

/*! @brief Create a struct Longtail_ContentIndex from array of struct Longtail_BlockIndex.
 *
 * Allocates and initializes a struct Longtail_ContentIndex structure from an array of
 * struct Longtail_BlockIndex using Longtail_Alloc()
 * The resulting struct Longtail_ContentIndex is freed with Longtail_Free()
 *
 * @param[in] max_block_size        Max block size
 * @param[in] max_chunks_per_block  Max chunks per block
 * @param[in] block_count           Block count - number of struct Longtail_BlockIndex pointers in @p block_indexes
 * @param[in] block_indexes         Array of pointers to initialized struct Longtail_BlockIndex
 * @param[out] out_content_index    Pointer to an struct Longtail_ContentIndex pointer
 * @return                          Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_CreateContentIndexFromBlocks(
    uint32_t max_block_size,
    uint32_t max_chunks_per_block,
    uint64_t block_count,
    struct Longtail_BlockIndex** block_indexes,
    struct Longtail_ContentIndex** out_content_index);

/*! @brief Create a struct Longtail_ContentIndex from an struct Longtail_VersionIndex.
 *
 * Creates a struct Longtail_ContentIndex from a struct Longtail_VersionIndex by bundling
 * chunks into blocks according to @p max_block_size and @p max_chunks_per_block.
 *
 * @param[in] hash_api              Hash API identifier
 * @param[in] version_index         Pointer to an initialized struct Longtail_VersionIndex
 * @param[in] max_block_size        Max block size
 * @param[in] max_chunks_per_block  Max chunks per block
 * @param[out] out_content_index    Pointer to an struct Longtail_ContentIndex pointer
 * @return                          Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_CreateContentIndex(
    struct Longtail_HashAPI* hash_api,
    struct Longtail_VersionIndex* version_index,
    uint32_t max_block_size,
    uint32_t max_chunks_per_block,
    struct Longtail_ContentIndex** out_content_index);

/*! @brief Create a struct Longtail_ContentIndex from an struct Longtail_VersionIndex.
 *
 * Creates a struct Longtail_ContentIndex from a struct Longtail_VersionIndex by bundling
 * chunks into blocks according to @p max_block_size and @p max_chunks_per_block.
 * It only includes data that is listed in the version_diff's modified and target added assets.
 *
 * @param[in] hash_api              Hash API identifier
 * @param[in] version_index         Pointer to an initialized struct Longtail_VersionIndex
 * @param[in] version_diff          Pointer to an initialized struct Longtail_VersionDiff
 * @param[in] max_block_size        Max block size
 * @param[in] max_chunks_per_block  Max chunks per block
 * @param[out] out_content_index    Pointer to an struct Longtail_ContentIndex pointer
 * @return                          Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_CreateContentIndexFromDiff(
    struct Longtail_HashAPI* hash_api,
    struct Longtail_VersionIndex* version_index,
    struct Longtail_VersionDiff* version_diff,
    uint32_t max_block_size,
    uint32_t max_chunks_per_block,
    struct Longtail_ContentIndex** out_content_index);

/*! @brief Create a struct Longtail_ContentIndex from discreet data.
 *
 * Creates a struct Longtail_ContentIndex from discreet data by bundling
 * chunks into blocks according to @p max_block_size and @p max_chunks_per_block.
 *
 * @param[in] hash_api              Hash API identifier
 * @param[in] chunk_count           Number of chunks
 * @param[in] chunk_hashes          Array of chunk hashes - entry count must match @p chunk_count
 * @param[in] chunk_sizes           Array of chunk sizes - entry count must match @p chunk_count
 * @param[in] optional_chunk_tags   Array of chunk tags - entry count must match @p chunk_count or be set to 0 for no tagging
 * @param[in] max_block_size        Max block size
 * @param[in] max_chunks_per_block  Max chunks per block
 * @param[out] out_content_index    Pointer to an struct Longtail_ContentIndex pointer
 * @return                          Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_CreateContentIndexRaw(
    struct Longtail_HashAPI* hash_api,
    uint64_t chunk_count,
    const TLongtail_Hash* chunk_hashes,
    const uint32_t* chunk_sizes,
    const uint32_t* optional_chunk_tags,
    uint32_t max_block_size,
    uint32_t max_chunks_per_block,
    struct Longtail_ContentIndex** out_content_index);

/*! @brief Writes a struct Longtail_ContentIndex to a byte buffer.
 *
 * Serializes a struct Longtail_ContentIndex to a buffer which is allocated using Longtail_Alloc()
 *
 * @param[in] content_index         Pointer to an initialized struct Longtail_ContentIndex
 * @param[out] out_buffer           Pointer to a buffer pointer intitialized on success
 * @param[out] out_size             Pointer to a size variable intitialized on success
 * @return                          Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_WriteContentIndexToBuffer(
    const struct Longtail_ContentIndex* content_index,
    void** out_buffer,
    size_t* out_size);

/*! @brief Reads a struct Longtail_ContentIndex from a byte buffer.
 *
 * Deserializes a struct Longtail_ContentIndex from a buffer, the struct Longtail_ContentIndex is allocated using Longtail_Alloc()
 *
 * @param[in] buffer                Buffer containing the serialized struct Longtail_ContentIndex
 * @param[in] size                  Size of the buffer
 * @param[out] out_content_index    Pointer to an struct Longtail_ContentIndex pointer
 * @return                          Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_ReadContentIndexFromBuffer(
    const void* buffer,
    size_t size,
    struct Longtail_ContentIndex** out_content_index);

/*! @brief Writes a struct Longtail_ContentIndex.
 *
 * Serializes a struct Longtail_ContentIndex to a file in a struct Longtail_StorageAPI at the specified path.
 * The parent folder of the file path must exist.
 *
 * @param[in] storage_api   An initialized struct Longtail_StorageAPI
 * @param[in] content_index Pointer to an initialized struct Longtail_ContentIndex
 * @param[in] path          A path in the storage api to store the content index to
 * @return                  Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_WriteContentIndex(
    struct Longtail_StorageAPI* storage_api,
    struct Longtail_ContentIndex* content_index,
    const char* path);

/*! @brief Reads a struct Longtail_ContentIndex.
 *
 * Deserializes a struct Longtail_ContentIndex from a file in a struct Longtail_StorageAPI at the specified path.
 * The file must exist.
 *
 * @param[in] storage_api           An initialized struct Longtail_StorageAPI
 * @param[in] path                  A path in the storage api to read the Content index from
 * @param[out] out_content_index    Pointer to an struct Longtail_ContentIndex pointer
 * @return                          Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_ReadContentIndex(
    struct Longtail_StorageAPI* storage_api,
    const char* path,
    struct Longtail_ContentIndex** out_content_index);

/*! @brief Write content blocks from version data
 *
 * Writes all blocks for @p version_content_index using @p version_index and asset_path as data source to a block store
 *
 * @param[in] source_storage_api        An initialized struct Longtail_StorageAPI
 * @param[in] block_store_api           An initialized struct Longtail_BlockStoreAPI
 * @param[in] hash_api                  An implementation of struct Longtail_HashAPI interface. This must match the hashing api used to create both content index index and version index
 * @param[in] job_api                   An initialized struct Longtail_JobAPI
 * @param[in] progress_api              An initialized struct Longtail_ProgressAPI, or 0 for no progress reporting
 * @param[in] optional_cancel_api       An implementation of struct Longtail_CancelAPI interface or null if no cancelling is required
 * @param[in] optional_cancel_token     A cancel token or null if @p optional_cancel_api is null
 * @param[in] content_index             The content index to write, all blocks in content index will be written
 * @param[in] version_index             Version index of data in  @p assets_folder
 * @param[in] assets_folder             Path of version data inside @p source_storage_api
 * @return                  Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_WriteContent(
    struct Longtail_StorageAPI* source_storage_api,
    struct Longtail_BlockStoreAPI* block_store_api,
    struct Longtail_JobAPI* job_api,
    struct Longtail_ProgressAPI* progress_api,
    struct Longtail_CancelAPI* optional_cancel_api,
    Longtail_CancelAPI_HCancelToken optional_cancel_token,
    struct Longtail_ContentIndex* content_index,
    struct Longtail_VersionIndex* version_index,
    const char* assets_folder);

/*! @brief Generate a content index with what is missing.
 *
 * Any content in @p version_index that is not present in @p content_index will be included in @p out_content_index
 * Chunks that are not present in @p content_index will be bundled up in blocks according to @p max_block_size and @p max_chunks_per_block.
 *
 * @param[in] hash_api              An implementation of struct Longtail_HashAPI interface. This must match the hashing api used to create both content index index and version index
 * @param[in] content_index         The known content to check against
 * @param[in] version_index         The version index content you test against @p reference_content_index
 * @param[in] max_block_size        The maximum size if bytes one block is allowed to be
 * @param[in] max_chunks_per_block  The maximum number of chunks allowed inside one block
 * @param[out] out_content_index    The resulting missing content index will be created and assigned to this pointer reference if successful
 * @return                          Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_CreateMissingContent(
    struct Longtail_HashAPI* hash_api,
    const struct Longtail_ContentIndex* content_index,
    const struct Longtail_VersionIndex* version_index,
    uint32_t max_block_size,
    uint32_t max_chunks_per_block,
    struct Longtail_ContentIndex** out_content_index);


/*! @brief Generate a content index with what is missing.
 *
 * Any content in @p content_index that is not present in @p reference_content_index will be included in @p out_content_index
 * The content verification will only add blocks from @p content_index that has chunks it can not
 * find in @p reference_content_index.
 *
 * The missing blocks are added in complete form so you might get redundant content in @p out_content_index.
 *
 * @param[in] hash_identifier           The hash identifier to use, if any of the content_indexes has any blocks their hash identifier must match @p hash_identifier
 * @param[in] reference_content_index   The known content to check against
 * @param[in] content_index             The content you want to test against @p reference_content_index
 * @param[out] out_content_index        The resulting missing content index will be created and assigned to this pointer reference if successful
 * @return                              Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_GetMissingContent(
    uint32_t hash_identifier,
    const struct Longtail_ContentIndex* reference_content_index,
    const struct Longtail_ContentIndex* content_index,
    struct Longtail_ContentIndex** out_content_index);

/*! @brief Retarget a content index to match an existing content index.
 *
 * Create a new struct Longtail_ContentIndex keeping only the blocks in @p reference_content_index
 * that contains chunks found in @content_index.
 *
 * The result is the chunks known in @p reference_content_index arrangend in blocks known to @p reference_content_index.
 * Use Longtail_CreateMissingContent() to create blocks of any chunks not found in @p reference_content_index
 *
 * @param[in] reference_content_index   The known content to check against
 * @param[in] requested_content_index   The content you want to test against @p reference_content_index
 * @param[out] out_content_index        The blocks/chunks of requested_content_index found in reference_content_index
 * @return                              Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_RetargetContent(
    const struct Longtail_ContentIndex* reference_content_index,
    const struct Longtail_ContentIndex* requested_content_index,
    struct Longtail_ContentIndex** out_content_index);

/*! @brief Merge two content indexes.
 *
 * Create a join of two content indexes, @p local_content_index has precedence, if a chunk is present in @p local_content_index the
 * block containing it is added to @p out_content_index over a block from @p new_content_index.
 *
 * @param[in] job_api                   An initialized struct Longtail_JobAPI
 * @param[in] local_content_index       The known content used a base
 * @param[in] new_content_index         The content you want add - only blocks containing chunks not in @p local_content_index will be added
 * @param[out] out_content_index        The resulting content index will be created and assigned to this pointer reference if successful
 * @return                              Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_MergeContentIndex(
    struct Longtail_JobAPI* job_api,
    struct Longtail_ContentIndex* local_content_index,
    struct Longtail_ContentIndex* new_content_index,
    struct Longtail_ContentIndex** out_content_index);

/*! @brief Add two content indexes.
 *
 * Create a join of two content indexes, @p local_content_index is added first and @p new_content_index is added to that
 * without any checking of duplicate chunks or blocks.
 *
 * @param[in] local_content_index       The known content used a base
 * @param[in] new_content_index         The content you want add - add chunks and blocks from @p new_content_index will be added
 * @param[out] out_content_index        The resulting content index will be created and assigned to this pointer reference if successful
 * @return                              Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_AddContentIndex(
    struct Longtail_ContentIndex* local_content_index,
    struct Longtail_ContentIndex* new_content_index,
    struct Longtail_ContentIndex** out_content_index);

/*! @brief Unpack and write a version.
 *
 * Writes out a full version to @p version_storage_api at path @p version_path.
 * Usually done to an empty folder since it will not remove any assets that are not in @p version_index.
 * Uses @p content_index to know where chunks are located in blocks - this can either be the full
 * content index of @p block_storage_api or a content index that is slimmed down using Longtail_BlockStore_RetargetContent.
 * Blocks are fetched from @p block_storage_api on demand.
 *
 * @param[in] block_storage_api     An implementation of struct Longtail_BlockStoreAPI interface
 * @param[in] version_storage_api   An implementation of struct Longtail_StorageAPI interface
 * @param[in] job_api               An implementation of struct Longtail_JobAPI interface
 * @param[in] progress_api          An initialized struct Longtail_ProgressAPI, or 0 for no progress reporting
 * @param[in] optional_cancel_api   An implementation of struct Longtail_CancelAPI interface or null if no cancelling is required
 * @param[in] optional_cancel_token A cancel token or null if @p optional_cancel_api is null
 * @param[in] content_index         The content index for @p block_store_api
 * @param[in] version_index         The version index for the version to write
 * @param[in] version_path          The path in @p version_storage_api to write the version to
 * @param[in] retain_permissions    Flag for setting permissions - 0 = don't set permissions, 1 = set permissions
 * @return                          Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_WriteVersion(
    struct Longtail_BlockStoreAPI* block_storage_api,
    struct Longtail_StorageAPI* version_storage_api,
    struct Longtail_JobAPI* job_api,
    struct Longtail_ProgressAPI* progress_api,
    struct Longtail_CancelAPI* optional_cancel_api,
    Longtail_CancelAPI_HCancelToken optional_cancel_token,
    const struct Longtail_ContentIndex* content_index,
    const struct Longtail_VersionIndex* version_index,
    const char* version_path,
    int retain_permissions);

/*! @brief Get the difference between to struct Longtail_VersionIndex.
 *
 * Returns a struct Longtail_VersionDiff with the additions, modifications and deletions required to change
 * a version from @p source_version to @p target_version.
 *
 * @param[in] hash_api             An implementation of struct Longtail_HashAPI interface
 * @param[in] source_version       The version index we have
 * @param[in] target_version       The version index we want
 * @param[out] out_version_diff    The resulting diff between @p source_version and @p target_version
 * @return                         Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_CreateVersionDiff(
    struct Longtail_HashAPI* hash_api,
    const struct Longtail_VersionIndex* source_version,
    const struct Longtail_VersionIndex* target_version,
    struct Longtail_VersionDiff** out_version_diff);

/*! @brief Unpack and modify a version.
 *
 * Applies the changes from @p version_diff to change a version from @p source_version to @p target_version.
 * Uses @p content_index to know where chunks are located in blocks - this can either be the full
 * content index of @p block_storage_api or a content index that is slimmed down using Longtail_BlockStore_RetargetContent.
 * Blocks are fetched from @p block_storage_api on demand.
 *
 * @param[in] block_storage_api     An implementation of struct Longtail_BlockStoreAPI interface
 * @param[in] version_storage_api   An implementation of struct Longtail_StorageAPI interface
 * @param[in] hash_api              An implementation of struct Longtail_HashAPI interface
 * @param[in] job_api               An implementation of struct Longtail_JobAPI interface
 * @param[in] progress_api          An initialized struct Longtail_ProgressAPI, or 0 for no progress reporting
 * @param[in] optional_cancel_api   An implementation of struct Longtail_CancelAPI interface or null if no cancelling is required
 * @param[in] optional_cancel_token A cancel token or null if @p optional_cancel_api is null
 * @param[in] content_index         @p target_version retargetted to @p block_storage_api (see Longtail_BlockStoreAPI::RetargetContent)
 * @param[in] source_version        The version index for the current version
 * @param[in] target_version        The version index for the target version
 * @param[in] version_diff          The version diff between @p source_version and @p target_version
 * @param[in] version_path          The path in @p version_storage_api to update
 * @param[in] retain_permissions    Flag for setting permissions - 0 = don't set permissions, 1 = set permissions
 * @return                          Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_ChangeVersion(
    struct Longtail_BlockStoreAPI* block_store_api,
    struct Longtail_StorageAPI* version_storage_api,
    struct Longtail_HashAPI* hash_api,
    struct Longtail_JobAPI* job_api,
    struct Longtail_ProgressAPI* progress_api,
    struct Longtail_CancelAPI* optional_cancel_api,
    Longtail_CancelAPI_HCancelToken optional_cancel_token,
    const struct Longtail_ContentIndex* content_index,
    const struct Longtail_VersionIndex* source_version,
    const struct Longtail_VersionIndex* target_version,
    const struct Longtail_VersionDiff* version_diff,
    const char* version_path,
    int retain_permissions);

/*! @brief Get the size of the block index data.
 *
 * This size is just for the data of the block index excluding the struct Longtail_BlockIndex.
 *
 * @param[in] chunk_count     Number of chunks in the block
 * @return                    The requires size in bytes
 */
LONGTAIL_EXPORT size_t Longtail_GetBlockIndexDataSize(uint32_t chunk_count);

/*! @brief Get the size of the block index.
 *
 * This size is for the data of the block index including the struct Longtail_BlockIndex.
 *
 * @param[in] chunk_count     Number of chunks in the block
 * @return                    The requires size in bytes
 */
LONGTAIL_EXPORT size_t Longtail_GetBlockIndexSize(uint32_t chunk_count);

/*! @brief Initialize a struct Longtail_BlockIndex.
 *
 * Initialized a chunk of memory into a struct Longtail_BlockIndex
 *
 * @param[in] mem           The chunk of memory to initialize
 * @param[in] chunk_count   Number of chunks in block
 * @return                  An initialized struct Longtail_BlockIndex, or 0 on bad parameters / out of memory
 */
LONGTAIL_EXPORT struct Longtail_BlockIndex* Longtail_InitBlockIndex(void* mem, uint32_t chunk_count);

/*! @brief Initialize a struct Longtail_BlockIndex from block index data.
 *
 * Initialized a struct Longtail_BlockIndex from block index data
 *
 * @param[in] block_index   The struct Longtail_BlockIndex to initialize
 * @param[in] data          The block index data
 * @param[in] data_size     The size of the block index data
 * @return                  Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_InitBlockIndexFromData(
    struct Longtail_BlockIndex* block_index,
    void* data,
    uint64_t data_size);

/*! @brief Initialize a struct Longtail_BlockIndex from discreet data.
 *
 * Initialized a struct Longtail_BlockIndex from discreet data. Allocated with Longtail_Alloc() and freed with Longtail_Free()
 *
 * @param[in] hash_api          An implementation of struct Longtail_HashAPI interface
 * @param[in] tag               The tag for the block - ususally a compression identifier
 * @param[in] chunk_count       Number of chunks in the block - @p chunk_indexes size must match @p chunk_count
 * @param[in] chunk_indexes     Indexing into @p chunk_hashes
 * @param[in] chunk_hashes      Array of chunk hashes - it can contain many more hashes than chunks in block - use @p chunk_indexes array to identify hashes
 * @param[in] chunk_sizes       Array of chunk sizes - it can contain many more sizes than chunks in block - use @p chunk_indexes array to identify sizes
 * @param[out] out_block_index  The resulting content index will be created and assigned to this pointer reference if successful
 * @return                      Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_CreateBlockIndex(
    struct Longtail_HashAPI* hash_api,
    uint32_t tag,
    uint32_t chunk_count,
    const uint64_t* chunk_indexes,
    const TLongtail_Hash* chunk_hashes,
    const uint32_t* chunk_sizes,
    struct Longtail_BlockIndex** out_block_index);

/*! @brief Writes a struct Longtail_BlockIndex to a byte buffer.
 *
 * Serializes a struct Longtail_BlockIndex to a buffer which is allocated using Longtail_Alloc()
 *
 * @param[in] block_index   Pointer to an initialized struct Longtail_BlockIndex
 * @param[out] out_buffer   Pointer to a buffer pointer intitialized on success
 * @param[out] out_size     Pointer to a size variable intitialized on success
 * @return                  Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_WriteBlockIndexToBuffer(
    const struct Longtail_BlockIndex* block_index,
    void** out_buffer,
    size_t* out_size);

/*! @brief Reads a struct Longtail_BlockIndex from a byte buffer.
 *
 * Deserializes a struct Longtail_BlockIndex from a buffer, the struct Longtail_BlockIndex is allocated using Longtail_Alloc()
 *
 * @param[in] buffer            Buffer containing the serialized struct Longtail_BlockIndex
 * @param[in] size              Size of the buffer
 * @param[out] out_block_index  Pointer to an struct Longtail_BlockIndex pointer
 * @return                      Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_ReadBlockIndexFromBuffer(
    const void* buffer,
    size_t size,
    struct Longtail_BlockIndex** out_block_index);

/*! @brief Writes a struct Longtail_BlockIndex.
 *
 * Serializes a struct Longtail_BlockIndex to a file in a struct Longtail_StorageAPI at the specified path.
 * The parent folder of the file path must exist.
 *
 * @param[in] storage_api   An initialized struct Longtail_StorageAPI
 * @param[in] block_index   Pointer to an initialized struct Longtail_BlockIndex
 * @param[in] path          A path in the storage api to store the block index to
 * @return                  Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_WriteBlockIndex(
    struct Longtail_StorageAPI* storage_api,
    struct Longtail_BlockIndex* block_index,
    const char* path);

/*! @brief Reads a struct Longtail_BlockIndex.
 *
 * Deserializes a struct Longtail_BlockIndex from a file in a struct Longtail_StorageAPI at the specified path.
 * The file must exist.
 *
 * @param[in] storage_api       An initialized struct Longtail_StorageAPI
 * @param[in] path              A path in the storage api to read the block index from
 * @param[out] out_block_index  Pointer to an struct Longtail_BlockIndex pointer
 * @return                      Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_ReadBlockIndex(
    struct Longtail_StorageAPI* storage_api,
    const char* path,
    struct Longtail_BlockIndex** out_block_index);

/*! @brief Get the size of the stored block
 *
 * This size is for the data of the block  including the struct Longtail_StoredBlock.
 *
 * @param[in] block_data_size   Size of the block data
 * @return                      The requires size in bytes
 */
LONGTAIL_EXPORT size_t Longtail_GetStoredBlockSize(
    size_t block_data_size);

/*! @brief Initialize a struct Longtail_StoredBlock from stored block data.
 *
 * Initialized a struct Longtail_StoredBlock from block data
 *
 * @param[in] stored_block      The struct Longtail_StoredBlock to initialize
 * @param[in] block_data        The stored block data
 * @param[in] block_data_size   The size of the stored block data
 * @return                      Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_InitStoredBlockFromData(
    struct Longtail_StoredBlock* stored_block,
    void* block_data,
    size_t block_data_size);

/*! @brief Initialize a struct Longtail_StoredBlock from discreet data.
 *
 * Initialized a struct Longtail_StoredBlock from discreet data. Allocated with Longtail_Alloc() and freed with Longtail_Free()
 *
 * @param[in] block_hash        The hash of the stored block
 * @param[in] hash_identifier   The identifier of the hash type
 * @param[in] chunk_count       Number of chunks in the stored block - @p chunk_hashes and @p chunk_sizes size must match @p chunk_count
 * @param[in] tag               Tag for the block (for example compression algorithm identifier)
 * @param[in] chunk_hashes      Array of chunk hashes of size @p chunk_count
 * @param[in] chunk_sizes       Array of chunk sizes of size @p chunk_count
 * @param[in] block_data_size   Size of the stored block data
 * @param[out] out_stored_block The resulting stored block will be created and assigned to this pointer reference if successful
 * @return                      Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_CreateStoredBlock(
    TLongtail_Hash block_hash,
    uint32_t hash_identifier,
    uint32_t chunk_count,
    uint32_t tag,
    TLongtail_Hash* chunk_hashes,
    uint32_t* chunk_sizes,
    uint32_t block_data_size,
    struct Longtail_StoredBlock** out_stored_block);

/*! @brief Writes a struct Longtail_StoredBlock to a byte buffer.
 *
 * Serializes a struct Longtail_StoredBlock to a buffer which is allocated using Longtail_Alloc()
 *
 * @param[in] stored_block  Pointer to an initialized struct Longtail_StoredBlock
 * @param[out] out_buffer   Pointer to a buffer pointer intitialized on success
 * @param[out] out_size     Pointer to a size variable intitialized on success
 * @return                  Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_WriteStoredBlockToBuffer(
    const struct Longtail_StoredBlock* stored_block,
    void** out_buffer,
    size_t* out_size);

/*! @brief Reads a struct Longtail_StoredBlock from a byte buffer.
 *
 * Deserializes a struct Longtail_StoredBlock from a buffer, the struct Longtail_StoredBlock is allocated using Longtail_Alloc()
 *
 * @param[in] buffer            Buffer containing the serialized struct Longtail_StoredBlock
 * @param[in] size              Size of the buffer
 * @param[out] out_stored_block Pointer to an struct Longtail_StoredBlock pointer
 * @return                      Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_ReadStoredBlockFromBuffer(
    const void* buffer,
    size_t size,
    struct Longtail_StoredBlock** out_stored_block);

/*! @brief Writes a struct Longtail_StoredBlock.
 *
 * Serializes a struct Longtail_StoredBlock to a file in a struct Longtail_StorageAPI at the specified path.
 * The parent folder of the file path must exist.
 *
 * @param[in] storage_api   An initialized struct Longtail_StorageAPI
 * @param[in] stored_block  Pointer to an initialized struct Longtail_BlockIndex
 * @param[in] path          A path in the storage api to store the stored block to
 * @return                  Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_WriteStoredBlock(
    struct Longtail_StorageAPI* storage_api,
    struct Longtail_StoredBlock* stored_block,
    const char* path);

/*! @brief Reads a struct Longtail_StoredBlock.
 *
 * Deserializes a struct Longtail_StoredBlock from a file in a struct Longtail_StorageAPI at the specified path.
 * The file must exist.
 *
 * @param[in] storage_api       An initialized struct Longtail_StoredBlock
 * @param[in] path              A path in the storage api to read the stored block from
 * @param[out] out_stored_block Pointer to an struct Longtail_StoredBlock pointer
 * @return                      Return code (errno style), zero on success
 */
LONGTAIL_EXPORT int Longtail_ReadStoredBlock(
    struct Longtail_StorageAPI* storage_api,
    const char* path,
    struct Longtail_StoredBlock** out_stored_block);

/*! @brief Validate that content_index contains all of version_index.
 *
 * Validates that all chunks required for @p version_index are present in @p content_index
 * Validates that reconstructing an asset via chunks results in the same size as recorded in @p version_index
 *
 * @param[in] content_index         The content index to validate
 * @param[in] version_index         The version index used to validate the content of @p content_index
 * @return                          Return code (errno style), zero on success. Success is when all content required is present
 */
LONGTAIL_EXPORT int Longtail_ValidateContent(
    const struct Longtail_ContentIndex* content_index,
    const struct Longtail_VersionIndex* version_index);

/*! @brief Validate that version_index contains all of content_index.
 *
 * Validates that all chunks present in @p content_index are present in @p version_index
 * Validates that reconstructing an asset via chunks results in the same size as recorded in @p version_index
 *
 * @param[in] content_index         The content index to validate
 * @param[in] version_index         The version index used to validate the content of @p content_index
 * @return                          Return code (errno style), zero on success. Success is when all content required is present
 */
LONGTAIL_EXPORT int Longtail_ValidateVersion(
    const struct Longtail_ContentIndex* content_index,
    const struct Longtail_VersionIndex* version_index);

struct Longtail_BlockIndex
{
    TLongtail_Hash* m_BlockHash;
    uint32_t* m_HashIdentifier;
    uint32_t* m_ChunkCount;
    uint32_t* m_Tag;
    TLongtail_Hash* m_ChunkHashes; //[]
    uint32_t* m_ChunkSizes; // []
};

LONGTAIL_EXPORT uint32_t Longtail_BlockIndex_GetChunkCount(const struct Longtail_BlockIndex* block_index);
LONGTAIL_EXPORT const uint32_t* Longtail_BlockIndex_GetChunkTag(const struct Longtail_BlockIndex* block_index);
LONGTAIL_EXPORT const TLongtail_Hash* Longtail_BlockIndex_GetChunkHashes(const struct Longtail_BlockIndex* block_index);
LONGTAIL_EXPORT const uint32_t* Longtail_BlockIndex_GetChunkSizes(const struct Longtail_BlockIndex* block_index);

typedef int (*Longtail_StoredBlock_DisposeFunc)(struct Longtail_StoredBlock* stored_block);

struct Longtail_StoredBlock
{
    Longtail_StoredBlock_DisposeFunc Dispose;
    struct Longtail_BlockIndex* m_BlockIndex;
    void* m_BlockData;
    uint32_t m_BlockChunksDataSize;
};

LONGTAIL_EXPORT void Longtail_StoredBlock_Dispose(struct Longtail_StoredBlock* stored_block);
LONGTAIL_EXPORT struct Longtail_BlockIndex* Longtail_StoredBlock_GetBlockIndex(struct Longtail_StoredBlock* stored_block);
LONGTAIL_EXPORT void* Longtail_BlockIndex_BlockData(struct Longtail_StoredBlock* stored_block);
LONGTAIL_EXPORT uint32_t Longtail_BlockIndex_GetBlockChunksDataSize(struct Longtail_StoredBlock* stored_block);

struct Longtail_FileInfos
{
    uint32_t m_Count;
    uint32_t m_PathDataSize;
    uint64_t* m_Sizes;
    uint32_t* m_PathStartOffsets;
    uint16_t* m_Permissions;
    char* m_PathData;
};

LONGTAIL_EXPORT uint32_t Longtail_FileInfos_GetCount(const struct Longtail_FileInfos* file_infos);
LONGTAIL_EXPORT const char* Longtail_FileInfos_GetPath(const struct Longtail_FileInfos* file_infos, uint32_t index);
LONGTAIL_EXPORT const struct Longtail_Paths* Longtail_FileInfos_GetPaths(const struct Longtail_FileInfos* file_infos);
LONGTAIL_EXPORT uint64_t Longtail_FileInfos_GetSize(const struct Longtail_FileInfos* file_infos, uint32_t index);
LONGTAIL_EXPORT const uint16_t* Longtail_FileInfos_GetPermissions(const struct Longtail_FileInfos* file_infos, uint32_t index);

extern uint32_t Longtail_CurrentContentIndexVersion;

struct Longtail_ContentIndex
{
    uint32_t* m_Version;
    uint32_t* m_HashIdentifier;
    uint32_t* m_MaxBlockSize;
    uint32_t* m_MaxChunksPerBlock;
    uint64_t* m_BlockCount;
    uint64_t* m_ChunkCount;

    TLongtail_Hash* m_BlockHashes;      // []
    TLongtail_Hash* m_ChunkHashes;      // []
    uint64_t* m_ChunkBlockIndexes;      // []
};

LONGTAIL_EXPORT uint32_t Longtail_ContentIndex_GetVersion(const struct Longtail_ContentIndex* content_index);
LONGTAIL_EXPORT uint32_t Longtail_ContentIndex_GetHashAPI(const struct Longtail_ContentIndex* content_index);
LONGTAIL_EXPORT uint64_t Longtail_ContentIndex_GetBlockCount(const struct Longtail_ContentIndex* content_index);
LONGTAIL_EXPORT uint64_t Longtail_ContentIndex_GetChunkCount(const struct Longtail_ContentIndex* content_index);
LONGTAIL_EXPORT TLongtail_Hash* Longtail_ContentIndex_BlockHashes(const struct Longtail_ContentIndex* content_index);

struct Longtail_VersionIndex
{
    uint32_t* m_Version;
    uint32_t* m_HashIdentifier;
    uint32_t* m_TargetChunkSize;
    uint32_t* m_AssetCount;
    uint32_t* m_ChunkCount;
    uint32_t* m_AssetChunkIndexCount;
    TLongtail_Hash* m_PathHashes;       // []
    TLongtail_Hash* m_ContentHashes;    // []
    uint64_t* m_AssetSizes;             // []
    uint32_t* m_AssetChunkCounts;       // []
    // uint64_t* m_CreationDates;       // []
    // uint64_t* m_ModificationDates;   // []
    uint32_t* m_AssetChunkIndexStarts;  // []
    uint32_t* m_AssetChunkIndexes;      // []
    TLongtail_Hash* m_ChunkHashes;      // []

    uint32_t* m_ChunkSizes;             // []
    uint32_t* m_ChunkTags;              // []

    uint32_t* m_NameOffsets;            // []
    uint32_t m_NameDataSize;
    uint16_t* m_Permissions;            // []
    char* m_NameData;
};

LONGTAIL_EXPORT uint32_t Longtail_VersionIndex_GetVersion(const struct Longtail_VersionIndex* content_index);
LONGTAIL_EXPORT uint32_t Longtail_VersionIndex_GetHashAPI(const struct Longtail_VersionIndex* content_index);
LONGTAIL_EXPORT uint32_t Longtail_VersionIndex_GetAssetCount(const struct Longtail_VersionIndex* content_index);
LONGTAIL_EXPORT uint32_t Longtail_VersionIndex_GetChunkCount(const struct Longtail_VersionIndex* content_index);

struct Longtail_VersionDiff
{
    uint32_t* m_SourceRemovedCount;
    uint32_t* m_TargetAddedCount;
    uint32_t* m_ModifiedContentCount;
    uint32_t* m_ModifiedPermissionsCount;
    uint32_t* m_SourceRemovedAssetIndexes;
    uint32_t* m_TargetAddedAssetIndexes;
    uint32_t* m_SourceContentModifiedAssetIndexes;
    uint32_t* m_TargetContentModifiedAssetIndexes;
    uint32_t* m_SourcePermissionsModifiedAssetIndexes;
    uint32_t* m_TargetPermissionsModifiedAssetIndexes;
};

int Longtail_GetPathHash(struct Longtail_HashAPI* hash_api, const char* path, TLongtail_Hash* out_hash);

size_t Longtail_LookupTable_GetSize(size_t capacity);
struct Longtail_LookupTable* Longtail_LookupTable_Create(void* mem, size_t capacity, struct Longtail_LookupTable* optional_source_entries);
int Longtail_LookupTable_Put(struct Longtail_LookupTable* lut, uint64_t key, uint64_t value);
uint64_t* Longtail_LookupTable_PutUnique(struct Longtail_LookupTable* lut, uint64_t key, uint64_t value);
uint64_t* Longtail_LookupTable_Get(const struct Longtail_LookupTable* lut, uint64_t key);
uint64_t Longtail_LookupTable_GetSpaceLeft(const struct Longtail_LookupTable* lut);

///////////// Test functions

int Longtail_MakeFileInfos(
    uint32_t path_count,
    const char* const* path_names,
    const uint64_t* file_sizes,
    const uint16_t* file_permissions,
    struct Longtail_FileInfos** out_file_infos);

size_t Longtail_GetVersionIndexSize(
    uint32_t asset_count,
    uint32_t chunk_count,
    uint32_t asset_chunk_index_count,
    uint32_t path_data_size);

int Longtail_BuildVersionIndex(
    void* mem,
    size_t mem_size,
    const struct Longtail_FileInfos* file_infos,
    const TLongtail_Hash* path_hashes,
    const TLongtail_Hash* content_hashes,
    const uint32_t* asset_chunk_index_starts,
    const uint32_t* asset_chunk_counts,
    uint32_t asset_chunk_index_count,
    const uint32_t* asset_chunk_indexes,
    uint32_t chunk_count,
    const uint32_t* chunk_sizes,
    const TLongtail_Hash* chunk_hashes,
    const uint32_t* optional_chunk_tags,
    uint32_t hash_api_identifier,
    uint32_t target_chunk_size,
    struct Longtail_VersionIndex** out_version_index);

#ifdef __cplusplus
}
#endif
