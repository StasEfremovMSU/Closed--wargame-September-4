using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class loadOfficer  
{


    public int[] command;

    public float[] charisma;

    public float[] tacticMind;

    public float[] LeadSkill;

    public int[] IdOfficer;

    public float[] LeadDist;

    public float[] moral;

    public float[] x;

    public float[] y;

    public float[] z;

    public float[] angle;
    
    ////////////////////////////////////////////////////////
    

    public int temp;

    // Здесь и ниже параметры обычных солдат

    /// <summary>
    /// Стрелок ли
    /// </summary>
    public bool[] archer;

    /// <summary>
    /// Навык стрельбы
    /// </summary>
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
    /// Тип солдата
    /// </summary>
    public int[] typeSoldiers;

    /// <summary>
    /// Число - тип юнита (стрелок/пехотинец/кавалерия и прочее)
    /// </summary>
    public int[] typeUnit;

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
    public int[] defDamage;

    /// <summary>
    /// Защита по умолчанию
    /// </summary>
    public int[] defDef;

    /// <summary>
    /// Мощность удара по умолчанию
    /// </summary>
    public int[] defPower;

    public int[] RegNum;

    // Знаменосец, барабанщик, офицер
    public int[] IdOfficerType;

    /// <summary>
    /// Список оружия и доспехов у офицера, хранится в виде контейнера с ид предметами
    /// </summary>
    public List<int>[] items;

	public loadOfficer(int n)
    {
        IdOfficerType = new int[n];
        x = new float[n];
        y = new float[n];
        z = new float[n];
        angle = new float[n];

        command = new int[n];
        charisma = new float[n];
        tacticMind = new float[n];
        LeadSkill = new float [n];
        IdOfficer = new int[n];
        LeadDist = new float[n];
        moral = new float[n];
        
        temp = 0;
        name = new string[n];
        archer = new bool[n];
        archerSkill = new float[n];
        typeUnit = new int[n];
        stamina = new float [n];
        speed = new float[n];
        holdWeight = new float[n];
        defDamage = new int[n];
        defDef = new int[n];
        defPower = new int[n];
        RegNum = new int[n];
        items = new List<int>[n];
    }

    public void addOfficer(string[] a)
    {
        x[temp] = float.Parse(a[0]);
        y[temp] = float.Parse(a[1]);
        z[temp] = float.Parse(a[2]);
        
        command[temp] = int.Parse (a[3]);
        IdOfficer[temp] = int.Parse(a[4]);
        LeadDist[temp] = float.Parse(a[5]);
        moral[temp] = float.Parse(a[6]);

        name[temp] = a[7];
        if (int.Parse(a[8]) == 1)
        {
            archer[temp] = true;
        }
        else
        {
            archer[temp] = false;
        }
        archerSkill[temp] = float.Parse(a[9]);
        typeUnit[temp] = int.Parse(a[10]);
        stamina[temp] = float.Parse(a[11]);
        speed[temp] = float.Parse(a[12]);
        holdWeight[temp] = float.Parse(a[13]);
        defDamage[temp] = int.Parse(a[14]);
        defDef[temp] = int.Parse(a[15]);
        defPower[temp] = int.Parse(a[16]);

        charisma[temp] = float.Parse(a[17]);
        tacticMind[temp] = float.Parse(a[18]);
        LeadSkill[temp] = float.Parse(a[19]);
        RegNum[temp] = int.Parse(a[20]);
        items[temp] = new List<int>();
        for (int t = 21; t < a.Length; t++)
        {
           
            items[temp].Add(int.Parse(a[t]));
        }
        temp++;
    }

    public void ReCalculate(loadItem b)
    {
        // Перебор солдат
        for (int i = 0; i < temp; i++) 
        {
            float tempWeight = 0;
            for (int t = 0; t < items[i].Count; t++)
            {
                defDamage[i] += b.damage[t];
                defDef[i] += b.def[t];
                defPower[i] += b.power[t];
                tempWeight += b.weight[t];
            }

            for (int t = 0; t < items[i].Count; t++)
            {
                if (items[i][t] == 6)
                {
                    IdOfficerType[i] = 3;
                }
                if (items[i][t] == 7)
                {
                    IdOfficerType[i] = 4;
                }
                if (items[i][t] == 8)
                {
                    IdOfficerType[i] = 1;
                }
                if (items[i][t] == 9)
                {
                    IdOfficerType[i] = 2;
                }
            }
        }
    }

  

}
