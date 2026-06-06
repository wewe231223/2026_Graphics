#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <numeric>
#include <set>
#include <type_traits>
#include <utility>

namespace Utility {
    template<typename Type>
    struct IsDuration : std::false_type {
    };

    template<typename Rep, typename Period>
    struct IsDuration<std::chrono::duration<Rep, Period>> : std::true_type {
    };

    template<typename Type>
    concept TimeUnit = IsDuration<Type>::value;

    class Time final {
    public:
        using Clock = std::chrono::high_resolution_clock;
        using Rep = double;
        using Period = std::nano;
        using TimePoint = Clock::time_point;
        using Duration = std::chrono::duration<double, Period>;

        enum class ScaleMode {
            ResultTimeScaled,
            ResultTimeUnscaled
        };

    private:
        struct Event final {
            TimePoint mInvokeTime{};
            std::chrono::nanoseconds mTimeout{};
            std::function<bool()> mCallBack{ []() { return false; } };

            bool operator<(const Event& Other) const {
                return mInvokeTime < Other.mInvokeTime;
            }
        };

    public:
        Time() = default;
        ~Time() = default;
        Time(const Time& Other) = default;
        Time& operator=(const Time& Other) = default;
        Time(Time&& Other) noexcept = default;
        Time& operator=(Time&& Other) noexcept = default;

    public:
        template<typename ResultType = double, TimeUnit TimeUnitType = std::chrono::seconds>
        [[nodiscard]]
        ResultType GetDeltaTime(ScaleMode Scale = ScaleMode::ResultTimeUnscaled) const {
            if (Scale == ScaleMode::ResultTimeScaled) {
                return std::chrono::duration_cast<std::chrono::duration<ResultType, typename TimeUnitType::period>>(mDeltaTime * mTimeScale).count();
            }

            return std::chrono::duration_cast<std::chrono::duration<ResultType, typename TimeUnitType::period>>(mDeltaTime).count();
        }

        template<typename ResultType = double, TimeUnit TimeUnitType = std::chrono::seconds>
        [[nodiscard]]
        ResultType GetTimeSinceStarted(ScaleMode Scale = ScaleMode::ResultTimeUnscaled) const {
            if (Scale == ScaleMode::ResultTimeScaled) {
                return std::chrono::duration_cast<std::chrono::duration<ResultType, typename TimeUnitType::period>>(mScaledStarted).count();
            }

            Duration AbsoluteElapsed{ Clock::now() - mAbsoluteStarted };
            return std::chrono::duration_cast<std::chrono::duration<ResultType, typename TimeUnitType::period>>(AbsoluteElapsed).count();
        }

        template<typename ResultType = double, TimeUnit TimeUnitType = std::chrono::seconds>
        [[nodiscard]]
        ResultType GetTimeSinceSceneStarted() const {
            Duration Elapsed{ Clock::now() - mSceneStarted };
            return std::chrono::duration_cast<std::chrono::duration<ResultType, typename TimeUnitType::period>>(Elapsed).count();
        }

        template<typename ResultType = double, TimeUnit TimeUnitType = std::chrono::seconds>
        [[nodiscard]]
        ResultType GetSmoothDeltaTime() const {
            Duration SumOfSamples{ std::accumulate(mDeltaTimeBuffer.begin(), mDeltaTimeBuffer.end(), Duration::zero(), [](const Duration& First, const Duration& Second) {
                if (Second.count() <= 0.0) {
                    return First;
                }

                return First + Second;
            }) };
            return std::chrono::duration_cast<std::chrono::duration<ResultType, typename TimeUnitType::period>>(SumOfSamples / mDeltaTimeBufferSize).count();
        }

        template<typename RepType, typename PeriodType>
        void AddEvent(std::chrono::duration<RepType, PeriodType> TimeValue, std::function<bool()>&& CallBack) {
            mEvents.emplace(Event{ Clock::now() + TimeValue, std::chrono::duration_cast<std::chrono::nanoseconds>(TimeValue), std::move(CallBack) });
        }

        TimePoint Now() const {
            return Clock::now();
        }

        double SetTimeScale(double Scale = 1.0) {
            double PreviousScale{ mTimeScale };
            mTimeScale = Scale;
            return PreviousScale;
        }

        double GetTimeScale() const {
            return mTimeScale;
        }

        std::uint64_t GetFrameCount() const {
            return mFrameCount;
        }

        void AdvanceTime() {
            UpdateDeltaTime();
            SampleDeltaTime();
            AddScaledStarted();
            CheckEvent();
        }

        void StartSceneTime() {
            mSceneStarted = Clock::now();
        }

        void Reset() {
            const TimePoint CurrentTime{ Clock::now() };
            mDeltaTime = Duration::zero();
            mScaledStarted = Duration::zero();
            mFrameCount = 0U;
            mDeltaTimeSamplingIndex = 0U;
            mDeltaTimeBuffer.fill(Duration::zero());
            mPrev = CurrentTime;
            mSceneStarted = CurrentTime;
            mAbsoluteStarted = CurrentTime;
            mEvents.clear();
        }

    private:
        void UpdateDeltaTime() {
            TimePoint CurrentTime{ Clock::now() };
            mDeltaTime = std::chrono::duration_cast<Duration>(CurrentTime - mPrev);
            mPrev = CurrentTime;
            ++mFrameCount;
        }

        void AddScaledStarted() {
            mScaledStarted += mDeltaTime * mTimeScale;
        }

        void SampleDeltaTime() {
            mDeltaTimeBuffer[mDeltaTimeSamplingIndex] = mDeltaTime * mTimeScale;
            mDeltaTimeSamplingIndex = (mDeltaTimeSamplingIndex + 1U) % mDeltaTimeBufferSize;
        }

        bool PopEvent() {
            Event CurrentEvent{ *mEvents.begin() };
            if (CurrentEvent.mInvokeTime >= Clock::now()) {
                return false;
            }

            if (std::invoke(CurrentEvent.mCallBack)) {
                mEvents.emplace(Event{ Clock::now() + CurrentEvent.mTimeout, std::move(CurrentEvent.mTimeout), std::move(CurrentEvent.mCallBack) });
            }

            mEvents.erase(mEvents.begin());
            return true;
        }

        void CheckEvent() {
            if (mEvents.empty()) {
                return;
            }

            while (PopEvent()) {
            }
        }

    private:
        Duration mDeltaTime{};
        Duration mScaledStarted{};
        std::uint64_t mFrameCount{};
        static constexpr unsigned int mDeltaTimeBufferSize{ 10U };
        unsigned int mDeltaTimeSamplingIndex{};
        std::array<Duration, mDeltaTimeBufferSize> mDeltaTimeBuffer{};
        TimePoint mPrev{ Clock::now() };
        TimePoint mSceneStarted{ Clock::now() };
        TimePoint mAbsoluteStarted{ Clock::now() };
        double mTimeScale{ 1.0 };
        std::set<Event> mEvents{};
    };
}
