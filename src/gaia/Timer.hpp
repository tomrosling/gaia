#pragma once

namespace gaia
{

class Timer 
{
public:
    Timer();
    float GetSecondsAndReset();

private:
    LARGE_INTEGER m_ticksPerSecond;
    LARGE_INTEGER m_lastTick;
};

}
