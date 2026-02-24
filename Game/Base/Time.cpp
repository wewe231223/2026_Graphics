#include "Time.h"

namespace Globals {
	Time& Time::Get() {
		static Time instance{};
		return instance;
	}

	Time::time_point Time::Now() const {
		return clock::now();
	}

	double Time::SetTimeScale(double scale) {
		auto temp = mTimeScale;
		mTimeScale = scale;
		return temp;
	}


	double Time::GetTimeScale() {
		return mTimeScale;
	}

	uint64_t Time::GetFrameCount() {
		return mFrameCount;
	}

	void Time::AdvanceTime() {
		Time::UpdateDeltaTime();
		Time::SampleDeltaTime();
		Time::AddScaledStarted();
		Time::CheckEvent();
	}

	void Time::StartSceneTime() {
		mSceneStarted = clock::now();
	}

	void Time::UpdateDeltaTime() {
		auto now = clock::now();
		mDeltaTime = std::chrono::duration_cast<duration>(now - mPrev);
		mPrev = now;
		mFrameCount++;
	}
	void Time::AddScaledStarted() {
		mScaledStarted += mDeltaTime * mTimeScale;
	}

	void Time::SampleDeltaTime() {
		mDeltaTimeBuffer[mDeltaTimeSampleingIndex] = mDeltaTime * mTimeScale;
		mDeltaTimeSampleingIndex = (mDeltaTimeSampleingIndex + 1) % mDeltaTimeBufferSize;
	}

	bool Time::PopEvent() {
		Event ev = *mEvents.begin();

		if (ev.mInvokeTime < clock::now()) {
			if (std::invoke(ev.mCallBack)) {
				mEvents.emplace(clock::now() + ev.mTimeout, std::move(ev.mTimeout), std::move(ev.mCallBack));
			}
			mEvents.erase(mEvents.begin());

			return true;
		}
		return false;
	}

	void Time::CheckEvent() {
		if (mEvents.empty()) {
			return;
		}
		while (Time::PopEvent());
	}

}