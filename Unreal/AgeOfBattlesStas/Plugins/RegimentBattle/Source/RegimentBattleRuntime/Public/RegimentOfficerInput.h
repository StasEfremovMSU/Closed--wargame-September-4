#pragma once

#include "Math/UnrealMathUtility.h"

struct FRegimentOfficerInput
{
    enum class EAttack { None, Strike, Clear };

    struct FActions
    {
        EAttack Attack = EAttack::None;
        bool bParry = false;
        bool bJump = false;
    };

    bool bCommandsOpen = false;

    void ToggleCommands(bool bPlotting)
    {
        bCommandsOpen = !bPlotting && !bCommandsOpen;
        ClearActions();
    }

    void RememberActions(bool bStrike, bool bParry, bool bJump)
    {
        bStrikePressed |= bStrike;
        bParryPressed |= bParry;
        bJumpPressed |= bJump;
    }

    void ClearActions()
    {
        bStrikePressed = bParryPressed = bJumpPressed = false;
    }

    FActions ConsumeActions(bool bDirect)
    {
        const bool bParry = bDirect && bParryPressed;
        const bool bStrike = bDirect && bStrikePressed && !bParry;
        const FActions Actions{
            bStrike ? EAttack::Strike : bClearAttack ? EAttack::Clear : EAttack::None,
            bParry, bDirect && bJumpPressed};
        bClearAttack = bStrike;
        ClearActions();
        return Actions;
    }

    static float Speed(float Maximum, bool bRunning)
    {
        return Maximum * (bRunning ? 1.f : .54f);
    }

private:
    bool bStrikePressed = false;
    bool bParryPressed = false;
    bool bJumpPressed = false;
    bool bClearAttack = false;
};
