#include "longtail_fsblockstore.h"

#include "../../src/ext/stb_ds.h"
#include "../longtail_platform.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

struct BlockHashToBlockState
{
    uint64_t key;
    uint32_t value;
};

#define TMP_EXTENSION_LENGTH (1 + 16)

struct FSBlockStoreAPI
{
    struct Longtail_BlockStoreAPI m_BlockStoreAPI;
    struct Longtail_JobAPI* m_JobAPI;
    struct Longtail_StorageAPI* m_StorageAPI;
    char* m_ContentPath;

    TLongtail_Atomic64 m_StatU64[Longtail_BlockStoreAPI_StatU64_Count];

    HLongtail_SpinLock m_Lock;

    struct Longtail_ContentIndex* m_ContentIndex;
    struct BlockHashToBlockState* m_BlockState;
    struct Longtail_BlockIndex** m_AddedBlockIndexes;
    const char* m_BlockExtension;
    const char* m_ContentIndexLockPath;
    uint32_t m_DefaultMaxBlockSize;
    uint32_t m_DefaultMaxChunksPerBlock;
    char m_TmpExtension[TMP_EXTENSION_LENGTH + 1];
};

#define BLOCK_NAME_LENGTH   23

static const char* HashLUT = "0123456789abcdef";

static void GetUniqueExtension(uint64_t id, char* extension)
{
    extension[0] = '.';
    extension[1] = HashLUT[(id >> 60) & 0xf];
    extension[2] = HashLUT[(id >> 56) & 0xf];
    extension[3] = HashLUT[(id >> 52) & 0xf];
    extension[4] = HashLUT[(id >> 48) & 0xf];
    extension[5] = HashLUT[(id >> 44) & 0xf];
    extension[6] = HashLUT[(id >> 40) & 0xf];
    extension[7] = HashLUT[(id >> 36) & 0xf];
    extension[8] = HashLUT[(id >> 32) & 0xf];
    extension[9] = HashLUT[(id >> 28) & 0xf];
    extension[10] = HashLUT[(id >> 24) & 0xf];
    extension[11] = HashLUT[(id >> 20) & 0xf];
    extension[12] = HashLUT[(id >> 16) & 0xf];
    extension[13] = HashLUT[(id >> 12) & 0xf];
    extension[14] = HashLUT[(id >> 8) & 0xf];
    extension[15] = HashLUT[(id >> 4) & 0xf];
    extension[16] = HashLUT[(id >> 0) & 0xf];
    extension[17] = 0;
}

static void GetBlockName(TLongtail_Hash block_hash, char* out_name)
{
    LONGTAIL_FATAL_ASSERT(out_name, return)
    out_name[7] = HashLUT[(block_hash >> 60) & 0xf];
    out_name[8] = HashLUT[(block_hash >> 56) & 0xf];
    out_name[9] = HashLUT[(block_hash >> 52) & 0xf];
    out_name[10] = HashLUT[(block_hash >> 48) & 0xf];
    out_name[11] = HashLUT[(block_hash >> 44) & 0xf];
    out_name[12] = HashLUT[(block_hash >> 40) & 0xf];
    out_name[13] = HashLUT[(block_hash >> 36) & 0xf];
    out_name[14] = HashLUT[(block_hash >> 32) & 0xf];
    out_name[15] = HashLUT[(block_hash >> 28) & 0xf];
    out_name[16] = HashLUT[(block_hash >> 24) & 0xf];
    out_name[17] = HashLUT[(block_hash >> 20) & 0xf];
    out_name[18] = HashLUT[(block_hash >> 16) & 0xf];
    out_name[19] = HashLUT[(block_hash >> 12) & 0xf];
    out_name[20] = HashLUT[(block_hash >> 8) & 0xf];
    out_name[21] = HashLUT[(block_hash >> 4) & 0xf];
    out_name[22] = HashLUT[(block_hash >> 0) & 0xf];
    out_name[0] = out_name[7];
    out_name[1] = out_name[8];
    out_name[2] = out_name[9];
    out_name[3] = out_name[10];
    out_name[4] = '/';
    out_name[5] = '0';
    out_name[6] = 'x';
}

static char* GetBlockPath(struct Longtail_StorageAPI* storage_api, const char* content_path, const char* block_extension, TLongtail_Hash block_hash)
{
    LONGTAIL_FATAL_ASSERT(storage_api, return 0)
    LONGTAIL_FATAL_ASSERT(content_path, return 0)
    char file_name[7 + BLOCK_NAME_LENGTH + 15 + 1];
    strcpy(file_name, "chunks/");
    GetBlockName(block_hash, &file_name[7]);
    strcpy(&file_name[7 + BLOCK_NAME_LENGTH], block_extension);
    return storage_api->ConcatPath(storage_api, content_path, file_name);
}

static char* GetTempBlockPath(struct Longtail_StorageAPI* storage_api, const char* content_path, TLongtail_Hash block_hash, const char* tmp_extension)
{
    LONGTAIL_FATAL_ASSERT(storage_api, return 0)
    LONGTAIL_FATAL_ASSERT(content_path, return 0)
    char file_name[7 + BLOCK_NAME_LENGTH + TMP_EXTENSION_LENGTH + 1];
    strcpy(file_name, "chunks/");
    GetBlockName(block_hash, &file_name[7]);
    strcpy(&file_name[7 + BLOCK_NAME_LENGTH], tmp_extension);
    return storage_api->ConcatPath(storage_api, content_path, file_name);
}

static int SafeWriteContentIndex(struct FSBlockStoreAPI* api)
{
    struct Longtail_StorageAPI* storage_api = api->m_StorageAPI;
    const char* content_path = api->m_ContentPath;

    char tmp_store_path[5 + TMP_EXTENSION_LENGTH + 1];
    strcpy(tmp_store_path, "store");
    strcpy(&tmp_store_path[5], api->m_TmpExtension);
    const char* content_index_path_tmp = storage_api->ConcatPath(storage_api, content_path, tmp_store_path);
    int err = EnsureParentPathExists(storage_api, content_index_path_tmp);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "SafeWriteContentIndex(%p) EnsureParentPathExists() failed with %d",
            api,
            err)
        Longtail_Free((char*)content_index_path_tmp);
        return err;
    }

    const char* content_index_path = storage_api->ConcatPath(storage_api, content_path, "store.lci");

    struct Longtail_ContentIndex* content_index = api->m_ContentIndex;
    if (storage_api->IsFile(storage_api, content_index_path))
    {
        struct Longtail_ContentIndex* existing_content_index = 0;
        err = Longtail_ReadContentIndex(storage_api, content_index_path, &existing_content_index);
        if (err)
        {
            LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "SafeWriteContentIndex(%p) Longtail_ReadContentIndex() failed with %d",
                api,
                err)
            Longtail_Free((void*)content_index_path);
            Longtail_Free((void*)content_index_path_tmp);
            return err;
        }
        struct Longtail_ContentIndex* merged_content_index = 0;
        err = Longtail_MergeContentIndex(
            api->m_JobAPI,
            existing_content_index,
            content_index,
            &merged_content_index);
        if (err)
        {
            LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "SafeWriteContentIndex(%p) Longtail_MergeContentIndex() failed with %d",
                api,
                err)
            Longtail_Free(existing_content_index);
            Longtail_Free((void*)content_index_path);
            Longtail_Free((void*)content_index_path_tmp);
            return err;
        }
        Longtail_Free(existing_content_index);
        content_index = merged_content_index;
    }

    err = Longtail_WriteContentIndex(storage_api, content_index, content_index_path_tmp);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "SafeWriteContentIndex(%p) Longtail_WriteContentIndex() failed with %d",
            api,
            err)
        Longtail_Free((void*)content_index_path);
        Longtail_Free((void*)content_index_path_tmp);
        return err;
    }

    if (storage_api->IsFile(storage_api, content_index_path))
    {
        err = storage_api->RemoveFile(storage_api, content_index_path);
        if (err)
        {
            LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "SafeWriteContentIndex(%p) RemoveFile() failed with %d",
                api,
                err)
            Longtail_Free((void*)content_index_path);
            storage_api->RemoveFile(storage_api, content_index_path_tmp);
            Longtail_Free((void*)content_index_path_tmp);
            return err;
        }
    }

    err = storage_api->RenameFile(storage_api, content_index_path_tmp, content_index_path);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "SafeWriteContentIndex(%p) RenameFile() failed with %d",
            api,
            err)
        storage_api->RemoveFile(storage_api, content_index_path_tmp);
    }

    if (!err)
    {
        if (api->m_ContentIndex != content_index)
        {
            Longtail_Free(api->m_ContentIndex);
            api->m_ContentIndex = content_index;
        }
    }

    Longtail_Free((void*)content_index_path);
    Longtail_Free((void*)content_index_path_tmp);

    return err;
}

static int SafeWriteStoredBlock(struct FSBlockStoreAPI* api, struct Longtail_StorageAPI* storage_api, const char* content_path, const char* block_extension, struct Longtail_StoredBlock* stored_block)
{
    TLongtail_Hash block_hash = *stored_block->m_BlockIndex->m_BlockHash;
    char* block_path = GetBlockPath(storage_api, content_path, block_extension, block_hash);

    // Check if block exists, if it does it is just the store content index that is out of sync.
    // Don't write the block unless we have to
    if (storage_api->IsFile(storage_api, block_path))
    {
        Longtail_Free((void*)block_path);
        return 0;
    }

    char* tmp_block_path = GetTempBlockPath(storage_api, content_path, block_hash, api->m_TmpExtension);
    int err = EnsureParentPathExists(storage_api, tmp_block_path);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "SafeWriteStoredBlock(%p, %s, %p) failed with %d",
            storage_api, content_path, stored_block,
            err)
        Longtail_Free((char*)tmp_block_path);
        Longtail_Free((char*)block_path);
        return err;
    }

    err = Longtail_WriteStoredBlock(storage_api, stored_block, tmp_block_path);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "SafeWriteStoredBlock(%p, %s, %p) failed with %d",
            storage_api, content_path, stored_block,
            err)
        Longtail_Free((char*)tmp_block_path);
        Longtail_Free((char*)block_path);
        return err;
    }

    err = storage_api->RenameFile(storage_api, tmp_block_path, block_path);
    if (err)
    {
        int remove_err = storage_api->RemoveFile(storage_api, tmp_block_path);
        if (remove_err)
        {
            LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_WARNING, "SafeWriteStoredBlock(%p, %s, %p) cant remove redundant temp block file, failed with %d",
                storage_api, content_path, stored_block,
                remove_err)
        }
    }

    if (err && (err != EEXIST))
    {
        // Someone beat us to it, all good.
        if (storage_api->IsFile(storage_api, block_path))
        {
            Longtail_Free((char*)tmp_block_path);
            Longtail_Free((void*)block_path);
            return 0;
        }
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "SafeWriteStoredBlock(%p, %s, %p) failed to rename temp block file, failed with %d",
            storage_api, content_path, stored_block,
            err)
        Longtail_Free((char*)tmp_block_path);
        Longtail_Free((char*)block_path);
        return err;
    }

    Longtail_Free((char*)tmp_block_path);
    Longtail_Free((char*)block_path);
    return 0;
}

static int UpdateContentIndex(
    struct Longtail_ContentIndex* current_content_index,
    struct Longtail_BlockIndex** added_block_indexes,
    struct Longtail_ContentIndex** out_content_index)
{
    struct Longtail_ContentIndex* added_content_index;
    int err = Longtail_CreateContentIndexFromBlocks(
        *current_content_index->m_MaxBlockSize,
        *current_content_index->m_MaxChunksPerBlock,
        (uint64_t)(arrlen(added_block_indexes)),
        added_block_indexes,
        &added_content_index);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "UpdateContentIndex(%p, %p, %p) failed with %d",
            current_content_index, added_block_indexes, out_content_index,
            err)
        return err;
    }
    struct Longtail_ContentIndex* new_content_index;
    err = Longtail_AddContentIndex(
        current_content_index,
        added_content_index,
        &new_content_index);
    Longtail_Free(added_content_index);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "UpdateContentIndex(%p, %p, %p) failed with %d",
            current_content_index, added_block_indexes, out_content_index,
            err)
        Longtail_Free(added_content_index);
        return err;
    }
    *out_content_index = new_content_index;
    return 0;
}

struct FSStoredBlock
{
    struct Longtail_StoredBlock m_StoredBlock;
};

static int FSStoredBlock_Dispose(struct Longtail_StoredBlock* stored_block)
{
    LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_DEBUG, "FSStoredBlock_Dispose(%p)", stored_block)
    LONGTAIL_FATAL_ASSERT(stored_block, return EINVAL)
    Longtail_Free(stored_block);
    return 0;
}

struct ScanBlockJob
{
    struct Longtail_StorageAPI* m_StorageAPI;
    const char* m_ContentPath;
    const char* m_ChunksPath;
    const char* m_BlockPath;
    const char* m_BlockExtension;
    struct Longtail_BlockIndex* m_BlockIndex;
    int m_Err;
};

int EndsWith(const char *str, const char *suffix)
{
    if (!str || !suffix)
        return 0;
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    if (lensuffix >  lenstr)
        return 0;
    return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}

static int ScanBlock(void* context, uint32_t job_id, int is_cancelled)
{
    LONGTAIL_FATAL_ASSERT(context != 0, return 0)
    struct ScanBlockJob* job = (struct ScanBlockJob*)context;
    if (is_cancelled)
    {
        job->m_Err = ECANCELED;
        return 0;
    }

    const char* block_path = job->m_BlockPath;
    if (!EndsWith(block_path, job->m_BlockExtension))
    {
        job->m_Err = ENOENT;
        return 0;
    }

    struct Longtail_StorageAPI* storage_api = job->m_StorageAPI;
    const char* chunks_path = job->m_ChunksPath;
    char* full_block_path = storage_api->ConcatPath(storage_api, chunks_path, block_path);

    job->m_Err = Longtail_ReadBlockIndex(
        storage_api,
        full_block_path,
        &job->m_BlockIndex);

    if (job->m_Err == 0)
    {
        TLongtail_Hash block_hash = *job->m_BlockIndex->m_BlockHash;
        char* validate_file_name = GetBlockPath(storage_api, job->m_ContentPath, job->m_BlockExtension, block_hash);
        if (strcmp(validate_file_name, full_block_path) != 0)
        {
            Longtail_Free(job->m_BlockIndex);
            job->m_BlockIndex = 0;
            job->m_Err = EBADF;
        }
        Longtail_Free(validate_file_name);
    }

    Longtail_Free(full_block_path);
    full_block_path = 0;
    return 0;
}

static int ReadContent(
    struct Longtail_StorageAPI* storage_api,
    struct Longtail_JobAPI* job_api,
    uint32_t max_block_size,
    uint32_t max_chunks_per_block,
    const char* content_path,
    const char* block_extension,
    struct Longtail_ContentIndex** out_content_index)
{
    LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_DEBUG, "ReadContent(%p, %p, %u, %u, %s, %s, %p)",
        storage_api, job_api, max_block_size, max_chunks_per_block, content_path, block_extension, out_content_index)

    LONGTAIL_FATAL_ASSERT(storage_api != 0, return EINVAL)
    LONGTAIL_FATAL_ASSERT(job_api != 0, return EINVAL)
    LONGTAIL_FATAL_ASSERT(content_path != 0, return EINVAL)
    LONGTAIL_FATAL_ASSERT(out_content_index != 0, return EINVAL)

    LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_DEBUG, "FSBlockStore::ReadContent(%p, %p, %s, %p)",
        storage_api, job_api, content_path, out_content_index)

    const char* chunks_path = Longtail_Strdup(content_path);//storage_api->ConcatPath(storage_api, content_path, "chunks");
    if (!chunks_path)
    {
        return ENOMEM;
    }

    struct Longtail_FileInfos* file_infos;
    int err = Longtail_GetFilesRecursively(
        storage_api,
        0,
        0,
        0,
        chunks_path,
        &file_infos);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_WARNING, "FSBlockStore::ReadContent(%p, %p, %s, %p) failed with %d",
            storage_api, job_api, chunks_path, out_content_index,
            err)
        Longtail_Free((void*)chunks_path);
        return err;
    }

    uint32_t path_count = file_infos->m_Count;
    if (path_count == 0)
    {
        err = Longtail_CreateContentIndexFromBlocks(
            max_block_size,
            max_chunks_per_block,
            0,
            0,
            out_content_index);
        Longtail_Free(file_infos);
        Longtail_Free((void*)chunks_path);
        return err;
    }
    Longtail_JobAPI_Group job_group;
    err = job_api->ReserveJobs(job_api, path_count, &job_group);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "FSBlockStore::ReadContent(%p, %p, %s, %p) failed with %d",
            storage_api, job_api, content_path, out_content_index,
            err)
        Longtail_Free(file_infos);
        Longtail_Free((void*)chunks_path);
        return err;
    }

    size_t scan_jobs_size = sizeof(struct ScanBlockJob) * path_count;
    struct ScanBlockJob* scan_jobs = (struct ScanBlockJob*)Longtail_Alloc(scan_jobs_size);
    if (!scan_jobs)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "FSBlockStore::ReadContent(%p, %p, %s, %p) failed with %d",
            storage_api, job_api, content_path, out_content_index,
            ENOMEM)
        Longtail_Free(file_infos);
        Longtail_Free((void*)chunks_path);
        return ENOMEM;
    }

    for (uint32_t path_index = 0; path_index < path_count; ++path_index)
    {
        struct ScanBlockJob* job = &scan_jobs[path_index];
        const char* block_path = &file_infos->m_PathData[file_infos->m_PathStartOffsets[path_index]];
        job->m_BlockIndex = 0;

        job->m_StorageAPI = storage_api;
        job->m_ContentPath = content_path;
        job->m_ChunksPath = chunks_path;
        job->m_BlockPath = block_path;
        job->m_BlockExtension = block_extension;
        job->m_BlockIndex = 0;
        job->m_Err = EINVAL;

        Longtail_JobAPI_JobFunc job_func[] = {ScanBlock};
        void* ctx[] = {job};
        Longtail_JobAPI_Jobs jobs;
        err = job_api->CreateJobs(job_api, job_group, 1, job_func, ctx, &jobs);
        LONGTAIL_FATAL_ASSERT(!err, return err)
        err = job_api->ReadyJobs(job_api, 1, jobs);
        LONGTAIL_FATAL_ASSERT(!err, return err)
    }

    err = job_api->WaitForAllJobs(job_api, job_group, 0, 0, 0);
    if (err)
    {
        LONGTAIL_LOG(err == ECANCELED ? LONGTAIL_LOG_LEVEL_INFO : LONGTAIL_LOG_LEVEL_ERROR, "FSBlockStore::ReadContent(%p, %p, %s, %p) failed with %d",
            storage_api, job_api, content_path, out_content_index,
            err)
        Longtail_Free(scan_jobs);
        Longtail_Free(file_infos);
        Longtail_Free((void*)chunks_path);
        return err;
    }

    size_t block_indexes_size = sizeof(struct Longtail_BlockIndex*) * (path_count);
    struct Longtail_BlockIndex** block_indexes = (struct Longtail_BlockIndex**)Longtail_Alloc(block_indexes_size);
    if (!block_indexes)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "FSBlockStore::ReadContent(%p, %p, %s, %p) failed with %d",
            storage_api, job_api, content_path, out_content_index,
            ENOMEM)
        Longtail_Free(scan_jobs);
        Longtail_Free(file_infos);
        Longtail_Free((void*)chunks_path);
        return ENOMEM;
    }

    uint64_t block_count = 0;
    for (uint32_t path_index = 0; path_index < path_count; ++path_index)
    {
        struct ScanBlockJob* job = &scan_jobs[path_index];
        if (job->m_Err == 0)
        {
            block_indexes[block_count] = job->m_BlockIndex;
            ++block_count;
        }
    }
    Longtail_Free(scan_jobs);
    scan_jobs = 0;

    Longtail_Free(file_infos);
    file_infos = 0;

    Longtail_Free((void*)chunks_path);
    chunks_path = 0;

    err = Longtail_CreateContentIndexFromBlocks(
        max_block_size,
        max_chunks_per_block,
        block_count,
        block_indexes,
        out_content_index);

    for (uint32_t b = 0; b < block_count; ++b)
    {
        Longtail_Free(block_indexes[b]);
    }
    Longtail_Free(block_indexes);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "FSBlockStore::ReadContent(%p, %p, %s, %p) failed with %d",
            storage_api, job_api, content_path, out_content_index,
            err)
    }
    return err;
}

static int FSBlockStore_PutStoredBlock(
    struct Longtail_BlockStoreAPI* block_store_api,
    struct Longtail_StoredBlock* stored_block,
    struct Longtail_AsyncPutStoredBlockAPI* async_complete_api)
{
    LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_DEBUG, "FSBlockStore_PutStoredBlock(%p, %p, %p)",
        block_store_api, stored_block, async_complete_api)
    LONGTAIL_VALIDATE_INPUT(block_store_api, return EINVAL)
    LONGTAIL_VALIDATE_INPUT(stored_block, return EINVAL)
    LONGTAIL_VALIDATE_INPUT(async_complete_api != 0, return EINVAL)

    struct FSBlockStoreAPI* fsblockstore_api = (struct FSBlockStoreAPI*)block_store_api;
    Longtail_AtomicAdd64(&fsblockstore_api->m_StatU64[Longtail_BlockStoreAPI_StatU64_PutStoredBlock_Count], 1);
    Longtail_AtomicAdd64(&fsblockstore_api->m_StatU64[Longtail_BlockStoreAPI_StatU64_PutStoredBlock_Chunk_Count], *stored_block->m_BlockIndex->m_ChunkCount);
    Longtail_AtomicAdd64(&fsblockstore_api->m_StatU64[Longtail_BlockStoreAPI_StatU64_PutStoredBlock_Byte_Count], Longtail_GetBlockIndexDataSize(*stored_block->m_BlockIndex->m_ChunkCount) + stored_block->m_BlockChunksDataSize);

    uint64_t block_hash = *stored_block->m_BlockIndex->m_BlockHash;

    Longtail_LockSpinLock(fsblockstore_api->m_Lock);
    intptr_t block_ptr = hmgeti(fsblockstore_api->m_BlockState, block_hash);
    if (block_ptr != -1)
    {
        // Already busy doing put or the block already has been stored
        Longtail_UnlockSpinLock(fsblockstore_api->m_Lock);
        async_complete_api->OnComplete(async_complete_api, 0);
        return 0;
    }

    hmput(fsblockstore_api->m_BlockState, block_hash, 0);
    Longtail_UnlockSpinLock(fsblockstore_api->m_Lock);

    int err = SafeWriteStoredBlock(fsblockstore_api, fsblockstore_api->m_StorageAPI, fsblockstore_api->m_ContentPath, fsblockstore_api->m_BlockExtension, stored_block);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "FSBlockStore_PutStoredBlock(%p, %p, %p) failed with %d",
            block_store_api, stored_block, async_complete_api,
            err)
        Longtail_AtomicAdd64(&fsblockstore_api->m_StatU64[Longtail_BlockStoreAPI_StatU64_PutStoredBlock_FailCount], 1);
        Longtail_LockSpinLock(fsblockstore_api->m_Lock);
        hmdel(fsblockstore_api->m_BlockState, block_hash);
        Longtail_UnlockSpinLock(fsblockstore_api->m_Lock);
        async_complete_api->OnComplete(async_complete_api, err);
        return 0;
    }

    void* block_index_buffer;
    size_t block_index_buffer_size;
    err = Longtail_WriteBlockIndexToBuffer(stored_block->m_BlockIndex, &block_index_buffer, &block_index_buffer_size);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "FSBlockStore_PutStoredBlock(%p, %p, %p) failed with %d",
            block_store_api, stored_block, async_complete_api,
            err)
        Longtail_AtomicAdd64(&fsblockstore_api->m_StatU64[Longtail_BlockStoreAPI_StatU64_PutStoredBlock_FailCount], 1);
        async_complete_api->OnComplete(async_complete_api, err);
        return 0;
    }
    struct Longtail_BlockIndex* block_index_copy;
    err = Longtail_ReadBlockIndexFromBuffer(block_index_buffer, block_index_buffer_size, &block_index_copy);
    Longtail_Free(block_index_buffer);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "FSBlockStore_PutStoredBlock(%p, %p, %p) failed with %d",
            block_store_api, stored_block, async_complete_api,
            err)
        Longtail_AtomicAdd64(&fsblockstore_api->m_StatU64[Longtail_BlockStoreAPI_StatU64_PutStoredBlock_FailCount], 1);
        async_complete_api->OnComplete(async_complete_api, err);
        return 0;
    }

    Longtail_LockSpinLock(fsblockstore_api->m_Lock);
    hmput(fsblockstore_api->m_BlockState, block_hash, 1);
    arrput(fsblockstore_api->m_AddedBlockIndexes, block_index_copy);
    Longtail_UnlockSpinLock(fsblockstore_api->m_Lock);

    async_complete_api->OnComplete(async_complete_api, 0);
    return 0;
}

static int FSBlockStore_PreflightGet(struct Longtail_BlockStoreAPI* block_store_api, const struct Longtail_ContentIndex* content_index)
{
    LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_DEBUG, "FSBlockStore_PreflightGet(%p, %p)",
        block_store_api, content_index)
    LONGTAIL_VALIDATE_INPUT(block_store_api, return EINVAL)
    LONGTAIL_VALIDATE_INPUT(content_index, return EINVAL)
    struct FSBlockStoreAPI* fsblockstore_api = (struct FSBlockStoreAPI*)block_store_api;
    Longtail_AtomicAdd64(&fsblockstore_api->m_StatU64[Longtail_BlockStoreAPI_StatU64_PreflightGet_Count], 1);
    return 0;
}

static int FSBlockStore_GetStoredBlock(
    struct Longtail_BlockStoreAPI* block_store_api,
    uint64_t block_hash,
    struct Longtail_AsyncGetStoredBlockAPI* async_complete_api)
{
    LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_DEBUG, "FSBlockStore_GetStoredBlock(%p, 0x" PRIx64 ", %p)",
        block_store_api, block_hash, async_complete_api)
    LONGTAIL_VALIDATE_INPUT(block_store_api, return EINVAL)
    LONGTAIL_VALIDATE_INPUT(async_complete_api, return EINVAL)

    struct FSBlockStoreAPI* fsblockstore_api = (struct FSBlockStoreAPI*)block_store_api;
    Longtail_AtomicAdd64(&fsblockstore_api->m_StatU64[Longtail_BlockStoreAPI_StatU64_GetStoredBlock_Count], 1);

    Longtail_LockSpinLock(fsblockstore_api->m_Lock);
    intptr_t block_ptr = hmgeti(fsblockstore_api->m_BlockState, block_hash);
    if (block_ptr == -1)
    {
        char* block_path = GetBlockPath(fsblockstore_api->m_StorageAPI, fsblockstore_api->m_ContentPath, fsblockstore_api->m_BlockExtension, block_hash);
        if (!fsblockstore_api->m_StorageAPI->IsFile(fsblockstore_api->m_StorageAPI, block_path))
        {
            Longtail_Free((void*)block_path);
            Longtail_UnlockSpinLock(fsblockstore_api->m_Lock);
            return ENOENT;
        }
        Longtail_Free((void*)block_path);
        hmput(fsblockstore_api->m_BlockState, block_hash, 1);
        block_ptr = hmgeti(fsblockstore_api->m_BlockState, block_hash);
    }
    uint32_t state = fsblockstore_api->m_BlockState[block_ptr].value;
    Longtail_UnlockSpinLock(fsblockstore_api->m_Lock);
    while (state == 0)
    {
        Longtail_Sleep(1000);
        Longtail_LockSpinLock(fsblockstore_api->m_Lock);
        state = hmget(fsblockstore_api->m_BlockState, block_hash);
        Longtail_UnlockSpinLock(fsblockstore_api->m_Lock);
    }
    char* block_path = GetBlockPath(fsblockstore_api->m_StorageAPI, fsblockstore_api->m_ContentPath, fsblockstore_api->m_BlockExtension, block_hash);

    struct Longtail_StoredBlock* stored_block;
    int err = Longtail_ReadStoredBlock(fsblockstore_api->m_StorageAPI, block_path, &stored_block);
    if (err)
    {
        LONGTAIL_LOG(err == ENOENT ? LONGTAIL_LOG_LEVEL_INFO : LONGTAIL_LOG_LEVEL_WARNING, "FSBlockStore_GetStoredBlock(%p, 0x" PRIx64 ", %p) failed with %d",
            block_store_api, block_hash, async_complete_api,
            err)
        Longtail_AtomicAdd64(&fsblockstore_api->m_StatU64[Longtail_BlockStoreAPI_StatU64_GetStoredBlock_FailCount], 1);
        Longtail_Free((char*)block_path);
        return err;
    }
    Longtail_AtomicAdd64(&fsblockstore_api->m_StatU64[Longtail_BlockStoreAPI_StatU64_GetStoredBlock_Chunk_Count], *stored_block->m_BlockIndex->m_ChunkCount);
    Longtail_AtomicAdd64(&fsblockstore_api->m_StatU64[Longtail_BlockStoreAPI_StatU64_GetStoredBlock_Byte_Count], Longtail_GetBlockIndexDataSize(*stored_block->m_BlockIndex->m_ChunkCount) + stored_block->m_BlockChunksDataSize);

    Longtail_Free(block_path);

    async_complete_api->OnComplete(async_complete_api, stored_block, 0);
    return 0;
}

int FSBlockStore_GetContentIndexFromStorage(
    struct FSBlockStoreAPI* fsblockstore_api,
    struct Longtail_ContentIndex** out_content_index)
{
    LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_DEBUG, "FSBlockStore_GetContentIndexFromStorage(%p, %p)",
        fsblockstore_api, out_content_index)
    struct Longtail_StorageAPI* storage_api = fsblockstore_api->m_StorageAPI;
    struct Longtail_JobAPI* job_api = fsblockstore_api->m_JobAPI;
    const char* content_path = fsblockstore_api->m_ContentPath;
    const char* block_extension = fsblockstore_api->m_BlockExtension;
    uint32_t default_max_block_size = fsblockstore_api->m_DefaultMaxBlockSize;
    uint32_t default_max_chunks_per_block = fsblockstore_api->m_DefaultMaxChunksPerBlock;

    struct Longtail_ContentIndex* content_index = 0;

    int err = EnsureParentPathExists(storage_api, fsblockstore_api->m_ContentIndexLockPath);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "FSBlockStore_GetContentIndexFromStorage(%p, %p) failed with %d",
            fsblockstore_api, out_content_index,
            err)
        return err;
    }
    Longtail_StorageAPI_HLockFile content_index_lock_file;
    err = storage_api->LockFile(storage_api, fsblockstore_api->m_ContentIndexLockPath, &content_index_lock_file);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "FSBlockStore_GetContentIndexFromStorage(%p, %p) failed with %d",
            fsblockstore_api, out_content_index,
            err)
        return err;
    }

    const char* content_index_path = storage_api->ConcatPath(storage_api, content_path, "store.lci");

    if (storage_api->IsFile(storage_api, content_index_path))
    {
        int err = Longtail_ReadContentIndex(storage_api, content_index_path, &content_index);
        if (err)
        {
            LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "FSBlockStore_GetContentIndexFromStorage(%p, %p) failed with %d",
                fsblockstore_api, out_content_index,
                err)
            Longtail_Free((void*)content_index_path);
            storage_api->UnlockFile(storage_api, content_index_lock_file);
            return err;
        }
    }
    storage_api->UnlockFile(storage_api, content_index_lock_file);

    Longtail_Free((void*)content_index_path);
    if (content_index)
    {
        *out_content_index = content_index;
        return 0;
    }
    err = ReadContent(
        storage_api,
        job_api,
        default_max_block_size,
        default_max_chunks_per_block,
        content_path,
        block_extension,
        &content_index);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "FSBlockStore_GetContentIndexFromStorage(%p, %p, `%s`, `%s`, %u, %u, %p) failed with %d",
            storage_api, job_api, content_path, block_extension, default_max_block_size, default_max_chunks_per_block, out_content_index,
            err)
        return err;
    }
    *out_content_index = content_index;
    return 0;
}


static int FSBlockStore_GetIndexSync(
    struct FSBlockStoreAPI* fsblockstore_api,
    struct Longtail_ContentIndex** out_content_index)
{
    LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_DEBUG, "FSBlockStore_GetIndexSync(%p, %p)",
        fsblockstore_api, out_content_index)
    Longtail_LockSpinLock(fsblockstore_api->m_Lock);
    if (!fsblockstore_api->m_ContentIndex)
    {
        struct Longtail_ContentIndex* content_index;
        int err = FSBlockStore_GetContentIndexFromStorage(
            fsblockstore_api,
            &content_index);
        if (err)
        {
            LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "FSBlockStore_GetIndexSync(%p, %p) failed with %d",
                fsblockstore_api, out_content_index,
                err)
            return err;
        }

        if (fsblockstore_api->m_ContentIndex)
        {
            struct Longtail_ContentIndex* merged_content_index;
            err = Longtail_MergeContentIndex(
                fsblockstore_api->m_JobAPI,
                content_index,
                fsblockstore_api->m_ContentIndex,
                &merged_content_index);
            if (err)
            {
                LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "FSBlockStore_GetIndexSync(%p, %p) failed with %d",
                    fsblockstore_api, out_content_index,
                    err)
                Longtail_Free(content_index);
                return err;
            }
            Longtail_Free(content_index);
            Longtail_Free(fsblockstore_api->m_ContentIndex);
            fsblockstore_api->m_ContentIndex = 0;
            content_index = merged_content_index;
        }

        fsblockstore_api->m_ContentIndex = content_index;
        uint64_t block_count = *content_index->m_BlockCount;
        for (uint64_t b = 0; b < block_count; ++b)
        {
            uint64_t block_hash = content_index->m_BlockHashes[b];
            hmput(fsblockstore_api->m_BlockState, block_hash, 1);
        }
    }

    intptr_t new_block_count = arrlen(fsblockstore_api->m_AddedBlockIndexes);
    if (new_block_count > 0)
    {
        struct Longtail_ContentIndex* new_content_index;
        int err = UpdateContentIndex(
            fsblockstore_api->m_ContentIndex,
            fsblockstore_api->m_AddedBlockIndexes,
            &new_content_index);
        if (err)
        {
            LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "FSBlockStore_GetIndexSync(%p, %p) failed with %d",
                fsblockstore_api, out_content_index,
                err)
            Longtail_UnlockSpinLock(fsblockstore_api->m_Lock);
            return err;
        }

        Longtail_Free(fsblockstore_api->m_ContentIndex);
        fsblockstore_api->m_ContentIndex = new_content_index;

        while(new_block_count-- > 0)
        {
            struct Longtail_BlockIndex* block_index = fsblockstore_api->m_AddedBlockIndexes[new_block_count];
            Longtail_Free(block_index);
        }
        arrfree(fsblockstore_api->m_AddedBlockIndexes);
    }

    size_t content_index_size;
    void* tmp_content_buffer;
    int err = Longtail_WriteContentIndexToBuffer(fsblockstore_api->m_ContentIndex, &tmp_content_buffer, &content_index_size);
    Longtail_UnlockSpinLock(fsblockstore_api->m_Lock);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "FSBlockStore_GetIndexSync(%p, %p) failed with %d",
            fsblockstore_api, out_content_index,
            err)
        Longtail_Free(tmp_content_buffer);
        return err;
    }
    struct Longtail_ContentIndex* content_index;
    err = Longtail_ReadContentIndexFromBuffer(tmp_content_buffer, content_index_size, &content_index);
    Longtail_Free(tmp_content_buffer);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "FSBlockStore_GetIndexSync(%p, %p) failed with %d",
            fsblockstore_api, out_content_index,
            err)
        return err;
    }
    *out_content_index = content_index;
    return 0;
}

static int FSBlockStore_RetargetContent(
    struct Longtail_BlockStoreAPI* block_store_api,
    const struct Longtail_ContentIndex* content_index,
    struct Longtail_AsyncRetargetContentAPI* async_complete_api)
{
    LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_DEBUG, "FSBlockStore_RetargetContent(%p, %p, %p)",
        block_store_api, content_index, async_complete_api)
    LONGTAIL_VALIDATE_INPUT(block_store_api, return EINVAL)
    LONGTAIL_VALIDATE_INPUT(content_index, return EINVAL)
    LONGTAIL_VALIDATE_INPUT(async_complete_api, return EINVAL)

    struct FSBlockStoreAPI* fsblockstore_api = (struct FSBlockStoreAPI*)block_store_api;
    Longtail_AtomicAdd64(&fsblockstore_api->m_StatU64[Longtail_BlockStoreAPI_StatU64_RetargetContent_Count], 1);
    struct Longtail_ContentIndex* store_content_index;
    int err = FSBlockStore_GetIndexSync(fsblockstore_api, &store_content_index);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "FSBlockStore_RetargetContent(%p, %p, %p) failed with %d",
            block_store_api, content_index, async_complete_api,
            err)
        Longtail_AtomicAdd64(&fsblockstore_api->m_StatU64[Longtail_BlockStoreAPI_StatU64_RetargetContent_FailCount], 1);
        return err;
    }
    struct Longtail_ContentIndex* retargeted_content_index;
    err = Longtail_RetargetContent(store_content_index, content_index, &retargeted_content_index);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "FSBlockStore_RetargetContent(%p, %p, %p) failed with %d",
            block_store_api, content_index, async_complete_api,
            err)
        Longtail_AtomicAdd64(&fsblockstore_api->m_StatU64[Longtail_BlockStoreAPI_StatU64_RetargetContent_FailCount], 1);
        Longtail_Free(store_content_index);
        return err;
    }
    Longtail_Free(store_content_index);

    async_complete_api->OnComplete(async_complete_api, retargeted_content_index, 0);
    return 0;
}

static int FSBlockStore_GetStats(struct Longtail_BlockStoreAPI* block_store_api, struct Longtail_BlockStore_Stats* out_stats)
{
    LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_DEBUG, "FSBlockStore_GetStats(%p, %p)", block_store_api, out_stats)
    LONGTAIL_VALIDATE_INPUT(block_store_api, return EINVAL)
    LONGTAIL_VALIDATE_INPUT(out_stats, return EINVAL)
    struct FSBlockStoreAPI* fsblockstore_api = (struct FSBlockStoreAPI*)block_store_api;
    Longtail_AtomicAdd64(&fsblockstore_api->m_StatU64[Longtail_BlockStoreAPI_StatU64_GetStats_Count], 1);
    memset(out_stats, 0, sizeof(struct Longtail_BlockStore_Stats));
    for (uint32_t s = 0; s < Longtail_BlockStoreAPI_StatU64_Count; ++s)
    {
        out_stats->m_StatU64[s] = fsblockstore_api->m_StatU64[s];
    }
    return 0;
}

static int FSBlockStore_Flush(struct Longtail_BlockStoreAPI* block_store_api, struct Longtail_AsyncFlushAPI* async_complete_api)
{
    struct FSBlockStoreAPI* api = (struct FSBlockStoreAPI*)block_store_api;
    Longtail_AtomicAdd64(&api->m_StatU64[Longtail_BlockStoreAPI_StatU64_Flush_Count], 1);

    Longtail_LockSpinLock(api->m_Lock);

    int err = 0;

    intptr_t new_block_count = arrlen(api->m_AddedBlockIndexes);
    if (new_block_count > 0)
    {
        if (api->m_ContentIndex)
        {
            struct Longtail_ContentIndex* new_content_index;
            err = UpdateContentIndex(
                api->m_ContentIndex,
                api->m_AddedBlockIndexes,
                &new_content_index);
            if (err)
            {
                LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "FSBlockStore_Flush(%p, %p) failed with %d",
                    block_store_api, async_complete_api,
                    err)
            }
            else
            {
                Longtail_Free(api->m_ContentIndex);
                api->m_ContentIndex = new_content_index;
            }
        }
        else
        {
            err = Longtail_CreateContentIndexFromBlocks(
                api->m_DefaultMaxBlockSize,
                api->m_DefaultMaxChunksPerBlock,
                (uint64_t)(arrlen(api->m_AddedBlockIndexes)),
                api->m_AddedBlockIndexes,
                &api->m_ContentIndex);
            if (err)
            {
                LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "FSBlockStore_Flush(%p, %p) failed with %d",
                    block_store_api, async_complete_api,
                    err)
            }
        }
        intptr_t free_block_index = new_block_count;
        while(free_block_index-- > 0)
        {
            struct Longtail_BlockIndex* block_index = api->m_AddedBlockIndexes[free_block_index];
            Longtail_Free(block_index);
        }
    }
    arrfree(api->m_AddedBlockIndexes);

    if (api->m_ContentIndex)
    {
        int err = EnsureParentPathExists(api->m_StorageAPI, api->m_ContentIndexLockPath);
        if (err)
        {
            LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "FSBlockStore_Flush(%p, %p) failed with %d",
                block_store_api, async_complete_api,
                err)
        }
        else
        {
            const char* content_index_path = api->m_StorageAPI->ConcatPath(api->m_StorageAPI, api->m_ContentPath, "store.lci");
            Longtail_StorageAPI_HLockFile content_index_lock_file;
            int err = api->m_StorageAPI->LockFile(api->m_StorageAPI, api->m_ContentIndexLockPath, &content_index_lock_file);
            if (!err)
            {
                if (new_block_count > 0 || (!api->m_StorageAPI->IsFile(api->m_StorageAPI, content_index_path)))
                {
                    err = SafeWriteContentIndex(api);
                    if (err)
                    {
                        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_WARNING, "Failed to store content index for `%s`, %d", api->m_ContentPath, err);
                    }
                }
                api->m_StorageAPI->UnlockFile(api->m_StorageAPI, content_index_lock_file);
            }
            Longtail_Free((void*)content_index_path);
        }
    }

    Longtail_UnlockSpinLock(api->m_Lock);

    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "FSBlockStore_Flush(%p, %p) failed with %d",
            block_store_api, async_complete_api,
            err)
        Longtail_AtomicAdd64(&api->m_StatU64[Longtail_BlockStoreAPI_StatU64_Flush_FailCount], 1);
    }

    if (async_complete_api)
    {
        async_complete_api->OnComplete(async_complete_api, err);
        return 0;
    }
    return err;
}

static void FSBlockStore_Dispose(struct Longtail_API* api)
{
    LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_DEBUG, "FSBlockStore_Dispose(%p)", api)
    LONGTAIL_FATAL_ASSERT(api, return)
    struct FSBlockStoreAPI* fsblockstore_api = (struct FSBlockStoreAPI*)api;

    int err = FSBlockStore_Flush(&fsblockstore_api->m_BlockStoreAPI, 0);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_WARNING, "FSBlockStore_Flush failed for `%s`, %d", fsblockstore_api->m_ContentPath, err);
    }

    hmfree(fsblockstore_api->m_BlockState);
    fsblockstore_api->m_BlockState = 0;
    Longtail_DeleteSpinLock(fsblockstore_api->m_Lock);
    Longtail_Free(fsblockstore_api->m_Lock);
    Longtail_Free((void*)fsblockstore_api->m_ContentIndexLockPath);
    Longtail_Free(fsblockstore_api->m_ContentPath);
    Longtail_Free(fsblockstore_api->m_ContentIndex);
    Longtail_Free(fsblockstore_api);
}

static int FSBlockStore_Init(
    void* mem,
    struct Longtail_JobAPI* job_api,
    struct Longtail_StorageAPI* storage_api,
    const char* content_path,
    uint32_t default_max_block_size,
    uint32_t default_max_chunks_per_block,
    const char* optional_extension,
    uint64_t unique_id,
    struct Longtail_BlockStoreAPI** out_block_store_api)
{
    LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_DEBUG, "FSBlockStore_Init(%p, %p, %s, %u, %u, %p)",
        mem, storage_api, content_path, default_max_block_size, default_max_chunks_per_block, out_block_store_api)
    LONGTAIL_FATAL_ASSERT(mem, return EINVAL)
    LONGTAIL_FATAL_ASSERT(storage_api, return EINVAL)
    LONGTAIL_FATAL_ASSERT(content_path, return EINVAL)
    LONGTAIL_FATAL_ASSERT(out_block_store_api, return EINVAL)

    struct Longtail_BlockStoreAPI* block_store_api = Longtail_MakeBlockStoreAPI(
        mem,
        FSBlockStore_Dispose,
        FSBlockStore_PutStoredBlock,
        FSBlockStore_PreflightGet,
        FSBlockStore_GetStoredBlock,
        FSBlockStore_RetargetContent,
        FSBlockStore_GetStats,
        FSBlockStore_Flush);
    if (!block_store_api)
    {
        return EINVAL;
    }

    struct FSBlockStoreAPI* api = (struct FSBlockStoreAPI*)block_store_api;

    api->m_JobAPI = job_api;
    api->m_StorageAPI = storage_api;
    api->m_ContentPath = Longtail_Strdup(content_path);
    api->m_ContentIndex = 0;
    api->m_BlockState = 0;
    api->m_AddedBlockIndexes = 0;
    api->m_BlockExtension = optional_extension ? optional_extension : ".lrb";
    api->m_ContentIndexLockPath = storage_api->ConcatPath(storage_api, content_path, "store.lci.sync");

    GetUniqueExtension(unique_id, api->m_TmpExtension);
    api->m_DefaultMaxBlockSize = default_max_block_size;
    api->m_DefaultMaxChunksPerBlock = default_max_chunks_per_block;

    for (uint32_t s = 0; s < Longtail_BlockStoreAPI_StatU64_Count; ++s)
    {
        api->m_StatU64[s] = 0;
    }

    int err = Longtail_CreateSpinLock(Longtail_Alloc(Longtail_GetSpinLockSize()), &api->m_Lock);
    if (err)
    {
        hmfree(api->m_BlockState);
        api->m_BlockState = 0;
        Longtail_Free(api->m_ContentIndex);
        api->m_ContentIndex = 0;
        return err;
    }
    *out_block_store_api = block_store_api;
    return 0;
}

struct Longtail_BlockStoreAPI* Longtail_CreateFSBlockStoreAPI(
    struct Longtail_JobAPI* job_api,
    struct Longtail_StorageAPI* storage_api,
    const char* content_path,
    uint32_t default_max_block_size,
    uint32_t default_max_chunks_per_block,
    const char* optional_extension)
{
    LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_INFO, "Longtail_CreateFSBlockStoreAPI(%p, %s, %u, %u)",
        storage_api, content_path, default_max_block_size, default_max_chunks_per_block)
    LONGTAIL_VALIDATE_INPUT(storage_api != 0, return 0)
    LONGTAIL_VALIDATE_INPUT(content_path != 0, return 0)
    LONGTAIL_VALIDATE_INPUT(default_max_block_size != 0, return 0)
    LONGTAIL_VALIDATE_INPUT(default_max_chunks_per_block != 0, return 0)
    LONGTAIL_VALIDATE_INPUT(optional_extension == 0 || strlen(optional_extension) < 15, return 0)
    size_t api_size = sizeof(struct FSBlockStoreAPI);
    void* mem = Longtail_Alloc(api_size);
    if (!mem)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "Longtail_CreateFSBlockStoreAPI(%p, %s, %u, %u) failed with %d",
            storage_api, content_path, default_max_block_size, default_max_chunks_per_block,
            ENOMEM)
        return 0;
    }
    uint64_t computer_id = Longtail_GetProcessIdentity();
    uintptr_t instance_id = (uintptr_t)mem;
    uint64_t unique_id = computer_id ^ instance_id;

    struct Longtail_BlockStoreAPI* block_store_api;
    int err = FSBlockStore_Init(
        mem,
        job_api,
        storage_api,
        content_path,
        default_max_block_size,
        default_max_chunks_per_block,
        optional_extension,
        unique_id,
        &block_store_api);
    if (err)
    {
        LONGTAIL_LOG(LONGTAIL_LOG_LEVEL_ERROR, "Longtail_CreateFSBlockStoreAPI(%p, %s, %u, %u) failed with %d",
            storage_api, content_path, default_max_block_size, default_max_chunks_per_block,
            err)
        Longtail_Free(mem);
        return 0;
    }
    return block_store_api;
}
