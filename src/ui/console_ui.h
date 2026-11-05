#pragma once

#include <mutex>
#include <string_view>

namespace pmake::ui {

class ConsoleUI {
public:
    explicit ConsoleUI(bool quiet = false, bool color = false);

    void onTaskStarted(std::string_view displayName, int total);
    void onTaskFinished(std::string_view name, bool success, double duration,
                        std::string_view errorMsg = {});
    void onBuildFinished(bool success, double durationSeconds);

private:
    int  taskIndex_  = 0;
    int  totalTasks_ = 0;
    bool quiet_      = false;
    bool color_      = false;
    std::mutex outputMutex_;
};

} // namespace pmake::ui