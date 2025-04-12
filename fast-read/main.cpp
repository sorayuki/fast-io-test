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

int g_threads = 1;
int g_infile_threads = 1;
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


class FastDirCRCFileMapping: public FastDirCRC {
public:
    virtual void process_file(const std::filesystem::path& path) {
        boost::interprocess::file_mapping file(path.generic_wstring().c_str(), boost::interprocess::read_only);
        boost::interprocess::mapped_region region(file, boost::interprocess::read_only);
        auto data = region.get_address();
        auto size = region.get_size();
        
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
            offset += memrange.NumberOfBytes;
        }
        boost::crc_32_type crc;
        // for(int64_t i = 0; i < size; i += 4096) {
        //     crc.process_byte(((uint8_t*)data)[i]);
        // }
        processed_bytes += size;
        //std::cout << "Processed file: " << path << ", CRC: " << crc.checksum() << "\n";
    }
};


class FastDirCRCFileMappingStep: public FastDirCRC {
    public:
        virtual void process_file(const std::filesystem::path& path) {
            boost::interprocess::file_mapping file(path.generic_wstring().c_str(), boost::interprocess::read_only);
            auto size = std::filesystem::file_size(path);
            
            int64_t offset = 0;
            while(offset < size) {
                auto cursize = g_blocksize_kb * 1024LL;
                if (cursize > size - offset)
                    cursize = size - offset;
                boost::interprocess::mapped_region region(file, boost::interprocess::read_only, offset, cursize);
                WIN32_MEMORY_RANGE_ENTRY memrange;
                memrange.NumberOfBytes = cursize;
                memrange.VirtualAddress = (uint8_t*)region.get_address();
                if (PrefetchVirtualMemory(GetCurrentProcess(), 1, &memrange, 0) == FALSE) {
                    std::cerr << "PrefetchVirtualMemory failed: " << GetLastError() << "\n";
                }
                offset += memrange.NumberOfBytes;
            }
            boost::crc_32_type crc;
            // for(int64_t i = 0; i < size; i += 4096) {
            //     crc.process_byte(((uint8_t*)data)[i]);
            // }
            processed_bytes += size;
            //std::cout << "Processed file: " << path << ", CRC: " << crc.checksum() << "\n";
        }
};

class FastDirCRCReadFile: public FastDirCRC {
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
            // for(int64_t i = 0; i < readsize; i += 4096) {
            //     crc.process_byte(buffer[i]);
            // }
            offset += readsize;
        }
        processed_bytes += size;
        //std::cout << "Processed file: " << path << ", CRC: " << crc.checksum() << "\n";
    }
};


class FastDirCRCSeqReadFile: public FastDirCRC {
    boost::asio::thread_pool crcpool { (size_t)g_infile_threads };

    virtual void process_file(const std::filesystem::path& path) {
        if (g_threads != 1) {
            throw std::runtime_error("Thread count must be 1 for sequential read");
        }
        boost::asio::random_access_file file(thrpool.get_executor());
        file.open(path.generic_string().c_str(), boost::asio::random_access_file::read_only);
        auto size = file.size();
        boost::crc_32_type crc;

        int blocksize = g_blocksize_kb * 1024;

        int64_t offset = 0;
        std::future<void> prevfuture;
        while(offset < size) {
            std::promise<void> promise;
            auto nf = promise.get_future();
            boost::asio::post(crcpool.get_executor(), [&, offset, pf = std::move(prevfuture), p = std::move(promise)]() mutable {
                auto readsize = size - offset;
                if (readsize > blocksize)
                    readsize = blocksize;
                std::vector<uint8_t> buffer(readsize);
                boost::asio::read_at(file, offset, boost::asio::buffer(buffer));
                if (pf.valid())
                    pf.get();
                // for(int i = 0; i < readsize; i += 4096) {
                //     crc.process_byte(buffer[i]);
                // }
                p.set_value();
            });
            prevfuture = std::move(nf);
            offset += blocksize;
        }
        prevfuture.get();
        //std::cout << "Processed file: " << path << ", CRC: " << crc.checksum() << "\n";
        processed_bytes += size;
    }
};

int main(int argc, char* argv[]){
    int mode = std::stoi(argv[1]);
    g_threads = std::stoi(argv[2]);
    g_infile_threads = std::stoi(argv[3]);
    g_blocksize_kb = std::stoi(argv[4]);

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
            dowork(FastDirCRCReadFile{});
            break;
        }
        case 3: {
            dowork(FastDirCRCSeqReadFile{});
            break;
        }
        default:
            ;
    }

    return 0;
}
