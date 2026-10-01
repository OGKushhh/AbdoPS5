#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_PIPELINE_ASYNCPIPELINECOMPILER_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_PIPELINE_ASYNCPIPELINECOMPILER_H_

#include "common/common.h"
#include "common/threads.h"
#include "common/uniqueFunction.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

namespace Libs::Graphics {

// Kyty-032: background workers that overlap guest-shader translation with the GPU replay
// thread. The GPU thread submits translation jobs for shader stages it will need shortly
// (vertex stages of the current draw) and consumes the results when it reaches them. Jobs
// that are not finished by then are still translated inline, so behaviour never depends
// on worker availability; the pool only removes work from the critical path.
class AsyncPipelineCompiler {
public:
	explicit AsyncPipelineCompiler(uint32_t worker_count);
	~AsyncPipelineCompiler();
	KYTY_CLASS_NO_COPY(AsyncPipelineCompiler);

	void Submit(Common::UniqueFunction<void()> job);

private:
	static void WorkerTrampoline(void* instance);

	Common::Mutex                               m_mutex;
	Common::CondVar                             m_work_available;
	std::deque<Common::UniqueFunction<void()>>  m_jobs;
	std::vector<std::unique_ptr<Common::Thread>> m_workers;
	bool                                        m_stopping = false;
};

} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_PIPELINE_ASYNCPIPELINECOMPILER_H_
