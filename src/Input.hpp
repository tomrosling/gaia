#pragma once

namespace gaia
{

enum class SpecialKey
{
    Shift
};

class Input
{
public:
    bool IsCharKeyDown(char key) const { return (m_charFlags & (1 << CharKeyToBitIndex(key))); }
    bool IsSpecialKeyDown(SpecialKey key) const { return m_specialKeyFlags & (1 << (int)key); }
    Vec2i GetMousePos() const { return m_mousePos; }
    Vec2i GetMouseDelta() const { return m_mouseDelta; }

    void SetCharKeyDown(char key) { m_charFlags |= (1 << CharKeyToBitIndex(key)); }
    void SetCharKeyUp(char key) { m_charFlags &= ~(1 << CharKeyToBitIndex(key)); }
    void SetSpecialKeyDown(SpecialKey key) { m_specialKeyFlags |= (1 << (int)key); }
    void SetSpecialKeyUp(SpecialKey key) { m_specialKeyFlags &= ~(1 << (int)key); }
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
    uint32_t m_specialKeyFlags = 0;
    Vec2i m_mousePos = Vec2iZero;
    Vec2i m_mouseDelta = Vec2iZero;
    bool m_mouseValid = true;
    bool m_shift = false;
};

}
