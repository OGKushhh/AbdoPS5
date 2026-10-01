#include "graphics/host_gpu/renderer/pipeline/asyncPipelineCompiler.h"

#include "common/assert.h"
#include "common/profiler.h"

#include <utility>

namespace Libs::Graphics {

AsyncPipelineCompiler::AsyncPipelineCompiler(uint32_t worker_count) {
	EXIT_IF(worker_count == 0);
	m_workers.reserve(worker_count);
	for (uint32_t i = 0; i < worker_count; i++) {
		m_workers.push_back(
			std::make_unique<Common::Thread>(&AsyncPipelineCompiler::WorkerTrampoline, this));
	}
}

AsyncPipelineCompiler::~AsyncPipelineCompiler() {
	{
		Common::LockGuard lock(m_mutex);
		m_stopping = true;
		m_work_available.SignalAll();
	}
	for (auto& worker: m_workers) {
		worker->Join();
	}
}

void AsyncPipelineCompiler::Submit(Common::UniqueFunction<void> job) {
	EXIT_IF(!job);
	Common::LockGuard lock(m_mutex);
	EXIT_IF(m_stopping);
	m_jobs.push_back(std::move(job));
	m_work_available.Signal();
}

void AsyncPipelineCompiler::WorkerTrampoline(void* instance) {
	auto* self = static_cast<AsyncPipelineCompiler*>(instance);
	EXIT_IF(self == nullptr);
	KYTY_PROFILER_THREAD("Thread_AsyncShaders");
	for (;;) {
		Common::UniqueFunction<void> job;
		{
			Common::LockGuard lock(self->m_mutex);
			while (self->m_jobs.empty() && !self->m_stopping) {
				self->m_work_available.Wait(&self->m_mutex);
			}
			if (self->m_jobs.empty() && self->m_stopping) {
				return;
			}
			job = std::move(self->m_jobs.front());
			self->m_jobs.pop_front();
		}
		job();
	}
}

} // namespace Libs::Graphics
