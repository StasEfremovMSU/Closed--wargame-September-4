using System.Collections;
using System.Collections.Generic;
using UnityEngine;

// Класс в котором генеральный штаб определяет линию, части

public class GeneralMod {
    /// <summary>
    /// 0  Держать выгодную позицию
    /// 1 Произвести фронтовую кавалерийскую атаку
    /// 2 Фланговые маневры
    /// 3 Организация котла
    /// 4 Ложное отступление
    /// 5 Общее наступление
    /// 6 Заход в тыл
    /// 7 Оборона
    /// </summary>
    public int totalTactic;

    /// <summary>
    /// Количество частей, на сколько поделилась армия (1 / 2 / 3 "центр, прав/лев фланг" / 4 " + обход")
    /// </summary>
    public int numberOfLines;

    /// <summary>
    ///  Количество тактических линий
    /// </summary>
    public int deepOfLines;

    float generalMind = 1;

    int command;

    /// <summary>
    /// То, что происходит при запуске
    /// </summary>
	public void Start () 
    {
	}

    /// <summary>
    /// Поиск общей тактики для боя
    /// </summary>
    public void  StartFindTactic()
    {
        if (mainStatic.warbandParam[command].GlobalTypeWarband == 3)
        {
            float [] P = new float[5];
            for (int i = 0; i < P.Length; i++)
            {
                P[i] = 30;
            }

            if (MapData.flagDefence == true)
            {
                P[0] += 90 * generalMind;
            }
            if (mainStatic.warbandParam[command].PersentOfCavalary > 0.50f)
            {
                P[1] += 80 * generalMind;
            }
            if (mainStatic.warbandParam[command].PersentOfCavalary > 0.20f && MapData.goodForFlangBattle == true)
            {
                P[2] += 30 * generalMind;
            }

            if (mainStatic.warbandParam[command].TacticSkills > 0.6f)
            {
                P[3] += 20 * generalMind;
            }

            if (mainStatic.warbandParam[command].TacticSkills > 0.6f)
            {
                P[4] += 20 * generalMind;
            }
            if (mainStatic.warbandParam[command].WeAreBill == true)
            {
                P[5] += 80 * generalMind;
            }

            if (mainStatic.warbandParam[command].MobilGroup == true)
            {
                P[5] += 40 * generalMind;
            }

            if (mainStatic.warbandParam[command].WeAreSmall == true)
            {
                P[6] += 200 * generalMind;
            }

            float sum = 0;
            for (int i = 0; i < P.Length; i++)
            {
                sum += P[i];
            }
            for (int i = 0; i < P.Length; i++)
            {
                P[i] = P[i] /sum;
            }


            float r = Random.Range(0f, 1f);
            if (r < P[0])
            {
                totalTactic = 0;
            }
            else
            {
                if (r < P[1])
                {
                    totalTactic = 1;
                }
                else
                {
                    if (r < P[2])
                    {
                        totalTactic = 2;
                    }
                    else
                    {
                        if (r < P[3])
                        {
                            totalTactic = 3;
                        }
                        else
                        {
                            if (r < P[4])
                            {
                                totalTactic = 4;
                            }
                            else
                            {
                                if (r < P[5])
                                {
                                    totalTactic = 5;
                                }
                                else
                                {
                                    totalTactic = 6;
                                }
                            }
                            
                        }
                    }
                }
            }

        }
    }

	
	// Update is called once per frame
	void Update () {
		
	}
}
