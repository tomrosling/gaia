#include "Timer.hpp"

namespace gaia
{

Timer::Timer()
{
    ::QueryPerformanceFrequency(&m_ticksPerSecond);
    ::QueryPerformanceCounter(&m_lastTick);
}

float Timer::GetSecondsAndReset()
{
    LARGE_INTEGER newTick;
    ::QueryPerformanceCounter(&newTick);

    LARGE_INTEGER elapsedTicks;
    elapsedTicks.QuadPart = newTick.QuadPart - m_lastTick.QuadPart;
    m_lastTick = newTick;

    return (float)((double)elapsedTicks.QuadPart / (double)m_ticksPerSecond.QuadPart);
}

}
