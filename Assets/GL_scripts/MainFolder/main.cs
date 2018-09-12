using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using System;
using System.IO;

public class main : MonoBehaviour
{

    /// <summary>
    ///  Объекты, которые будем спавнить во время боя
    /// </summary>
    public GameObject[] objects = new GameObject[2];

    public GameObject officer_test;

    public GameObject test_dead_inf;

    public GameObject arrow;

    private int VDU;

    void Start()
    {
        /// Для обработки количества убитых в тестах солдат
        using (StreamReader reader = File.OpenText("Count.txt"))
        {
            string h = null;
            while ((h = reader.ReadLine()) != null)
            {
                VDU = Convert.ToInt32(h);
            }
        }
        /// Загружаем характеристики солдат
        // Пока пустое StaticLoad.load();
        // Паралельно считывание из файлов:


        /// Загружаем стартовую расстановку полков
        List<temm> reader1 = LoadTxt.loadTxt("map1.txt");
        loadRegiment LoadRegiment = new loadRegiment(LoadTxt.number);

        for (int i = 0; i < reader1.Count; i++)
        {
            regStat t = new regStat();
            mainStatic.regiment_stat.Add(t);
            LoadRegiment.addregiment(reader1[i].x);

        }
        LoadRegiment.numReg = LoadTxt.number;



        // Загружаем предметы
        List<temm> reader2 = LoadTxt.loadTxt("item.txt");
        loadItem LoadItem = new loadItem(LoadTxt.number);

        for (int i = 0; i < reader2.Count; i++)
        {
            LoadItem.additem(reader2[i].x);
        }


        //Загружаем типы солдат и их описание
        List<temm> reader3 = LoadTxt.loadTxt("soldiers.txt");
        loadSoldiers LoadSoldier = new loadSoldiers(LoadTxt.number);
        for (int i = 0; i < reader3.Count; i++)
        {
            LoadSoldier.addSold(reader3[i].x);
        }
        // Тут корректно

        /// Конец паралельных вычислений в блоке

        /// Здесь считаем что одето на юнита и какие параметры ему это меняет
        CalculateSoldParam SoldParameters = new CalculateSoldParam(LoadSoldier.temp);
        SoldParameters.Calc(LoadSoldier, LoadItem);

        /// Считывает файл с оферским составом 
        List<temm> reader4 = LoadTxt.loadTxt("officer.txt");
        loadOfficer LoadOfficer = new loadOfficer(LoadTxt.number);
        for (int i = 0; i < reader4.Count; i++)
        {
            LoadOfficer.addOfficer(reader4[i].x);
        }

        LoadOfficer.ReCalculate(LoadItem);


        // Уникальное id солдата
        int n_obj = 0;

        // Здесь нехорошо, перекладываем данные из LoadData в mainStatic.regiment_stat
        // Попытка переделать - закоменченный скрипт Load Batle
        // Цикл по количеству полков

        mainStatic.panic_regiment_stat = new List<regStat>();

        for (int n = 0; n < LoadRegiment.numReg; n++)
        {
            LoadRegiment.putregiment(n);

            mainStatic.regiment_stat[n].orderBack = false;
            mainStatic.regiment_stat[n].orderForw = false;
            mainStatic.regiment_stat[n].orderRotL = false;
            mainStatic.regiment_stat[n].orderRotW = false;
            mainStatic.regiment_stat[n].archAlone = false;
            mainStatic.regiment_stat[n].archTotewer = false;


            mainStatic.regiment_stat[n].angle = LoadRegiment.angle[n];
            mainStatic.regiment_stat[n].regFirstLine = LoadRegiment.w[n];

            mainStatic.regiment_stat[n].regPosition.x = LoadRegiment.x[n];
            mainStatic.regiment_stat[n].regPosition.y = LoadRegiment.y[n];
            mainStatic.regiment_stat[n].regPosition.z = LoadRegiment.z[n];

            mainStatic.regiment_stat[n].figthMas = new List<int>();


            mainStatic.regiment_stat[n].prim_soldier_count = LoadRegiment.size[n];
            mainStatic.regiment_stat[n].soldier_count = LoadRegiment.size[n];

            mainStatic.regiment_stat[n].typeSoldiers = new int[10];
            mainStatic.regiment_stat[n].Position_Reg = new Vector3[4];




            // Передаем данные п статический массив

            int t_x, t_z; t_x = t_z = 0;

            int ccount = 0; // Индекс установленных уже солдат
            int cc = 0; // Индекс типов солдат
            // Создаем солдат в построении
            for (int i = 0; i < LoadRegiment.size[n]; i++)
            {
                if (ccount < LoadRegiment.sol[n][cc].num)
                {
                    ccount++;
                }
                else
                {
                    cc++;
                    ccount = 0;
                }

                if (t_x == LoadRegiment.w[n])
                {
                    t_x = 0; t_z++;
                }
                Vector3 v, v2; float tx = 0; float tz = 0;
                v.y = 0f;
                v.x = t_x * 3;
                v.z = t_z * 3 * (-1);
                tx = v.x - LoadRegiment.w[n] / 2 * 3; tz = v.z;
                float a = LoadRegiment.angle[n];
                /// Сначала расставляем, потом поворачиваем на a
                v.x = tx * Mathf.Cos(a) + tz * Mathf.Sin(a);
                v.z = -tx * Mathf.Sin(a) + tz * Mathf.Cos(a);
                v2.x = v.x + LoadRegiment.x[n];
                v2.y = 0f;// v.y + LoadRegiment.y[n];
                v2.z = v.z + LoadRegiment.z[n];

                int kk;
                if (SoldParameters.spearman[LoadRegiment.sol[n][cc].id] == true)
                {
                    kk = 2;
                }
                else
                {
                    if (SoldParameters.arch[LoadRegiment.sol[n][cc].id] == true)
                    {
                        kk = 3;
                    }
                    else
                    {
                        kk = 1;
                    }
                }

                mainStatic.regiment_stat[n].soldiers.Add(Instantiate(objects[kk], v2, Quaternion.identity * Quaternion.AngleAxis(a, new Vector3(0, 0, 0))).GetComponent<UnitSoldiers>());


                if (SoldParameters.spearman[LoadRegiment.sol[n][cc].id] == true)
                {
                    mainStatic.regiment_stat[n].soldiers[i].unitType = 2;
                }
                else
                {
                    if (SoldParameters.arch[LoadRegiment.sol[n][cc].id] == true)
                    {
                        mainStatic.regiment_stat[n].soldiers[i].unitType = 3;
                    }
                    else
                    {
                        mainStatic.regiment_stat[n].soldiers[i].unitType = 1;

                    }
                }                    
                mainStatic.regiment_stat[n].soldiers[i].new_position = v2;
                mainStatic.regiment_stat[n].soldiers[i].listItem = SoldParameters.itemList[LoadRegiment.sol[n][cc].id];
				Debug.Log ("-----------");
				Debug.Log (n);
				//Debug.Log (SoldParameters.archer[n-1]);
                mainStatic.regiment_stat[n].soldiers[i].archer =
					SoldParameters.archer[LoadRegiment.sol[n][cc].id];

                mainStatic.regiment_stat[n].soldiers[i].spearman = SoldParameters.spearman[LoadRegiment.sol[n][cc].id];
                mainStatic.regiment_stat[n].soldiers[i].k1 = t_x;
                mainStatic.regiment_stat[n].soldiers[i].k2 = t_z;
                mainStatic.regiment_stat[n].soldiers[i].angle = LoadRegiment.angle[n];
                mainStatic.regiment_stat[n].soldiers[i].matrix_position = new Vector3(v.x, v.y, v.z);
                mainStatic.regiment_stat[n].soldiers[i].gl_number = n_obj;
                mainStatic.regiment_stat[n].soldiers[i].numSoldier = i;
                mainStatic.regiment_stat[n].soldiers[i].numReg = n;

                if (mainStatic.regiment_stat[n].command == 0)
                {
                    if (SoldParameters.arch[LoadRegiment.sol[n][cc].id] == true)
                    {
                        //mainStatic.regiment_stat[n].soldiers[i].transform.GetComponent<MeshRenderer>().material.color = Color.green;
                    }
                    //else
                    //mainStatic.regiment_stat[n].soldiers[i].transform.GetComponent<MeshRenderer>().material.color = Color.blue;
                }
                else
                {
                    if (SoldParameters.arch[LoadRegiment.sol[n][cc].id] == true)
                    {
                        //mainStatic.regiment_stat[n].soldiers[i].transform.GetComponent<MeshRenderer>().material.color = Color.yellow;
                    }
                    //else
                    //mainStatic.regiment_stat[n].soldiers[i].transform.GetComponent<MeshRenderer>().material.color = Color.red;
                }

                // Указали номер, в котором
                mainStatic.regiment_stat[n].soldiers[i].numReg = n;
                mainStatic.regiment_stat[n].soldiers[i].command = mainStatic.regiment_stat[n].command;
                /// Должны передать параметры id солдата
                mainStatic.regiment_stat[n].soldiers[i].attack = 17;// SoldParameters.damage[LoadRegiment.sol[n][cc].id];
                mainStatic.regiment_stat[n].soldiers[i].defence = 3;//SoldParameters.defens[LoadRegiment.sol[n][cc].id];
                mainStatic.regiment_stat[n].soldiers[i].power = SoldParameters.power[LoadRegiment.sol[n][cc].id];
                if (mainStatic.regiment_stat[n].soldiers[i].getObject().GetComponent<UnitArcher>() != null)
                {
                    mainStatic.regiment_stat[n].soldiers[i].getObject().GetComponent<UnitArcher>().archerSkill = SoldParameters.archerSkill[LoadRegiment.sol[n][cc].id];
                    mainStatic.regiment_stat[n].soldiers[i].archer = SoldParameters.arch[LoadRegiment.sol[n][cc].id];
                    mainStatic.regiment_stat[n].soldiers[i].getObject().GetComponent<UnitArcher>().archerType = SoldParameters.arcType[LoadRegiment.sol[n][cc].id];
                    mainStatic.regiment_stat[n].soldiers[i].getObject().GetComponent<UnitArcher>().archerPower = SoldParameters.achPower[LoadRegiment.sol[n][cc].id];
                }
                mainStatic.regiment_stat[n].soldiers[i].IdSoldiers = SoldParameters.soldierId[LoadRegiment.sol[n][cc].id];
                t_x++;
                n_obj++;
            }
        }



        for (int t = 0; t < LoadRegiment.numReg; t++)
        {
            mainStatic.updateRegimentPosition(t);
        }

        mainStatic.all_officers = new List<UnitOfficer>();

        int OfCount = 0;
        for (int j = 0; j < LoadOfficer.temp; j++)
        {
            UnitSoldiers temp = new UnitSoldiers();

            temp.position = new Vector3(LoadOfficer.x[j], LoadOfficer.y[j], LoadOfficer.z[j]);
            temp.angle = LoadOfficer.angle[j];

            mainStatic.all_officers.Add(Instantiate(officer_test, new Vector3(LoadOfficer.x[j], LoadOfficer.y[j], LoadOfficer.z[j]), Quaternion.identity).GetComponent<UnitOfficer>());
            
            if (LoadOfficer.command[j] == 0)
                {
                    //mainStatic.all_officers[j].transform.GetComponent<MeshRenderer>().material.color = Color.blue;
                }
                else
                {
                    //mainStatic.all_officers[j].transform.GetComponent<MeshRenderer>().material.color = Color.green;
                }
            
            
            mainStatic.all_officers[j].GetSoldiers().position = new Vector3(LoadOfficer.x[j], LoadOfficer.y[j], LoadOfficer.z[j]);
            mainStatic.all_officers[j].GetSoldiers().angle = LoadOfficer.angle[j];
            mainStatic.all_officers[j].GetSoldiers().attack = LoadOfficer.defDamage[j];
            mainStatic.all_officers[j].charisma = LoadOfficer.charisma[j];
            mainStatic.all_officers[j].GetSoldiers().command = LoadOfficer.command[j];
            mainStatic.all_officers[j].GetSoldiers().defence = LoadOfficer.defDef[j];

            mainStatic.all_officers[j].IdOfficer = OfCount;
            mainStatic.all_officers[j].IdTypeOfficers = LoadOfficer.IdOfficerType[j];
            OfCount++;
            mainStatic.all_officers[j].LeadDist = LoadOfficer.LeadDist[j];
            mainStatic.all_officers[j].LeadSkill = LoadOfficer.LeadSkill[j];
            mainStatic.all_officers[j].moral = LoadOfficer.moral[j];
            mainStatic.all_officers[j].GetSoldiers().new_position = mainStatic.all_officers[j].GetSoldiers().position;
            mainStatic.all_officers[j].GetSoldiers().control = false;
            mainStatic.all_officers[j].GetSoldiers().power = LoadOfficer.defPower[j];
            mainStatic.all_officers[j].GetSoldiers().speed = LoadOfficer.speed[j];
            mainStatic.all_officers[j].tacticMind = LoadOfficer.tacticMind[j];
            mainStatic.all_officers[j].TurnsWithReg = 0;
            mainStatic.all_officers[j].RegimentNum = LoadOfficer.RegNum[j];
        }
        // Пробежимся по массиву и офицеров перекинем В полки, возможно будут дублироваться

        for (int r = 0; r < mainStatic.all_officers.Count; r++)
        {
            if (mainStatic.all_officers[r].RegimentNum != 99999)
            {
                mainStatic.regiment_stat[mainStatic.all_officers[r].RegimentNum].officers.Add(mainStatic.all_officers[r]);
            }
        }
        mainStatic.listFigReg = new List<figth>();
        DeadBuff.deathBuf = new List<DeathBuf>();
        ArrowBuff.archBuf = new List<ArchBuf>();
        mainStatic.arr = new List<ArrowControl>();

        mapCell.mapCellconstr();
        mapCell.PutAllReg();
    }






    void Update()
    {

        using (StreamWriter writer = File.CreateText("Count.txt"))
        {
            //Debug.Log(VDU);
            writer.WriteLine(VDU);
        }



        /// Пересчитываем время с последнего тактического шага
        mainStatic.t = Time.time - mainStatic.t0;


        if (mainStatic.t > mainStatic.TACTICPERIOD)
        {

            TactUpdate.makeTacticUpdate();

            mainStatic.arrowCount = 0;

        }

        else
        {
            // добавляем стрелу
            if (mainStatic.arrowCount < ArrowBuff.archBuf.Count)
            if (ArrowBuff.archBuf[mainStatic.arrowCount].time < mainStatic.t)
            {
                /*Debug.Log("+++++");
                Debug.Log(mainStatic.arrowCount);
                Debug.Log(ArrowBuff.archBuf.Count);
                Debug.Log(ArrowBuff.archBuf[mainStatic.arrowCount].stPos);
                Debug.Log(ArrowBuff.archBuf[mainStatic.arrowCount].speed);
                Debug.Log(ArrowBuff.archBuf[mainStatic.arrowCount].forw);*/
                mainStatic.arr.Add( Instantiate(arrow, 
                    ArrowBuff.archBuf[mainStatic.arrowCount].stPos,
                    ArrowBuff.archBuf[mainStatic.arrowCount].forw ).GetComponent<ArrowControl>());
                 
                /*GameObject gg = Instantiate(arrow,
                    ArrowBuff.archBuf[mainStatic.arrowCount].stPos,
                    ArrowBuff.archBuf[mainStatic.arrowCount].forw);
                //gg.AddComponent<ArrowControl>();
                
                mainStatic.arr.Add(gg.GetComponent<ArrowControl>() as ArrowControl);
                */
                //mainStatic.arr[(mainStatic.arr.Count - 1)].
                //    put(ArrowBuff.archBuf[mainStatic.arrowCount].speed, ArrowBuff.archBuf[mainStatic.arrowCount].forw);
                mainStatic.arr[(mainStatic.arr.Count - 1)].speed =
                    ArrowBuff.archBuf[mainStatic.arrowCount].speed;
                mainStatic.arr[(mainStatic.arr.Count - 1)].forw = ArrowBuff.archBuf[mainStatic.arrowCount].forw;
                mainStatic.arrowCount++;
            }

            // Вызываем смерти
            if (mainStatic.deathcount < DeadBuff.deathBuf.Count)

                //// Если время смерти пришло
                if (DeadBuff.deathBuf[mainStatic.deathcount].time < mainStatic.t)
                {

                    /*Debug.Log("-----");
                    Debug.Log(DeadBuff.deathBuf[mainStatic.deathcount].regNum);
                    Debug.Log(DeadBuff.deathBuf[mainStatic.deathcount].solNum);
                    Debug.Log(mainStatic.regiment_stat[
                           (DeadBuff.deathBuf[mainStatic.deathcount].regNum)
                               ].
                               soldiers.Count);*/

                    Destroy(mainStatic.regiment_stat[
                           (DeadBuff.deathBuf[mainStatic.deathcount].regNum)
                               ].
                               soldiers[DeadBuff.deathBuf[mainStatic.deathcount].solNum].getObject());

                    /*      mainStatic.regiment_stat[
                             (DeadBuff.deathBuf[mainStatic.deathcount].regNum)
                                 ].
                                 soldiers.RemoveAt(DeadBuff.deathBuf[mainStatic.deathcount].solNum);*/

                    Vector3 tpos = mainStatic.regiment_stat[
                               (DeadBuff.deathBuf[mainStatic.deathcount].regNum)
                                   ].soldiers[
                                    (DeadBuff.deathBuf[mainStatic.deathcount].solNum)
                           ].transform.position;

                    tpos.y = 0.24f;

                    Quaternion a = mainStatic.regiment_stat[DeadBuff.deathBuf[mainStatic.deathcount].regNum].
                        soldiers[DeadBuff.deathBuf[mainStatic.deathcount].solNum].transform.rotation;
                    // Добавить тело
                    mainStatic.deads[mainStatic.deathnumber] = Instantiate(test_dead_inf, tpos,
                        Quaternion.identity * a * Quaternion.Euler(-90, 0, 0)).GetComponent<unit200>();

                    // Уменьшаем число солдат в полку
                    mainStatic.regiment_stat[DeadBuff.deathBuf[mainStatic.deathcount].regNum].soldier_count--;

                    VDU++;

                    // Передвинуть счетчик смерти на следующего
                    mainStatic.deathcount++;
                }
        }
    }
}

public class figth
{
    public List<int>  M;

    public figth()
    {
        M = new List<int>();
    }
}