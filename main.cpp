#include <filesystem>
#include <list>
#include <mutex>
#include <vector>
#include <future>
#include <assert.h>

namespace filesystem = std::filesystem;

struct DeleteDirectoryContext {
    std::atomic_bool result{ true };
    std::mutex mutex;
    std::list<std::future<void>> tasks;
};

void DeleteDirectoryParallel(const std::wstring& dir_path, std::shared_ptr<int> parent_deleter, DeleteDirectoryContext& context) {
    std::unique_lock<std::mutex> lg(context.mutex);
    auto task = std::async(std::launch::async, [parent_deleter, dir_path, &context]() {
        auto target = filesystem::path(dir_path);
        // 在所有任务完成之后删除空目录
        auto delete_empty_dir = [parent_deleter, target, &context](int*) {
            if (filesystem::exists(target)) {
                assert(filesystem::is_directory(target));
                assert(filesystem::is_empty(target));
                if (!filesystem::remove(target))
                    context.result = false;
            }
        };

        std::shared_ptr<int> remove_on_destruct(new int{ 0 }, delete_empty_dir);

        // 清空目录下的所有东西 
        std::vector<filesystem::path> files_to_remove;
        std::vector<filesystem::path> dirs_to_remove;
        filesystem::directory_iterator end_iter;
        for (filesystem::directory_iterator dir_iter(target); dir_iter != end_iter; ++dir_iter) {
            if (filesystem::is_directory(*dir_iter)) {
                dirs_to_remove.emplace_back(dir_iter->path());
            }
            else if (filesystem::is_regular_file(*dir_iter) || filesystem::is_symlink(*dir_iter)) {
                files_to_remove.emplace_back(dir_iter->path());
            }
        }

        // 把当前目录下所有子文件夹的删除任务放入任务队列
        std::unique_lock<std::mutex> lg(context.mutex);
        for (auto& dir : dirs_to_remove) {
            if (dir.filename() == "." || dir.filename() == "..")
                continue;

            auto async_task = std::async(std::launch::async, [parent_deleter, remove_on_destruct, dir, &context]() {
                DeleteDirectoryParallel(dir.generic_wstring(), remove_on_destruct, context);
            });
            context.tasks.emplace_back(std::move(async_task));
        }
        dirs_to_remove.clear();
        lg.unlock();

        // 删除所有文件
        for (auto& file : files_to_remove) {
            assert(filesystem::is_regular_file(file));
            if (!filesystem::remove(file))
                context.result = false;
        }
    });

    context.tasks.emplace_back(std::move(task)); // 当这个task完成时，所有子文件夹的删除任务已经放入队列了
}

bool DeleteDirectoryFast(const std::wstring& dir_path) {
    DeleteDirectoryContext ctx;
    DeleteDirectoryParallel(dir_path, {}, ctx);

    // 等待所有任务完成
    auto it = ctx.tasks.begin();
    std::unique_lock<std::mutex> lg(ctx.mutex);
    while (it != ctx.tasks.end()) {
        lg.unlock();
        it->get();
        lg.lock();
        ++it;
    }

    return ctx.result;
}

#include <iostream>
int main(int, char**){
    using clock = std::chrono::steady_clock;
    auto begintp = clock::now();
    int r = DeleteDirectoryFast(L"F:\\dev\\boost_1_87_0_11");
    auto endtp = clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endtp - begintp);
    std::cout << "Time taken: " << duration.count() << " ms" << std::endl;
    std::cout << r << std::endl;
    return 0;
}
