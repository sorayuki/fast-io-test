#include <stdint.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <filesystem>
#include <boost/asio.hpp>
#include <boost/crc.hpp>
#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>

class FastDirCRC {
    boost::asio::thread_pool thrpool;
    std::atomic_int64_t processed_bytes { 0 };

public:
    FastDirCRC(int threads) : thrpool(threads) {}

    void process_directory_impl(const std::filesystem::path& dir) {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                boost::asio::post(thrpool.executor(), [&, entry]() {
                    boost::interprocess::file_mapping file(entry.path().generic_wstring().c_str(), boost::interprocess::read_only);
                    boost::interprocess::mapped_region region(file, boost::interprocess::read_only);
                    auto data = region.get_address();
                    auto size = region.get_size();
                    boost::crc_32_type crc;
                    for(int64_t i = 0; i < size; i += 4096) {
                        crc.process_byte(((uint8_t*)data)[i]);
                    }
                    processed_bytes += size;
                    std::cout << "Processed file: " << entry.path() << ", CRC: " << crc.checksum() << "\n";
                });
            }
            else if (entry.is_directory()) {
                process_directory_impl(entry.path());
            }
        }
    }

    void process_directory(const std::filesystem::path& dir) {
        process_directory_impl(dir);
        thrpool.wait();
    }

    int64_t get_processed_bytes() const {
        return processed_bytes.load();
    }
};

int main(int, char**){
    FastDirCRC crc(32);
    std::filesystem::path dir = R"__(D:\SteamLibrary\steamapps\common\Cyberpunk 2077\archive\pc\ep1)__";
    auto beign_time = std::chrono::steady_clock::now();
    crc.process_directory(dir);
    auto end_time = std::chrono::steady_clock::now();
    auto elapsed_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - beign_time).count();
    std::cout << "Total processed bytes: " << crc.get_processed_bytes() << "\n";
    std::cout << "Elapsed time: " << elapsed_time << " ms\n";
    std::cout << "Average speed: " << (crc.get_processed_bytes() / (elapsed_time / 1000.0) / (1024 * 1024)) << " MB/s\n";
    return 0;
}
