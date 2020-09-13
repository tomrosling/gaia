#include "Input.hpp"

namespace gaia 
{

void Input::MouseMove(Vec2i newPos)
{
    if (m_mouseValid)
    {
        m_mouseDelta += newPos - m_mousePos;
    }
    else
    {
        Assert(m_mouseDelta == Vec2iZero);
        m_mouseValid = true;
    }

    m_mousePos = newPos;
}

void Input::EndFrame()
{
    // Deltas are accumulated across a single frame, so reset them at the end.
    m_mouseDelta = Vec2iZero;
}

void Input::LoseFocus()
{
    m_charFlags = 0;
    m_specialKeyFlags = 0;
    m_mouseFlags = 0;
    m_mouseDelta = Vec2iZero;
    m_mouseValid = false;
}

}
