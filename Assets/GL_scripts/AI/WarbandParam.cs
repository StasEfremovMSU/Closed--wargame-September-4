using System.Collections;
using System.Collections.Generic;
using UnityEngine;

/// <summary>
/// Параметры армий
/// </summary>
public class WarbandParam {

    /// <summary>
    /// Общий тип исорический
    ///  1 - группа воинов
    ///  2 - античная армия
    ///  3 - феодальная армия
    ///  4 - регулярная армия
    /// </summary>
    public int GlobalTypeWarband  = 3;

    /// <summary>
    ///  Процент кавалеристов
    /// </summary>
    public float PersentOfCavalary = 0f;

    /// <summary>
    /// Строевая  выучка cолдат в среднем по армии
    /// </summary>
    public float TacticSkills;

    /// <summary>
    /// Наличие мобильного отряда, готового зайти в тыл
    /// </summary>
    public bool MobilGroup;

    /// <summary>
    ///  Наших союзников много
    /// </summary>
    public bool WeAreBill;

    /// <summary>
    /// Мы в меньшинстве
    /// </summary>
    public bool WeAreSmall;

    /// <summary>
    /// Имеет преимущество на карте
    /// </summary>
    public bool MapDefense;

    /// <summary>
    /// Сидит в замке
    /// </summary>
    public bool castle;


}
