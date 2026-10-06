#pragma once

#if (defined(SF2000) || defined(PLATFORM_SF2000)) && !defined(PC_TEST)
#include <cstdio>
#include <cstdlib>
#include <string>
#include <cstdint>
#include <chrono>
#include <functional>

// Include standard concurrency headers so their include guards are set
#include <mutex>
#include <condition_variable>
#include <thread>

#ifdef __cplusplus
extern "C" {
#endif
static inline int rmdir(const char* path) { return ::remove(path); }
#ifdef __cplusplus
}
#endif

namespace std
{
    using ::snprintf;
    using ::vsnprintf;
    using ::abs;
    using ::labs;
    using ::llabs;
    using ::strtof;
    using ::strtod;
    using ::strtold;
    using ::strtol;
    using ::strtoll;
    using ::strtoul;
    using ::strtoull;

    struct once_flag
    {
        bool _called = false;
        constexpr once_flag() noexcept = default;
        once_flag(const once_flag&) = delete;
        once_flag& operator=(const once_flag&) = delete;
    };

    template<typename _Callable, typename... _Args>
    void call_once(once_flag& __flag, _Callable&& __f, _Args&&... __args)
    {
        if (!__flag._called)
        {
            __flag._called = true;
            std::forward<_Callable>(__f)(std::forward<_Args>(__args)...);
        }
    }

    inline string to_string(int val)
    {
        char buf[32];
        ::snprintf(buf, sizeof(buf), "%d", val);
        return string(buf);
    }

    inline string to_string(unsigned int val)
    {
        char buf[32];
        ::snprintf(buf, sizeof(buf), "%u", val);
        return string(buf);
    }

    inline string to_string(long val)
    {
        char buf[32];
        ::snprintf(buf, sizeof(buf), "%ld", val);
        return string(buf);
    }

    inline string to_string(unsigned long val)
    {
        char buf[32];
        ::snprintf(buf, sizeof(buf), "%lu", val);
        return string(buf);
    }

    inline string to_string(long long val)
    {
        char buf[32];
        ::snprintf(buf, sizeof(buf), "%lld", val);
        return string(buf);
    }

    inline string to_string(unsigned long long val)
    {
        char buf[32];
        ::snprintf(buf, sizeof(buf), "%llu", val);
        return string(buf);
    }

    inline string to_string(float val)
    {
        char buf[32];
        ::snprintf(buf, sizeof(buf), "%f", (double)val);
        return string(buf);
    }

    inline string to_string(double val)
    {
        char buf[32];
        ::snprintf(buf, sizeof(buf), "%f", val);
        return string(buf);
    }

    inline int stoi(const string& str, size_t* idx = nullptr, int base = 10)
    {
        char* end = nullptr;
        long val = ::strtol(str.c_str(), &end, base);
        if (idx) *idx = (size_t)(end - str.c_str());
        return (int)val;
    }

    inline long stol(const string& str, size_t* idx = nullptr, int base = 10)
    {
        char* end = nullptr;
        long val = ::strtol(str.c_str(), &end, base);
        if (idx) *idx = (size_t)(end - str.c_str());
        return val;
    }

    inline long long stoll(const string& str, size_t* idx = nullptr, int base = 10)
    {
        char* end = nullptr;
        long long val = ::strtoll(str.c_str(), &end, base);
        if (idx) *idx = (size_t)(end - str.c_str());
        return val;
    }

    inline unsigned long stoul(const string& str, size_t* idx = nullptr, int base = 10)
    {
        char* end = nullptr;
        unsigned long val = ::strtoul(str.c_str(), &end, base);
        if (idx) *idx = (size_t)(end - str.c_str());
        return val;
    }

    inline unsigned long long stoull(const string& str, size_t* idx = nullptr, int base = 10)
    {
        char* end = nullptr;
        unsigned long long val = ::strtoull(str.c_str(), &end, base);
        if (idx) *idx = (size_t)(end - str.c_str());
        return val;
    }

    inline float stof(const string& str, size_t* idx = nullptr)
    {
        char* end = nullptr;
        float val = ::strtof(str.c_str(), &end);
        if (idx) *idx = (size_t)(end - str.c_str());
        return val;
    }

    inline double stod(const string& str, size_t* idx = nullptr)
    {
        char* end = nullptr;
        double val = ::strtod(str.c_str(), &end);
        if (idx) *idx = (size_t)(end - str.c_str());
        return val;
    }

#if !defined(_GLIBCXX_HAS_GTHREADS)
    struct mutex
    {
        constexpr mutex() noexcept = default;
        ~mutex() = default;
        mutex(const mutex&) = delete;
        mutex& operator=(const mutex&) = delete;
        void lock() {}
        bool try_lock() { return true; }
        void unlock() {}
    };

    struct recursive_mutex
    {
        constexpr recursive_mutex() noexcept = default;
        ~recursive_mutex() = default;
        recursive_mutex(const recursive_mutex&) = delete;
        recursive_mutex& operator=(const recursive_mutex&) = delete;
        void lock() {}
        bool try_lock() { return true; }
        void unlock() {}
    };

    enum class cv_status { no_timeout, timeout };

    class condition_variable
    {
    public:
        condition_variable() = default;
        ~condition_variable() = default;
        condition_variable(const condition_variable&) = delete;
        condition_variable& operator=(const condition_variable&) = delete;
        void notify_one() noexcept {}
        void notify_all() noexcept {}
        template<typename _Lock>
        void wait(_Lock&) {}
        template<typename _Lock, typename _Predicate>
        void wait(_Lock&, _Predicate __p)
        {
            while (!__p()) {}
        }
        template<typename _Lock, typename _Rep, typename _Period>
        cv_status wait_for(_Lock&, const std::chrono::duration<_Rep, _Period>&)
        {
            return cv_status::no_timeout;
        }
        template<typename _Lock, typename _Rep, typename _Period, typename _Predicate>
        bool wait_for(_Lock&, const std::chrono::duration<_Rep, _Period>&, _Predicate __p)
        {
            return __p();
        }
        template<typename _Lock, typename _Clock, typename _Duration>
        cv_status wait_until(_Lock&, const std::chrono::time_point<_Clock, _Duration>&)
        {
            return cv_status::no_timeout;
        }
        template<typename _Lock, typename _Clock, typename _Duration, typename _Predicate>
        bool wait_until(_Lock&, const std::chrono::time_point<_Clock, _Duration>&, _Predicate __p)
        {
            return __p();
        }
    };

    class thread
    {
    public:
        class id
        {
        public:
            constexpr id() noexcept : _id(0) {}
            explicit id(uint32_t i) : _id(i) {}
            bool operator==(const id& o) const { return _id == o._id; }
            bool operator!=(const id& o) const { return _id != o._id; }
            bool operator<(const id& o) const { return _id < o._id; }
        private:
            uint32_t _id;
        };

        thread() noexcept {}
        template<typename _Callable, typename... _Args>
        explicit thread(_Callable&&, _Args&&...) {}
        ~thread() = default;
        thread(thread&&) = default;
        thread& operator=(thread&&) = default;
        thread(const thread&) = delete;
        thread& operator=(const thread&) = delete;
        bool joinable() const noexcept { return false; }
        void join() {}
        void detach() {}
        id get_id() const noexcept { return id(1); }
    };

    namespace this_thread
    {
        inline void yield() noexcept {}
        template<typename _Rep, typename _Period>
        void sleep_for(const std::chrono::duration<_Rep, _Period>&) {}
        inline thread::id get_id() noexcept { return thread::id(1); }
    }
#endif

}
#endif
