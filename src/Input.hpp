#pragma once

namespace gaia
{

class Input
{
public:
    bool IsCharKeyDown(char key) const { return (m_charFlags & (1 << CharKeyToBitIndex(key))); }
    bool IsShiftDown() const { return m_shift; } // TODO: Handle virtual keys better!
    Vec2i GetMouseDelta() const { return m_mouseDelta; }

    void SetCharKeyDown(char key) { m_charFlags |= (1 << CharKeyToBitIndex(key)); }
    void SetCharKeyUp(char key) { m_charFlags &= ~(1 << CharKeyToBitIndex(key)); }
    void SetShiftDown(bool down) { m_shift = down; } // TODO: Handle virtual keys better!
    void MouseMove(Vec2i newPos);
    void EndFrame();
    void LoseFocus();
    
private:
    static int CharKeyToBitIndex(char key)
    {
        assert('A' <= key && key <= 'Z');
        return key - 'A';
    }

    uint32_t m_charFlags = 0;
    Vec2i m_mousePos = Vec2iZero;
    Vec2i m_mouseDelta = Vec2iZero;
    bool m_mouseValid = true;
    bool m_shift = false;
};

}
