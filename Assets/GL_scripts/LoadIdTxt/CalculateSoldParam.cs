using System.Collections;
using System.Collections.Generic;
using UnityEngine;

/// <summary>
/// Здесь к характеристикам добавляются модификаторы от оружия, которое есть на каждом
/// </summary>

public   class CalculateSoldParam  {


    public bool[] arch;

    public bool[] spearman;


      public int[] achPower;

      public int[] arcType;

      public string[] name;

      public int[] soldierId;

      public bool[] archer;

      public float[] speed;

      public float[] damage;

      public float[] defens;

      public float[] power;

      public float[] itemsWeight;

      public float[] archerSkill;

      // Строка с описанием вещей игрока
      public string[] itemList;


      public CalculateSoldParam(int n)
    {
        spearman = new bool[n];

        arch = new bool[n];
        
        achPower = new int[n] ;

        arcType = new int[n] ;
        
        name = new string[n];

        soldierId = new int[n];

        archer = new bool[n];

        archerSkill = new float[n];

        speed = new float[n];

        damage = new float[n];

        defens = new float[n];

        power = new float[n];

        itemsWeight = new float[n];

        itemList = new string[n];
    }

      public void Calc(loadSoldiers a, loadItem b)
    {

        soldierId = a.soldiersId;
        name = a.name;
        speed = a.speed;
        defens = a.defDef;
        damage = a.defDamage;
        power = a.defPower;
        archer = a.archer;
        archerSkill = a.archerSkill;
        itemsWeight = a.holdWeight;

        // Перебор всех солдат
        for (int i = 0; i < a.temp; i++)
        {
            float tempWeight = 0;

            arch[i] = false;
            spearman[i] = false;

            Debug.Log("Length" + a.items[i]);
            // Индекс по оружию, которое у него есть
            for (int y = 0; y < a.items[i].Count; y++)
            {
               // Рассчитываем для каждого предмета
               damage[i] += b.damage[a.items[i][y]];
               defens[i] += b.def[a.items[i][y]];
               power[i]  += b.power[a.items[i][y]];
               tempWeight+= b.weight[a.items[i][y]];

               if (b.name[a.items[i][y]] == "bow")
               {
                   /// Если есть лук
                   arch[i] = true;
                   arcType[i] = 0;
                   achPower[i] = 0;
               }

               if (b.name[a.items[i][y]] == "sword")
               {
               }

               if (b.name[a.items[i][y]] == "spear")
               {
                   spearman[i] = true;
               }

               if (b.name[a.items[i][y]] == "crossbow")
               {
                   arch[i] = true;
               }


               // Дописываем список предметов у i того типа солдат
               Debug.Log("Add weapon " + b.name[a.items[i][y]]);
               itemList[i] += b.name[a.items[i][y]]; itemList[i] += " ";
            }

            /// Если много лишнего веса, то скорость падает 
            speed[i] += speed[i] * (itemsWeight[i] - tempWeight)*0.01f;



        }
        
    }
}