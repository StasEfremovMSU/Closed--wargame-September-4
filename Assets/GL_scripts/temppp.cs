using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class temppp : MonoBehaviour {
    /*
    void RePutSolForOfficerFirst(int f)
    {
        senior_officer = false;
        junior_officer = false;
        standard_bearer = false;
        trumpeter = false;

        int e = 0;
        for (int r = 0; r < officers.Count; r++)
                    {
                        bool kj = ((senior_officer == false && officers[r].IdTypeOfficers == 1) ||
                (junior_officer == false && officers[r].IdTypeOfficers == 2) ||
                (standard_bearer == false && officers[r].IdTypeOfficers == 3) ||
                (trumpeter == false && officers[r].IdTypeOfficers == 4));
                        if (kj == true)
                        {
                            
                            /// Перекидываем солдата в конецC:\September 4\Assets\GL_scripts\MainFolder\GameStatus.cs
                            int t1 = 0; int t2 = 0;
                            t1 = (soldiers.Count + e) % regFirstLine + 1;
                            t2 = soldiers.Count / regFirstLine+1;
                            if (t1 > regFirstLine)
                            {
                                t2++;
                                t1 = 0;
                            }

                            int d = 0;
                            for (int g = 0; g < soldiers.Count; g++)
                            {
                                if (soldiers[g].tempForRebuilding == f)
                                    d = g;
                            }
                            soldiers[d].new_position = NewSolPos(t1, t2);
                            soldiers[d].k1 = t1;
                            soldiers[d].k2 = t2;
                            Debug.Log(t1 + "  " + t2);
                            soldiers[d].matrix_position = soldiers[d].new_position;

                            /// кидаем офицера в начало
                            officers[r].GetSoldiers().new_position = NewSolPos(f, 0);
                            officers[r].GetSoldiers().k1 = f;
                            officers[r].GetSoldiers().k2 = 0;
                            officers[r].GetSoldiers().matrix_position = officers[r].GetSoldiers().new_position;
                            if (officers[r].IdTypeOfficers == 1)
                                senior_officer = true;
                            if (officers[r].IdTypeOfficers == 2)
                                junior_officer = true;
                            if (officers[r].IdTypeOfficers == 3)
                                standard_bearer = true;
                            if (officers[r].IdTypeOfficers == 4)
                                trumpeter = true; 
                            e++;
                            f++;
                        }
                    }
    }


    void RePutSolForOfficerSec(int f)
    {

        senior_officer = false;
        junior_officer = false;
        standard_bearer = false;
        trumpeter = false;
        int e = 0;
        int y = soldiers.Count / regFirstLine;

        int ccount = 0;
        for (int r = 0; r < officers.Count; r++)
        {
            
         
             if (senior_officer == false && officers[r].IdTypeOfficers == 1)
             {
                 senior_officer = true;
                 ccount++;
             }
             else
             {
                 if (junior_officer == false && officers[r].IdTypeOfficers == 2)
                 {
                     junior_officer = true;
                     ccount++;
                 }
                 else
                 {
                     if (standard_bearer == false && officers[r].IdTypeOfficers == 3) 
                     {
                         standard_bearer = true;
                         ccount++;
                     }
                     else
                     {
                         if (trumpeter == false && officers[r].IdTypeOfficers == 4)
                         {
                             trumpeter = true;
                             ccount++;
                         }
                         else
                         {


                         }

                     }

                 }

             }

        }


        senior_officer = false;
        junior_officer = false;
        standard_bearer = false;
        trumpeter = false;
        for (int r = 0; r < officers.Count; r++)
        {
            bool kj = ((senior_officer == false && officers[r].IdTypeOfficers == 1) || 
                (junior_officer == false && officers[r].IdTypeOfficers == 2)  ||
                (standard_bearer == false && officers[r].IdTypeOfficers == 3) ||
                (trumpeter == false && officers[r].IdTypeOfficers == 4)) ;
            if ( kj == true)
            {
                /// Куда сдвинется солдат
                int t1 = 0; int t2 = 0;
                t1 = (soldiers.Count + e) % regFirstLine ;
                t2 = soldiers.Count /regFirstLine ;

                if (t1 > regFirstLine)
                {
                    t2++;
                    t1 = 0;
                }


                int d = 0;
                for (int g = 0; g < soldiers.Count; g++)
                {
                    if (soldiers[g].tempForRebuilding == f)
                    { d = g; }
                }
                Debug.Log(f + "=====");
                Debug.Log(d + "=====");
                // проверяем, есть ли там
                if (soldiers[d] != null && d!=0)
                {
                    soldiers[d].new_position = NewSolPos(t1, t2);
                    soldiers[d].k1 = t1;
                    soldiers[d].k2 = t2;
                }
                t1 =  (f )% regFirstLine; t2 = f/ regFirstLine;
                /// кидаем офицера в начало
                officers[r].GetSoldiers().new_position = NewSolPos(t1, t2);
                officers[r].GetSoldiers().k1 = t1;
                officers[r].GetSoldiers().k2 = t2;
                soldiers[d].matrix_position = soldiers[d].new_position;
                officers[r].GetSoldiers().matrix_position = soldiers[r].new_position;
                if (officers[r].IdTypeOfficers == 1)
                senior_officer = true; 
                if (officers[r].IdTypeOfficers == 2)
                junior_officer = true; 
                if (officers[r].IdTypeOfficers == 3)
                standard_bearer = true; 
                if (officers[r].IdTypeOfficers == 4)
                trumpeter = true;
                f++;
                e++;
            }
        }
    }

     * 
     * 
     * 
     * 
     * 
     * 
	sol_brains
      /*
                /// Идея первая. Если ряд
                float y = transform.position.y;
                Ray ray11 = new Ray(transform.position + transform.right * 0.3f, transform.forward);
                Ray ray12 = new Ray(transform.position - transform.right * 0.3f, transform.forward);
                Ray ray2 = new Ray(transform.position, mainStatic.myRot(transform.forward, 3.1415f / 4));
                Ray ray3 = new Ray(transform.position, mainStatic.myRot(transform.forward, 3.1415f / 4 * 7));
                Ray RF = new Ray(transform.position, mainStatic.myRot(transform.forward, 3.1415f / 4));
                Ray R = new Ray(transform.position, mainStatic.myRot(transform.forward, 3.1415f / 2));
                Ray LF = new Ray(transform.position, mainStatic.myRot(transform.forward, 3.1415f / 4 * 7));
                Ray L = new Ray(transform.position, mainStatic.myRot(transform.forward, 3.1415f / 2 * 3));
                Ray LLF = new Ray(transform.position, mainStatic.myRot(transform.forward, 3.1415f / 4 * 7.3f));
                Ray RRF = new Ray(transform.position, mainStatic.myRot(transform.forward, 3.1415f / 6));

                bool sm = false;
                bool sm1 = false;

                RaycastHit[] hit = new RaycastHit[9];

                bool res1 = false; bool res2 = false; bool res3 = false; int en = 99;


                bool temp1 = Physics.Raycast(ray11, out hit[1], 2.0f);
                bool temp2 = Physics.Raycast(ray12, out hit[2], 2.0f);
                bool temp3 = Physics.Raycast(L, out hit[3], 2.0f);
                bool temp4 = Physics.Raycast(LF, out hit[4], 2.0f);
                bool temp5 = Physics.Raycast(R, out hit[5], 2.0f);
                bool temp6 = Physics.Raycast(RF, out hit[6], 5.0f);
                bool temp7 = Physics.Raycast(LLF, out hit[7], 5.0f);
                bool temp8 = Physics.Raycast(RRF, out hit[8], 5.0f);

                for (int t = 1; t < hit.Length; t++)
                {
                    if (hit[t].transform != null)
                        if (hit[t].transform.GetComponent<UnitSoldiers>() != null)
                        {
                            if (hit[t].transform.GetComponent<UnitSoldiers>().command != command)
                            {
                                en = t; return; // Когда рядом враг, солдат стоит
                            }
                        }
                }

                if (en != 99)
                {
                    // рядом враг, солдат стоит
                }
                else
                {
                    if (temp1 == true || temp2 == true)
                    {
                        if (
                 ///temp5 == false &&
                  temp6 == false && temp8)
                        {
                            // справа пусто, идем направо
                            transform.LookAt(mainStatic.myRot(transform.forward, 3.1415f / 100, transform.position.y) + transform.position);
                            transform.position = transform.position + transform.forward * Time.deltaTime; sm = true;
                        }
                        else
                        {
                            if (
                 //temp3 == false &&
                  temp4 == false && temp7)
                            {
                                // справа пусто, идем направо
                                transform.LookAt(mainStatic.myRot(transform.forward, 3.1415f * 1.99f, transform.position.y) + transform.position);
                                transform.position = transform.position + transform.forward * Time.deltaTime; sm = true;
                            }
                        }
                    }
                    else
                    {
                        sm = false; sm1 = true;
                    }

                    if (sm == false)
                    {
                        // Рядом нет врага, и впереди нет никого, то поворачиваемся к цели и идем к ней
                        Remaketarget();
                        Vector3 v = new Vector3(0,0,0);
                        v = nowTargetPosition - transform.position; v = v.normalized;
                        v = v - transform.forward; v = v / 10;

                        transform.LookAt(transform.position + transform.forward + v);
                        //transform.position += transform.forward * Time.deltaTime;
                    }
                    if (sm1 == true)
                    {
                        transform.position += transform.forward * Time.deltaTime;
                    }

                }

                    //transform.position = new Vector3(transform.position.x, 0f, transform.position.z);

                /*
                if (temp == false && temp2 == false)
                {
                    transform.position += transform.forward * Time.deltaTime; sm = true;

                }
                else
                {
                    temp = Physics.Raycast(ray2, out hit1, 4.0f);
                    if (temp == false)
                    {
                        transform.position += rotV(transform.forward, 3.1415f / 4) * Time.deltaTime; sm = true;
                        transform.LookAt(rotV(transform.forward, 3.1415f / 4));// поворот взгляда
                    }
                    else
                    {
                        temp = Physics.Raycast(ray3, out hit1, 4.0f);
                        if (temp == false)
                        {
                            transform.position += rotV(transform.forward, 3.1415f / 4 * 7) * Time.deltaTime; sm = true;
                            transform.LookAt(rotV(transform.forward, 3.1415f / 4 * 7));
                        }

                    }
                }
                transform.position = new Vector3(transform.position.x, 0f, transform.position.z);
               
                if (false == false)
                {
                    Remaketarget();
                    transform.LookAt(nowTargetPosition);
                }
                //////////////////////////////////////////////////////////////////
 

      /*
    public void at()
    {
        
        // Модифицировать, чтобы предпочтение на тех, кто стоит спереди
        // 1 Искать ближайшего вражеского  солдата из своей группы схваток
        float dr = 999;
        // Номер полка и солдата на которого он агрится
        int enemyNumSol = 0;
        int enemyNumReg = 0;
        // Для каждого из массива противников

        int u = 0;
        bool sucs = false;

        // Для каждого полка противника
        for (int i = 0; i < mainStatic.regiment_stat[numReg].figthMas.Count; i++)
        {
            u++;
            // Если он враг к текущему
            if (mainStatic.regiment_stat[mainStatic.regiment_stat[numReg].figthMas[i]].command != command)
            {
                // Для каждого солдата считаем дистанцию
                for (int r = 0; r < mainStatic.regiment_stat[mainStatic.regiment_stat[numReg].figthMas[i]].soldiers.Count; r++)
                {

                    //u++;
                    // может добавить модуль по ч разности меньше? это должно ускорить
                    float tt = Vector3.Distance(position, mainStatic.regiment_stat[mainStatic.regiment_stat[numReg].figthMas[i]].soldiers[r].position);
                    //Vector3 t = new Vector3 (mainStatic.regiment_stat[mainStatic.regiment_stat[numReg].figthMas[i]].soldiers[r].position.x,mainStatic.regiment_stat[mainStatic.regiment_stat[numReg].figthMas[i]].soldiers[r].position.y, mainStatic.regiment_stat[mainStatic.regiment_stat[numReg].figthMas[i]].soldiers[r].position.z );
                    //float tt = Mathf.Sqrt(Mathf.Pow(position.x - t.x, 2) + Mathf.Pow(position.z - t.z, 2));

                    if ((tt < dr))// && (tt < 6f))
                    {
                        // Если до врага ближе, чем до другого, то он новый враг и расстояние до него обновить
                        enemyNumSol = r; dr = tt; enemyNumReg = mainStatic.regiment_stat[numReg].figthMas[i]; sucs = true;
                    }

                }
            }
        }
     

        Vector3 targetPosition = position;

        if (sucs == true)
        {
            // теперь знаем, кого атаковать, если удар удачный - добавляем в буфер смерти
            bool result = attacka(attack, mainStatic.regiment_stat[enemyNumReg].soldiers[enemyNumSol].defence, power);

            if (result == true)
            {
                DeathBuf r;
                r = new DeathBuf();
                r.regNum = enemyNumReg;
                r.solNum = enemyNumSol;
                r.time = Random.Range(0, 1000) / 1000.0f * mainStatic.TACTICPERIOD;
                DeadBuff.put(r);
                //string a = "";
                //a += r.regNum; a += " "; a += r.solNum; a += " "; a += r.time;
            }
        }
    }





      
    
	}*/

    // Если полки столкнулись, присвоить каждому новое состояние и собрать в контейнер listFight номера воюющих
    /*for (int t = 0; t < mainStatic.regiment_stat.Count; t++)
    {
        if (mainStatic.regiment_stat[t].statePanic == false)
            for (int t1 = 0; t1 < mainStatic.regiment_stat.Count; t1++)
            {
                if (mainStatic.regiment_stat[t1].statePanic == false)
                    if (mainStatic.regiment_stat[t].command != mainStatic.regiment_stat[t1].command)
                        if (Mathf.Abs(mainStatic.regiment_stat[t].regPosition.z - mainStatic.regiment_stat[t1].regPosition.z) < 10)
                            if (Mathf.Abs(mainStatic.regiment_stat[t].regPosition.x - mainStatic.regiment_stat[t1].regPosition.x) < 10)
                            {
                                mainStatic.regiment_stat[t].stateFighting = true;
                                mainStatic.regiment_stat[t1].stateFighting = true;
                                addRegToFigth(t, t1);

                            }
            }
    }*/



}
