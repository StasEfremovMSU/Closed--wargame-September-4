using System.Collections;
using System.Collections.Generic;
using UnityEngine;

/// <summary>
/// Параметры солдат без вооружения, считанные из файла
/// </summary>

public class loadSoldiers  {



    public int temp;

    public bool[] archer;

    public float[] archerSkill;

    
    /// <summary>
    /// Id солдат (имеющих одно название и одни характеристики)
    /// </summary>
    public int[] soldiersId;

    /// <summary>
    /// Название солдата
    /// </summary>
    public string[] name;

    /// <summary>
    /// Выносливость на действия (не используется)
    /// </summary>
    public float[] stamina;

    /// <summary>
    /// Скорость передвижения
    /// </summary>
    public float[] speed;

    /// <summary>
    /// Переносимый вес
    /// </summary>
    public float[] holdWeight;

    /// <summary>
    /// Атака по умолчанию (от телосложения)
    /// </summary>
    public float[] defDamage;

    /// <summary>
    /// Защита по умолчанию
    /// </summary>
    public float[] defDef;

    /// <summary>
    /// Мощность удара по умолчанию
    /// </summary>
    public float[] defPower;

    /// <summary>
    /// Список оружия и доспехов у солдат, хранится в виде контейнера с ид предметами
    /// </summary>
    public List<int>[] items;

    public loadSoldiers(int n)
    {
        temp = 0;
        name = new string[n];

        archer = new bool[n];
        archerSkill = new float[n];
        soldiersId = new int[n];
        stamina = new float [n];
        speed = new float[n];
        holdWeight = new float[n];
        defDamage = new float[n];
        defDef = new float[n];
        defPower = new float[n];
        items = new List<int>[n];

    }

    public void addSold ( string[] a)
    {
        Debug.Log(" addSoldiers ");

        name[temp] = a[0];
        soldiersId[temp] = int.Parse(a[1]);
        if (float.Parse(a[2]) == 1.0f)
        { archer[temp] = true; }
        else
        {
            archer[temp] = false;
        }

        archerSkill[temp] = float.Parse(a[3]);

        stamina[temp] = float.Parse(a[4]);
        speed[temp] = float.Parse(a[5]);
        holdWeight[temp] = float.Parse(a[6]);

        defDamage [temp] = float.Parse(a[7]);
        defDef [temp]= float.Parse(a[8]);
        defPower[temp] = float.Parse(a[9]);

        items[temp] = new List<int>();
        for (int t = 10; t < a.Length; t++)
        {
            items[temp].Add(int.Parse(a[t]));
        }

        ///Debug.Log("Length List Elements " + items[temp].Count);
        for (int t = 0; t < items[temp].Count; t++)
        {
           // Debug.Log(" element  " + items[temp][t]);
        }


        temp++;
    }


    


}
