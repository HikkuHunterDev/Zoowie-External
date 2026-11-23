#pragma once

namespace app::trace_session
{
#if defined(ILIKESOURCE_SHOT_TRACE_ENABLED) && ILIKESOURCE_SHOT_TRACE_ENABLED
void initialize();
void shutdown() noexcept;
#else
inline void initialize() {}
inline void shutdown() noexcept {}
#endif
} // namespace app::trace_session
