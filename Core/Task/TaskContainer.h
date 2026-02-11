#pragma once 
#include <vector>
#include <algorithm>
#include <ranges>
#include <concepts>
#include <functional>
#include <utility>
#include <span>
#include "Utility/ErrorHandler.h"

// TODO 
// 실제 Task 디자인 시작. 
// Task Graph 의 구체적인 구현 방안. 


namespace Core {
    namespace Task {

        template <typename P, typename T>
        concept ValidProjection = requires(P p, T t) {
            { std::invoke(p, t) } -> std::totally_ordered;
        };

        template <typename T, typename Projection = std::identity> requires std::movable<T>&& ValidProjection<Projection, T>
        class TaskContainer {
            using Key = std::remove_cvref_t<std::invoke_result_t<Projection, T>>;
        public:
            explicit TaskContainer(Projection proj = {}) : mProjection(std::move(proj)) {}

            TaskContainer(const TaskContainer& other) : mBuffer(other.mBuffer),
                mCount(other.mCount),
                mProjection(other.mProjection),
                mDirtyFlag(other.mDirtyFlag) {}

            TaskContainer& operator=(const TaskContainer& other) {
                if (this != &other) {
                    mBuffer = other.mBuffer; 
                    mCount = other.mCount;
                    mProjection = other.mProjection;
                    mDirtyFlag = other.mDirtyFlag;
                }
                return *this;
            }

            TaskContainer(TaskContainer&& other) noexcept
                : mBuffer(std::move(other.mBuffer)),         
                mCount(std::exchange(other.mCount, 0)),     
                mProjection(std::move(other.mProjection)),
                mDirtyFlag(std::exchange(other.mDirtyFlag, false)) {}


            TaskContainer& operator=(TaskContainer&& other) noexcept {
                if (this != &other) {
                    mBuffer = std::move(other.mBuffer);
                    mCount = std::exchange(other.mCount, 0);
                    mProjection = std::move(other.mProjection);
                    mDirtyFlag = std::exchange(other.mDirtyFlag, false);
                }
                return *this;
            }

        public:
            void Reset() {
                mCount = 0;
                mDirtyFlag = false;
            }

            void Reserve(size_t capacity) {
                mBuffer.reserve(capacity);
            }

            template <typename... Args>
            T& emplace(Args&&... args) {
                mDirtyFlag = true;
                if (mCount < mBuffer.size()) {
                    mBuffer[mCount] = T(std::forward<Args>(args)...);
                    return mBuffer[mCount++];
                }
                mCount++;
                return mBuffer.emplace_back(std::forward<Args>(args)...);
            }

            void Push(const T& value) {
                emplace(value);
            }

            void Push(T&& value) {
                emplace(std::move(value));
            }

            void Update() {
                if (!mDirtyFlag or mCount == 0) {
                    return;
                }

                auto validRange = std::span(mBuffer.data(), mCount);
                std::ranges::sort(validRange, std::less<>{}, mProjection);

                mDirtyFlag = false;
            }

            [[nodiscard]] T& operator[](size_t index) {
                if (index < mCount) {
                    ErrorHandler::report("TaskContainer", "Index out of range", ErrorHandler::Level::Critical);
                }
                return mBuffer[index];
            }

            [[nodiscard]] const T& operator[](size_t index) const {
				if (index < mCount) {
					ErrorHandler::report("TaskContainer", "Index out of range", ErrorHandler::Level::Critical);
				}
                return mBuffer[index];
            }

            [[nodiscard]] size_t Size() const { 
                return mCount; 
            }

            [[nodiscard]] size_t mCapacity() const { 
                return mBuffer.capacity(); 
            }

            T* data() { 
                return mBuffer.data(); 
            }

            const T* data() const { 
                return mBuffer.data(); 
            }

            auto begin() { 
                return mBuffer.begin(); 
            }

            auto end() { 
                return mBuffer.begin() + mCount; 
            }

            auto begin() const { 
                return mBuffer.begin(); 
            }

            auto end() const { 
                return mBuffer.begin() + mCount; 
            }

        private:
            std::vector<T> mBuffer{};
            size_t mCount{};
            Projection mProjection;
            bool mDirtyFlag{ false };
        };
    }
}
