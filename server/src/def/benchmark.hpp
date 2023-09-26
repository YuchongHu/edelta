//
// Created by Lokyin Zhao on 2022/11/3.
//

#ifndef DEDUP_SERVER_BENCHMARK_HPP
#define DEDUP_SERVER_BENCHMARK_HPP

#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include <boost/format.hpp>

#include "def/exception.hpp"

namespace dedup::benchmark {
/**
 * @brief A timer that records the total time through multiple Laps
 */
class Timer {
private:
    using clock_t = std::chrono::steady_clock;

public:
    using time_point_t = clock_t::time_point;
    using duration_t = clock_t::duration;
    using rep_count_t = clock_t::rep;

private:
    /**
     * @brief total time recorded by this timer, representing the number of ticks
     */
    std::atomic<rep_count_t> repCnt_;

public:
    Timer() = default;

    Timer(Timer const &) = delete;

    Timer(Timer &&) = delete;

    Timer &operator=(Timer const &) = delete;

    Timer &operator=(Timer &&) = delete;

    /**
     * @brief getShareIndex the total time recorded by this timer
     */
    [[nodiscard]] auto getTotalDuration() const {
        return duration_t{this->repCnt_};
    }

    /**
     * @brief record a lap to the total time
     */
    void recordLap(const duration_t::rep &r) {
        repCnt_ += r;
    }

    /**
     * @brief getShareIndex the current time pointer of the timer
     */
    static time_point_t Now() noexcept {
        return clock_t::now();
    }

    /**
     * @brief convert the duration to a string, in 'mins secs millisecs' format
     */
    [[nodiscard]] std::string to_string() const {
        std::stringstream strs{};
        auto duration = getTotalDuration();
        auto minutes = std::chrono::duration_cast<std::chrono::minutes>(duration).count();
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration).count() % 60;
        auto millisecs = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() % 1000;
        strs << minutes << "m " << seconds << "s " << millisecs << "ms";
        return strs.str();
    }

    [[nodiscard]] uint64_t to_seconds() const {
        return std::chrono::duration_cast<std::chrono::seconds>(getTotalDuration()).count();
    }

    [[nodiscard]] uint64_t to_milliseconds() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(getTotalDuration()).count();
    }
};

/**
 * @brief a scoped lap start timing immediately when constructing,
 * end timing when destructing, and automatically record in the corresponding timer
 */
class ScopedLap {
private:
    Timer &timer_;
    const Timer::time_point_t startTime_;

public:
    ScopedLap() = delete;

    explicit ScopedLap(Timer &timer) : timer_(timer), startTime_(Timer::Now()) {
    }

    ScopedLap(ScopedLap const &) = delete;

    ScopedLap(ScopedLap &&) = delete;

    ScopedLap &operator=(ScopedLap const &) = delete;

    ScopedLap &operator=(ScopedLap &&) = delete;

    ~ScopedLap() {
        this->timer_.recordLap((Timer::Now() - startTime_).count());
    }
};

/**
 * @brief tag for UniqueLap ctor, indicating that a UniqueLap obj does not timing immediately after constructed
 */
struct DeferLap_t {};
inline constexpr DeferLap_t DeferLap;

class UniqueLap {
private:
    Timer *timer_;
    bool isTiming_;
    Timer::duration_t totalLapDuration_;
    Timer::time_point_t lastLapTime_;

public:
    UniqueLap() = delete;

    explicit UniqueLap(Timer &timer)
        : timer_(&timer), isTiming_(true), totalLapDuration_(0), lastLapTime_(Timer::Now()) {
    }

    UniqueLap(Timer &timer, DeferLap_t deferLap)
        : timer_(&timer), isTiming_(false), totalLapDuration_(0), lastLapTime_() {
    }

    UniqueLap(UniqueLap const &) = delete;

    UniqueLap &operator=(UniqueLap const &) = delete;

    UniqueLap(UniqueLap &&obj) noexcept
        : timer_(obj.timer_), isTiming_(obj.isTiming_), totalLapDuration_(obj.totalLapDuration_),
          lastLapTime_(obj.lastLapTime_) {
        obj.timer_ = nullptr;
        obj.isTiming_ = false;
        obj.totalLapDuration_ = Timer::duration_t::zero();
        obj.lastLapTime_ = Timer::time_point_t{};
    }

    UniqueLap &operator=(UniqueLap &&rhs) noexcept {
        if (this != &rhs) { // avoid self-assignment
            // invalidate the lhs
            if (this->timer_ != nullptr) {
                if (this->isTiming_) {
                    this->totalLapDuration_ += Timer::Now() - lastLapTime_;
                }
                this->timer_->recordLap((this->totalLapDuration_).count());
            }
            // take over elements from the rhs
            this->timer_ = nullptr;
            this->isTiming_ = false;
            this->totalLapDuration_ = Timer::duration_t::zero();
            this->lastLapTime_ = Timer::time_point_t{};
            this->swap(rhs);
        }
        return *this;
    }

    void swap(UniqueLap &obj) noexcept {
        using std::swap;
        swap(this->timer_, obj.timer_);
        swap(this->isTiming_, obj.isTiming_);
        swap(this->totalLapDuration_, obj.totalLapDuration_);
        swap(this->lastLapTime_, obj.lastLapTime_);
    }

    ~UniqueLap() noexcept {
        if (timer_ != nullptr) {
            if (isTiming_) {
                totalLapDuration_ += Timer::Now() - lastLapTime_;
            }
            timer_->recordLap((totalLapDuration_).count());
        }
    }

    /**
     * @brief start timing
     * @throw utility::DedupException if the lap is already in timing status
     */
    void start() {
        if (isTiming_) {
            throw DedupException(BOOST_CURRENT_LOCATION, "try to start a timing lap");
        }
        isTiming_ = !isTiming_;
        lastLapTime_ = Timer::Now();
    }

    /**
     * @brief stop timing, and record the time of this lap
     * @throw utility::DedupException if the lap is already in non-timing status
     */
    void stop() {
        if (!isTiming_) {
            throw DedupException(BOOST_CURRENT_LOCATION, "try to stop a non-timing lap");
        }
        isTiming_ = !isTiming_;
        totalLapDuration_ += Timer::Now() - lastLapTime_;
    }
};

} // namespace dedup::benchmark

/**
 * @brief convert the duration to a string, in '%Mmins %Ssecs' format
 */
inline std::string to_string(dedup::benchmark::Timer const &obj) {
    return obj.to_string();
}

inline void swap(dedup::benchmark::UniqueLap &lhs, dedup::benchmark::UniqueLap &rhs) noexcept {
    lhs.swap(rhs);
}

namespace std {
template <>
inline void swap(dedup::benchmark::UniqueLap &lhs, dedup::benchmark::UniqueLap &rhs) noexcept {
    lhs.swap(rhs);
}
} // namespace std

namespace dedup {
class Benchmark {
private:
    [[noreturn]] static void Cmd() {
        while (true) {
            std::string cmd{};
            std::cin >> cmd;
            if (cmd == "r" /* for restore */) {
                std::cout << RestoreBenchmarkResult() << std::endl;
            }else {
                std::cout << Result() << std::endl;
            }
        }
    }

    [[noreturn]] static void FileLog() {
        while (true) {
//            std::ofstream log{std::string{config::BENCHMARK_LOG_NAME} + '-' +
//                                  std::to_string(config::GetAddress().port()),
//                              std::ios::trunc};
//            log << Result();
//            log.close();
            std::this_thread::sleep_for(config::BENCHMARK_LOG_INTERVAL);
            std::cout << Result() << std::endl;
        }
    }

    static std::string SizeToString(uint64_t size) {
        auto kiloByte = (size >> 10) % 1024;
        auto megaByte = (size >> 20) % 1024;
        auto gigaByte = (size >> 30);
        boost::format fmt{"%1%G %2%M %3%K"};
        fmt % gigaByte % megaByte % kiloByte;
        return fmt.str();
    }

    inline static std::atomic<uint64_t> UniqueCnt_{0};

    inline static std::atomic<uint64_t> DuplicateCnt_{0};
    inline static std::atomic<uint64_t> DeltaCompressedCnt_{0};
    inline static std::atomic<uint64_t> SecretSize_{0};

    inline static std::atomic<uint64_t> ShareSize_{0};
    inline static std::atomic<uint64_t> DeltaCompressedSize_{0};
    inline static std::atomic<uint64_t> DedupSize_{0};
    inline static std::atomic<uint64_t> RecipeSize_{0};

public:
    static void Init() {
        static std::once_flag onceFlag{};
        std::call_once(onceFlag, []() {
            std::thread{[]() { Cmd(); }}.detach();
            std::thread{[]() { FileLog(); }}.detach();
        });
    }

    static std::string Result() {
        boost::format outFmt{"[Benchmark]\n"
                             "\tfirst stage dedup time: %1%\n"
                             "\tsecond stage dedup time: %2%\n"
                             "\tsuper feature time: %3%\n"
                             "\trestore time: %4%\n"
                             "\trestore from delta time: %17%\n"
                             "\tdisk write time: %12%\n"
                             "\ttotal shares: %5%\n"
                             "\tunique shares: %6% (%7%%%)\n"
                             "\tduplicate shares: %8% (%9%%%) \n"
                             "\tdelta compressed shares: %10% (%11%%%)\n"
                             "\tsecret size: %13%\n"
                             "\tshare size: %14%\n"
                             "\tdedup size: %15%\n"
                             "\tdelta compressed size: %16%\n"
                             "\trecipe size: %18%\n"};
        auto firstStageTime = FirstStageTimer().to_string();
        auto secondStageTime = SecondStageTimer().to_string();
        auto superFeatureTime = SuperFeatureTimer().to_string();
        auto restoreTime = RestoreTimer().to_string();
        auto diskWriteTime = DiskWriteTimer().to_string();
        auto uniqueShare = UniqueCnt_.load();
        auto duplicateShare = DuplicateCnt_.load();
        auto deltaShare = DeltaCompressedCnt_.load();
        auto totalShare = uniqueShare + duplicateShare + deltaShare;
        auto uniqueRatio = totalShare == 0 ? 0 : uniqueShare * 100 / totalShare;
        auto duplicateRatio = totalShare == 0 ? 0 : duplicateShare * 100 / totalShare;
        auto deltaRatio = totalShare == 0 ? 0 : deltaShare * 100 / totalShare;
        auto totalSecretSize = SizeToString(SecretSize_.load());
        auto totalShareSize = SizeToString(ShareSize_.load());
        auto dedupSize = SizeToString(DedupSize_.load());
        auto deltaCompressedSize = SizeToString(DeltaCompressedSize_.load());
        auto restoreFromDeltaTime = RestoreFromDeltaTimer().to_string();
        auto recipeSize = SizeToString(RecipeSize_.load());

        outFmt.bind_arg(1, firstStageTime);
        outFmt.bind_arg(2, secondStageTime);
        outFmt.bind_arg(3, superFeatureTime);
        outFmt.bind_arg(4, restoreTime);
        outFmt.bind_arg(5, totalShare);
        outFmt.bind_arg(6, uniqueShare);
        outFmt.bind_arg(7, uniqueRatio);
        outFmt.bind_arg(8, duplicateShare);
        outFmt.bind_arg(9, duplicateRatio);
        outFmt.bind_arg(10, deltaShare);
        outFmt.bind_arg(11, deltaRatio);
        outFmt.bind_arg(12, diskWriteTime);
        outFmt.bind_arg(13, totalSecretSize);
        outFmt.bind_arg(14, totalShareSize);
        outFmt.bind_arg(15, dedupSize);
        outFmt.bind_arg(16, deltaCompressedSize);
        outFmt.bind_arg(17, restoreFromDeltaTime);
        outFmt.bind_arg(18, recipeSize);

        return outFmt.str();
    }

    static std::string RestoreBenchmarkResult() {
        boost::format outFmt{"[Restore Benchmark]\n"
                             "\ttotal time: %1%\n"
                             "\tunique/duplicate share time: %5%\n"
                             "\tdelta share time: %2%\n"
                             "\trecipe time: %3%\n"
                             "\tindex time: %4%\n"
                             "\t-delta shares-\n"
                             "\tbase share index time: %6%\n"
                             "\tbase share data time: %7%\n"
                             "\tdelta data time: %8%\n"
                             "\tdelta compute time: %9%\n"
                             };
        outFmt.bind_arg(1, RestoreTimer().to_string());
        outFmt.bind_arg(2, RestoreFromDeltaTimer().to_string());
        outFmt.bind_arg(3, RestoreRecipeTimer().to_string());
        outFmt.bind_arg(4, RestoreShareIndexTimer().to_string());
        outFmt.bind_arg(5, RestoreCommonShareTimer().to_string());
        outFmt.bind_arg(6, RestoreDeltaBaseIndexTimer().to_string());
        outFmt.bind_arg(7, RestoreDeltaBaseShareDataTimer().to_string());
        outFmt.bind_arg(8, RestoreDeltaShareDataTimer().to_string());
        outFmt.bind_arg(9, DeltaRestoreComputeTimer().to_string());

        return outFmt.str();
    }

    static benchmark::Timer &FirstStageTimer() {
        static benchmark::Timer timer{};
        return timer;
    }

    static benchmark::Timer &SecondStageTimer() {
        static benchmark::Timer timer{};
        return timer;
    }

    static benchmark::Timer &RestoreTimer() {
        static benchmark::Timer timer{};
        return timer;
    }

    static benchmark::Timer &SuperFeatureTimer() {
        static benchmark::Timer timer{};
        return timer;
    }

    static benchmark::Timer &RestoreFromDeltaTimer() {
        static benchmark::Timer timer{};
        return timer;
    }

    static benchmark::Timer &RestoreDeltaShareDataTimer(){
        static benchmark::Timer timer{};
        return timer;
    }

    static benchmark::Timer &RestoreDeltaBaseShareDataTimer(){
        static benchmark::Timer timer{};
        return timer;
    }

    static benchmark::Timer &DiskWriteTimer() {
        static benchmark::Timer timer{};
        return timer;
    }

    static benchmark::Timer &RestoreCommonShareTimer() {
        static benchmark::Timer timer{};
        return timer;
    }

    static benchmark::Timer &RestoreRecipeTimer() {
        static benchmark::Timer timer{};
        return timer;
    }

    static benchmark::Timer &RestoreDeltaIndexTimer() {
        static benchmark::Timer timer{};
        return timer;
    }

    static benchmark::Timer &RestoreDeltaBaseIndexTimer() {
        static benchmark::Timer timer{};
        return timer;
    }

    static benchmark::Timer &RestoreShareIndexTimer() {
        static benchmark::Timer timer{};
        return timer;
    }

    static benchmark::Timer &DeltaRestoreComputeTimer(){
        static benchmark::Timer timer{};
        return timer;
    }

    static void LogSecretSize(uint64_t size) {
        SecretSize_ += size;
    }

    static void LogUniqueShare(std::size_t shareSize) {
        UniqueCnt_++;
        ShareSize_ += shareSize;
    }

    static void LogDuplicateShare(std::size_t dupShareSize) {
        DuplicateCnt_++;
        DedupSize_ += dupShareSize;
    }

    static void LogDeltaCompressed(std::size_t shareSize, std::size_t deltaSize) {
        DeltaCompressedCnt_++;
        ShareSize_ += deltaSize;
        DeltaCompressedSize_ += shareSize - deltaSize;
    }

    static void LogRecipe(std::size_t recipeSize) {
        RecipeSize_ += recipeSize;
    }
};
} // namespace dedup

#endif //RECIPESTORER_CPP_BENCHMARK_HPP
