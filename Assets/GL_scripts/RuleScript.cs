using System.Collections;
using System.Collections.Generic;
using UnityEngine;

/// <summary>
/// Правило, которое определяет поведение полка. Возвращает множитель. Родительский класс
/// </summary>
public class RuleScript_regiment {

    /// Коэффициенты можно менять изменяя особенности своей армии. То есть генерируется при старте армии

    /// <summary>
    /// Коэффициент исполнения лучшим офицером
    /// </summary>
    private float brain1;

    private float brain2;

    private float brain3;

    /// <summary>
    /// Коэффициент исполнения посредственным офицером
    /// </summary>
    private float brain4;

    /// Потом дописать для разных эпох, пока средневоковая армия

    /// Возвращает численный коэффициент 
    //public RulePair make(int reg_num);
	
}

public struct RulePair 
{
    public int tactic_num;
    public float weight;
}