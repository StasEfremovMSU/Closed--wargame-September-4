using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public static  class TactUpdate  {

    static int b = 30;

    public static void makeTacticUpdate()
    {
        if (b > 1)
        {
            mainStatic.regiment_stat[0].countTypes();
            mainStatic.regiment_stat[0].typeSpecification = 0;
            mainStatic.regiment_stat[0].rebuilding(2);
            mainStatic.updateRegimentPosition(0);

            b = 0;
        }
        else
        {
            for (int i = 0; i < mainStatic.regiment_stat.Count; i++ )
                for (int r = 0; r < mainStatic.regiment_stat[i].officers.Count; r ++)
                OfficerStatement.MakeOfficerStatement(i,r);
        }
        // Очищаем массив со смертями на всяк пожарный
        DeadBuff.Delete();
        ArrowBuff.Delete();

        // счетчик смерти на 0
        mainStatic.deathcount = 0;

        // Удаляем пустые места от убитых солдат + проверка на стойкость
        for (int i = 0; i < mainStatic.regiment_stat.Count; i++)
        {
            int t = 0;
            while (t < mainStatic.regiment_stat[i].soldiers.Count)
            {
                if (mainStatic.regiment_stat[i].soldiers[t] == null)
                {
                    mainStatic.regiment_stat[i].soldiers.RemoveAt(t);
                }
                else
                {
                    t++;
                }
            }

            for (int j = 0; j < mainStatic.regiment_stat[i].soldiers.Count; j++)
            {
                mainStatic.regiment_stat[i].soldiers[j].numSoldier = j;
            }


            //// Все драки прекращаются.
            //mainStatic.regiment_stat[i].stateFighting = false;
           /* Debug.Log("ST");
            Debug.Log(mainStatic.regiment_stat[i].soldier_count);
            Debug.Log(mainStatic.regiment_stat[i].prim_soldier_count);
            */
            /// Прохождение теста на панику
            if (mainStatic.regiment_stat[i].soldier_count < (mainStatic.regiment_stat[i].prim_soldier_count / 2))
            {
                mainStatic.regiment_stat[i].statePanic = true;
                mainStatic.regiment_stat[i].stateFighting = false;
                Debug.Log("PANIC");

                // В какую сторону побежит?
                // Направление в которое побежит
                Vector3 PanicTarget = new Vector3(0, 0, 0);

                for (int s = 0; s < mainStatic.regiment_stat[i].figthMas.Count; s++)
                {
                    if (mainStatic.regiment_stat[mainStatic.regiment_stat[i].figthMas[s]].command != mainStatic.regiment_stat[i].command)
                    {
                        //Debug.Log(PanicTarget);
                        PanicTarget += (mainStatic.regiment_stat[i].regPosition - mainStatic.regiment_stat[mainStatic.regiment_stat[i].figthMas[s]].regPosition).normalized;
                    }
                }
                PanicTarget = PanicTarget.normalized;
                //Debug.Log(PanicTarget);
                float x = PanicTarget.x; float y = PanicTarget.y; float z = PanicTarget.z;

                // Считаем угол между двумя векторами
                if (x >= 0 && z >= 0)
                {
                    mainStatic.regiment_stat[i].angle = Mathf.Acos(z) + 3.1415f /2;
                }
                if (x > 0 && z < 0)
                {
                    mainStatic.regiment_stat[i].angle = -Mathf.Acos(z) + 3.1415f + 3.1415f / 2;
                }
                if (x <= 0 && z <= 0)
                {
                    mainStatic.regiment_stat[i].angle = Mathf.Acos(z) + 3.1415f + 3.1415f / 2;
                }
                if (x < 0 && z > 0)
                {
                    mainStatic.regiment_stat[i].angle = -Mathf.Acos(-z) + 3.1415f * 2 + 3.1415f / 2;
                }
            }
            if (mainStatic.regiment_stat[i].statePanic == true)
            {
                mainStatic.makeRegimentMove(i, 6.0f);
                mainStatic.updateRegimentPosition(i);
            }

            for (int h = 0; h < mainStatic.regiment_stat.Count; h++)
                mainStatic.regiment_stat[h].figthMas.Clear();
            //mainStatic.regiment_stat[i].figthMas.Clear();

        }

        UpdateEnReg();

        mainStatic.addMeshForBattle();
        mainStatic.dellMeshForBattle();

        // Формируем в каждом полку fightMas в котором содержатся номера полков, которые участвуют с ним в одной схватке
        // Для кажого списка
        for (int y = 0; y < mainStatic.regiment_stat.Count; y++)
        {
            mainStatic.regiment_stat[y].figthMas.Clear();
        }

        for (int y = 0; y < mainStatic.listFigReg.Count; y++)
        {
            // Для каждого элемента в списке одной схватке
            for (int r = 0; r < mainStatic.listFigReg[y].M.Count; r++)
            {
                // Сравнить с каждым другим
                for (int w = 0; w < mainStatic.listFigReg[y].M.Count; w++)
                {
                    if (r != w)
                    {
                        mainStatic.regiment_stat[mainStatic.listFigReg[y].M[r]].
                            figthMas.Add(mainStatic.listFigReg[y].M[w]);
                    }
                }
            }
        }


        /// Раздел в котором добавляем точки для навигации солдат во время боя
        /// Каждый период если полк сражается

        for (int t = 0; t < mainStatic.regiment_stat.Count; t++)
        {
            if (mainStatic.regiment_stat[t].stateFighting == true)
            {
                for (int i = 0; i < mainStatic.regiment_stat[t].figthMas.Count; i++)
                {
                    if (mainStatic.regiment_stat[mainStatic.regiment_stat[t].figthMas[i]].command != mainStatic.regiment_stat[t].command)
                    {
                        mainStatic.regiment_stat[mainStatic.regiment_stat[t].figthMas[i]].make_position();
                        mainStatic.regiment_stat[t].targPos.Clear();

                        Vector3 v3 = new Vector3(0, 0, 0);
                        /// Для клина и каре отдельно!
                        if (mainStatic.regiment_stat[mainStatic.regiment_stat[t].figthMas[i]].regFirstLine < 6 && mainStatic.regiment_stat[mainStatic.regiment_stat[t].figthMas[i]].regLastLine < 8)
                        {           
                            v3 = mainStatic.regiment_stat[mainStatic.regiment_stat[t].figthMas[i]].Position_Reg[0]
                                + mainStatic.regiment_stat[mainStatic.regiment_stat[t].figthMas[i]].Position_Reg[1]
                                + mainStatic.regiment_stat[mainStatic.regiment_stat[t].figthMas[i]].Position_Reg[2]
                                + mainStatic.regiment_stat[mainStatic.regiment_stat[t].figthMas[i]].Position_Reg[3];
                            v3 = v3 / 4;

                            if (mainStatic.regiment_stat[t].command == 1)
                                Debug.Log("HUD");
                            mainStatic.regiment_stat[t].targPos.Add(v3);
                        }
                        else
                        {
                            Vector3 v5 = mainStatic.regiment_stat[mainStatic.regiment_stat[t].figthMas[i]].Position_Reg[0]
                                + mainStatic.regiment_stat[mainStatic.regiment_stat[t].figthMas[i]].Position_Reg[1]
                                + mainStatic.regiment_stat[mainStatic.regiment_stat[t].figthMas[i]].Position_Reg[2]
                                + mainStatic.regiment_stat[mainStatic.regiment_stat[t].figthMas[i]].Position_Reg[3];
                            v5 /= 4;
                            mainStatic.regiment_stat[t].targPos.Add(v5);
                            
                            /// Сдвигаемся на 6 в сторону, если там есть солдат,
                            bool fl = true;
                            float j = 6 ;
                            while (fl == true)
                            {
                                if (j < mainStatic.regiment_stat[mainStatic.regiment_stat[t].figthMas[i]].regFirstLine / 2)
                                {
                                    v3 = v5 + j * mainStatic.regiment_stat[mainStatic.regiment_stat[t].figthMas[i]].regDistanseLine * (mainStatic.regiment_stat[mainStatic.regiment_stat[t].figthMas[i]].Position_Reg[1] - mainStatic.regiment_stat[mainStatic.regiment_stat[t].figthMas[i]].Position_Reg[0]).normalized;
                                    mainStatic.regiment_stat[t].targPos.Add(v3);
                                    v3 = v5 - j * mainStatic.regiment_stat[mainStatic.regiment_stat[t].figthMas[i]].regDistanseLine * (mainStatic.regiment_stat[mainStatic.regiment_stat[t].figthMas[i]].Position_Reg[1] - mainStatic.regiment_stat[mainStatic.regiment_stat[t].figthMas[i]].Position_Reg[0]).normalized;
                                    mainStatic.regiment_stat[t].targPos.Add(v3);

                                    if (mainStatic.regiment_stat[t].command == 1)
                                        Debug.Log("HUD2");
                                    j+=6;
                                }
                                else
                                {
                                    fl = false;
                                }
                            }

                        }
                    }
                }
            }
        }



        // Найти сражающиеся полки и для каждого солдата участника вызвать атаку
        for (int t = 0; t < mainStatic.regiment_stat.Count; t++)
        {
            if (mainStatic.regiment_stat[t].stateFighting == true)
            {
                for (int r = 0; r < mainStatic.regiment_stat[t].soldiers.Count; r++)
                {
                    mainStatic.regiment_stat[t].soldiers[r].at();
                }
            }
        }

        // Сортируем смерти по времени исполения
        DeadBuff.mySort();

        // Сортируем стрелы по времени 
        ArrowBuff.mySort();

        // Удалим повторяющиеся 
        mainStatic.t0 = Time.time;

        for (int t = 0; t < mainStatic.regiment_stat.Count; t++)
        {
            if (mainStatic.regiment_stat[t].stateFighting == false)
            {
                if (mainStatic.regiment_stat[t].orderForw == true)
                {
                    mainStatic.makeRegimentMove(t, 1.0f);
                    mainStatic.updateRegimentPosition(t);
                    mainStatic.regiment_stat[t].orderForw = false;
                }

                if (mainStatic.regiment_stat[t].orderBack == true)
                {
                    mainStatic.makeRegimentMove(t, -1.0f);
                    mainStatic.updateRegimentPosition(t);
                    mainStatic.regiment_stat[t].orderBack = false;
                }

                if (mainStatic.regiment_stat[t].orderRotW == true)
                {
                    mainStatic.makeRegimentRotation(t, +.3f);
                    mainStatic.updateRegimentPosition(t);
                    mainStatic.regiment_stat[t].orderRotW = false;
                    mainStatic.regiment_stat[t].delta_angle = 0;
                }
                if (mainStatic.regiment_stat[t].orderRotL == true)
                {
                    mainStatic.makeRegimentRotation(t, -.3f);
                    mainStatic.updateRegimentPosition(t);
                    mainStatic.regiment_stat[t].orderRotL = false;
                    mainStatic.regiment_stat[t].delta_angle = 0;
                }
                if (mainStatic.regiment_stat[t].archTotewer == true)
                {
                    mainStatic.regiment_stat[t].archerAt(t, TactUpdate.nearestReg(t), true);
                    mainStatic.regiment_stat[t].archTotewer = false;

                }
                if (mainStatic.regiment_stat[t].archAlone == true)
                {
                    mainStatic.regiment_stat[t].archerAt(t, TactUpdate.nearestReg(t), false);
                    mainStatic.regiment_stat[t].archAlone = false;
                }


            }
        }
    }



    /// <summary>
    /// Пытаемся добавить номера столкнувшихся полков
    /// </summary>
    /// <param name="x"></param>
    /// <param name="y"></param>
    public static void addRegToFigth(int x, int y)
    {
       bool d1 = false; bool d2 = false;
        int w = 0; int w1 = 0;

        for (int t = 0; t < mainStatic.listFigReg.Count; t ++)
       {
           for (int e = 0; e < mainStatic.listFigReg[t].M.Count; e++)
           {
               if (mainStatic.listFigReg[t].M[e] == x) { d1 = true; w = t; }
               if (mainStatic.listFigReg[t].M[e] == y) { d2 = true; w1 = t; }
           }
       }
        
        // Если нашли первый индекс, а второй нет, значит добавить второй к первому
       if ((d1 == true) && (d2 == false))
       {
           mainStatic.listFigReg[w].M.Add(y);

           mainStatic.regiment_stat[x].figthNumber = w;
           mainStatic.regiment_stat[y].figthNumber = w;
       }

        // Если нашли второй индекс, а первый нет, значит добавить первый ко второму
       if ((d1 == false) && (d2 == true))
       {
           mainStatic.listFigReg[w1].M.Add(x);

           mainStatic.regiment_stat[x].figthNumber = w1;
           mainStatic.regiment_stat[y].figthNumber = w1;
       }

       // Если нет ни того, ни другого - значит создаем новую группу
       if ((d1 == false) && (d2 == false))
       {
           figth r = new figth ();
           r.M.Add (x);
           r.M.Add (y);
           mainStatic.listFigReg.Add(r);
       }
        // если оба нашлись, то все хорошо

       if ((d1 == true) && (d2 == true))
       {
       }
    }


    public static int nearestReg(int h)
    {
        int res = 0;
        float dis = 99999;
        for (int g = 0; g < mainStatic.regiment_stat.Count; g++)
        {
            if (h != g)
                if (mainStatic.regiment_stat[h].command != mainStatic.regiment_stat[g].command)
                {
                    if ((mainStatic.regiment_stat[h].regPosition - mainStatic.regiment_stat[g].regPosition).magnitude < dis)
                    {
                        res = g;
                        dis = (mainStatic.regiment_stat[h].regPosition - mainStatic.regiment_stat[g].regPosition).magnitude;
                    }
                }
        }

        return res;
    }


    public static void UpdateEnReg()
    {
        /// Собрать полки в сражающиеся
        for (int t = 0; t < mainStatic.regiment_stat.Count; t++)
        {
            if (mainStatic.regiment_stat[t].statePanic != true)
            {
                /// 
                int r = FindEnemiesRegiment(0, t);

                if (r < 999 && r != t && mainStatic.regiment_stat[r].statePanic != true)
                {
                    mainStatic.regiment_stat[t].stateFighting = true;
                    mainStatic.regiment_stat[r].stateFighting = true;
                    addRegToFigth(t, r);
                    return;
                }

                r = FindEnemiesRegimentBack(mainStatic.regiment_stat[t].soldiers.Count - 1, t);
                if (r < 999 && r != t && mainStatic.regiment_stat[r].statePanic != true)
                {
                    mainStatic.regiment_stat[t].stateFighting = true;
                    mainStatic.regiment_stat[r].stateFighting = true;
                    addRegToFigth(t, r);
                    return;
                }

                if (mainStatic.regiment_stat[t].soldiers.Count > mainStatic.regiment_stat[t].regFirstLine * 1.5f)
                {
                    r = FindEnemiesRegiment(mainStatic.regiment_stat[t].regFirstLine, t);
                    if (r < 999 && r != t && mainStatic.regiment_stat[r].statePanic != true)
                    {
                        mainStatic.regiment_stat[t].stateFighting = true;
                        mainStatic.regiment_stat[r].stateFighting = true;
                        addRegToFigth(t, r);
                        return;
                    }
                }

                if (mainStatic.regiment_stat[t].soldiers.Count > mainStatic.regiment_stat[t].regFirstLine * 2f)
                {
                    r = FindEnemiesRegimentBack(mainStatic.regiment_stat[t].soldiers.Count - mainStatic.regiment_stat[t].regFirstLine, t);
                    if (r < 999 && r != t && mainStatic.regiment_stat[r].statePanic != true)
                    {
                        mainStatic.regiment_stat[t].stateFighting = true;
                        mainStatic.regiment_stat[r].stateFighting = true;
                        addRegToFigth(t, r);
                        return;
                    }
                }
            }
             
        }
    }



    /// <summary>
    /// Из солдата пускаем лучи, чтобы найити полк врага
    /// </summary>
    /// <param name="sNumber"></param>
    /// <param name="regnum"></param>
    /// <returns></returns>
    public static int FindEnemiesRegiment(int sNumber, int regnum)
    {
        int res = 99999;
        Ray ray1 = new Ray(mainStatic.regiment_stat[regnum].soldiers[sNumber].getObject().transform.position + new Vector3(0, 2, 0),
            mainStatic.regiment_stat[regnum].soldiers[sNumber].getObject().transform.forward);

        Debug.DrawLine(mainStatic.regiment_stat[regnum].soldiers[sNumber].getObject().transform.position + new Vector3(0, 2, 0),
            mainStatic.regiment_stat[regnum].soldiers[sNumber].getObject().transform.position + new Vector3(0, 2, 0) +
            mainStatic.regiment_stat[regnum].soldiers[sNumber].getObject().transform.forward);
        RaycastHit hit1;
        bool temp = Physics.Raycast(ray1, out hit1, 10.0f);

        if (hit1.transform!= null)
        {

            if (hit1.transform.GetComponent<UnitSoldiers>().numReg != regnum)
            {
                res = hit1.transform.GetComponent<UnitSoldiers>().numReg; return res;
            }
        }
        else
        {

            Ray ray2 = new Ray(mainStatic.regiment_stat[regnum].soldiers[sNumber].getObject().transform.position + new Vector3(0, 2, 0),
                mainStatic.myRot(mainStatic.regiment_stat[regnum].soldiers[sNumber].getObject().transform.forward, 3.1415f * 3 / 4));
            RaycastHit hit2;
            bool temp2 = Physics.Raycast(ray2, out hit2, 10.0f);

            if (hit2.transform!= null)
            {
                if (hit2.transform.GetComponent<UnitSoldiers>().numReg != regnum)
                {
                    res = hit2.transform.GetComponent<UnitSoldiers>().numReg;
                }
            }
            else
            {

                Ray ray3 = new Ray(mainStatic.regiment_stat[regnum].soldiers[sNumber].getObject().transform.position + new Vector3(0, 2, 0),
                    mainStatic.myRot(mainStatic.regiment_stat[regnum].soldiers[sNumber].getObject().transform.forward, 3.1415f * 1 / 4));
                RaycastHit hit3;
                bool temp3 = Physics.Raycast(ray3, out hit3, 10.0f);

                if (hit3.transform!= null)
                {
                    if (hit3.transform.GetComponent<UnitSoldiers>().numReg != regnum)
                    {
                        res = hit3.transform.GetComponent<UnitSoldiers>().numReg;
                    }
                }
            }
        }
        return res;
    }

    public static int FindEnemiesRegimentBack(int sNumber, int regnum)
    {
        int res = 99999;
        Ray ray1 = new Ray(mainStatic.regiment_stat[regnum].soldiers[sNumber].getObject().transform.position + new Vector3(0, 2, 0),-
            mainStatic.regiment_stat[regnum].soldiers[sNumber].getObject().transform.forward);
        RaycastHit hit1;
        bool temp = Physics.Raycast(ray1, out hit1, 10.0f);

        if (hit1.transform != null)
        {
            if (hit1.transform.GetComponent<UnitSoldiers>() != null)
            res = hit1.transform.GetComponent<UnitSoldiers>().numReg;
        }
        else
        {

            Ray ray2 = new Ray(mainStatic.regiment_stat[regnum].soldiers[sNumber].getObject().transform.position + new Vector3(0, 2, 0),-
                mainStatic.myRot(mainStatic.regiment_stat[regnum].soldiers[sNumber].getObject().transform.forward, 3.1415f * 3 / 4));
            RaycastHit hit2;
            bool temp2 = Physics.Raycast(ray2, out hit2, 10.0f);

            if (hit2.transform!= null)
            {
                if (hit2.transform.GetComponent<UnitSoldiers>() != null)
                res = hit2.transform.GetComponent<UnitSoldiers>().numReg;
            }
            else
            {

                Ray ray3 = new Ray(mainStatic.regiment_stat[regnum].soldiers[sNumber].getObject().transform.position + new Vector3(0, 2, 0),
                    -mainStatic.myRot(mainStatic.regiment_stat[regnum].soldiers[sNumber].getObject().transform.forward, 3.1415f * 1 / 4));
                RaycastHit hit3;
                bool temp3 = Physics.Raycast(ray3, out hit3, 10.0f);

                if (hit3.transform != null)
                {
                    if (hit3.transform.GetComponent<UnitSoldiers>() != null)
                    res = hit3.transform.GetComponent<UnitSoldiers>().numReg;
                }
            }
        }
        return res;
    }
}
