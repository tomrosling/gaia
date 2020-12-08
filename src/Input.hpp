#pragma once

namespace gaia
{

enum class SpecialKey
{
    Shift
};

enum class MouseButton
{
    Left,
    Right,
    Middle
};

class Input
{
public:
    static constexpr Vec2i NoCursorLockPos{ -1, -1 };

    bool IsCharKeyDown(char key) const { return (m_charFlags & (1 << CharKeyToBitIndex(key))); }
    bool IsSpecialKeyDown(SpecialKey key) const { return m_specialKeyFlags & (1 << (int)key); }
    bool IsMouseButtonDown(MouseButton button) const { return m_mouseFlags & (1 << (int)button); }
    Vec2i GetMousePos() const { return m_mousePos; }
    Vec2i GetMouseDelta() const { return m_mouseDelta; }

    void SetCharKeyDown(char key) { m_charFlags |= (1 << CharKeyToBitIndex(key)); }
    void SetCharKeyUp(char key) { m_charFlags &= ~(1 << CharKeyToBitIndex(key)); }
    void SetSpecialKeyDown(SpecialKey key) { m_specialKeyFlags |= (1 << (int)key); }
    void SetSpecialKeyUp(SpecialKey key) { m_specialKeyFlags &= ~(1 << (int)key); }
    void SetMouseButtonDown(MouseButton button) { m_mouseFlags |= (1 << (int)button); }
    void SetMouseButtonUp(MouseButton button) { m_mouseFlags &= ~(1 << (int)button); }
    void MouseMove(Vec2i newPos);
    void EndFrame();
    void LoseFocus();
    void EnableCursorLock(Vec2i pos);
    bool DisableCursorLock();
    Vec2i GetCursorLockPos() const { return m_cursorLockPos; }
    bool IsCursorLocked() const { return m_cursorLockPos != NoCursorLockPos; }

private:
    static int CharKeyToBitIndex(char key)
    {
        Assert('A' <= key && key <= 'Z');
        return key - 'A';
    }

    uint32 m_charFlags = 0;
    uint32 m_specialKeyFlags = 0;
    uint32 m_mouseFlags = 0;
    Vec2i m_mousePos = Vec2iZero;
    Vec2i m_mouseDelta = Vec2iZero;
    Vec2i m_cursorLockPos = NoCursorLockPos;
    bool m_mouseValid = true;
};

}
