#define _WIN32_WINNT 0x0A00

#include <stdint.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <future>
#include <filesystem>
#include <stdexcept>

#include <boost/asio.hpp>
#include <boost/crc.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>

#include <memoryapi.h>

#define READ_EVERY_PAGE

int g_threads = 1;
int g_blocksize_kb = 128;

class FastDirCRC {
protected:
    boost::asio::thread_pool thrpool { (size_t)g_threads };
    boost::asio::io_context ioctx;
    std::atomic_int64_t processed_bytes { 0 };

    virtual void process_file(const std::filesystem::path& path) = 0;

    void process_directory_impl(const std::filesystem::path& dir) {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                boost::asio::post(thrpool.executor(), [&, entry]() {
                    process_file(entry);
                });
            }
            else if (entry.is_directory()) {
                process_directory_impl(entry.path());
            }
        }
    }

public:
    FastDirCRC() {}

    void process_directory(const std::filesystem::path& dir) {
        process_directory_impl(dir);
        thrpool.wait();
    }

    int64_t get_processed_bytes() const {
        return processed_bytes.load();
    }
};


void PrefetchRegion(const boost::interprocess::mapped_region& region) {
    WIN32_MEMORY_RANGE_ENTRY memrange;
    memrange.NumberOfBytes = region.get_size();
    memrange.VirtualAddress = (uint8_t*)region.get_address();
    if (PrefetchVirtualMemory(GetCurrentProcess(), 1, &memrange, 0) == FALSE) {
        std::cerr << "PrefetchVirtualMemory failed: " << GetLastError() << "\n";
    }
}


class FastDirCRCFileMapping: public FastDirCRC {
public:
    virtual void process_file(const std::filesystem::path& path) {
        boost::interprocess::file_mapping file(path.generic_wstring().c_str(), boost::interprocess::read_only);
        boost::interprocess::mapped_region region(file, boost::interprocess::read_only);
        auto data = region.get_address();
        auto size = region.get_size();
        
        boost::crc_32_type crc;
        int64_t offset = 0;
        while(offset < size) {
            WIN32_MEMORY_RANGE_ENTRY memrange;
            memrange.NumberOfBytes = g_blocksize_kb * 1024LL;
            memrange.VirtualAddress = (uint8_t*)data + offset;
            if (memrange.NumberOfBytes > size - offset) {
                memrange.NumberOfBytes = size - offset;
            }
            if (PrefetchVirtualMemory(GetCurrentProcess(), 1, &memrange, 0) == FALSE) {
                std::cerr << "PrefetchVirtualMemory failed: " << GetLastError() << "\n";
            }
#if defined(READ_EVERY_PAGE)
            for(int64_t i = 0; i < memrange.NumberOfBytes; i += 4096) {
                crc.process_byte(((uint8_t*)data)[i]);
            }
#endif
            offset += memrange.NumberOfBytes;
        }
        processed_bytes += size;
        //std::cout << "Processed file: " << path << ", CRC: " << crc.checksum() << "\n";
    }
};


class FastDirCRCFileMappingStep: public FastDirCRC {
protected:
    struct MapCtx {
        int64_t offset = 0;
        int64_t size = 0;
        boost::interprocess::mapped_region region;

        MapCtx(boost::interprocess::file_mapping& file, int64_t offset, int64_t size)
            : offset(offset), size(size)
        {
            if (size == 0)
                return;
            region = {file, boost::interprocess::read_only, offset, (size_t)size};

            PrefetchRegion(region);
        }

        MapCtx() {}
    };

    struct FileCtx {
        boost::interprocess::file_mapping file;
        int64_t offset;
        int64_t size;

        FileCtx(const std::filesystem::path& path)
            : file(path.generic_wstring().c_str(), boost::interprocess::read_only)
        {
            size = std::filesystem::file_size(path);
            offset = 0;
        }

        MapCtx NextMapCtx() {
            auto blocksize = g_blocksize_kb * 1024LL;
            blocksize = (std::min)(blocksize, size - offset);
            auto retval = MapCtx(file, offset, blocksize);
            offset += blocksize;
            return retval;
        }
    };

public:
    virtual void process_file(const std::filesystem::path& path) {
        FileCtx file(path);
        
        boost::crc_32_type crc;
        for(;;) {
            MapCtx curctx = file.NextMapCtx();
            if (curctx.size == 0)
                break;
            
            auto data = (uint8_t*)curctx.region.get_address();
#if defined(READ_EVERY_PAGE)
            for(int64_t i = 0; i < curctx.size; i += 4096) {
                crc.process_byte(data[i]);
            }
#endif
            processed_bytes += curctx.size;
        }
        //std::cout << "Processed file: " << path << ", CRC: " << crc.checksum() << "\n";
    }
};


class FastDirCRCFileMappingStepPipeline: public FastDirCRCFileMappingStep {
public:
    virtual void process_file(const std::filesystem::path& path) {
        FileCtx file(path);
        
        boost::crc_32_type crc;
        std::future<MapCtx> nextctx = std::async(std::launch::async, [&]() { return file.NextMapCtx(); });
        for(;;) {
            MapCtx curctx = nextctx.get();
            if (curctx.size == 0)
                break;
            nextctx = std::async(std::launch::async, [&]() { return file.NextMapCtx(); });

            auto data = (uint8_t*)curctx.region.get_address();
#if defined(READ_EVERY_PAGE)
            for(int64_t i = 0; i < curctx.size; i += 4096) {
                crc.process_byte(data[i]);
            }
#endif
            processed_bytes += curctx.size;
        }
        //std::cout << "Processed file: " << path << ", CRC: " << crc.checksum() << "\n";
    }
};


class FastDirCRCCFile: public FastDirCRC {
public:
    virtual void process_file(const std::filesystem::path& path) {
        boost::crc_32_type crc;
        std::vector<uint8_t> buffer(g_blocksize_kb * 1024);
        FILE* fp = fopen(path.generic_string().c_str(), "rb");
        for(;;) {
            auto readsize = fread(buffer.data(), 1, buffer.size(), fp);
#if defined(READ_EVERY_PAGE)
            for(int64_t i = 0; i < readsize; i += 4096) {
                crc.process_byte(buffer[i]);
            }
#endif
            if (readsize < buffer.size())
                break;
        }
        processed_bytes += std::filesystem::file_size(path);
        //std::cout << "Processed file: " << path << ", CRC: " << crc.checksum() << "\n";
    }
};


class FastDirCRCWinFile: public FastDirCRC {
public:
    virtual void process_file(const std::filesystem::path& path) {
        boost::crc_32_type crc;
        std::vector<uint8_t> buffer(g_blocksize_kb * 1024);
        HANDLE hFile = CreateFileW(path.generic_wstring().c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        for(;;) {
            DWORD readsize = 0;
            ReadFile(hFile, buffer.data(), buffer.size(), &readsize, NULL);
#if defined(READ_EVERY_PAGE)
            for(int64_t i = 0; i < readsize; i += 4096) {
                crc.process_byte(buffer[i]);
            }
#endif
            if (readsize < buffer.size())
                break;
        }
        processed_bytes += std::filesystem::file_size(path);
        //std::cout << "Processed file: " << path << ", CRC: " << crc.checksum() << "\n";
    }
};


class FastDirCRCWinFileOverlapped: public FastDirCRC {
public:
    virtual void process_file(const std::filesystem::path& path) {
        boost::asio::random_access_file file(thrpool.get_executor());
        file.open(path.generic_string().c_str(), boost::asio::random_access_file::read_only);
        auto size = file.size();
        boost::crc_32_type crc;
        std::vector<uint8_t> buffer(g_blocksize_kb * 1024);
        int64_t offset = 0;
        while(offset < size) {
            auto readsize = file.read_some_at(offset, boost::asio::buffer(buffer));
#if defined(READ_EVERY_PAGE)
            for(int64_t i = 0; i < readsize; i += 4096) {
                crc.process_byte(buffer[i]);
            }
#endif
            offset += readsize;
        }
        processed_bytes += size;
        //std::cout << "Processed file: " << path << ", CRC: " << crc.checksum() << "\n";
    }
};


int main(int argc, char* argv[]){
    int mode = std::stoi(argv[1]);
    g_threads = std::stoi(argv[2]);
    g_blocksize_kb = std::stoi(argv[3]);

    std::filesystem::path dir = R"__(D:\SteamLibrary\steamapps\common\Cyberpunk 2077\archive\pc\ep1)__";
    auto dowork = [&](auto crc) {
        auto beign_time = std::chrono::steady_clock::now();
        crc.process_directory(dir);
        auto end_time = std::chrono::steady_clock::now();
        auto elapsed_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - beign_time).count();
        std::cout << "Total processed bytes: " << crc.get_processed_bytes() << "\n";
        std::cout << "Elapsed time: " << elapsed_time << " ms\n";
        std::cout << "Average speed: " << (crc.get_processed_bytes() / (elapsed_time / 1000.0) / (1024 * 1024)) << " MB/s\n";
    };

    switch(mode) {
        case 0: {
            dowork(FastDirCRCFileMapping{});
            break;
        }
        case 1: {
            dowork(FastDirCRCFileMappingStep{});
            break;
        }
        case 2: {
            dowork(FastDirCRCFileMappingStepPipeline{});
            break;
        }
        case 3: {
            dowork(FastDirCRCWinFileOverlapped{});
            break;
        }
        case 4: {
            dowork(FastDirCRCCFile{});
            break;
        }
        case 5: {
            dowork(FastDirCRCWinFile{});
            break;
        }
        default:
            ;
    }

    return 0;
}
