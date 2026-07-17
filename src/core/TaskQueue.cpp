#include "core/TaskQueue.h"

#include <algorithm>

namespace core {

void TaskQueue::push(IndexTask task) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopped)
            return;
        if (std::find(m_tasks.cbegin(), m_tasks.cend(), task) != m_tasks.cend())
            return; // identical task already pending — indexing it once is enough
        m_tasks.push_back(std::move(task));
    }
    // Notify outside the lock so the woken consumer doesn't immediately block.
    m_taskAvailable.notify_one();
}

std::optional<IndexTask> TaskQueue::pop() {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_taskAvailable.wait(lock, [this] { return m_stopped || !m_tasks.empty(); });
    if (m_stopped)
        return std::nullopt;
    IndexTask task = std::move(m_tasks.front());
    m_tasks.pop_front();
    return task;
}

void TaskQueue::stop() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopped = true;
        m_tasks.clear();
    }
    m_taskAvailable.notify_all();
}

bool TaskQueue::isStopped() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stopped;
}

int TaskQueue::size() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<int>(m_tasks.size());
}

} // namespace core
