#include "../third-party/jctest/src/jc_test.h"
#include "../third-party/meow_hash/meow_hash_x64_aesni.h"

#define LONGTAIL_IMPLEMENTATION
#include "../src/longtail.h"
#define BIKESHED_IMPLEMENTATION
#include "../third-party/bikeshed/bikeshed.h"
#include "../third-party/nadir/src/nadir.h"
#include "../third-party/jc_containers/src/jc_hashtable.h"
#include "../third-party/trove/src/trove.h"

#include "../third-party/lizard/lib/lizard_common.h"
#include "../third-party/lizard/lib/lizard_decompress.h"
#include "../third-party/lizard/lib/lizard_compress.h"

#include <inttypes.h>

#include <algorithm>

#if defined(_WIN32)
    #include <malloc.h>
    #define alloca _alloca
#else
    #include <alloca.h>
#endif

#if defined(_WIN32)

void Trove_NormalizePath(char* path)
{
    while (*path)
    {
        *path++ = *path == '\\' ? '/' : *path;
    }
}

void Trove_DenormalizePath(char* path)
{
    while (*path)
    {
        *path++ = *path == '/' ? '\\' : *path;
    }
}

#endif

#if defined(__APPLE__) || defined(__linux__)

void Trove_NormalizePath(char* )
{

}

void Trove_DenormalizePath(char* )
{

}

#endif

namespace Jobs
{

struct ReadyCallback
{
    Bikeshed_ReadyCallback cb = {Ready};
    ReadyCallback()
    {
        m_Semaphore = nadir::CreateSema(malloc(nadir::GetSemaSize()), 0);
    }
    ~ReadyCallback()
    {
        nadir::DeleteSema(m_Semaphore);
        free(m_Semaphore);
    }
    static void Ready(struct Bikeshed_ReadyCallback* ready_callback, uint8_t channel, uint32_t ready_count)
    {
        ReadyCallback* cb = (ReadyCallback*)ready_callback;
        nadir::PostSema(cb->m_Semaphore, ready_count);
    }
    static void Wait(ReadyCallback* cb)
    {
        nadir::WaitSema(cb->m_Semaphore);
    }
    nadir::HSema m_Semaphore;
};

struct ThreadWorker
{
    ThreadWorker()
        : stop(0)
        , shed(0)
        , semaphore(0)
        , thread(0)
    {
    }

    ~ThreadWorker()
    {
    }

    bool CreateThread(Bikeshed in_shed, nadir::HSema in_semaphore, nadir::TAtomic32* in_stop)
    {
        shed               = in_shed;
        stop               = in_stop;
        semaphore          = in_semaphore;
        thread             = nadir::CreateThread(malloc(nadir::GetThreadSize()), ThreadWorker::Execute, 0, this);
        return thread != 0;
    }

    void JoinThread()
    {
        nadir::JoinThread(thread, nadir::TIMEOUT_INFINITE);
    }

    void DisposeThread()
    {
        nadir::DeleteThread(thread);
        free(thread);
    }

    static int32_t Execute(void* context)
    {
        ThreadWorker* _this = reinterpret_cast<ThreadWorker*>(context);

        while (*_this->stop == 0)
        {
            if (!Bikeshed_ExecuteOne(_this->shed, 0))
            {
                nadir::WaitSema(_this->semaphore);
            }
        }
        return 0;
    }

    nadir::TAtomic32*   stop;
    Bikeshed            shed;
    nadir::HSema        semaphore;
    nadir::HThread      thread;
};

}







struct AssetFolder
{
    const char* m_FolderPath;
};

LONGTAIL_DECLARE_ARRAY_TYPE(AssetFolder, malloc, free)

typedef void (*ProcessEntry)(void* context, const char* root_path, const char* file_name);

static int RecurseTree(const char* root_folder, ProcessEntry entry_processor, void* context)
{
    AssetFolder* asset_folders = SetCapacity_AssetFolder((AssetFolder*)0, 256);

    uint32_t folder_index = 0;

    Push_AssetFolder(asset_folders)->m_FolderPath = strdup(root_folder);

    HTrove_FSIterator fs_iterator = (HTrove_FSIterator)alloca(Trove_GetFSIteratorSize());
    while (folder_index != GetSize_AssetFolder(asset_folders))
    {
        const char* asset_folder = asset_folders[folder_index++].m_FolderPath;

        if (Trove_StartFind(fs_iterator, asset_folder))
        {
            do
            {
                if (const char* dir_name = Trove_GetDirectoryName(fs_iterator))
                {
                    Push_AssetFolder(asset_folders)->m_FolderPath = Trove_ConcatPath(asset_folder, dir_name);
                    if (GetSize_AssetFolder(asset_folders) == GetCapacity_AssetFolder(asset_folders))
                    {
                        uint32_t unprocessed_count = (GetSize_AssetFolder(asset_folders) - folder_index);
                        if (folder_index > 0)
                        {
                            if (unprocessed_count > 0)
                            {
                                memmove(asset_folders, &asset_folders[folder_index], sizeof(AssetFolder) * unprocessed_count);
                                SetSize_AssetFolder(asset_folders, unprocessed_count);
                            }
                            folder_index = 0;
                        }
                        else
                        {
                            AssetFolder* asset_folders_new = SetCapacity_AssetFolder((AssetFolder*)0, GetCapacity_AssetFolder(asset_folders) + 256);
                            if (unprocessed_count > 0)
                            {
                                SetSize_AssetFolder(asset_folders_new, unprocessed_count);
                                memcpy(asset_folders_new, &asset_folders[folder_index], sizeof(AssetFolder) * unprocessed_count);
                            }
                            Free_AssetFolder(asset_folders);
                            asset_folders = asset_folders_new;
                            folder_index = 0;
                        }
                    }
                }
                else if(const char* file_name = Trove_GetFileName(fs_iterator))
                {
                    entry_processor(context, asset_folder, file_name);
                }
            }while(Trove_FindNext(fs_iterator));
            Trove_CloseFind(fs_iterator);
        }
        free((void*)asset_folder);
    }
    Free_AssetFolder(asset_folders);
    return 1;
}

struct AssetPaths
{
    char** paths;
    uint32_t count;
};

void FreeAssetpaths(AssetPaths* assets)
{
    for (uint32_t i = 0; i < assets->count; ++i)
    {
        free(assets->paths[i]);
    }

    free(assets->paths);
}

int GetContent(const char* root_path, AssetPaths* out_assets)
{
    out_assets->paths = 0;
    out_assets->count = 0;

    struct Context {
        const char* root_path;
        AssetPaths* assets;
    };

    auto add_folder = [](void* context, const char* root_path, const char* file_name)
    {
        Context* asset_context = (Context*)context;
        AssetPaths* assets = asset_context->assets;

        size_t base_path_length = strlen(asset_context->root_path);

        char* full_path = (char*)Trove_ConcatPath(root_path, file_name);
        Trove_NormalizePath(full_path);
        const char* s = &full_path[base_path_length];
        if (*s == '/')
        {
            ++s;
        }

        assets->paths = (char**)realloc(assets->paths, sizeof(char*) * (assets->count + 1));
        assets->paths[assets->count] = strdup(s);

        free(full_path);

		++assets->count;
    };

    Context context = { root_path, out_assets };

    RecurseTree(root_path, add_folder, &context);

    return 0;
}

struct AssetHashInfo
{
    uint64_t m_ContentSize;
    TLongtail_Hash m_PathHash;
    TLongtail_Hash m_ContentHash;
};

struct HashJob
{
    AssetHashInfo* m_AssetHashInfo;
    const char* m_RootPath;
    const char* m_Path;
    nadir::TAtomic32* m_PendingCount;
};

static TLongtail_Hash GetPathHash(const char* path)
{
    meow_state state;
    MeowBegin(&state, MeowDefaultSeed);
    MeowAbsorb(&state, strlen(path), (void*)path);
    TLongtail_Hash path_hash = MeowU64From(MeowEnd(&state, 0), 0);
    return path_hash;
}

static Bikeshed_TaskResult HashFile(Bikeshed shed, Bikeshed_TaskID, uint8_t, void* context)
{
    HashJob* hash_job = (HashJob*)context;
    char* path = (char*)Trove_ConcatPath(hash_job->m_RootPath, hash_job->m_Path);
    Trove_DenormalizePath(path);

    HTroveOpenReadFile file_handle = Trove_OpenReadFile(path);
    meow_state state;
    MeowBegin(&state, MeowDefaultSeed);
    if(file_handle)
    {
        uint64_t file_size = Trove_GetFileSize(file_handle);
        hash_job->m_AssetHashInfo->m_ContentSize = file_size;

        uint8_t batch_data[65536];
        uint64_t offset = 0;
        while (offset != file_size)
        {
            meow_umm len = (meow_umm)((file_size - offset) < sizeof(batch_data) ? (file_size - offset) : sizeof(batch_data));
            bool read_ok = Trove_Read(file_handle, offset, len, batch_data);
            assert(read_ok);
            offset += len;
            MeowAbsorb(&state, len, batch_data);
        }
        Trove_CloseReadFile(file_handle);
    }
    meow_u128 hash = MeowEnd(&state, 0);
    hash_job->m_AssetHashInfo->m_ContentHash = MeowU64From(hash, 0);
    hash_job->m_AssetHashInfo->m_PathHash = GetPathHash(hash_job->m_Path);
    nadir::AtomicAdd32(hash_job->m_PendingCount, -1);
    free((char*)path);
    return BIKESHED_TASK_RESULT_COMPLETE;
}

struct ProcessHashContext
{
    Bikeshed m_Shed;
    HashJob* m_HashJobs;
    nadir::TAtomic32* m_PendingCount;
};

void GetFileHashes(Bikeshed shed, const char* root_path, const char** paths, uint64_t asset_count, AssetHashInfo* out_asset_hash_info)
{
    ProcessHashContext context;
    context.m_Shed = shed;
    context.m_HashJobs = new HashJob[asset_count];
    nadir::TAtomic32 pendingCount = 0;
    context.m_PendingCount = &pendingCount;

    uint64_t assets_left = asset_count;
    static const uint32_t BATCH_SIZE = 64;
    Bikeshed_TaskID task_ids[BATCH_SIZE];
    BikeShed_TaskFunc func[BATCH_SIZE];
    void* ctx[BATCH_SIZE];
    for (uint32_t i = 0; i < BATCH_SIZE; ++i)
    {
        func[i] = HashFile;
    }
    uint64_t offset = 0;
    while (offset < asset_count) {
        uint64_t assets_left = asset_count - offset;
        uint32_t batch_count = assets_left > BATCH_SIZE ? BATCH_SIZE : (uint32_t)assets_left;
        for (uint32_t i = 0; i < batch_count; ++i)
        {
            HashJob* job = &context.m_HashJobs[offset + i];
			ctx[i] = &context.m_HashJobs[i + offset];
			job->m_RootPath = root_path;
            job->m_Path = paths[i + offset];
            job->m_PendingCount = &pendingCount;
            job->m_AssetHashInfo = &out_asset_hash_info[i + offset];
            job->m_AssetHashInfo->m_ContentSize = 0;
        }

        while (!Bikeshed_CreateTasks(shed, batch_count, func, ctx, task_ids))
        {
            nadir::Sleep(1000);
        }

        {
            nadir::AtomicAdd32(&pendingCount, batch_count);
            Bikeshed_ReadyTasks(shed, batch_count, task_ids);
        }
        offset += batch_count;
    }

    int32_t old_pending_count = 0;
    while (pendingCount > 0)
    {
        if (Bikeshed_ExecuteOne(shed, 0))
        {
            continue;
        }
        if (old_pending_count != pendingCount)
        {
            old_pending_count = pendingCount;
            printf("Files left to hash: %d\n", old_pending_count);
        }
        nadir::Sleep(1000);
    }

    delete [] context.m_HashJobs;
}

struct VersionIndex
{
    uint64_t m_AssetCount;
    TLongtail_Hash* m_PathHash;
    TLongtail_Hash* m_AssetHash;
    uint32_t* m_NameOffset;
    char* m_NameData;
};

size_t GetVersionIndexSize(uint32_t asset_count, char* const* asset_paths)
{
    uint64_t name_data_size = 0;
    for (uint32_t i = 0; i < asset_count; ++i)
    {
        name_data_size += strlen(asset_paths[i]) + 1;
    }

    size_t version_index_size = sizeof(uint64_t) +
        sizeof(VersionIndex) +
        (sizeof(TLongtail_Hash) * asset_count) +
        (sizeof(TLongtail_Hash) * asset_count) +
        (sizeof(uint64_t) * asset_count) +
        name_data_size;

    return version_index_size;
}

void InitVersionIndex(VersionIndex* version_index)
{
    uint64_t asset_count = version_index->m_AssetCount;

    char* p = (char*)version_index;
    p += sizeof(VersionIndex);

    version_index->m_PathHash = (TLongtail_Hash*)p;
    p += (sizeof(TLongtail_Hash) * asset_count);

    version_index->m_AssetHash = (TLongtail_Hash*)p;
    p += (sizeof(TLongtail_Hash) * asset_count);

    version_index->m_NameOffset = (uint32_t*)p;
    p += (sizeof(uint32_t) * asset_count);

    version_index->m_NameData = (char*)p;
}

VersionIndex* BuildVersionIndex(void* mem, uint32_t asset_count, char* const* asset_paths, const AssetHashInfo* asset_hash_info)
{
    VersionIndex* version_index = (VersionIndex*)mem;
    version_index->m_AssetCount = asset_count;
    InitVersionIndex(version_index);

    uint32_t name_offset = 0;
    for (uint32_t i = 0; i < asset_count; ++i)
    {
        version_index->m_PathHash[i] = asset_hash_info[i].m_PathHash;
        version_index->m_AssetHash[i] = asset_hash_info[i].m_ContentHash;
        version_index->m_NameOffset[i] = name_offset;
        uint32_t path_length = (uint32_t)strlen(asset_paths[i]) + 1;
        memcpy(&version_index->m_NameData[name_offset], asset_paths[i], path_length);
        name_offset += path_length;
    }
    return version_index;
}

struct ContentIndex
{
    uint64_t m_AssetCount;
    TLongtail_Hash* m_AssetHash;
    TLongtail_Hash* m_BlockHash;
    uint64_t* m_BlockOffset;
    uint64_t* m_AssetSize;
};

TEST(Longtail, ScanContent)
{
    Jobs::ReadyCallback ready_callback;
    Bikeshed shed = Bikeshed_Create(malloc(BIKESHED_SIZE(65536, 0, 1)), 65536, 0, 1, &ready_callback.cb);

    nadir::TAtomic32 stop = 0;

    static const uint32_t WORKER_COUNT = 7;
    Jobs::ThreadWorker workers[WORKER_COUNT];
    for (uint32_t i = 0; i < WORKER_COUNT; ++i)
    {
        workers[i].CreateThread(shed, ready_callback.m_Semaphore, &stop);
    }


//    const char* root_path = "D:\\TestContent\\Source\\gitc352a449064be52c8965fe28856bd914b0bbae0c";
    const char* root_path = "D:\\TestContent\\Source\\gita1ffa22e55b2278eae44b15535dc143c41342d5c";

//    const char* root_path = "/Users/danengelbrecht/Documents/Projects/blossom_blast_saga/build/default";
//    const char* cache_path = "D:\\Temp\\longtail\\cache";
//    const char* cache_path = "/Users/danengelbrecht/tmp/cache";
    AssetPaths asset_paths;

    GetContent(root_path, &asset_paths);

    AssetHashInfo* asset_hash_infos = new AssetHashInfo[asset_paths.count];

    GetFileHashes(shed, root_path, (const char**)asset_paths.paths, asset_paths.count, asset_hash_infos);

    size_t version_index_size = GetVersionIndexSize(asset_paths.count, asset_paths.paths);
    void* version_index_mem = malloc(version_index_size);
    printf("Asset count: %" PRIu64 ", Version index size: %" PRIu64, (uint64_t)asset_paths.count, (uint64_t)version_index_size);

    VersionIndex* version_index = BuildVersionIndex(version_index_mem, asset_paths.count, asset_paths.paths, asset_hash_infos);

    delete [] asset_hash_infos;
    FreeAssetpaths(&asset_paths);

    {
        HTroveOpenWriteFile file_handle = Trove_OpenWriteFile("D:\\Temp\\VersionIndex.lvi");
        Trove_Write(file_handle, 0, version_index_size, version_index);
        Trove_CloseWriteFile(file_handle);
    }
    free(version_index);

    {
        HTroveOpenReadFile file_handle = Trove_OpenReadFile("D:\\Temp\\VersionIndex.lvi");
        version_index_size = Trove_GetFileSize(file_handle);
        version_index = (VersionIndex*)malloc(version_index_size);
        Trove_Read(file_handle, 0, version_index_size, version_index);
        InitVersionIndex(version_index);
        Trove_CloseReadFile(file_handle);
    }

    if (1)
    {
        for (uint64_t i = 0; i < version_index->m_AssetCount; ++i)
        {
            const char* asset_path  = &version_index->m_NameData[version_index->m_NameOffset[i]];

            char path_hash_str[64];
            sprintf(path_hash_str, "0x%" PRIx64, version_index->m_AssetHash[i]);
            char content_hash_str[64];
            sprintf(content_hash_str, "0x%" PRIx64, version_index->m_AssetHash[i]);

            printf("%s (%s) = %s\n", asset_path, path_hash_str, content_hash_str);
        }
    }

    free(version_index);
}





#if 0
struct TestStorage
{
    Longtail_ReadStorage m_Storge = {/*PreflightBlocks, */AqcuireBlockStorage, ReleaseBlock};
//    static uint64_t PreflightBlocks(struct Longtail_ReadStorage* storage, uint32_t block_count, struct Longtail_BlockStore* blocks)
//    {
//        TestStorage* test_storage = (TestStorage*)storage;
//        return 0;
//    }
    static const uint8_t* AqcuireBlockStorage(struct Longtail_ReadStorage* storage, uint32_t block_index)
    {
        TestStorage* test_storage = (TestStorage*)storage;
        return 0;
    }
    static void ReleaseBlock(struct Longtail_ReadStorage* storage, uint32_t block_index)
    {
        TestStorage* test_storage = (TestStorage*)storage;
    }
};

struct AssetFolder
{
    const char* m_FolderPath;
};

LONGTAIL_DECLARE_ARRAY_TYPE(AssetFolder, malloc, free)

typedef void (*ProcessEntry)(void* context, const char* root_path, const char* file_name);

static int RecurseTree(const char* root_folder, ProcessEntry entry_processor, void* context)
{
    AssetFolder* asset_folders = SetCapacity_AssetFolder((AssetFolder*)0, 256);

    uint32_t folder_index = 0;

    Push_AssetFolder(asset_folders)->m_FolderPath = strdup(root_folder);

    HTrove_FSIterator fs_iterator = (HTrove_FSIterator)alloca(Trove_GetFSIteratorSize());
    while (folder_index != GetSize_AssetFolder(asset_folders))
    {
        const char* asset_folder = asset_folders[folder_index++].m_FolderPath;

        if (Trove_StartFind(fs_iterator, asset_folder))
        {
            do
            {
                if (const char* dir_name = Trove_GetDirectoryName(fs_iterator))
                {
                    Push_AssetFolder(asset_folders)->m_FolderPath = Trove_ConcatPath(asset_folder, dir_name);
                    if (GetSize_AssetFolder(asset_folders) == GetCapacity_AssetFolder(asset_folders))
                    {
                        uint32_t unprocessed_count = (GetSize_AssetFolder(asset_folders) - folder_index);
                        if (folder_index > 0)
                        {
                            if (unprocessed_count > 0)
                            {
                                memmove(asset_folders, &asset_folders[folder_index], sizeof(AssetFolder) * unprocessed_count);
                                SetSize_AssetFolder(asset_folders, unprocessed_count);
                            }
                            folder_index = 0;
                        }
                        else
                        {
                            AssetFolder* asset_folders_new = SetCapacity_AssetFolder((AssetFolder*)0, GetCapacity_AssetFolder(asset_folders) + 256);
                            if (unprocessed_count > 0)
                            {
                                SetSize_AssetFolder(asset_folders_new, unprocessed_count);
                                memcpy(asset_folders_new, &asset_folders[folder_index], sizeof(AssetFolder) * unprocessed_count);
                            }
                            Free_AssetFolder(asset_folders);
                            asset_folders = asset_folders_new;
                            folder_index = 0;
                        }
                    }
                }
                else if(const char* file_name = Trove_GetFileName(fs_iterator))
                {
                    entry_processor(context, asset_folder, file_name);
                }
            }while(Trove_FindNext(fs_iterator));
            Trove_CloseFind(fs_iterator);
        }
        free((void*)asset_folder);
    }
    Free_AssetFolder(asset_folders);
    return 1;
}

TEST(Longtail, Basic)
{
    TestStorage storage;
}

struct Longtail_PathEntry
{
    TLongtail_Hash m_PathHash;
    uint32_t m_PathOffset;
    uint32_t m_ParentIndex;
    uint32_t m_ChildCount;  // (uint32_t)-1 for files
};

struct Longtail_StringCacheEntry
{
	TLongtail_Hash m_StringHash;
	uint32_t m_StringOffset;
};

struct Longtail_StringCache
{
	Longtail_StringCacheEntry* m_Entry;
	char* m_DataBuffer;
	uint32_t m_EntryCount;
	uint32_t m_DataBufferSize;
};

uint32_t AddString(Longtail_StringCache* cache, const char* string, uint32_t string_length, TLongtail_Hash string_hash)
{
	for (uint32_t i = 0; i < cache->m_EntryCount; ++i)
	{
		if (cache->m_Entry->m_StringHash == string_hash)
		{
			return cache->m_Entry->m_StringOffset;
		}
	}
	uint32_t string_offset = cache->m_DataBufferSize;
	cache->m_Entry = (Longtail_StringCacheEntry*)realloc(cache->m_Entry, sizeof(Longtail_StringCacheEntry) * (cache->m_EntryCount + 1));
	cache->m_Entry[cache->m_EntryCount].m_StringHash = string_hash;
	cache->m_DataBuffer = (char*)realloc(cache->m_DataBuffer, cache->m_DataBufferSize + string_length + 1);
	memcpy(&cache->m_DataBuffer[cache->m_DataBufferSize], string, string_length);
	cache->m_DataBuffer[cache->m_DataBufferSize + string_length] = '\0';
	cache->m_DataBufferSize += (string_length + 1);
	cache->m_EntryCount += 1;
	return string_offset;
}

struct Longtail_Paths
{
    Longtail_PathEntry* m_PathEntries;
    char* m_PathStorage;
};
/*
uint32_t find_path_offset(const Longtail_Paths& paths, const TLongtail_Hash* sub_path_hashes, uint32_t path_count, TLongtail_Hash sub_path_hash)
{
	uint32_t path_index = 0;
	while (path_index < path_count)
	{
		if (sub_path_hashes[path_index] == sub_path_hash)
		{
			return paths.m_PathEntries[path_index].m_PathOffset;
		}
		++path_index;
	}
	return path_count;
};
*/
TEST(Longtail, PathIndex)
{
	Longtail_StringCache string_cache;
	memset(&string_cache, 0, sizeof(string_cache));

	uint32_t path_count = 0;
	uint32_t path_data_size = 0;
	Longtail_Paths paths;
	paths.m_PathEntries = 0;
    paths.m_PathStorage = (char*)0;
	TLongtail_Hash* sub_path_hashes = 0;

    auto find_path_index = [](const Longtail_Paths& paths, TLongtail_Hash* sub_path_hashes, uint32_t path_count, uint32_t parent_path_index, TLongtail_Hash sub_path_hash)
    {
        uint32_t path_index = 0;
        while (path_index < path_count)
        {
            if (sub_path_hashes[path_index] == sub_path_hash && paths.m_PathEntries[path_index].m_ParentIndex == parent_path_index)
            {
                return path_index;
            }
            ++path_index;
        }
        return path_count;
    };

	auto get_sub_path_hash = [](const char* path_begin, const char* path_end)
    {
        meow_state state;
        MeowBegin(&state, MeowDefaultSeed);
        MeowAbsorb(&state, (meow_umm)(path_end - path_begin), (void*)path_begin);
        uint64_t path_hash = MeowU64From(MeowEnd(&state, 0), 0);
        return path_hash;
    };

    auto find_sub_path_end = [](const char* path)
    {
        while (*path && *path != '/')
        {
            ++path;
        }
        return path;
    };

    auto add_sub_path = [](Longtail_Paths& paths, uint32_t path_count, uint32_t parent_path_index, bool is_directory, TLongtail_Hash path_hash, Longtail_StringCache* string_cache, TLongtail_Hash*& sub_path_hashes, const char* sub_path, uint32_t sub_path_length, TLongtail_Hash sub_path_hash)
    {
		sub_path_hashes = (TLongtail_Hash*)realloc(sub_path_hashes, sizeof(TLongtail_Hash) * (path_count + 1));
		sub_path_hashes[path_count] = sub_path_hash;
		paths.m_PathEntries = (Longtail_PathEntry*)realloc(paths.m_PathEntries, sizeof(Longtail_PathEntry) * (path_count + 1));
        paths.m_PathEntries[path_count].m_PathHash = path_hash;
		paths.m_PathEntries[path_count].m_ParentIndex = parent_path_index;
		paths.m_PathEntries[path_count].m_PathOffset = AddString(string_cache, sub_path, sub_path_length, sub_path_hash);
        paths.m_PathEntries[path_count].m_ChildCount = is_directory ? 0u : (uint32_t)-1;
        if (parent_path_index != (uint32_t)-1)
        {
    		paths.m_PathEntries[parent_path_index].m_ChildCount++;
        }
        return path_count;
    };

    const char* root_path = "D:\\TestContent\\Version_1";
//    const char* root_path = "/Users/danengelbrecht/Documents/Projects/blossom_blast_saga/build/default";
    const char* cache_path = "D:\\Temp\\longtail\\cache";
//    const char* cache_path = "/Users/danengelbrecht/tmp/cache";

    struct Folders
    {
        char** folder_names;
        uint32_t folder_count;
    };

    Folders folders;
    folders.folder_names = 0;
    folders.folder_count = 0;

    auto add_folder = [](void* context, const char* root_path, const char* file_name)
    {
        Folders* folders = (Folders*)context;
        uint32_t folder_count = 0;
        folders->folder_names = (char**)realloc(folders->folder_names, sizeof(char*) * (folders->folder_count + 1));
        folders->folder_names[folders->folder_count] = (char*)malloc(strlen(root_path) + 1 + strlen(file_name) + 1);
        strcpy(folders->folder_names[folders->folder_count], root_path);
        strcpy(&folders->folder_names[folders->folder_count][strlen(root_path)], "/");
        strcpy(&folders->folder_names[folders->folder_count][strlen(root_path) + 1], file_name);
        char* backslash = strchr(folders->folder_names[folders->folder_count], '\\');
        while (backslash)
        {
            *backslash = '/';
            backslash = strchr(folders->folder_names[folders->folder_count], '\\');
        }
		++folders->folder_count;
    };

    RecurseTree(root_path, add_folder, &folders);

    for (uint32_t p = 0; p < folders.folder_count; ++p)
    {
        uint32_t parent_path_index = (uint32_t)-1;
        const char* path = folders.folder_names[p];
        const char* sub_path_end = find_sub_path_end(path);
        while (*sub_path_end)
        {
            TLongtail_Hash sub_path_hash = get_sub_path_hash(path, sub_path_end);
            uint32_t path_index = find_path_index(paths, sub_path_hashes, path_count, parent_path_index, sub_path_hash);
            if (path_index == path_count)
            {
				const uint32_t path_length = (uint32_t)(sub_path_end - path);
                TLongtail_Hash path_hash = get_sub_path_hash(folders.folder_names[p], sub_path_end);
                path_index = add_sub_path(paths, path_count, parent_path_index, true, path_hash, &string_cache, sub_path_hashes, path, path_length, sub_path_hash);
				++path_count;
            }

            parent_path_index = path_index;
            path = sub_path_end + 1;
            sub_path_end = find_sub_path_end(path);
        }
        TLongtail_Hash sub_path_hash = get_sub_path_hash(path, sub_path_end);
        ASSERT_EQ(path_count, find_path_index(paths, sub_path_hashes, path_count, parent_path_index, sub_path_hash));
        const uint32_t path_length = (uint32_t)(sub_path_end - path);
        TLongtail_Hash path_hash = get_sub_path_hash(folders.folder_names[p], sub_path_end);
        add_sub_path(paths, path_count, parent_path_index, false, path_hash, &string_cache, sub_path_hashes, path, path_length, sub_path_hash);
		++path_count;
	}

	free(folders.folder_names);

	paths.m_PathStorage = string_cache.m_DataBuffer;
	string_cache.m_DataBuffer = 0;

	for (uint32_t sub_path_index = 0; sub_path_index < path_count; ++sub_path_index)
	{
		char* path = strdup(&paths.m_PathStorage[paths.m_PathEntries[sub_path_index].m_PathOffset]);
		uint32_t parent_path_index = paths.m_PathEntries[sub_path_index].m_ParentIndex;
		while (parent_path_index != (uint32_t)-1)
		{
			const char* parent_path = &paths.m_PathStorage[paths.m_PathEntries[parent_path_index].m_PathOffset];
			size_t path_length = strlen(path);
			size_t parent_path_length = strlen(parent_path);
			path = (char*)realloc(path, path_length + 1 + parent_path_length + 1);
			memmove(&path[parent_path_length + 1], path, path_length);
			path[path_length + 1 + parent_path_length] = '\0';
			path[parent_path_length] = '/';
			memcpy(path, parent_path, parent_path_length);
			parent_path_index = paths.m_PathEntries[parent_path_index].m_ParentIndex;
		}
        if (paths.m_PathEntries[sub_path_index].m_ChildCount == (uint32_t)-1)
        {
            printf("File '%s'\n", path);
        }
        else
        {
            printf("Folder '%s' (%u items)\n", path, paths.m_PathEntries[sub_path_index].m_ChildCount);
        }
		free(path);
	};

    free(sub_path_hashes);
    free(paths.m_PathStorage);
    free(paths.m_PathEntries);
}

struct Asset
{
    const char* m_Path;
    uint64_t m_Size;
    meow_u128 m_Hash;
};

struct HashJob
{
    Asset m_Asset;
    nadir::TAtomic32* m_PendingCount;
};

static Bikeshed_TaskResult HashFile(Bikeshed shed, Bikeshed_TaskID, uint8_t, void* context)
{
    HashJob* hash_job = (HashJob*)context;
    HTroveOpenReadFile file_handle = Trove_OpenReadFile(hash_job->m_Asset.m_Path);
    meow_state state;
    MeowBegin(&state, MeowDefaultSeed);
    if(file_handle)
    {
        uint64_t file_size = Trove_GetFileSize(file_handle);
        hash_job->m_Asset.m_Size = file_size;

        uint8_t batch_data[65536];
        uint64_t offset = 0;
        while (offset != file_size)
        {
            meow_umm len = (meow_umm)((file_size - offset) < sizeof(batch_data) ? (file_size - offset) : sizeof(batch_data));
            bool read_ok = Trove_Read(file_handle, offset, len, batch_data);
            assert(read_ok);
            offset += len;
            MeowAbsorb(&state, len, batch_data);
        }
        Trove_CloseReadFile(file_handle);
    }
    meow_u128 hash = MeowEnd(&state, 0);
    hash_job->m_Asset.m_Hash = hash;
    nadir::AtomicAdd32(hash_job->m_PendingCount, -1);
    return BIKESHED_TASK_RESULT_COMPLETE;
}

struct ProcessHashContext
{
    Bikeshed m_Shed;
    HashJob* m_HashJobs;
    nadir::TAtomic32* m_AssetCount;
    nadir::TAtomic32* m_PendingCount;
};

static void ProcessHash(void* context, const char* root_path, const char* file_name)
{
    ProcessHashContext* process_hash_context = (ProcessHashContext*)context;
    Bikeshed shed = process_hash_context->m_Shed;
    uint32_t asset_count = nadir::AtomicAdd32(process_hash_context->m_AssetCount, 1);
    HashJob* job = &process_hash_context->m_HashJobs[asset_count - 1];
    job->m_Asset.m_Path = Trove_ConcatPath(root_path, file_name);
    job->m_Asset.m_Size = 0;
    job->m_PendingCount = process_hash_context->m_PendingCount;
    BikeShed_TaskFunc func[1] = {HashFile};
    void* ctx[1] = {job};
    Bikeshed_TaskID task_id;
    while (!Bikeshed_CreateTasks(shed, 1, func, ctx, &task_id))
    {
        nadir::Sleep(1000);
    }
    {
        nadir::AtomicAdd32(process_hash_context->m_PendingCount, 1);
        Bikeshed_ReadyTasks(shed, 1, &task_id);
    }
}

struct ReadyCallback
{
    Bikeshed_ReadyCallback cb = {Ready};
    ReadyCallback()
    {
        m_Semaphore = nadir::CreateSema(malloc(nadir::GetSemaSize()), 0);
    }
    ~ReadyCallback()
    {
        nadir::DeleteSema(m_Semaphore);
        free(m_Semaphore);
    }
    static void Ready(struct Bikeshed_ReadyCallback* ready_callback, uint8_t channel, uint32_t ready_count)
    {
        ReadyCallback* cb = (ReadyCallback*)ready_callback;
        nadir::PostSema(cb->m_Semaphore, ready_count);
    }
    static void Wait(ReadyCallback* cb)
    {
        nadir::WaitSema(cb->m_Semaphore);
    }
    nadir::HSema m_Semaphore;
};

struct ThreadWorker
{
    ThreadWorker()
        : stop(0)
        , shed(0)
        , semaphore(0)
        , thread(0)
    {
    }

    ~ThreadWorker()
    {
    }

    bool CreateThread(Bikeshed in_shed, nadir::HSema in_semaphore, nadir::TAtomic32* in_stop)
    {
        shed               = in_shed;
        stop               = in_stop;
        semaphore          = in_semaphore;
        thread             = nadir::CreateThread(malloc(nadir::GetThreadSize()), ThreadWorker::Execute, 0, this);
        return thread != 0;
    }

    void JoinThread()
    {
        nadir::JoinThread(thread, nadir::TIMEOUT_INFINITE);
    }

    void DisposeThread()
    {
        nadir::DeleteThread(thread);
        free(thread);
    }

    static int32_t Execute(void* context)
    {
        ThreadWorker* _this = reinterpret_cast<ThreadWorker*>(context);

        while (*_this->stop == 0)
        {
            if (!Bikeshed_ExecuteOne(_this->shed, 0))
            {
                nadir::WaitSema(_this->semaphore);
            }
        }
        return 0;
    }

    nadir::TAtomic32*   stop;
    Bikeshed            shed;
    nadir::HSema        semaphore;
    nadir::HThread      thread;
};


//struct StoredBlock
//{
//    TLongtail_Hash m_Tag;
//    TLongtail_Hash m_Hash;
//    uint64_t m_Size;
//};

//struct LiveBlock
//{
//    uint8_t* m_Data;
//    uint64_t m_CommitedSize;
//};

//LONGTAIL_DECLARE_ARRAY_TYPE(StoredBlock, malloc, free)
//LONGTAIL_DECLARE_ARRAY_TYPE(LiveBlock, malloc, free)
LONGTAIL_DECLARE_ARRAY_TYPE(uint32_t, malloc, free)

// This storage is responsible for storing/retrieveing the data, caching if data is on remote store and compression
struct BlockStorage
{
    int (*BlockStorage_WriteBlock)(BlockStorage* storage, TLongtail_Hash hash, uint64_t length, const void* data);
    int (*BlockStorage_ReadBlock)(BlockStorage* storage, TLongtail_Hash hash, uint64_t length, void* data);
    uint64_t (*BlockStorage_GetStoredSize)(BlockStorage* storage, TLongtail_Hash hash);
};

struct DiskBlockStorage;

struct CompressJob
{
    nadir::TAtomic32* active_job_count;
    BlockStorage* base_storage;
    TLongtail_Hash hash;
    uint64_t length;
    const void* data;
};

static Bikeshed_TaskResult CompressFile(Bikeshed shed, Bikeshed_TaskID, uint8_t, void* context)
{
    CompressJob* compress_job = (CompressJob*)context;
    const size_t max_dst_size = Lizard_compressBound((int)compress_job->length);
    void* compressed_buffer = malloc(sizeof(int32_t) + max_dst_size);

    bool ok = false;
    int compressed_size = Lizard_compress((const char*)compress_job->data, &((char*)compressed_buffer)[sizeof(int32_t)], (int)compress_job->length, (int)max_dst_size, 44);//LIZARD_MAX_CLEVEL);
    if (compressed_size > 0)
    {
        compressed_buffer = realloc(compressed_buffer, (size_t)(sizeof(int) + compressed_size));
        ((int*)compressed_buffer)[0] = (int)compress_job->length;

        compress_job->base_storage->BlockStorage_WriteBlock(compress_job->base_storage, compress_job->hash, sizeof(int32_t) + compressed_size, compressed_buffer);
        free(compressed_buffer);
    }
    nadir::AtomicAdd32(compress_job->active_job_count, -1);
    free(compress_job);
    return BIKESHED_TASK_RESULT_COMPLETE;
}

struct CompressStorage
{
    BlockStorage m_Storage = {WriteBlock, ReadBlock, GetStoredSize};

    CompressStorage(BlockStorage* base_storage, Bikeshed shed, nadir::TAtomic32* active_job_count)
        : m_BaseStorage(base_storage)
        , m_Shed(shed)
        , m_ActiveJobCount(active_job_count)
    {

    }

    static int WriteBlock(BlockStorage* storage, TLongtail_Hash hash, uint64_t length, const void* data)
    {
        CompressStorage* compress_storage = (CompressStorage*)storage;

        uint8_t* p = (uint8_t*)(malloc((size_t)(sizeof(CompressJob) + length)));

        CompressJob* compress_job = (CompressJob*)p;
        p += sizeof(CompressJob);
        compress_job->active_job_count = compress_storage->m_ActiveJobCount;
        compress_job->base_storage = compress_storage->m_BaseStorage;
        compress_job->hash = hash;
        compress_job->length = length;
        compress_job->data = p;

        memcpy((void*)compress_job->data, data, (size_t)length);
        BikeShed_TaskFunc taskfunc[1] = { CompressFile };
        void* context[1] = {compress_job};
        Bikeshed_TaskID task_ids[1];
        while (!Bikeshed_CreateTasks(compress_storage->m_Shed, 1, taskfunc, context, task_ids))
        {
            nadir::Sleep(1000);
        }
        nadir::AtomicAdd32(compress_storage->m_ActiveJobCount, 1);
        Bikeshed_ReadyTasks(compress_storage->m_Shed, 1, task_ids);
        return 1;
    }

    static int ReadBlock(BlockStorage* storage, TLongtail_Hash hash, uint64_t length, void* data)
    {
        CompressStorage* compress_storage = (CompressStorage*)storage;

        uint64_t compressed_size = compress_storage->m_BaseStorage->BlockStorage_GetStoredSize(compress_storage->m_BaseStorage, hash);
        if (compressed_size == 0)
        {
            return 0;
        }

        char* compressed_buffer = (char*)malloc((size_t)(compressed_size));
        int ok = compress_storage->m_BaseStorage->BlockStorage_ReadBlock(compress_storage->m_BaseStorage, hash, compressed_size, compressed_buffer);
        if (!ok)
        {
            free(compressed_buffer);
            return false;
        }

        int32_t raw_size = ((int32_t*)compressed_buffer)[0];
        assert(length <= raw_size);

        int result = Lizard_decompress_safe((const char*)(compressed_buffer + sizeof(int32_t)), (char*)data, (int)compressed_size, (int)length);
        free(compressed_buffer);
        ok = result >= length;
        return ok ? 1 : 0;
    }

    static uint64_t GetStoredSize(BlockStorage* storage, TLongtail_Hash hash)
    {
        CompressStorage* compress_storage = (CompressStorage*)storage;
        int32_t size = 0;
        int ok = compress_storage->m_BaseStorage->BlockStorage_ReadBlock(compress_storage->m_BaseStorage, hash, sizeof(int32_t), &size);

        return ok ? size : 0;
    }


    nadir::TAtomic32* m_ActiveJobCount;
    BlockStorage* m_BaseStorage;
    Bikeshed m_Shed;
};

struct DiskBlockStorage
{
    BlockStorage m_Storage = {WriteBlock, ReadBlock, GetStoredSize};
    DiskBlockStorage(const char* store_path)
        : m_StorePath(store_path)
        , m_ActiveJobCount(0)
    {}

    static int WriteBlock(BlockStorage* storage, TLongtail_Hash hash, uint64_t length, const void* data)
    {
        DiskBlockStorage* disk_block_storage = (DiskBlockStorage*)storage;

        const char* path = disk_block_storage->MakeBlockPath(hash);

        HTroveOpenWriteFile f = Trove_OpenWriteFile(path);
        free((char*)path);
        if (!f)
        {
            return 0;
        }

        bool ok = Trove_Write(f, 0, length, data);
        Trove_CloseWriteFile(f);
        return ok ? 1 : 0;
    }

    static int ReadBlock(BlockStorage* storage, TLongtail_Hash hash, uint64_t length, void* data)
    {
        DiskBlockStorage* disk_block_storage = (DiskBlockStorage*)storage;
        const char* path = disk_block_storage->MakeBlockPath(hash);
        HTroveOpenReadFile f = Trove_OpenReadFile(path);
        free((char*)path);
        if (!f)
        {
            return 0;
        }

        bool ok = Trove_Read(f, 0, length, data);
        Trove_CloseReadFile(f);
        return ok ? 1 : 0;
    }

    static uint64_t GetStoredSize(BlockStorage* storage, TLongtail_Hash hash)
    {
        DiskBlockStorage* disk_block_storage = (DiskBlockStorage*)storage;
        const char* path = disk_block_storage->MakeBlockPath(hash);
        HTroveOpenReadFile f = Trove_OpenReadFile(path);
        free((char*)path);
        if (!f)
        {
            return 0;
        }
        uint64_t size = Trove_GetFileSize(f);
        Trove_CloseReadFile(f);
        return size;
    }

    const char* MakeBlockPath(TLongtail_Hash hash)
    {
        char file_name[64];
        sprintf(file_name, "0x%" PRIx64, hash);
        const char* path = Trove_ConcatPath(m_StorePath, file_name);
        return path;
    }
    const char* m_StorePath;
    nadir::TAtomic32 m_ActiveJobCount;
};


struct SimpleWriteStorage
{
    Longtail_WriteStorage m_Storage = {AddExistingBlock, AllocateBlockStorage, WriteBlockData, CommitBlockData, FinalizeBlock};

    SimpleWriteStorage(BlockStorage* block_storage, uint32_t default_block_size)
        : m_BlockStore(block_storage)
        , m_DefaultBlockSize(default_block_size)
        , m_Blocks()
        , m_CurrentBlockIndex(0)
        , m_CurrentBlockData(0)
        , m_CurrentBlockSize(0)
        , m_CurrentBlockUsedSize(0)
    {

    }

    ~SimpleWriteStorage()
    {
        if (m_CurrentBlockData)
        {
            PersistCurrentBlock(this);
            free((void*)m_CurrentBlockData);
        }
        Free_TLongtail_Hash(m_Blocks);
    }

    static int AddExistingBlock(struct Longtail_WriteStorage* storage, TLongtail_Hash hash, uint32_t* out_block_index)
    {
        SimpleWriteStorage* simple_storage = (SimpleWriteStorage*)storage;
        *out_block_index = GetSize_TLongtail_Hash(simple_storage->m_Blocks);
        simple_storage->m_Blocks = EnsureCapacity_TLongtail_Hash(simple_storage->m_Blocks, 16u);
        *Push_TLongtail_Hash(simple_storage->m_Blocks) = hash;
        return 1;
    }

    static int AllocateBlockStorage(struct Longtail_WriteStorage* storage, TLongtail_Hash tag, uint64_t length, Longtail_BlockStore* out_block_entry)
    {
        SimpleWriteStorage* simple_storage = (SimpleWriteStorage*)storage;
        simple_storage->m_Blocks = EnsureCapacity_TLongtail_Hash(simple_storage->m_Blocks, 16u);
        if (simple_storage->m_CurrentBlockData && (simple_storage->m_CurrentBlockUsedSize + length) > simple_storage->m_DefaultBlockSize)
        {
            PersistCurrentBlock(simple_storage);
            if (length > simple_storage->m_CurrentBlockSize)
            {
                free((void*)simple_storage->m_CurrentBlockData);
                simple_storage->m_CurrentBlockData = 0;
                simple_storage->m_CurrentBlockSize = 0;
                simple_storage->m_CurrentBlockUsedSize = 0;
            }
            else
            {
                simple_storage->m_CurrentBlockIndex = GetSize_TLongtail_Hash(simple_storage->m_Blocks);
                *Push_TLongtail_Hash(simple_storage->m_Blocks) = 0xfffffffffffffffflu;
                simple_storage->m_CurrentBlockUsedSize = 0;
            }
        }
        if (0 == simple_storage->m_CurrentBlockData)
        {
            uint32_t block_size = (uint32_t)(length > simple_storage->m_DefaultBlockSize ? length : simple_storage->m_DefaultBlockSize);
            simple_storage->m_CurrentBlockSize = block_size;
            simple_storage->m_CurrentBlockData = (uint8_t*)malloc(block_size);
            simple_storage->m_CurrentBlockUsedSize = 0;
            simple_storage->m_CurrentBlockIndex = GetSize_TLongtail_Hash(simple_storage->m_Blocks);
            *Push_TLongtail_Hash(simple_storage->m_Blocks) = 0xfffffffffffffffflu;
        }
        out_block_entry->m_BlockIndex = simple_storage->m_CurrentBlockIndex;
        out_block_entry->m_StartOffset = simple_storage->m_CurrentBlockUsedSize;
        out_block_entry->m_Length = length;
        simple_storage->m_CurrentBlockUsedSize += (uint32_t)length;
        return 1;
    }

    static int WriteBlockData(struct Longtail_WriteStorage* storage, const Longtail_BlockStore* block_entry, Longtail_InputStream input_stream, void* context)
    {
        SimpleWriteStorage* simple_storage = (SimpleWriteStorage*)storage;
        assert(block_entry->m_BlockIndex == simple_storage->m_CurrentBlockIndex);
        return input_stream(context, block_entry->m_Length, &simple_storage->m_CurrentBlockData[block_entry->m_StartOffset]);
    }

    static int CommitBlockData(struct Longtail_WriteStorage* , const Longtail_BlockStore* )
    {
        return 1;
    }

    static TLongtail_Hash FinalizeBlock(struct Longtail_WriteStorage* storage, uint32_t block_index)
    {
        SimpleWriteStorage* simple_storage = (SimpleWriteStorage*)storage;
        if (block_index == simple_storage->m_CurrentBlockIndex && simple_storage->m_CurrentBlockData)
        {
            if (0 == PersistCurrentBlock(simple_storage))
            {
                return 0;
            }
            free(simple_storage->m_CurrentBlockData);
            simple_storage->m_CurrentBlockData = 0;
            simple_storage->m_CurrentBlockSize = 0u;
            simple_storage->m_CurrentBlockUsedSize = 0u;
            simple_storage->m_CurrentBlockIndex = GetSize_TLongtail_Hash(simple_storage->m_Blocks);
        }
        assert(simple_storage->m_Blocks[block_index] != 0xfffffffffffffffflu);
        return simple_storage->m_Blocks[block_index];
    }

    static int PersistCurrentBlock(SimpleWriteStorage* simple_storage)
    {
        meow_state state;
        MeowBegin(&state, MeowDefaultSeed);
        MeowAbsorb(&state, simple_storage->m_CurrentBlockUsedSize, simple_storage->m_CurrentBlockData);
        TLongtail_Hash hash = MeowU64From(MeowEnd(&state, 0), 0);
        simple_storage->m_Blocks[simple_storage->m_CurrentBlockIndex] = hash;
        if (simple_storage->m_BlockStore->BlockStorage_GetStoredSize(simple_storage->m_BlockStore, hash) == 0)
        {
            return simple_storage->m_BlockStore->BlockStorage_WriteBlock(simple_storage->m_BlockStore, hash, simple_storage->m_CurrentBlockUsedSize, simple_storage->m_CurrentBlockData);
        }
        return 1;
    }

    BlockStorage* m_BlockStore;
    const uint32_t m_DefaultBlockSize;
    TLongtail_Hash* m_Blocks;
    uint32_t m_CurrentBlockIndex;
    uint8_t* m_CurrentBlockData;
    uint32_t m_CurrentBlockSize;
    uint32_t m_CurrentBlockUsedSize;
};

typedef uint8_t* TBlockData;

LONGTAIL_DECLARE_ARRAY_TYPE(TBlockData, malloc, free)

struct SimpleReadStorage
{
    Longtail_ReadStorage m_Storage = {/*PreflightBlocks, */AqcuireBlockStorage, ReleaseBlock};
    SimpleReadStorage(TLongtail_Hash* block_hashes_array, BlockStorage* block_storage)
        : m_Blocks(block_hashes_array)
        , m_BlockStorage(block_storage)
        , m_BlockData()
    {
        uint32_t block_count = GetSize_TLongtail_Hash(m_Blocks);
        m_BlockData = SetCapacity_TBlockData(m_BlockData, block_count);
        SetSize_TBlockData(m_BlockData, block_count);
        for (uint32_t i = 0; i < block_count; ++i)
        {
            m_BlockData[i] = 0;
        }
    }

    ~SimpleReadStorage()
    {
        Free_TBlockData(m_BlockData);
    }
//    static uint64_t PreflightBlocks(struct Longtail_ReadStorage* storage, uint32_t block_count, struct Longtail_BlockStore* blocks)
//    {
//        return 1;
//    }
    static const uint8_t* AqcuireBlockStorage(struct Longtail_ReadStorage* storage, uint32_t block_index)
    {
        SimpleReadStorage* simple_read_storage = (SimpleReadStorage*)storage;
        if (simple_read_storage->m_BlockData[block_index] == 0)
        {
            TLongtail_Hash hash = simple_read_storage->m_Blocks[block_index];
            uint32_t block_size = (uint32_t)simple_read_storage->m_BlockStorage->BlockStorage_GetStoredSize(simple_read_storage->m_BlockStorage, hash);
            if (block_size == 0)
            {
                return 0;
            }

            simple_read_storage->m_BlockData[block_index] = (uint8_t*)malloc(block_size);
            if (0 == simple_read_storage->m_BlockStorage->BlockStorage_ReadBlock(simple_read_storage->m_BlockStorage, hash, block_size, simple_read_storage->m_BlockData[block_index]))
            {
                free(simple_read_storage->m_BlockData[block_index]);
                simple_read_storage->m_BlockData[block_index] = 0;
                return 0;
            }
        }
        return simple_read_storage->m_BlockData[block_index];
    }

    static void ReleaseBlock(struct Longtail_ReadStorage* storage, uint32_t block_index)
    {
        SimpleReadStorage* simple_read_storage = (SimpleReadStorage*)storage;
        free(simple_read_storage->m_BlockData[block_index]);
        simple_read_storage->m_BlockData[block_index] = 0;
    }
    TLongtail_Hash* m_Blocks;
    BlockStorage* m_BlockStorage;
    uint8_t** m_BlockData;
};

#if 0
///////////////////// TODO: Refine and make part of Longtail

struct WriteStorage
{
    Longtail_WriteStorage m_Storage = {AddExistingBlock, AllocateBlockStorage, WriteBlockData, CommitBlockData, FinalizeBlock};

    WriteStorage(StoredBlock** blocks, uint64_t block_size, uint32_t tag_types, BlockStorage* block_storage)
        : m_Blocks(blocks)
        , m_LiveBlocks(0)
        , m_BlockStorage(block_storage)
        , m_BlockSize(block_size)
    {
        size_t hash_table_size = jc::HashTable<uint64_t, StoredBlock*>::CalcSize(tag_types);
        m_TagToBlocksMem = malloc(hash_table_size);
        m_TagToBlocks.Create(tag_types, m_TagToBlocksMem);
    }

    ~WriteStorage()
    {
        uint32_t size = Longtail_Array_GetSize(m_LiveBlocks);
        for (uint32_t i = 0; i < size; ++i)
        {
            LiveBlock* last_live_block = &m_LiveBlocks[i];
            if (last_live_block->m_Data)
            {
                PersistBlock(this, i);
            }
        }
        Longtail_Array_Free(m_LiveBlocks);
        free(m_TagToBlocksMem);
    }

    static int AddExistingBlock(struct Longtail_WriteStorage* storage, TLongtail_Hash hash, uint32_t* out_block_index)
    {
        WriteStorage* write_storage = (WriteStorage*)storage;
        uint32_t size = Longtail_Array_GetSize(*write_storage->m_Blocks);
        uint32_t capacity = Longtail_Array_GetCapacity(*write_storage->m_Blocks);
        if (capacity < (size + 1))
        {
            *write_storage->m_Blocks = Longtail_Array_SetCapacity(*write_storage->m_Blocks, capacity + 16u);
            write_storage->m_LiveBlocks = Longtail_Array_SetCapacity(write_storage->m_LiveBlocks, capacity + 16u);
        }

        StoredBlock* new_block = Longtail_Array_Push(*write_storage->m_Blocks);
        new_block->m_Tag = 0;
        new_block->m_Hash = hash;
        new_block->m_Size = 0;
        LiveBlock* live_block = Longtail_Array_Push(write_storage->m_LiveBlocks);
        live_block->m_Data = 0;
        live_block->m_CommitedSize = 0;
        *out_block_index = size;
        return 1;
    }

    static int AllocateBlockStorage(struct Longtail_WriteStorage* storage, TLongtail_Hash tag, uint64_t length, Longtail_BlockStore* out_block_entry)
    {
        WriteStorage* write_storage = (WriteStorage*)storage;
        uint32_t** existing_block_idx_array = write_storage->m_TagToBlocks.Get(tag);
        uint32_t* block_idx_ptr;
        if (existing_block_idx_array == 0)
        {
            uint32_t* block_idx_storage = Longtail_Array_IncreaseCapacity((uint32_t*)0, 16);
            write_storage->m_TagToBlocks.Put(tag, block_idx_storage);
            block_idx_ptr = block_idx_storage;
        }
        else
        {
            block_idx_ptr = *existing_block_idx_array;
        }

        uint32_t size = Longtail_Array_GetSize(block_idx_ptr);
        uint32_t capacity = Longtail_Array_GetCapacity(block_idx_ptr);
        if (capacity < (size + 1))
        {
            block_idx_ptr = Longtail_Array_SetCapacity(block_idx_ptr, capacity + 16u);
            write_storage->m_TagToBlocks.Put(tag, block_idx_ptr);
        }

        if (length < write_storage->m_BlockSize)
        {
            size = Longtail_Array_GetSize(block_idx_ptr);
            uint32_t best_fit = size;
            for (uint32_t i = 0; i < size; ++i)
            {
                uint32_t reuse_block_index = block_idx_ptr[i];
                LiveBlock* reuse_live_block = &write_storage->m_LiveBlocks[reuse_block_index];
                if (reuse_live_block->m_Data)
                {
                    StoredBlock* reuse_block = &(*write_storage->m_Blocks)[reuse_block_index];
                    uint64_t space_left = reuse_block->m_Size - reuse_live_block->m_CommitedSize;
                    if (space_left >= length)
                    {
                        if ((best_fit == size) || space_left < ((*write_storage->m_Blocks)[best_fit].m_Size - write_storage->m_LiveBlocks[best_fit].m_CommitedSize))
                        {
                            best_fit = reuse_block_index;
                        }
                    }
                }
            }
            if (best_fit != size)
            {
                out_block_entry->m_BlockIndex = best_fit;
                out_block_entry->m_Length = length;
                out_block_entry->m_StartOffset = write_storage->m_LiveBlocks[best_fit].m_CommitedSize;
                return 1;
            }
        }

        size = Longtail_Array_GetSize(*write_storage->m_Blocks);
        capacity = Longtail_Array_GetCapacity(*write_storage->m_Blocks);
        if (capacity < (size + 1))
        {
            *write_storage->m_Blocks = Longtail_Array_SetCapacity(*write_storage->m_Blocks, capacity + 16u);
            write_storage->m_LiveBlocks = Longtail_Array_SetCapacity(write_storage->m_LiveBlocks, capacity + 16u);
        }
        *Longtail_Array_Push(block_idx_ptr) = size;
        StoredBlock* new_block = Longtail_Array_Push(*write_storage->m_Blocks);
        new_block->m_Tag = tag;
        LiveBlock* live_block = Longtail_Array_Push(write_storage->m_LiveBlocks);
        live_block->m_CommitedSize = 0;
        new_block->m_Size = 0;

        uint64_t block_size = length > write_storage->m_BlockSize ? length : write_storage->m_BlockSize;
        new_block->m_Size = block_size;
        live_block->m_Data = (uint8_t*)malloc(block_size);

        out_block_entry->m_BlockIndex = size;
        out_block_entry->m_Length = length;
        out_block_entry->m_StartOffset = 0;

        return 1;
    }

    static int WriteBlockData(struct Longtail_WriteStorage* storage, const Longtail_BlockStore* block_entry, Longtail_InputStream input_stream, void* context)
    {
        WriteStorage* write_storage = (WriteStorage*)storage;
        StoredBlock* stored_block = &(*write_storage->m_Blocks)[block_entry->m_BlockIndex];
        LiveBlock* live_block = &write_storage->m_LiveBlocks[block_entry->m_BlockIndex];
        return input_stream(context, block_entry->m_Length, &live_block->m_Data[block_entry->m_StartOffset]);
    }

    static int CommitBlockData(struct Longtail_WriteStorage* storage, const Longtail_BlockStore* block_entry)
    {
        WriteStorage* write_storage = (WriteStorage*)storage;
        StoredBlock* stored_block = &(*write_storage->m_Blocks)[block_entry->m_BlockIndex];
        LiveBlock* live_block = &write_storage->m_LiveBlocks[block_entry->m_BlockIndex];
        live_block->m_CommitedSize = block_entry->m_StartOffset + block_entry->m_Length;
        if (live_block->m_CommitedSize >= (stored_block->m_Size - 8))
        {
            PersistBlock(write_storage, block_entry->m_BlockIndex);
        }

        return 1;
    }

    static TLongtail_Hash FinalizeBlock(struct Longtail_WriteStorage* storage, uint32_t block_index)
    {
        WriteStorage* write_storage = (WriteStorage*)storage;
        StoredBlock* stored_block = &(*write_storage->m_Blocks)[block_index];
        LiveBlock* live_block = &write_storage->m_LiveBlocks[block_index];
        if (live_block->m_Data)
        {
            if (0 == PersistBlock(write_storage, block_index))
            {
                return 0;
            }
        }
        return stored_block->m_Hash;
    }

    static int PersistBlock(WriteStorage* write_storage, uint32_t block_index)
    {
        StoredBlock* stored_block = &(*write_storage->m_Blocks)[block_index];
        LiveBlock* live_block = &write_storage->m_LiveBlocks[block_index];
        meow_state state;
        MeowBegin(&state, MeowDefaultSeed);
        MeowAbsorb(&state, live_block->m_CommitedSize, live_block->m_Data);
        stored_block->m_Hash = MeowU64From(MeowEnd(&state, 0), 0);

        if (write_storage->m_BlockStorage->BlockStorage_GetStoredSize(write_storage->m_BlockStorage, stored_block->m_Hash) == 0)
        {
            int result = write_storage->m_BlockStorage->BlockStorage_WriteBlock(write_storage->m_BlockStorage, stored_block->m_Hash, live_block->m_CommitedSize, live_block->m_Data);
            free(live_block->m_Data);
            live_block->m_Data = 0;
            stored_block->m_Size = live_block->m_CommitedSize;
            return result;
        }
        return 1;
    }

    jc::HashTable<uint64_t, uint32_t*> m_TagToBlocks;
    void* m_TagToBlocksMem;
    StoredBlock** m_Blocks;
    LiveBlock* m_LiveBlocks;
    BlockStorage* m_BlockStorage;
    uint64_t m_BlockSize;
};

struct ReadStorage
{
    Longtail_ReadStorage m_Storage = {/*PreflightBlocks, */AqcuireBlockStorage, ReleaseBlock};
    ReadStorage(StoredBlock* stored_blocks, BlockStorage* block_storage)
        : m_Blocks(stored_blocks)
        , m_LiveBlocks(0)
        , m_BlockStorage(block_storage)
    {
        m_LiveBlocks = Longtail_Array_SetCapacity(m_LiveBlocks, Longtail_Array_GetSize(m_Blocks));
    }
    ~ReadStorage()
    {
        Longtail_Array_Free(m_LiveBlocks);
    }
//    static uint64_t PreflightBlocks(struct Longtail_ReadStorage* storage, uint32_t block_count, struct Longtail_BlockStore* blocks)
//    {
//        return 1;
//    }
    static const uint8_t* AqcuireBlockStorage(struct Longtail_ReadStorage* storage, uint32_t block_index)
    {
        ReadStorage* read_storage = (ReadStorage*)storage;
        StoredBlock* stored_block = &read_storage->m_Blocks[block_index];
        LiveBlock* live_block = &read_storage->m_LiveBlocks[block_index];
        live_block->m_Data = (uint8_t*)malloc(stored_block->m_Size);
        live_block->m_CommitedSize = stored_block->m_Size;
        read_storage->m_BlockStorage->BlockStorage_ReadBlock(read_storage->m_BlockStorage, stored_block->m_Hash, live_block->m_CommitedSize, live_block->m_Data);
        return &live_block->m_Data[0];
    }

    static void ReleaseBlock(struct Longtail_ReadStorage* storage, uint32_t block_index)
    {
        ReadStorage* read_storage = (ReadStorage*)storage;
        LiveBlock* live_block = &read_storage->m_LiveBlocks[block_index];
        free(live_block->m_Data);
    }
    StoredBlock* m_Blocks;
    LiveBlock* m_LiveBlocks;
    BlockStorage* m_BlockStorage;
};

#endif














static int InputStream(void* context, uint64_t byte_count, uint8_t* data)
{
    Asset* asset = (Asset*)context;
    HTroveOpenReadFile f= Trove_OpenReadFile(asset->m_Path);
    if (f == 0)
    {
        return 0;
    }
    Trove_Read(f, 0, byte_count, data);
    Trove_CloseReadFile(f);
    return 1;
}

LONGTAIL_DECLARE_ARRAY_TYPE(uint8_t, malloc, free)

int OutputStream(void* context, uint64_t byte_count, const uint8_t* data)
{
    uint8_t** buffer = (uint8_t**)context;
    *buffer = SetCapacity_uint8_t(*buffer, (uint32_t)byte_count);
    SetSize_uint8_t(*buffer, (uint32_t)byte_count);
    memmove(*buffer, data, (size_t)byte_count);
    return 1;
}

static TLongtail_Hash GetPathHash(const char* path)
{
    meow_state state;
    MeowBegin(&state, MeowDefaultSeed);
    MeowAbsorb(&state, strlen(path), (void*)path);
    TLongtail_Hash path_hash = MeowU64From(MeowEnd(&state, 0), 0);
    return path_hash;
}

LONGTAIL_DECLARE_ARRAY_TYPE(Longtail_BlockStore, malloc, free)
LONGTAIL_DECLARE_ARRAY_TYPE(Longtail_BlockAssets, malloc, free)

struct Longtail_AssetRegistry
{
    // If all the assets are still in the new index, keep the block and the assets, otherwise throw it away

    // For each block we want the assets in that block
    // Build a map of all new asset content hash to "invalid block"

    // Iterate over asset content hashes is each block, check if we all the content hashes are in the new asset set
    //  Yes: Store block and set all the asset content hases in block to point to index of stored block
    //  No: Don't store block
    // Iterate over all assets, get path hash, look up content hash, look up block index in map from asset content has to block index
    // if "invalid block" start a new block and store content there

    // Queries:
    //  Is the asset content hash present

    // Need:
    //   A list of blocks with the asset content in them

    struct Longtail_AssetEntry* m_AssetArray;

};

struct OrderedCacheContent
{
    TLongtail_Hash* m_BlockHashes;
    Longtail_AssetEntry* m_AssetEntries;
    struct Longtail_BlockAssets* m_BlockAssets;
};

struct AssetSorter
{
    AssetSorter(const struct Longtail_AssetEntry* asset_entries)
        : asset_entries(asset_entries)
    {}
    const struct Longtail_AssetEntry* asset_entries;
    bool operator()(uint32_t left, uint32_t right)
    {
        const Longtail_AssetEntry& a = asset_entries[left];
        const Longtail_AssetEntry& b = asset_entries[right];
        if (a.m_BlockStore.m_BlockIndex == b.m_BlockStore.m_BlockIndex)
        {
            return a.m_BlockStore.m_StartOffset < b.m_BlockStore.m_StartOffset;
        }
        return a.m_BlockStore.m_BlockIndex < b.m_BlockStore.m_BlockIndex;
    }
};

struct OrderedCacheContent* BuildOrderedCacheContent(uint32_t asset_entry_count, const struct Longtail_AssetEntry* asset_entries, uint32_t block_count, const TLongtail_Hash* block_hashes)
{
    OrderedCacheContent* ordered_content = (OrderedCacheContent*)malloc(sizeof(OrderedCacheContent));
    ordered_content->m_BlockHashes = SetCapacity_TLongtail_Hash((TLongtail_Hash*)0, block_count);
    ordered_content->m_AssetEntries = SetCapacity_Longtail_AssetEntry((Longtail_AssetEntry*)0, asset_entry_count);
    ordered_content->m_BlockAssets = SetCapacity_Longtail_BlockAssets((Longtail_BlockAssets*)0, asset_entry_count); // Worst case

    uint32_t* asset_order = new uint32_t[asset_entry_count];
    for (uint32_t ai = 0; ai < asset_entry_count; ++ai)
    {
        asset_order[ai] = ai;
    }

    // Sort the asset references so the blocks are consecutive ascending
    std::sort(&asset_order[0], &asset_order[asset_entry_count], AssetSorter(asset_entries));

    uint32_t ai = 0;
    while (ai < asset_entry_count)
    {
        uint32_t asset_index = asset_order[ai];
        const struct Longtail_AssetEntry* a = &asset_entries[asset_index];
        uint32_t block_index = a->m_BlockStore.m_BlockIndex;
        Longtail_BlockAssets* block_asset = Push_Longtail_BlockAssets(ordered_content->m_BlockAssets);
        uint32_t block_asset_index = GetSize_Longtail_AssetEntry(ordered_content->m_AssetEntries);
        block_asset->m_AssetIndex = block_asset_index;
        block_asset->m_AssetCount = 1;
        *Push_Longtail_AssetEntry(ordered_content->m_AssetEntries) = *a;
        *Push_TLongtail_Hash(ordered_content->m_BlockHashes) = block_hashes[block_index];

        ++ai;
        asset_index = asset_order[ai];

        while (ai < asset_entry_count && asset_entries[asset_index].m_BlockStore.m_BlockIndex == block_index)
        {
            const struct Longtail_AssetEntry* a = &asset_entries[asset_index];
            *Push_Longtail_AssetEntry(ordered_content->m_AssetEntries) = *a;
            ++block_asset->m_AssetCount;

            ++ai;
            asset_index = asset_order[ai];
        }

    }

    delete [] asset_order;

    return ordered_content;
}

struct Longtail_IndexDiffer
{
    struct Longtail_AssetEntry* m_AssetArray;
    TLongtail_Hash* m_BlockHashes;
    struct Longtail_BlockAssets* m_BlockAssets;
    TLongtail_Hash* m_AssetHashes;
};

int Longtail_CompareAssetEntry(const void* element1, const void* element2)
{
    const struct Longtail_AssetEntry* entry1 = (const struct Longtail_AssetEntry*)element1;
    const struct Longtail_AssetEntry* entry2 = (const struct Longtail_AssetEntry*)element2;
    return (int)((int64_t)entry1->m_BlockStore.m_BlockIndex - (int64_t)entry2->m_BlockStore.m_BlockIndex);
}

Longtail_IndexDiffer* CreateDiffer(void* mem, uint32_t asset_entry_count, struct Longtail_AssetEntry* asset_entries, uint32_t block_count, TLongtail_Hash* block_hashes, uint32_t new_asset_count, TLongtail_Hash* new_asset_hashes)
{
    Longtail_IndexDiffer* index_differ = (Longtail_IndexDiffer*)mem;
    index_differ->m_AssetArray = 0;
    index_differ->m_BlockHashes = 0;
//    index_differ->m_AssetArray = Longtail_Array_SetCapacity(index_differ->m_AssetArray, asset_entry_count);
//    index_differ->m_BlockArray = Longtail_Array_SetCapacity(index_differ->m_BlockArray, block_entry_count);
//    memcpy(index_differ->m_AssetArray, asset_entries, sizeof(Longtail_AssetEntry) * asset_entry_count);
//    memcpy(index_differ->m_BlockArray, block_entries, sizeof(Longtail_BlockStore) * block_entry_count);
    qsort(asset_entries, asset_entry_count, sizeof(Longtail_AssetEntry), Longtail_CompareAssetEntry);
    uint32_t hash_size = jc::HashTable<TLongtail_Hash, uint32_t>::CalcSize(new_asset_count);
    void* hash_mem = malloc(hash_size);
    jc::HashTable<TLongtail_Hash, uint32_t> hashes;
    hashes.Create(new_asset_count, hash_mem);
    for (uint32_t i = 0; i < new_asset_count; ++i)
    {
        hashes.Put(new_asset_hashes[i], new_asset_count);
    }
    uint32_t b = 0;
    while (b < asset_entry_count)
    {
        uint32_t block_index = asset_entries[b].m_BlockStore.m_BlockIndex;
        uint32_t scan_b = b;
        uint32_t found_assets = 0;

        while (scan_b < asset_entry_count && (asset_entries[scan_b].m_BlockStore.m_BlockIndex) == block_index)
        {
            if (hashes.Get(asset_entries[scan_b].m_AssetHash))
            {
                ++found_assets;
            }
            ++scan_b;
        }
        uint32_t assets_in_block = scan_b - b;
        if (assets_in_block == found_assets)
        {
            for (uint32_t f = b; f < scan_b; ++f)
            {
                hashes.Put(asset_entries[f].m_AssetHash, block_index);
            }
        }
    }
    // We now have a map with assets that are not in the index already

    // TODO: Need to sort out the asset->block_entry->block_hash thing
    // How we add data to an existing block store, indexes and all

    index_differ->m_AssetArray = SetCapacity_Longtail_AssetEntry(index_differ->m_AssetArray, asset_entry_count);
    index_differ->m_BlockHashes = SetCapacity_TLongtail_Hash(index_differ->m_BlockHashes, block_count);

    for (uint32_t a = 0; a < new_asset_count; ++a)
    {
        TLongtail_Hash asset_hash = new_asset_hashes[a];
        uint32_t* existing_block_index = hashes.Get(asset_hash);
        if (existing_block_index)
        {
            Longtail_AssetEntry* asset_entry = Push_Longtail_AssetEntry(index_differ->m_AssetArray);
//            TLongtail_Hash* block_hash = Push_TLongtail_Hash(index_differ->m_BlockHashes);
            asset_entry->m_BlockStore.m_BlockIndex = *existing_block_index;
            asset_entry->m_AssetHash = 0;
        }
    }

    return index_differ;
}

static TLongtail_Hash GetContentTag(const char* path)
{
    const char * extension = strrchr(path, '.');
    if (extension)
    {
        if (strcmp(extension, ".uasset") == 0)
        {
            return 1000;
        }
        if (strcmp(extension, ".uexp") == 0)
        {
            if (strstr(path, "Meshes"))
            {
                return GetPathHash("Meshes");
            }
            if (strstr(path, "Textures"))
            {
                return GetPathHash("Textures");
            }
            if (strstr(path, "Sounds"))
            {
                return GetPathHash("Sounds");
            }
            if (strstr(path, "Animations"))
            {
                return GetPathHash("Animations");
            }
            if (strstr(path, "Blueprints"))
            {
                return GetPathHash("Blueprints");
            }
            if (strstr(path, "Characters"))
            {
                return GetPathHash("Characters");
            }
            if (strstr(path, "Effects"))
            {
                return GetPathHash("Effects");
            }
            if (strstr(path, "Materials"))
            {
                return GetPathHash("Materials");
            }
            if (strstr(path, "Maps"))
            {
                return GetPathHash("Maps");
            }
            if (strstr(path, "Movies"))
            {
                return GetPathHash("Movies");
            }
            if (strstr(path, "Slate"))
            {
                return GetPathHash("Slate");
            }
            if (strstr(path, "Sounds"))
            {
                return GetPathHash("MeshSoundses");
            }
        }
        return GetPathHash(extension);
    }
    return 2000;
}

struct ScanExistingDataContext
{
//    Bikeshed m_Shed;
//    HashJob* m_HashJobs;
//    nadir::TAtomic32* m_AssetCount;
//    nadir::TAtomic32* m_PendingCount;
    TLongtail_Hash* m_Hashes;
};

static void ScanHash(void* context, const char* , const char* file_name)
{
    ScanExistingDataContext* scan_context = (ScanExistingDataContext*)context;
    TLongtail_Hash hash;
    if (1 == sscanf(file_name, "0x%" PRIx64, &hash))
    {
        scan_context->m_Hashes = EnsureCapacity_TLongtail_Hash(scan_context->m_Hashes, 16u);
        *(Push_TLongtail_Hash(scan_context->m_Hashes)) = hash;
    }
}

#if 0
TEST(Longtail, ScanContent)
{
    ReadyCallback ready_callback;
    Bikeshed shed = Bikeshed_Create(malloc(BIKESHED_SIZE(131071, 0, 1)), 131071, 0, 1, &ready_callback.cb);

    nadir::TAtomic32 stop = 0;

    static const uint32_t WORKER_COUNT = 7;
    ThreadWorker workers[WORKER_COUNT];
    for (uint32_t i = 0; i < WORKER_COUNT; ++i)
    {
        workers[i].CreateThread(shed, ready_callback.m_Semaphore, &stop);
    }


//    const char* root_path = "D:\\TestContent\\Version_1";
    const char* root_path = "/Users/danengelbrecht/Documents/Projects/blossom_blast_saga/build/default";
//    const char* cache_path = "D:\\Temp\\longtail\\cache";
    const char* cache_path = "/Users/danengelbrecht/tmp/cache";

    ScanExistingDataContext scan_context;
    scan_context.m_Hashes = 0;
    RecurseTree(cache_path, ScanHash, &scan_context);
    uint32_t found_count = GetSize_TLongtail_Hash(scan_context.m_Hashes);
    for (uint32_t b = 0; b < found_count; ++b)
    {
        printf("Block %u, hash %llu\n",
            b,
            (unsigned long long)scan_context.m_Hashes[b]);
    }
    Free_TLongtail_Hash(scan_context.m_Hashes);


    nadir::TAtomic32 pendingCount = 0;
    nadir::TAtomic32 assetCount = 0;
 
    ProcessHashContext context;
    context.m_Shed = shed;
    context.m_HashJobs = new HashJob[1048576];
    context.m_AssetCount = &assetCount;
    context.m_PendingCount = &pendingCount;

    RecurseTree(root_path, ProcessHash, &context);

    int32_t old_pending_count = 0;
    while (pendingCount > 0)
    {
        if (Bikeshed_ExecuteOne(shed, 0))
        {
            continue;
        }
        if (old_pending_count != pendingCount)
        {
            old_pending_count = pendingCount;
            printf("Files left to hash: %d\n", old_pending_count);
        }
        nadir::Sleep(1000);
    }

    uint32_t hash_size = jc::HashTable<uint64_t, char*>::CalcSize(assetCount);
    void* hash_mem = malloc(hash_size);
    jc::HashTable<uint64_t, Asset*> hashes;
    hashes.Create(assetCount, hash_mem);

    uint64_t redundant_file_count = 0;
    uint64_t redundant_byte_size = 0;
    uint64_t total_byte_size = 0;

    for (int32_t i = 0; i < assetCount; ++i)
    {
        Asset* asset = &context.m_HashJobs[i].m_Asset;
        total_byte_size += asset->m_Size;
        meow_u128 hash = asset->m_Hash;
        uint64_t hash_key = MeowU64From(hash, 0);
        Asset** existing = hashes.Get(hash_key);
        if (existing)
        {
            if (MeowHashesAreEqual(asset->m_Hash, (*existing)->m_Hash))
            {
                printf("File `%s` matches file `%s` (%llu bytes): %08X-%08X-%08X-%08X\n",
                    asset->m_Path,
                    (*existing)->m_Path,
                    (unsigned long long)asset->m_Size,
                    MeowU32From(hash, 3), MeowU32From(hash, 2), MeowU32From(hash, 1), MeowU32From(hash, 0));
                redundant_byte_size += asset->m_Size;
                ++redundant_file_count;
                continue;
            }
            else
            {
                printf("Collision!\n");
                assert(false);
            }
        }
        hashes.Put(hash_key, asset);
    }
    printf("Found %llu redundant files comprising %llu bytes out of %llu bytes\n", (unsigned long long)redundant_file_count, (unsigned long long)redundant_byte_size, (unsigned long long)total_byte_size);

    Longtail_Builder builder;

    {
        DiskBlockStorage disk_block_storage(cache_path);
        CompressStorage block_storage(&disk_block_storage.m_Storage, shed, &pendingCount);
        {
            SimpleWriteStorage write_storage(&block_storage.m_Storage, 131072u);
            Longtail_Builder_Initialize(&builder, &write_storage.m_Storage);
            {
                jc::HashTable<uint64_t, Asset*>::Iterator it = hashes.Begin();
                while (it != hashes.End())
                {
                    uint64_t content_hash = *it.GetKey();
                    Asset* asset = *it.GetValue();
                    uint64_t path_hash = GetPathHash(asset->m_Path);
                    TLongtail_Hash tag = GetContentTag(asset->m_Path);


                    if (!Longtail_Builder_Add(&builder, path_hash, InputStream, asset, asset->m_Size, tag))
                    {
                        assert(false);
                    }
                    ++it;
                }

                Longtail_FinalizeBuilder(&builder);

                uint32_t asset_count = GetSize_Longtail_AssetEntry(builder.m_AssetEntries);
                uint32_t block_count = GetSize_TLongtail_Hash(builder.m_BlockHashes);

                for (uint32_t a = 0; a < asset_count; ++a)
                {
                    Longtail_AssetEntry* asset_entry = &builder.m_AssetEntries[a];
                    printf("Asset %u, %llu, in block %u, at %llu, size %llu\n",
                        a,
                        (unsigned long long)asset_entry->m_AssetHash,
                        (unsigned int)asset_entry->m_BlockStore.m_BlockIndex,
                        (unsigned long long)asset_entry->m_BlockStore.m_StartOffset,
                        (unsigned long long)asset_entry->m_BlockStore.m_Length);
                }

                for (uint32_t b = 0; b < block_count; ++b)
                {
                    printf("Block %u, hash %llu\n",
                        b,
                        (unsigned long long)builder.m_BlockHashes[b]);
                }
            }
        }


        old_pending_count = 0;
        while (pendingCount > 0)
        {
            if (Bikeshed_ExecuteOne(shed, 0))
            {
                continue;
            }
            if (old_pending_count != pendingCount)
            {
                old_pending_count = pendingCount;
                printf("Files left to store: %d\n", old_pending_count);
            }
            nadir::Sleep(1000);
        }

        printf("Comitted %u files\n", hashes.Size());

        {
            const size_t longtail_size = Longtail_GetSize(GetSize_Longtail_AssetEntry(builder.m_AssetEntries), GetSize_TLongtail_Hash(builder.m_BlockHashes));
            struct Longtail* longtail = Longtail_Open(malloc(longtail_size),
                GetSize_Longtail_AssetEntry(builder.m_AssetEntries), builder.m_AssetEntries,
                GetSize_TLongtail_Hash(builder.m_BlockHashes), builder.m_BlockHashes);

            {
                SimpleReadStorage read_storage(builder.m_BlockHashes, &block_storage.m_Storage);
                jc::HashTable<uint64_t, Asset*>::Iterator it = hashes.Begin();
                while (it != hashes.End())
                {
                    uint64_t hash_key = *it.GetKey();
                    Asset* asset = *it.GetValue();
                    uint64_t path_hash = GetPathHash(asset->m_Path);

                    uint8_t* data = 0;
                    if (!Longtail_Read(longtail, &read_storage.m_Storage, path_hash, OutputStream, &data))
                    {
                        assert(false);
                    }

                    meow_state state;
                    MeowBegin(&state, MeowDefaultSeed);
                    MeowAbsorb(&state, GetSize_uint8_t(data), data);
                    uint64_t verify_hash = MeowU64From(MeowEnd(&state, 0), 0);
                    assert(hash_key == verify_hash);

                    Free_uint8_t(data);
                    ++it;
                }
            }
            printf("Read back %u files\n", hashes.Size());

            free(longtail);
        }
    }
    struct OrderedCacheContent* ordered_content = BuildOrderedCacheContent(
        GetSize_Longtail_AssetEntry(builder.m_AssetEntries), builder.m_AssetEntries,
        GetSize_TLongtail_Hash(builder.m_BlockHashes), builder.m_BlockHashes);

    Longtail_Builder builder2;
    {
        DiskBlockStorage disk_block_storage2(cache_path);
        CompressStorage block_storage2(&disk_block_storage2.m_Storage, shed, &pendingCount);

        {
            SimpleWriteStorage write_storage2(&block_storage2.m_Storage, 131072u);
            Longtail_Builder_Initialize(&builder2, &write_storage2.m_Storage);

            uint32_t block_asset_count = GetSize_Longtail_BlockAssets(ordered_content->m_BlockAssets);

            for (uint32_t ba = 0; ba < block_asset_count; ++ba)
            {
                TLongtail_Hash block_hash = ordered_content->m_BlockHashes[ba];

                uint32_t block_asset_count = ordered_content->m_BlockAssets[ba].m_AssetCount;
                uint32_t asset_entry_index = ordered_content->m_BlockAssets[ba].m_AssetIndex;

                Longtail_Builder_AddExistingBlock(&builder2, block_hash, block_asset_count, &ordered_content->m_AssetEntries[asset_entry_index]);
            }

            Longtail_FinalizeBuilder(&builder2);
        }

        while (pendingCount > 0)
        {
            if (Bikeshed_ExecuteOne(shed, 0))
            {
                continue;
            }
            if (old_pending_count != pendingCount)
            {
                old_pending_count = pendingCount;
                printf("Files2 left to store: %d\n", old_pending_count);
            }
            nadir::Sleep(1000);
        }
        printf("Added %u existing assets in %u blocks\n", GetSize_Longtail_AssetEntry(ordered_content->m_AssetEntries), GetSize_TLongtail_Hash(ordered_content->m_BlockHashes));

        const size_t longtail_size = Longtail_GetSize(GetSize_Longtail_AssetEntry(builder2.m_AssetEntries), GetSize_TLongtail_Hash(builder2.m_BlockHashes));
        struct Longtail* longtail = Longtail_Open(malloc(longtail_size),
            GetSize_Longtail_AssetEntry(builder2.m_AssetEntries), builder2.m_AssetEntries,
            GetSize_TLongtail_Hash(builder2.m_BlockHashes), builder2.m_BlockHashes);

        {
            SimpleReadStorage read_storage(builder2.m_BlockHashes, &block_storage2.m_Storage);
            jc::HashTable<uint64_t, Asset*>::Iterator it = hashes.Begin();
            while (it != hashes.End())
            {
                uint64_t hash_key = *it.GetKey();
                Asset* asset = *it.GetValue();
                uint64_t path_hash = GetPathHash(asset->m_Path);

                uint8_t* data = 0;
                if (!Longtail_Read(longtail, &read_storage.m_Storage, path_hash, OutputStream, &data))
                {
                    assert(false);
                }

                meow_state state;
                MeowBegin(&state, MeowDefaultSeed);
                MeowAbsorb(&state, GetSize_uint8_t(data), data);
                uint64_t verify_hash = MeowU64From(MeowEnd(&state, 0), 0);
                assert(hash_key == verify_hash);

                Free_uint8_t(data);
                ++it;
            }
        }
        printf("Read back %u files\n", hashes.Size());

        free(longtail);
    }

    free(hash_mem);
    for (int32_t i = 0; i < assetCount; ++i)
    {
        Asset* asset = &context.m_HashJobs[i].m_Asset;
        free((void*)asset->m_Path);
    }
    delete [] context.m_HashJobs;

    nadir::AtomicAdd32(&stop, 1);
    ReadyCallback::Ready(&ready_callback.cb, 0, WORKER_COUNT);
    for (uint32_t i = 0; i < WORKER_COUNT; ++i)
    {
        workers[i].JoinThread();
    }

    free(shed);
}
#endif

#endif // 0